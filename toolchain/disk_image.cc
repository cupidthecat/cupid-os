#include "disk_image.h"

typedef struct {
  ctool_u32 partition;
  ctool_u32 cluster_sectors;
  ctool_u32 fat_sectors;
  ctool_u32 data_start;
} disk_layout_t;

static ctool_u32 disk_u16(const ctool_u8 *p) {
  return (ctool_u32)p[0] | ((ctool_u32)p[1] << 8u);
}

static ctool_u32 disk_u32(const ctool_u8 *p) {
  return disk_u16(p) | (disk_u16(p + 2u) << 16u);
}

static void disk_put16(ctool_u8 *p, ctool_u32 value) {
  p[0] = (ctool_u8)value; p[1] = (ctool_u8)(value >> 8u);
}

static void disk_put32(ctool_u8 *p, ctool_u32 value) {
  disk_put16(p, value); disk_put16(p + 2u, value >> 16u);
}

static void disk_zero(ctool_u8 *p) {
  ctool_u32 i;
  for (i = 0u; i < 512u; i++) p[i] = 0u;
}

static ctool_bool disk_equal(const ctool_u8 *a, const ctool_u8 *b) {
  ctool_u32 i;
  for (i = 0u; i < 512u; i++) if (a[i] != b[i]) return CTOOL_FALSE;
  return CTOOL_TRUE;
}

static ctool_status_t disk_read(ctool_disk_source_t source, ctool_u64 offset,
                                 ctool_u8 *bytes, ctool_u32 count) {
  if (offset > source.size || (ctool_u64)count > source.size - offset)
    return CTOOL_ERR_INPUT;
  if (count == 0u) return CTOOL_OK;
  if (source.read_exact == (ctool_status_t (*)(void *, ctool_u64, ctool_u8 *, ctool_u32))0)
    return CTOOL_ERR_INVALID_ARGUMENT;
  return source.read_exact(source.context, offset, bytes, count);
}

static ctool_status_t disk_choose_layout(ctool_u32 partition,
                                          disk_layout_t *layout) {
  ctool_u32 cluster_sectors;
  for (cluster_sectors = 1u; cluster_sectors <= 64u; cluster_sectors *= 2u) {
    ctool_u32 fat = 1u, previous = 0u;
    for (;;) {
      ctool_u64 overhead = 33u + 2u * (ctool_u64)fat;
      ctool_u32 clusters, needed;
      if (overhead >= partition) break;
      clusters = (partition - (ctool_u32)overhead) / cluster_sectors;
      needed = (ctool_u32)(((ctool_u64)clusters + 2u + 255u) / 256u);
      if (needed == fat) {
        if (clusters < 4085u || clusters >= 65525u || fat > 65535u) break;
        layout->partition = partition;
        layout->cluster_sectors = cluster_sectors;
        layout->fat_sectors = fat;
        layout->data_start = (ctool_u32)overhead;
        return CTOOL_OK;
      }
      if (needed == previous) break;
      previous = fat;
      fat = needed;
    }
  }
  return CTOOL_ERR_INPUT;
}

static ctool_status_t disk_render(const ctool_disk_image_request_t *r,
    const disk_layout_t *layout, ctool_u32 sector, ctool_u8 *bytes) {
  ctool_status_t status;
  ctool_u32 i;
  disk_zero(bytes);
  if (sector == 0u) {
    status = disk_read(r->bootloader, 0u, bytes, 446u);
    if (status != CTOOL_OK) return status;
    bytes[446] = 0x80u;
    bytes[447] = bytes[451] = 0xfeu;
    bytes[448] = bytes[449] = bytes[452] = bytes[453] = 0xffu;
    bytes[450] = 6u;
    disk_put32(bytes + 454u, r->fat_start_lba);
    disk_put32(bytes + 458u, layout->partition);
    bytes[510] = 0x55u; bytes[511] = 0xaau;
  } else if (sector < 5u) {
    return disk_read(r->bootloader, (ctool_u64)sector * 512u, bytes, 512u);
  } else if (sector < r->fat_start_lba) {
    ctool_u64 offset = (ctool_u64)(sector - 5u) * 512u;
    if (offset < r->kernel.size) {
      ctool_u64 remaining = r->kernel.size - offset;
      return disk_read(r->kernel, offset, bytes,
                       remaining >= 512u ? 512u : (ctool_u32)remaining);
    }
  } else if (sector == r->fat_start_lba) {
    const char *oem = "CUPIDOS ";
    const char *label = "CUPIDOS    ";
    const char *type = "FAT16   ";
    bytes[0] = 0xebu; bytes[1] = 0x3cu; bytes[2] = 0x90u;
    for (i = 0u; i < 8u; i++) { bytes[3u + i] = (ctool_u8)oem[i]; bytes[54u + i] = (ctool_u8)type[i]; }
    for (i = 0u; i < 11u; i++) bytes[43u + i] = (ctool_u8)label[i];
    disk_put16(bytes + 11u, 512u);
    bytes[13] = (ctool_u8)layout->cluster_sectors;
    disk_put16(bytes + 14u, 1u);
    bytes[16] = 2u;
    disk_put16(bytes + 17u, 512u);
    if (layout->partition < 65536u) disk_put16(bytes + 19u, layout->partition);
    else disk_put32(bytes + 32u, layout->partition);
    bytes[21] = 0xf8u;
    disk_put16(bytes + 22u, layout->fat_sectors);
    disk_put16(bytes + 24u, 63u);
    disk_put16(bytes + 26u, 255u);
    disk_put32(bytes + 28u, r->fat_start_lba);
    bytes[36] = 0x80u; bytes[38] = 0x29u;
    disk_put32(bytes + 39u, 0x0c001d05u);
    bytes[510] = 0x55u; bytes[511] = 0xaau;
  } else if (sector == r->fat_start_lba + 1u ||
             sector == r->fat_start_lba + 1u + layout->fat_sectors) {
    bytes[0] = 0xf8u; bytes[1] = bytes[2] = bytes[3] = 0xffu;
  }
  return CTOOL_OK;
}

