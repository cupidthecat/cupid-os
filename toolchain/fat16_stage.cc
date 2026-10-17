#include "fat16_stage.h"

#define FAT16_SECTOR_BYTES 512u
#define FAT16_END 0xffffu
#define FAT16_END_MIN 0xfff8u
#define FAT16_BAD_MIN 0xfff0u

struct ctool_fat16_volume {
  ctool_allocator_t allocator;
  ctool_fat16_store_t store;
  ctool_u32 partition;
  ctool_u32 fat_start;
  ctool_u32 fat_sectors;
  ctool_u32 fat_count;
  ctool_u32 root_start;
  ctool_u32 root_entries;
  ctool_u32 data_start;
  ctool_u32 cluster_sectors;
  ctool_u32 cluster_count;
  ctool_bool ready;
  ctool_bool attempted_write;
};

typedef struct {
  ctool_u32 sector;
  ctool_u32 offset;
  ctool_bool found;
  ctool_bool available;
  ctool_u8 entry[32];
} fat16_slot_t;

static ctool_u32 fat16_u16(const ctool_u8 *p) {
  return (ctool_u32)p[0] | ((ctool_u32)p[1] << 8u);
}

static ctool_u32 fat16_u32(const ctool_u8 *p) {
  return fat16_u16(p) | (fat16_u16(p + 2u) << 16u);
}

static void fat16_put16(ctool_u8 *p, ctool_u32 value) {
  p[0] = (ctool_u8)value;
  p[1] = (ctool_u8)(value >> 8u);
}

static void fat16_put32(ctool_u8 *p, ctool_u32 value) {
  fat16_put16(p, value);
  fat16_put16(p + 2u, value >> 16u);
}

static void fat16_copy(ctool_u8 *out, const ctool_u8 *in, ctool_u32 size) {
  ctool_u32 i;
  for (i = 0u; i < size; i++) out[i] = in[i];
}

static void fat16_zero(ctool_u8 *p, ctool_u32 size) {
  ctool_u32 i;
  for (i = 0u; i < size; i++) p[i] = 0u;
}

