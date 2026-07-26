#include "usb.h"
#include "memory.h"
#include "blockdev.h"
#include "serial.h"

#define MSC_CLASS     0x08
#define MSC_SUBCLASS_SCSI_TRANS 0x06
#define MSC_PROTO_BBB 0x50

#define MSC_GET_MAX_LUN 0xFE
#define MSC_RESET       0xFF

typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_transfer_length;
    uint8_t  flags;
    uint8_t  lun;
    uint8_t  cb_length;
    uint8_t  cb[16];
} cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t signature;
    uint32_t tag;
    uint32_t data_residue;
    uint8_t  status;
} csw_t;

#define CBW_SIG 0x43425355u
#define CSW_SIG 0x53425355u

typedef struct {
    usb_device_t *dev;
    uint8_t  ep_in, ep_out;
    uint8_t  max_packet_in, max_packet_out;
    uint8_t  max_lun;
    uint32_t tag;
    uint8_t  toggle_in;
    uint8_t  toggle_out;
    volatile uint32_t command_lock;
    bool online;
    block_device_t blk;
    uint32_t sector_size;
    uint32_t sector_count;
    char     name[8];
} msc_state_t;

static void msc_command_lock(msc_state_t *st) {
    while (
        __atomic_exchange_n(
            &st->command_lock,
            1u,
            __ATOMIC_ACQUIRE
        ) != 0u
    ) {
        __asm__ volatile("pause");
    }
}

static void msc_command_unlock(msc_state_t *st) {
    __atomic_store_n(&st->command_lock, 0u, __ATOMIC_RELEASE);
}

static int msc_bulk_transfer(msc_state_t *st, uint8_t dir, uint8_t ep,
                              uint8_t maxpkt, const void *buf, uint32_t len,
                              uint8_t *toggle) {
    usb_transfer_t t;
    t.dir = dir; t.endpoint = (uint8_t)(ep & 0x0Fu); t.device_addr = st->dev->address;
    t.max_packet = maxpkt; t.speed = st->dev->speed;
    t.data_toggle = *toggle;
    /* HC buffer is uint8_t*; safe cast - HC does not modify OUT data */
    t.buffer = (uint8_t *)(uint32_t)buf; t.length = len;
    t.tt_hub_addr = st->dev->tt_hub_addr; t.tt_port = st->dev->tt_port;
    int r = st->dev->hc->submit_sync(st->dev->hc, &t, 2000);
    *toggle = t.data_toggle;
    return r;
}

static int scsi_cmd(msc_state_t *st, const uint8_t *cdb, uint8_t cdb_len,
                    uint8_t dir, const void *data, uint32_t data_len) {
    msc_command_lock(st);
    if (!st->online || !st->dev) {
        msc_command_unlock(st);
        return -1;
    }

    cbw_t cbw = {0};
    cbw.signature = CBW_SIG;
    cbw.tag = ++st->tag;
    cbw.data_transfer_length = data_len;
    cbw.flags = (uint8_t)((dir == USB_DIR_IN) ? 0x80u : 0x00u);
    cbw.lun = 0;
    cbw.cb_length = cdb_len;
    for (int i = 0; i < cdb_len; i++) cbw.cb[i] = cdb[i];

    int result = 0;
    if (msc_bulk_transfer(st, USB_DIR_OUT, st->ep_out, st->max_packet_out,
                          &cbw, 31, &st->toggle_out) < 0) {
        result = -1;
    }
    if (result == 0 && data_len > 0) {
        uint8_t *tog = (dir == USB_DIR_IN) ? &st->toggle_in : &st->toggle_out;
        if (msc_bulk_transfer(st, dir, (uint8_t)((dir == USB_DIR_IN) ? st->ep_in : st->ep_out),
            (dir == USB_DIR_IN) ? st->max_packet_in : st->max_packet_out,
            data, data_len, tog) < 0) {
            result = -1;
        }
    }
    csw_t csw = {0};
    if (
        result == 0
        && msc_bulk_transfer(
            st,
            USB_DIR_IN,
            st->ep_in,
            st->max_packet_in,
            &csw,
            13,
            &st->toggle_in
        ) < 0
    ) {
        result = -1;
    }
    if (
        result == 0
        && (
            csw.signature != CSW_SIG
            || csw.tag != cbw.tag
        )
    ) {
        result = -2;
    }
    if (result == 0 && csw.status != 0) result = -3;
    msc_command_unlock(st);
    return result;
}

