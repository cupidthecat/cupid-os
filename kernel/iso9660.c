/* kernel/iso9660.c — ISO9660 / ECMA-119 + Rock Ridge parser. */

#include "iso9660.h"
#include "vfs.h"   /* for VFS_E* errno values */
#include "string.h"
#include "../drivers/serial.h"

#ifndef UINT32_MAX
#define UINT32_MAX 0xFFFFFFFFu
#endif

/* On-disk Primary Volume Descriptor layout (offsets after the header). */
#define PVD_OFF_TYPE             0     /* must be 1 */
#define PVD_OFF_ID               1     /* must be "CD001" */
#define PVD_OFF_VERSION          6     /* must be 1 */
#define PVD_OFF_LOGICAL_BS_LE    128   /* uint16 LE; must be 2048 */
#define PVD_OFF_ROOT_DIR_REC     156   /* 34-byte directory record */

/* Directory record layout. */
#define DIR_OFF_LEN              0     /* 1 byte; 0 = end-of-sector */
#define DIR_OFF_EXT_ATTR_LEN     1
#define DIR_OFF_EXTENT_LBA_LE    2     /* uint32 LE */
#define DIR_OFF_DATA_LEN_LE      10    /* uint32 LE */
#define DIR_OFF_FLAGS            25    /* bit 1 = is-directory */
#define DIR_OFF_NAME_LEN         32
#define DIR_OFF_NAME             33

/* Read helpers for unaligned multi-byte ints. */
static uint16_t rd_u16le(const uint8_t *p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}
static uint32_t rd_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int tolower_ascii(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static bool ci_eq(const char *a, const char *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        if (!a[i] || !b[i]) return a[i] == b[i];
        if (tolower_ascii((unsigned char)a[i]) !=
            tolower_ascii((unsigned char)b[i])) return false;
    }
    return true;
}

/* Copy base name bytes from directory record, stripping trailing ";1"
 * and optional trailing '.'. Returns the resulting length.
 * dst is NOT null-terminated by this function. */
static uint32_t strip_base_name(const uint8_t *src, uint8_t src_len,
                                char *dst, uint32_t dst_cap) {
    /* Find optional ';' version separator */
    uint32_t n = src_len;
    for (uint32_t i = 0; i < n; i++) {
        if (src[i] == ';') { n = i; break; }
    }
    /* Strip trailing '.' (common in dir-entries that had no extension). */
    if (n > 0 && src[n - 1] == '.') n--;
    if (n > dst_cap) n = dst_cap;
    for (uint32_t i = 0; i < n; i++) dst[i] = (char)src[i];
    return n;
}

/* SUSP entry header: two-char signature + length + version + data. */
#define SUSP_HDR_LEN 4

/* Return start of the System Use (SU) area within a directory record,
 * and its length. Caller owns the input record bytes. */
static void get_su_area(const uint8_t *rec, uint32_t rec_len,
                        const uint8_t **out_start, uint32_t *out_len) {
    uint8_t name_len = rec[DIR_OFF_NAME_LEN];
    uint32_t after_name = (uint32_t)(DIR_OFF_NAME + name_len);
    /* Pad byte if name_len is even (to maintain word alignment). */
    if ((name_len & 1u) == 0u) after_name++;
    if (after_name >= rec_len) { *out_start = NULL; *out_len = 0; return; }
    *out_start = rec + after_name;
    *out_len   = rec_len - after_name;
}

/* Detect SP (System Use SHarP) marker in a record's SU area. */
static bool susp_has_sp(const uint8_t *su, uint32_t su_len) {
    for (uint32_t i = 0; i + SUSP_HDR_LEN <= su_len; ) {
        uint8_t elen = su[i + 2];
        if (elen < SUSP_HDR_LEN || i + elen > su_len) break;
        if (su[i + 0] == 'S' && su[i + 1] == 'P' && elen >= 7 &&
            su[i + 4] == 0xBE && su[i + 5] == 0xEF) {
            return true;
        }
        i += elen;
    }
    return false;
}

/* Read one 2048-byte sector via block_device_t. */
static int read_sector(block_device_t *bdev, uint32_t lba, uint8_t *buf) {
    if (!bdev || !bdev->read) return VFS_EIO;
    int rc = bdev->read(bdev->driver_data, lba, 1, buf);
    return rc == 0 ? 0 : VFS_EIO;
}

/* Parse SU entries and run callback; follow up to MAX_CE_DEPTH CE chains. */
#define SUSP_MAX_CE_DEPTH 8