static ctool_status_t disk_reuse(const ctool_disk_image_request_t *r,
                                  ctool_bool *valid) {
  ctool_u8 bytes[512];
  ctool_status_t status;
  ctool_u32 cluster_sectors, reserved, fats, entries, total, fat_sectors;
  ctool_u64 overhead, clusters;
  *valid = CTOOL_FALSE;
  if (!r->previous_present || r->force_format ||
      r->previous.size != (ctool_u64)r->image_sectors * 512u) return CTOOL_OK;
  status = disk_read(r->previous, 0u, bytes, 512u);
  if (status != CTOOL_OK) return status;
  if (bytes[510] != 0x55u || bytes[511] != 0xaau ||
      (bytes[450] != 4u && bytes[450] != 6u && bytes[450] != 0x0eu) ||
      disk_u32(bytes + 454u) != r->fat_start_lba ||
      disk_u32(bytes + 458u) != r->image_sectors - r->fat_start_lba) return CTOOL_OK;
  status = disk_read(r->previous, (ctool_u64)r->fat_start_lba * 512u, bytes, 512u);
  if (status != CTOOL_OK) return status;
  cluster_sectors = bytes[13]; reserved = disk_u16(bytes + 14u);
  fats = bytes[16]; entries = disk_u16(bytes + 17u);
  total = disk_u16(bytes + 19u);
  if (total == 0u) total = disk_u32(bytes + 32u);
  fat_sectors = disk_u16(bytes + 22u);
  if (bytes[510] != 0x55u || bytes[511] != 0xaau || disk_u16(bytes + 11u) != 512u ||
      cluster_sectors == 0u || cluster_sectors > 64u ||
      (cluster_sectors & (cluster_sectors - 1u)) != 0u || reserved == 0u ||
      (fats != 1u && fats != 2u) || entries == 0u ||
      total != r->image_sectors - r->fat_start_lba || fat_sectors == 0u ||
      disk_u32(bytes + 28u) != r->fat_start_lba) return CTOOL_OK;
  overhead = reserved + (ctool_u64)fats * fat_sectors +
             ((ctool_u64)entries * 32u + 511u) / 512u;
  if (overhead >= total) return CTOOL_OK;
  clusters = (total - overhead) / cluster_sectors;
  if (clusters < 4085u || clusters >= 65525u ||
      clusters + 2u > (ctool_u64)fat_sectors * 256u) return CTOOL_OK;
  *valid = CTOOL_TRUE;
  return CTOOL_OK;
}

ctool_status_t ctool_disk_image_compose(const ctool_disk_image_request_t *r,
                                        ctool_disk_image_result_t *out) {
  disk_layout_t layout;
  ctool_disk_image_result_t result;
  ctool_u8 expected[512], actual[512];
  ctool_u32 sector, template_sectors;
  ctool_status_t status;
  result.ready = result.reused = result.attempted_write = CTOOL_FALSE;
  if (out == (ctool_disk_image_result_t *)0) return CTOOL_ERR_INVALID_ARGUMENT;
  *out = result;
  if (r == (const ctool_disk_image_request_t *)0 ||
      r->candidate.sector_count != r->image_sectors ||
      r->candidate.write_sector == (ctool_status_t (*)(void *, ctool_u32, const ctool_u8 *))0 ||
      r->fat_start_lba <= 5u || r->fat_start_lba >= r->image_sectors ||
      r->bootloader.size < 2560u ||
      r->kernel.size > (ctool_u64)r->fat_start_lba * 512u - 2560u)
    return CTOOL_ERR_INVALID_ARGUMENT;
  status = disk_choose_layout(r->image_sectors - r->fat_start_lba, &layout);
  if (status != CTOOL_OK) return status;
  template_sectors = r->fat_start_lba + layout.data_start;
  if (r->checked_template.size != (ctool_u64)template_sectors * 512u)
    return CTOOL_ERR_INPUT;
  for (sector = 0u; sector < template_sectors; sector++) {
    status = disk_render(r, &layout, sector, expected);
    if (status != CTOOL_OK) return status;
    status = disk_read(r->checked_template, (ctool_u64)sector * 512u, actual, 512u);
    if (status != CTOOL_OK) return status;
    if (!disk_equal(expected, actual)) return CTOOL_ERR_INPUT;
  }
  status = disk_reuse(r, &result.reused);
  if (status != CTOOL_OK) return status;
  for (sector = 0u; sector < r->image_sectors; sector++) {
    if (result.reused && sector >= r->fat_start_lba)
      status = disk_read(r->previous, (ctool_u64)sector * 512u, actual, 512u);
    else if (sector < template_sectors)
      status = disk_read(r->checked_template, (ctool_u64)sector * 512u, actual, 512u);
    else { disk_zero(actual); status = CTOOL_OK; }
    if (status != CTOOL_OK) { *out = result; return status; }
    result.attempted_write = CTOOL_TRUE;
    status = r->candidate.write_sector(r->candidate.context, sector, actual);
    if (status != CTOOL_OK) { *out = result; return status; }
  }
  result.ready = CTOOL_TRUE;
  *out = result;
  return CTOOL_OK;
}