static int scsi_inquiry(msc_state_t *st, uint8_t *buf36) {
    uint8_t cdb[6] = { 0x12, 0, 0, 0, 36, 0 };
    return scsi_cmd(st, cdb, 6, USB_DIR_IN, buf36, 36);
}
static int scsi_test_ready(msc_state_t *st) {
    uint8_t cdb[6] = { 0x00, 0, 0, 0, 0, 0 };
    return scsi_cmd(st, cdb, 6, USB_DIR_OUT, NULL, 0);
}
static int scsi_read_capacity(msc_state_t *st, uint32_t *last_lba, uint32_t *block_size) {
    uint8_t cdb[10] = { 0x25, 0,0,0,0,0,0,0,0,0 };
    uint8_t data[8] = {0};
    int r = scsi_cmd(st, cdb, 10, USB_DIR_IN, data, 8);
    if (r < 0) return r;
    *last_lba   = (uint32_t)(data[0]<<24) | (uint32_t)(data[1]<<16)
                | (uint32_t)(data[2]<<8)  | data[3];
    *block_size = (uint32_t)(data[4]<<24) | (uint32_t)(data[5]<<16)
                | (uint32_t)(data[6]<<8)  | data[7];
    return 0;
}
static int scsi_read10(msc_state_t *st, uint32_t lba, uint16_t n_blocks, void *buf) {
    uint8_t cdb[10] = { 0x28, 0,
        (uint8_t)(lba>>24), (uint8_t)(lba>>16), (uint8_t)(lba>>8), (uint8_t)lba,
        0, (uint8_t)(n_blocks>>8), (uint8_t)n_blocks, 0 };
    return scsi_cmd(st, cdb, 10, USB_DIR_IN, buf, (uint32_t)n_blocks * st->sector_size);
}
static int scsi_write10(msc_state_t *st, uint32_t lba, uint16_t n_blocks, const void *buf) {
    uint8_t cdb[10] = { 0x2A, 0,
        (uint8_t)(lba>>24), (uint8_t)(lba>>16), (uint8_t)(lba>>8), (uint8_t)lba,
        0, (uint8_t)(n_blocks>>8), (uint8_t)n_blocks, 0 };
    return scsi_cmd(st, cdb, 10, USB_DIR_OUT, buf, (uint32_t)n_blocks * st->sector_size);
}

static int blk_read(void *drv, uint32_t lba, uint32_t count, void *buffer) {
    return scsi_read10((msc_state_t*)drv, lba, (uint16_t)count, buffer);
}

static void msc_scan_mbr(msc_state_t *st);  /* forward decl */
static int blk_write(void *drv, uint32_t lba, uint32_t count, const void *buffer) {
    return scsi_write10((msc_state_t*)drv, lba, (uint16_t)count, buffer);
}

static void msc_block_release(void *driver_data) {
    if (driver_data) kfree(driver_data);
}

static bool msc_capacity_supported(
    uint32_t last_lba,
    uint32_t block_size
) {
    return block_size >= 512u && last_lba != 0xFFFFFFFFu;
}