typedef int (*susp_entry_cb)(const uint8_t *entry, uint32_t elen, void *ctx);

/* Walk SU area, invoking cb(entry, elen, ctx) per entry. Follows CE
 * continuation records. Stops on ST or depth limit. Returns 0 or -errno. */
static int susp_walk(block_device_t *bdev,
                     const uint8_t *su, uint32_t su_len,
                     susp_entry_cb cb, void *ctx) {
    uint8_t  ce_buf[ISO9660_LOGICAL_BLOCK_SIZE];
    uint32_t depth = 0;
    const uint8_t *cur   = su;
    uint32_t       cur_n = su_len;

    while (true) {
        uint32_t i = 0;
        while (i + SUSP_HDR_LEN <= cur_n) {
            uint8_t elen = cur[i + 2];
            if (elen < SUSP_HDR_LEN || i + elen > cur_n) return VFS_EIO;
            /* Signal terminator. */
            if (cur[i + 0] == 'S' && cur[i + 1] == 'T') return 0;
            /* CE chains. */
            if (cur[i + 0] == 'C' && cur[i + 1] == 'E' && elen >= 28) {
                if (depth >= SUSP_MAX_CE_DEPTH) return 0;
                uint32_t ce_lba    = rd_u32le(cur + i + 4);
                uint32_t ce_offset = rd_u32le(cur + i + 12);
                uint32_t ce_len    = rd_u32le(cur + i + 20);
                int rc = read_sector(bdev, ce_lba, ce_buf);
                if (rc < 0) return rc;
                if (ce_offset + ce_len > ISO9660_LOGICAL_BLOCK_SIZE) return VFS_EIO;
                cur   = ce_buf + ce_offset;
                cur_n = ce_len;
                depth++;
                i = 0;  /* restart scan in continuation buffer */
                continue;
            }
            /* User entry. */
            int rc = cb(cur + i, elen, ctx);
            if (rc < 0) return rc;
            i += elen;
        }
        /* Ran off end of this chunk without ST; stop. */
        return 0;
    }
}

typedef struct {
    char     *dst;
    uint32_t  cap;
    uint32_t  written;
    bool      complete;
} nm_ctx_t;

static int nm_entry_cb(const uint8_t *e, uint32_t elen, void *ctx) {
    nm_ctx_t *nc = (nm_ctx_t *)ctx;
    if (nc->complete) return 0;
    if (e[0] != 'N' || e[1] != 'M' || elen < 5) return 0;
    uint8_t flags = e[4];
    if (flags & 0x16u) return 0;  /* CURRENT/PARENT/ROOT */
    uint32_t name_bytes = (uint32_t)elen - 5u;
    const uint8_t *src = e + 5;
    for (uint32_t j = 0; j < name_bytes && nc->written < nc->cap - 1; j++) {
        nc->dst[nc->written++] = (char)src[j];
    }
    if ((flags & 0x01u) == 0u) nc->complete = true;
    return 0;
}

/* Returns bytes written to dst, null-terminated. 0 if no NM. */
static uint32_t susp_read_nm(block_device_t *bdev,
                             const uint8_t *su, uint32_t su_len,
                             char *dst, uint32_t dst_cap) {
    nm_ctx_t nc = { dst, dst_cap, 0, false };
    (void)susp_walk(bdev, su, su_len, nm_entry_cb, &nc);
    if (nc.written < dst_cap) dst[nc.written] = '\0';
    return nc.written;
}