static ctool_bool fat16_equal(const ctool_u8 *a, const ctool_u8 *b,
                               ctool_u32 size) {
  ctool_u32 i;
  for (i = 0u; i < size; i++) {
    if (a[i] != b[i]) return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

static ctool_status_t fat16_read(ctool_fat16_volume_t *v, ctool_u32 sector,
                                 ctool_u8 *bytes) {
  if (sector >= v->store.sector_count) return CTOOL_ERR_INPUT;
  return v->store.read_sector(v->store.context, sector, bytes);
}

static ctool_status_t fat16_write(ctool_fat16_volume_t *v, ctool_u32 sector,
                                  const ctool_u8 *bytes) {
  if (sector >= v->store.sector_count) return CTOOL_ERR_INPUT;
  v->attempted_write = CTOOL_TRUE;
  return v->store.write_sector(v->store.context, sector, bytes);
}

static ctool_bool fat16_cluster_valid(ctool_fat16_volume_t *v,
                                       ctool_u32 cluster) {
  return cluster >= 2u && cluster < v->cluster_count + 2u &&
         cluster < FAT16_BAD_MIN;
}

static ctool_u32 fat16_cluster_sector(ctool_fat16_volume_t *v,
                                       ctool_u32 cluster) {
  return v->data_start + (cluster - 2u) * v->cluster_sectors;
}

static ctool_status_t fat16_fat_sector(ctool_fat16_volume_t *v,
                                       ctool_u32 relative, ctool_u8 *bytes) {
  ctool_u8 other[FAT16_SECTOR_BYTES];
  ctool_u32 copy;
  ctool_status_t status;
  if (relative >= v->fat_sectors) return CTOOL_ERR_INPUT;
  status = fat16_read(v, v->fat_start + relative, bytes);
  if (status != CTOOL_OK) return status;
  for (copy = 1u; copy < v->fat_count; copy++) {
    status = fat16_read(v, v->fat_start + copy * v->fat_sectors + relative,
                         other);
    if (status != CTOOL_OK) return status;
    if (!fat16_equal(bytes, other, FAT16_SECTOR_BYTES)) return CTOOL_ERR_INPUT;
  }
  return CTOOL_OK;
}

static ctool_status_t fat16_get(ctool_fat16_volume_t *v, ctool_u32 cluster,
                                ctool_u32 *next) {
  ctool_u8 bytes[FAT16_SECTOR_BYTES];
  ctool_status_t status;
  if (!fat16_cluster_valid(v, cluster)) return CTOOL_ERR_INPUT;
  status = fat16_fat_sector(v, cluster / 256u, bytes);
  if (status == CTOOL_OK) *next = fat16_u16(bytes + (cluster % 256u) * 2u);
  return status;
}

static ctool_status_t fat16_set(ctool_fat16_volume_t *v, ctool_u32 cluster,
                                ctool_u32 next) {
  ctool_u8 bytes[FAT16_SECTOR_BYTES];
  ctool_u32 copy;
  ctool_status_t status;
  if (!fat16_cluster_valid(v, cluster)) return CTOOL_ERR_INPUT;
  status = fat16_fat_sector(v, cluster / 256u, bytes);
  if (status != CTOOL_OK) return status;
  fat16_put16(bytes + (cluster % 256u) * 2u, next);
  for (copy = 0u; copy < v->fat_count; copy++) {
    status = fat16_write(v, v->fat_start + copy * v->fat_sectors + cluster / 256u,
                          bytes);
    if (status != CTOOL_OK) return status;
  }
  return CTOOL_OK;
}

static ctool_status_t fat16_next(ctool_fat16_volume_t *v, ctool_u32 cluster,
                                 ctool_u32 *next) {
  ctool_status_t status = fat16_get(v, cluster, next);
  if (status != CTOOL_OK) return status;
  if (*next >= FAT16_END_MIN) *next = 0u;
  else if (*next >= FAT16_BAD_MIN || !fat16_cluster_valid(v, *next))
    return CTOOL_ERR_INPUT;
  return CTOOL_OK;
}

/* Validate the entire old chain before freeing its first entry. Floyd's
 * traversal detects cycles without retaining a volume-sized bitmap. */
static ctool_status_t fat16_check_chain(ctool_fat16_volume_t *v,
                                        ctool_u32 cluster) {
  ctool_u32 slow = cluster, fast = cluster, count;
  ctool_status_t status;
  for (count = 0u; count < v->cluster_count; count++) {
    status = fat16_next(v, slow, &slow);
    if (status != CTOOL_OK || slow == 0u) return status;
    status = fat16_next(v, fast, &fast);
    if (status != CTOOL_OK || fast == 0u) return status;
    status = fat16_next(v, fast, &fast);
    if (status != CTOOL_OK || fast == 0u) return status;
    if (slow == fast) return CTOOL_ERR_INPUT;
  }
  return CTOOL_ERR_INPUT;
}

static ctool_status_t fat16_free(ctool_fat16_volume_t *v, ctool_u32 cluster) {
  ctool_u32 next;
  ctool_status_t status = fat16_check_chain(v, cluster);
  if (status != CTOOL_OK) return status;
  while (cluster != 0u) {
    status = fat16_next(v, cluster, &next);
    if (status != CTOOL_OK) return status;
    status = fat16_set(v, cluster, 0u);
    if (status != CTOOL_OK) return status;
    cluster = next;
  }
  return CTOOL_OK;
}

static ctool_status_t fat16_allocate(ctool_fat16_volume_t *v,
                                      ctool_u32 *cursor, ctool_u32 *allocated) {
  ctool_u8 bytes[FAT16_SECTOR_BYTES];
  ctool_u32 loaded = v->fat_sectors, cluster, relative, index;
  ctool_status_t status;
  for (cluster = *cursor; fat16_cluster_valid(v, cluster); cluster++) {
    relative = cluster / 256u;
    if (relative != loaded) {
      status = fat16_fat_sector(v, relative, bytes);
      if (status != CTOOL_OK) return status;
      loaded = relative;
    }
    if (fat16_u16(bytes + (cluster % 256u) * 2u) == 0u) {
      status = fat16_set(v, cluster, FAT16_END);
      if (status != CTOOL_OK) return status;
      fat16_zero(bytes, FAT16_SECTOR_BYTES);
      for (index = 0u; index < v->cluster_sectors; index++) {
        status = fat16_write(v, fat16_cluster_sector(v, cluster) + index, bytes);
        if (status != CTOOL_OK) return status;
      }
      *allocated = cluster;
      *cursor = cluster + 1u;
      return CTOOL_OK;
    }
  }
  return CTOOL_ERR_LIMIT;
}

static ctool_bool fat16_name_valid(const ctool_fat16_name_t *name) {
  ctool_u32 i;
  ctool_bool padding = CTOOL_FALSE;
  if (name->bytes[0] == 0u || name->bytes[0] == 0xe5u ||
      name->bytes[0] == (ctool_u8)' ') return CTOOL_FALSE;
  for (i = 0u; i < 11u; i++) {
    ctool_u8 c = name->bytes[i];
    if (i == 8u) padding = CTOOL_FALSE;
    if (c == (ctool_u8)' ') padding = CTOOL_TRUE;
    else {
      if (padding || (c < 0x20u && !(i == 0u && c == 5u)) ||
          c == 0x7fu || (c >= 'a' && c <= 'z') || c == '"' || c == '*' ||
          c == '+' || c == ',' || c == '.' || c == '/' || c == ':' ||
          c == ';' || c == '<' || c == '=' || c == '>' || c == '?' ||
          c == '[' || c == '\\' || c == ']' || c == '|') return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

static ctool_status_t fat16_find(ctool_fat16_volume_t *v, ctool_u32 parent,
                                 const ctool_fat16_name_t *name,
                                 fat16_slot_t *slot) {
  ctool_u8 bytes[FAT16_SECTOR_BYTES];
  ctool_u32 start, entries, index, loaded = 0xffffffffu, sector, offset;
  ctool_status_t status;
  slot->found = CTOOL_FALSE;
  slot->available = CTOOL_FALSE;
  if (parent == 0u) {
    start = v->root_start;
    entries = v->root_entries;
  } else {
    if (!fat16_cluster_valid(v, parent)) return CTOOL_ERR_INPUT;
    start = fat16_cluster_sector(v, parent);
    entries = v->cluster_sectors * 16u;
  }
  for (index = 0u; index < entries; index++) {
    sector = start + index / 16u;
    offset = (index % 16u) * 32u;
    if (sector != loaded) {
      status = fat16_read(v, sector, bytes);
      if (status != CTOOL_OK) return status;
      loaded = sector;
    }
    if (bytes[offset] == 0u || bytes[offset] == 0xe5u) {
      if (!slot->available) {
        slot->available = CTOOL_TRUE;
        slot->sector = sector;
        slot->offset = offset;
      }
      if (bytes[offset] == 0u) return CTOOL_OK;
    } else if (bytes[offset + 11u] != 0x0fu &&
               fat16_equal(bytes + offset, name->bytes, 11u)) {
      slot->found = CTOOL_TRUE;
      slot->sector = sector;
      slot->offset = offset;
      fat16_copy(slot->entry, bytes + offset, 32u);
      return CTOOL_OK;
    }
  }
  return CTOOL_OK;
}

static void fat16_entry(ctool_u8 *entry, const ctool_u8 *name,
                         ctool_u8 attribute, ctool_u32 cluster, ctool_u32 size) {
  fat16_zero(entry, 32u);
  fat16_copy(entry, name, 11u);
  entry[11] = attribute;
  fat16_put16(entry + 26u, cluster);
  fat16_put32(entry + 28u, size);
}

static ctool_status_t fat16_put_slot(ctool_fat16_volume_t *v,
                                     const fat16_slot_t *slot,
                                     const ctool_fat16_name_t *name,
                                     ctool_u8 attribute, ctool_u32 cluster,
                                     ctool_u32 size) {
  ctool_u8 bytes[FAT16_SECTOR_BYTES];
  ctool_status_t status = fat16_read(v, slot->sector, bytes);
  if (status != CTOOL_OK) return status;
  fat16_entry(bytes + slot->offset, name->bytes, attribute, cluster, size);
  return fat16_write(v, slot->sector, bytes);
}

static ctool_status_t fat16_directory(ctool_fat16_volume_t *v,
                                       ctool_u32 parent,
                                       const ctool_fat16_name_t *name,
                                       ctool_u32 *child) {
  fat16_slot_t slot;
  ctool_u8 bytes[FAT16_SECTOR_BYTES];
  ctool_u32 cursor = 2u;
  ctool_status_t status = fat16_find(v, parent, name, &slot);
  if (status != CTOOL_OK) return status;
  if (slot.found) {
    if (!(slot.entry[11] & 0x10u) || (slot.entry[11] & 0x08u))
      return CTOOL_ERR_INPUT;
    *child = fat16_u16(slot.entry + 26u);
    if (!fat16_cluster_valid(v, *child)) return CTOOL_ERR_INPUT;
    return fat16_check_chain(v, *child);
  }
  if (!slot.available) return CTOOL_ERR_LIMIT;
  status = fat16_allocate(v, &cursor, child);
  if (status != CTOOL_OK) return status;
  fat16_zero(bytes, FAT16_SECTOR_BYTES);
  fat16_entry(bytes, (const ctool_u8 *)".          ", 0x10u, *child, 0u);
  fat16_entry(bytes + 32u, (const ctool_u8 *)"..         ", 0x10u, parent, 0u);
  status = fat16_write(v, fat16_cluster_sector(v, *child), bytes);
  if (status != CTOOL_OK) return status;
  return fat16_put_slot(v, &slot, name, 0x10u, *child, 0u);
}

ctool_status_t ctool_fat16_open(ctool_allocator_t allocator,
                                 ctool_fat16_store_t store,
                                 ctool_u32 partition_sector,
                                 ctool_fat16_volume_t **volume_out) {
  ctool_fat16_volume_t layout;
  ctool_fat16_volume_t *v;
  ctool_u8 bpb[FAT16_SECTOR_BYTES], fat[FAT16_SECTOR_BYTES];
  ctool_u32 total, reserved, root_sectors;
  ctool_u64 data;
  ctool_status_t status;
  if (volume_out != (ctool_fat16_volume_t **)0) *volume_out = (ctool_fat16_volume_t *)0;
  if (volume_out == (ctool_fat16_volume_t **)0 ||
      allocator.allocate == 0 || allocator.release == 0 ||
      store.read_sector == 0 || store.write_sector == 0 ||
      partition_sector >= store.sector_count) return CTOOL_ERR_INVALID_ARGUMENT;
  status = store.read_sector(store.context, partition_sector, bpb);
  if (status != CTOOL_OK) return status;
  if (bpb[510] != 0x55u || bpb[511] != 0xaau || fat16_u16(bpb + 11u) != 512u)
    return CTOOL_ERR_INPUT;
  layout.allocator = allocator;
  layout.store = store;
  layout.partition = partition_sector;
  layout.cluster_sectors = bpb[13];
  reserved = fat16_u16(bpb + 14u);
  layout.fat_count = bpb[16];
  layout.root_entries = fat16_u16(bpb + 17u);
  layout.fat_sectors = fat16_u16(bpb + 22u);
  total = fat16_u16(bpb + 19u);
  if (total == 0u) total = fat16_u32(bpb + 32u);
  root_sectors = (layout.root_entries * 32u + 511u) / 512u;
  data = (ctool_u64)reserved + (ctool_u64)layout.fat_count * layout.fat_sectors + root_sectors;
  if (layout.cluster_sectors == 0u || layout.cluster_sectors > 128u ||
      (layout.cluster_sectors & (layout.cluster_sectors - 1u)) != 0u ||
      reserved == 0u || layout.fat_count == 0u || layout.root_entries == 0u ||
      layout.fat_sectors == 0u || data >= total ||
      total > store.sector_count - partition_sector) return CTOOL_ERR_INPUT;
  layout.cluster_count = (total - (ctool_u32)data) / layout.cluster_sectors;
  if (layout.cluster_count < 4085u || layout.cluster_count >= 65525u ||
      layout.cluster_count + 2u > layout.fat_sectors * 256u) return CTOOL_ERR_INPUT;
  layout.fat_start = partition_sector + reserved;
  layout.root_start = layout.fat_start + layout.fat_count * layout.fat_sectors;
  layout.data_start = partition_sector + (ctool_u32)data;
  layout.ready = CTOOL_TRUE;
  layout.attempted_write = CTOOL_FALSE;
  status = fat16_fat_sector(&layout, 0u, fat);
  if (status != CTOOL_OK) return status;
  if (fat16_u16(fat) != (0xff00u | bpb[21]) ||
      (fat16_u16(fat + 2u) & 0x3fffu) != 0x3fffu) return CTOOL_ERR_INPUT;
  v = (ctool_fat16_volume_t *)allocator.allocate(allocator.context, sizeof(*v));
  if (v == (ctool_fat16_volume_t *)0) return CTOOL_ERR_NO_MEMORY;
  *v = layout;
  *volume_out = v;
  return CTOOL_OK;
}

void ctool_fat16_close(ctool_fat16_volume_t *v) {
  ctool_allocator_t allocator;
  if (v == (ctool_fat16_volume_t *)0) return;
  allocator = v->allocator;
  allocator.release(allocator.context, v, sizeof(*v));
}

static ctool_status_t fat16_stage_file(ctool_fat16_volume_t *v,
                                       const ctool_fat16_name_t *components,
                                       ctool_u32 count,
                                       ctool_fat16_payload_t payload) {
  fat16_slot_t slot;
  ctool_u8 bytes[FAT16_SECTOR_BYTES];
  ctool_u32 parent = 0u, index, old, cursor = 2u, first = 0u, previous = 0u;
  ctool_u32 cluster, needed, offset = 0u, sector, amount;
  ctool_status_t status;
  for (index = 0u; index + 1u < count; index++) {
    status = fat16_directory(v, parent, &components[index], &parent);
    if (status != CTOOL_OK) return status;
  }
  status = fat16_find(v, parent, &components[count - 1u], &slot);
  if (status != CTOOL_OK) return status;
  if (slot.found) {
    if (slot.entry[11] & 0x18u) return CTOOL_ERR_INPUT;
    old = fat16_u16(slot.entry + 26u);
    if (old == 0u) {
      if (fat16_u32(slot.entry + 28u) != 0u) return CTOOL_ERR_INPUT;
    } else {
      status = fat16_free(v, old);
      if (status != CTOOL_OK) return status;
    }
  } else if (!slot.available) return CTOOL_ERR_LIMIT;
  needed = payload.size / (v->cluster_sectors * 512u);
  if (payload.size % (v->cluster_sectors * 512u) != 0u) needed++;
  if (needed > v->cluster_count) return CTOOL_ERR_LIMIT;
  for (index = 0u; index < needed; index++) {
    status = fat16_allocate(v, &cursor, &cluster);
    if (status != CTOOL_OK) return status;
    if (first == 0u) first = cluster;
    if (previous != 0u) {
      status = fat16_set(v, previous, cluster);
      if (status != CTOOL_OK) return status;
    }
    previous = cluster;
  }
  cluster = first;
  while (offset < payload.size) {
    for (sector = 0u; sector < v->cluster_sectors && offset < payload.size; sector++) {
      amount = payload.size - offset;
      if (amount > 512u) amount = 512u;
      fat16_zero(bytes, 512u);
      status = payload.read_exact(payload.context, offset, bytes, amount);
      if (status != CTOOL_OK) return status;
      status = fat16_write(v, fat16_cluster_sector(v, cluster) + sector, bytes);
      if (status != CTOOL_OK) return status;
      offset += amount;
    }
    if (offset < payload.size) {
      status = fat16_next(v, cluster, &cluster);
      if (status != CTOOL_OK) return status;
      if (cluster == 0u) return CTOOL_ERR_INTERNAL;
    }
  }
  return fat16_put_slot(v, &slot, &components[count - 1u], 0x20u, first, payload.size);
}

ctool_status_t ctool_fat16_stage(ctool_fat16_volume_t *v,
                                  const ctool_fat16_name_t *components,
                                  ctool_u32 count,
                                  ctool_fat16_payload_t payload) {
  ctool_u32 index;
  ctool_status_t status;
  if (v == (ctool_fat16_volume_t *)0 || components == (const ctool_fat16_name_t *)0 ||
      count == 0u || (payload.size != 0u && payload.read_exact == 0))
    return CTOOL_ERR_INVALID_ARGUMENT;
  if (!v->ready) return CTOOL_ERR_INPUT;
  for (index = 0u; index < count; index++) {
    if (!fat16_name_valid(&components[index])) return CTOOL_ERR_PATH;
  }
  v->attempted_write = CTOOL_FALSE;
  status = fat16_stage_file(v, components, count, payload);
  if (status != CTOOL_OK && v->attempted_write) v->ready = CTOOL_FALSE;
  return status;
}