static usb_probe_result_t msc_probe(usb_device_t *dev) {
    if (dev->class_code != MSC_CLASS
        || dev->subclass != MSC_SUBCLASS_SCSI_TRANS
        || dev->protocol != MSC_PROTO_BBB) {
        return USB_PROBE_NOT_SUPPORTED;
    }

    msc_state_t *st = (msc_state_t*)kmalloc(sizeof(msc_state_t));
    if (!st) return USB_PROBE_RETRY;
    st->dev = dev;
    st->ep_in = 0x81; st->ep_out = 0x02;
    st->max_packet_in = 64; st->max_packet_out = 64;
    st->tag = 0;
    st->toggle_in = 0;
    st->toggle_out = 0;
    st->command_lock = 0;
    st->online = true;

    uint8_t maxlun = 0;
    /*
     * A Bulk-Only device may stall GET_MAX_LUN to report one logical unit.
     * Retry transport failures, then retain the required zero default.
     */
    (void)usb_control_retry(
        dev,
        0xA1,
        MSC_GET_MAX_LUN,
        0,
        0,
        &maxlun,
        1
    );
    st->max_lun = maxlun;

    uint8_t inq[36] = {0};
    if (scsi_inquiry(st, inq) < 0) {
        kfree(st);
        return USB_PROBE_RETRY;
    }

    int ready = -1;
    for (int i = 0; i < 100 && ready < 0; i++) {
        ready = scsi_test_ready(st);
        if (ready < 0) for (volatile int k = 0; k < 500000; k++) { }
    }
    if (ready < 0) {
        KWARN("usb_msc: not ready");
        kfree(st);
        return USB_PROBE_RETRY;
    }

    uint32_t last_lba = 0, block_size = 0;
    if (scsi_read_capacity(st, &last_lba, &block_size) < 0) {
        KWARN("usb_msc: read capacity failed");
        kfree(st);
        return USB_PROBE_RETRY;
    }
    if (!msc_capacity_supported(last_lba, block_size)) {
        KWARN("usb_msc: unsupported capacity %ux%u",
              last_lba, block_size);
        kfree(st);
        return USB_PROBE_REJECTED;
    }
    st->sector_size = block_size;
    st->sector_count = last_lba + 1;

    static uint32_t next_num = 0u;
    uint32_t n = next_num;
    st->name[0] = 'u'; st->name[1] = 's'; st->name[2] = 'b';
    st->name[3] = (char)('0' + (n % 10)); st->name[4] = 0;
    st->blk.name = st->name;
    st->blk.sector_size = block_size;
    st->blk.sector_count = last_lba + 1;
    st->blk.driver_data = st;
    st->blk.read = blk_read;
    st->blk.write = blk_write;
    st->blk.release = msc_block_release;
    st->blk.registry_ref_count = 0u;
    st->blk.registry_registered = false;
    if (blkdev_register(&st->blk) < 0) {
        KWARN("usb_msc: block device registry is full");
        st->online = false;
        st->dev = NULL;
        kfree(st);
        return USB_PROBE_RETRY;
    }
    next_num++;

    dev->driver_data = st;

    KINFO("usb_msc: %s %ux%u inq='%c%c%c%c%c%c%c%c'",
          st->name, st->sector_count, st->sector_size,
          inq[8], inq[9], inq[10], inq[11], inq[12], inq[13], inq[14], inq[15]);

    msc_scan_mbr(st);
    return 0;
}

static bool is_fat16_type(uint8_t t) {
    return t == 0x04 || t == 0x06 || t == 0x0E;
}

static void msc_scan_mbr(msc_state_t *st) {
    uint8_t *mbr = (uint8_t*)kmalloc(st->sector_size);
    if (!mbr) { KWARN("usb_msc: oom for mbr scan"); return; }

    if (blk_read(st, 0, 1, mbr) < 0) {
        KWARN("usb_msc: mbr read failed, %s is raw blockdev only", st->name);
        kfree(mbr); return;
    }
    if (!(mbr[510] == 0x55 && mbr[511] == 0xAA)) {
        KINFO("usb_msc: %s no MBR signature, raw blockdev only", st->name);
        kfree(mbr); return;
    }

    int fat16_found = 0;
    for (int p = 0; p < 4; p++) {
        uint8_t *pe = &mbr[0x1BE + p * 16];
        uint8_t type = pe[4];
        if (type == 0) continue;
        uint32_t lba = (uint32_t)pe[8] | ((uint32_t)pe[9] << 8)
                     | ((uint32_t)pe[10] << 16) | ((uint32_t)pe[11] << 24);
        uint32_t size = (uint32_t)pe[12] | ((uint32_t)pe[13] << 8)
                      | ((uint32_t)pe[14] << 16) | ((uint32_t)pe[15] << 24);
        KINFO("usb_msc: %s partition %d type=%x lba=%u size=%u",
              st->name, p, type, lba, size);
        if (is_fat16_type(type)) fat16_found++;
    }
    if (fat16_found > 0) {
        KINFO("usb_msc: %s has %d FAT16 partition(s); auto-mount NYI, use raw /dev/%s",
              st->name, fat16_found, st->name);
    }
    kfree(mbr);
}

static int msc_disconnect(usb_device_t *dev) {
    msc_state_t *st = dev->driver_data;
    if (st) {
        /*
         * Retire I/O before removing the public entry. References acquired
         * through blkdev_get keep this state alive until their matching puts.
         */
        msc_command_lock(st);
        st->online = false;
        st->dev = NULL;
        msc_command_unlock(st);

        dev->driver_data = NULL;
        if (blkdev_unregister(&st->blk) < 0) {
            msc_command_lock(st);
            st->dev = dev;
            st->online = true;
            msc_command_unlock(st);
            dev->driver_data = st;
            KERROR("usb_msc: could not unregister block device; attachment restored");
            return -1;
        }
    }
    KINFO("usb_msc: detached");
    return USB_PROBE_BOUND;
}

static usb_driver_t msc_driver = {
    .name = "usb-msc", .probe = msc_probe, .disconnect = msc_disconnect,
    .next = NULL
};

void usb_msc_init(void);
void usb_msc_init(void) {
    usb_register_driver(&msc_driver);
}