int iso9660_mount_parse(block_device_t *bdev, iso9660_mount_t *m) {
    if (!bdev || !m) return VFS_EINVAL;
    if (bdev->sector_size != ISO9660_LOGICAL_BLOCK_SIZE) return VFS_EINVAL;

    uint8_t sec[ISO9660_LOGICAL_BLOCK_SIZE];
    int rc = read_sector(bdev, ISO9660_PVD_LBA, sec);
    if (rc < 0) return rc;

    /* Validate magic */
    if (sec[PVD_OFF_TYPE] != 1) return VFS_EINVAL;
    if (sec[PVD_OFF_ID + 0] != 'C' || sec[PVD_OFF_ID + 1] != 'D' ||
        sec[PVD_OFF_ID + 2] != '0' || sec[PVD_OFF_ID + 3] != '0' ||
        sec[PVD_OFF_ID + 4] != '1') return VFS_EINVAL;
    if (sec[PVD_OFF_VERSION] != 1) return VFS_EINVAL;

    /* Logical block size must be 2048 for loopdev compatibility. */
    uint16_t lbs = rd_u16le(sec + PVD_OFF_LOGICAL_BS_LE);
    if (lbs != ISO9660_LOGICAL_BLOCK_SIZE) return VFS_EINVAL;

    /* Root directory record starts at offset 156 (34 bytes). */
    const uint8_t *root = sec + PVD_OFF_ROOT_DIR_REC;
    m->bdev              = bdev;
    m->root_extent_lba   = rd_u32le(root + DIR_OFF_EXTENT_LBA_LE);
    m->root_extent_size  = rd_u32le(root + DIR_OFF_DATA_LEN_LE);
    m->has_rockridge     = false;  /* Task 5 sets this after SUSP scan */

    /* Rock Ridge detection: read root extent sector 0, look at the
     * "." record (first record) and check its SU area for SP marker. */
    uint8_t root_sec[ISO9660_LOGICAL_BLOCK_SIZE];
    if (read_sector(bdev, m->root_extent_lba, root_sec) == 0) {
        uint8_t rlen = root_sec[0];
        if (rlen >= DIR_OFF_NAME + 1) {
            const uint8_t *su = NULL;
            uint32_t su_len = 0;
            get_su_area(root_sec, rlen, &su, &su_len);
            if (su && susp_has_sp(su, su_len)) {
                m->has_rockridge = true;
            }
        }
    }

    return 0;
}

/* Find the next record in a directory at byte-offset *off within extent
 * starting at extent_lba with size extent_size. Reads sector(s) into
 * caller-provided 2048-byte buffer. Returns 1 if a record was found,
 * 0 at end-of-directory, -errno on I/O error. */
static int next_dir_record(block_device_t *bdev,
                           uint32_t extent_lba, uint32_t extent_size,
                           uint32_t *off, uint8_t *sec, uint32_t *cur_lba,
                           const uint8_t **rec_ptr, uint32_t *rec_len) {
    while (*off < extent_size) {
        uint32_t sec_idx = *off / ISO9660_LOGICAL_BLOCK_SIZE;
        uint32_t in_sec  = *off % ISO9660_LOGICAL_BLOCK_SIZE;
        uint32_t lba     = extent_lba + sec_idx;
        if (*cur_lba != lba) {
            int rc = read_sector(bdev, lba, sec);
            if (rc < 0) return rc;
            *cur_lba = lba;
        }
        uint8_t len = sec[in_sec];
        if (len == 0) {
            /* Sector padding — skip to next sector */
            *off = (sec_idx + 1) * ISO9660_LOGICAL_BLOCK_SIZE;
            continue;
        }
        if (in_sec + len > ISO9660_LOGICAL_BLOCK_SIZE) {
            /* Malformed: record spans sector boundary (illegal in ECMA-119). */
            return VFS_EIO;
        }
        *rec_ptr = sec + in_sec;
        *rec_len = len;
        return 1;
    }
    return 0;
}

int iso9660_lookup(const iso9660_mount_t *m, const char *path,
                   iso9660_file_t *out) {
    if (!m || !path || !out) return VFS_EINVAL;

    /* Skip leading slashes */
    while (*path == '/') path++;

    uint32_t extent_lba  = m->root_extent_lba;
    uint32_t extent_size = m->root_extent_size;
    bool     is_dir      = true;

    /* Special-case: empty path after trimming = root */
    if (*path == '\0') {
        out->extent_lba       = extent_lba;
        out->file_size        = extent_size;
        out->pos              = 0;
        out->is_dir           = true;
        out->dir_walk_offset  = 0;
        return 0;
    }

    uint8_t sec[ISO9660_LOGICAL_BLOCK_SIZE];

    while (*path) {
        /* Extract component */
        const char *comp = path;
        uint32_t    comp_len = 0;
        while (path[comp_len] && path[comp_len] != '/') comp_len++;

        /* Walk directory for this component */
        uint32_t off = 0;
        uint32_t local_cur_lba = UINT32_MAX;
        bool found = false;
        while (off < extent_size) {
            const uint8_t *rec = NULL;
            uint32_t       rec_len = 0;
            int rc = next_dir_record(m->bdev, extent_lba, extent_size,
                                     &off, sec, &local_cur_lba,
                                     &rec, &rec_len);
            if (rc < 0) return rc;
            if (rc == 0) break;

            uint8_t name_len = rec[DIR_OFF_NAME_LEN];
            /* Skip "." (name_len=1, name=0x00) and ".." (name_len=1, name=0x01) */
            if (name_len == 1 &&
                (rec[DIR_OFF_NAME] == 0x00 || rec[DIR_OFF_NAME] == 0x01)) {
                off += rec_len;
                continue;
            }

            char  name_buf[ISO9660_MAX_NAME_LEN + 1];
            uint32_t name_len_effective;
            if (m->has_rockridge) {
                const uint8_t *su = NULL;
                uint32_t su_len = 0;
                get_su_area(rec, rec_len, &su, &su_len);
                uint32_t nm_len = su ? susp_read_nm(m->bdev, su, su_len,
                                                    name_buf,
                                                    ISO9660_MAX_NAME_LEN)
                                     : 0u;
                if (nm_len > 0) {
                    name_len_effective = nm_len;
                } else {
                    name_len_effective = strip_base_name(
                        rec + DIR_OFF_NAME, name_len,
                        name_buf, ISO9660_MAX_NAME_LEN);
                }
            } else {
                name_len_effective = strip_base_name(
                    rec + DIR_OFF_NAME, name_len,
                    name_buf, ISO9660_MAX_NAME_LEN);
            }
            name_buf[name_len_effective] = '\0';

            if (name_len_effective == comp_len &&
                ci_eq(name_buf, comp, comp_len)) {
                extent_lba  = rd_u32le(rec + DIR_OFF_EXTENT_LBA_LE);
                extent_size = rd_u32le(rec + DIR_OFF_DATA_LEN_LE);
                is_dir      = (rec[DIR_OFF_FLAGS] & 0x02) != 0;
                found = true;
                break;
            }

            off += rec_len;
        }
        if (!found) return VFS_ENOENT;

        path += comp_len;
        while (*path == '/') path++;

        if (*path && !is_dir) return VFS_ENOTDIR;
    }

    out->extent_lba      = extent_lba;
    out->file_size       = extent_size;
    out->pos             = 0;
    out->is_dir          = is_dir;
    out->dir_walk_offset = 0;
    return 0;
}

int iso9660_readdir_step(const iso9660_mount_t *m, iso9660_file_t *f,
                         char *out_name, uint32_t out_name_cap,
                         bool *out_is_dir, uint32_t *out_size) {
    if (!m || !f || !out_name || !out_is_dir || !out_size) return VFS_EINVAL;
    if (!f->is_dir) return VFS_ENOTDIR;

    uint8_t  sec[ISO9660_LOGICAL_BLOCK_SIZE];
    uint32_t cur_lba = UINT32_MAX;

    while (f->dir_walk_offset < f->file_size) {
        const uint8_t *rec     = NULL;
        uint32_t       rec_len = 0;
        int rc = next_dir_record(m->bdev, f->extent_lba, f->file_size,
                                 &f->dir_walk_offset, sec, &cur_lba,
                                 &rec, &rec_len);
        if (rc < 0) return rc;
        if (rc == 0) return 0;

        uint8_t name_len = rec[DIR_OFF_NAME_LEN];
        /* Skip "." and ".." (name_len=1, first byte 0x00 or 0x01). */
        if (name_len == 1 &&
            (rec[DIR_OFF_NAME] == 0x00 || rec[DIR_OFF_NAME] == 0x01)) {
            f->dir_walk_offset += rec_len;
            continue;
        }

        uint32_t name_len_effective;
        if (m->has_rockridge) {
            const uint8_t *su = NULL;
            uint32_t       su_len = 0;
            get_su_area(rec, rec_len, &su, &su_len);
            uint32_t nm_len = su ? susp_read_nm(m->bdev, su, su_len,
                                                 out_name, out_name_cap)
                                 : 0u;
            if (nm_len > 0) {
                name_len_effective = nm_len;
            } else {
                name_len_effective = strip_base_name(
                    rec + DIR_OFF_NAME, name_len,
                    out_name, out_name_cap - 1);
            }
        } else {
            name_len_effective = strip_base_name(
                rec + DIR_OFF_NAME, name_len,
                out_name, out_name_cap - 1);
        }
        if (name_len_effective < out_name_cap) out_name[name_len_effective] = '\0';
        else                                    out_name[out_name_cap - 1] = '\0';

        *out_is_dir = (rec[DIR_OFF_FLAGS] & 0x02) != 0;
        *out_size   = rd_u32le(rec + DIR_OFF_DATA_LEN_LE);

        f->dir_walk_offset += rec_len;
        return 1;
    }
    return 0;
}
