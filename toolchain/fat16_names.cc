#include "fat16_names.h"

typedef struct {
  ctool_u32 first;
  ctool_u32 last;
  ctool_u32 mapping;
  ctool_u32 count;
} fat16_name_range_t;

#include "fat16_name_profiles.inc"

static ctool_bool fat16_name_profile_valid(ctool_fat16_name_profile_t profile) {
  return profile == CTOOL_FAT16_NAMES_UNICODE_15 ||
         profile == CTOOL_FAT16_NAMES_UNICODE_16;
}

static ctool_bool fat16_name_scalar(ctool_string_t text, ctool_u32 *offset,
                                    ctool_u32 *scalar) {
  ctool_u32 index = *offset;
  ctool_u32 value;
  ctool_u32 remaining = 0u;
  ctool_u32 minimum = 0u;
  if (index >= text.size) return CTOOL_FALSE;
  value = (ctool_u8)text.data[index++];
  if (value >= 0xc2u && value <= 0xdfu) {
    value &= 31u; remaining = 1u; minimum = 0x80u;
  } else if (value >= 0xe0u && value <= 0xefu) {
    value &= 15u; remaining = 2u; minimum = 0x800u;
  } else if (value >= 0xf0u && value <= 0xf4u) {
    value &= 7u; remaining = 3u; minimum = 0x10000u;
  } else if (value == 0u || value >= 0x80u) return CTOOL_FALSE;
  if (remaining > text.size - index) return CTOOL_FALSE;
  while (remaining != 0u) {
    ctool_u32 byte = (ctool_u8)text.data[index++];
    if (byte < 0x80u || byte > 0xbfu) return CTOOL_FALSE;
    value = (value << 6u) | (byte & 63u);
    remaining--;
  }
  if (value < minimum || value > 0x10ffffu ||
      (value >= 0xd800u && value <= 0xdfffu)) return CTOOL_FALSE;
  *offset = index;
  *scalar = value;
  return CTOOL_TRUE;
}

static ctool_u32 fat16_name_mapping(ctool_u32 scalar,
    ctool_fat16_name_profile_t profile, ctool_u32 *count) {
  const fat16_name_range_t *ranges;
  ctool_u32 low = 0u;
  ctool_u32 high;
  *count = 0u;
  if (scalar < 128u) {
    const char *allowed = "$%'-_@~`!(){}^#&";
    ctool_u32 i;
    if (scalar >= 'a' && scalar <= 'z') scalar -= 'a' - 'A';
    if ((scalar >= 'A' && scalar <= 'Z') ||
        (scalar >= '0' && scalar <= '9')) {
      *count = 1u;
      return scalar;
    }
    for (i = 0u; allowed[i] != 0; i++) {
      if (scalar == (ctool_u8)allowed[i]) {
        *count = 1u;
        return scalar;
      }
    }
    return 0u;
  }
  if (profile == CTOOL_FAT16_NAMES_UNICODE_15) {
    ranges = fat16_name_unicode_15;
    high = (ctool_u32)(sizeof(fat16_name_unicode_15) /
                      sizeof(fat16_name_unicode_15[0]));
  } else {
    ranges = fat16_name_unicode_16;
    high = (ctool_u32)(sizeof(fat16_name_unicode_16) /
                      sizeof(fat16_name_unicode_16[0]));
  }
  while (low < high) {
    ctool_u32 middle = low + (high - low) / 2u;
    if (scalar < ranges[middle].first) high = middle;
    else if (scalar > ranges[middle].last) low = middle + 1u;
    else {
      *count = ranges[middle].count;
      return ranges[middle].mapping;
    }
  }
  return 0u;
}

static ctool_status_t fat16_name_part(ctool_string_t part,
    ctool_fat16_name_profile_t profile, ctool_u8 *out, ctool_u32 capacity,
    ctool_bool stem) {
  ctool_u32 offset = 0u;
  ctool_u32 used = 0u;
  ctool_u32 i;
  while (offset < part.size) {
    ctool_u32 scalar, count, mapping;
    if (!fat16_name_scalar(part, &offset, &scalar)) return CTOOL_ERR_PATH;
    mapping = fat16_name_mapping(scalar, profile, &count);
    for (i = 0u; i < count; i++) {
      if (used < capacity) out[used] = (ctool_u8)(mapping >> (i * 8u));
      if (used <= capacity) used++;
    }
  }
  if (stem && used == 0u) return CTOOL_ERR_PATH;
  if (stem && used > capacity) { out[6] = '~'; out[7] = '1'; }
  for (i = 0u; i < capacity; i++) {
    if (i >= used) out[i] = ' ';
    if (out[i] >= 128u) return CTOOL_ERR_PATH;
  }
  return CTOOL_OK;
}

ctool_status_t ctool_fat16_project_component(ctool_string_t component,
    ctool_fat16_name_profile_t profile, ctool_fat16_name_t *name_out) {
  ctool_fat16_name_t candidate;
  ctool_u32 dot = component.size;
  ctool_u32 i;
  ctool_string_t stem, extension;
  ctool_status_t status;
  if (name_out == (ctool_fat16_name_t *)0 ||
      component.data == (const char *)0 || !fat16_name_profile_valid(profile))
    return CTOOL_ERR_INVALID_ARGUMENT;
  if (component.size == 0u ||
      (component.size == 1u && component.data[0] == '.') ||
      (component.size == 2u && component.data[0] == '.' && component.data[1] == '.'))
    return CTOOL_ERR_PATH;
  for (i = 0u; i < component.size; i++) {
    if (component.data[i] == '/' || component.data[i] == '\\') return CTOOL_ERR_PATH;
    if (component.data[i] == '.') dot = i;
  }
  stem.data = component.data;
  stem.size = dot;
  extension.data = component.data + component.size;
  extension.size = 0u;
  if (dot < component.size) {
    extension.data = component.data + dot + 1u;
    extension.size = component.size - dot - 1u;
  }
  status = fat16_name_part(stem, profile, candidate.bytes, 8u, CTOOL_TRUE);
  if (status != CTOOL_OK) return status;
  status = fat16_name_part(extension, profile, candidate.bytes + 8u, 3u, CTOOL_FALSE);
  if (status != CTOOL_OK) return status;
  *name_out = candidate;
  return CTOOL_OK;
}

static ctool_bool fat16_name_separator(char byte) {
  return byte == '/' || byte == '\\';
}

ctool_status_t ctool_fat16_project_destination(ctool_string_t destination,
    ctool_fat16_name_profile_t profile, ctool_fat16_name_t *components_out,
    ctool_u32 capacity, ctool_u32 *count_out) {
  ctool_u32 pass, needed = 0u;
  if (count_out != (ctool_u32 *)0) *count_out = 0u;
  if (count_out == (ctool_u32 *)0 || destination.data == (const char *)0 ||
      !fat16_name_profile_valid(profile) ||
      (components_out == (ctool_fat16_name_t *)0 && capacity != 0u) ||
      capacity > 0xffffffffu / (ctool_u32)sizeof(ctool_fat16_name_t))
    return CTOOL_ERR_INVALID_ARGUMENT;
  if (destination.size == 0u || destination.data[0] != '/') return CTOOL_ERR_PATH;
  for (pass = 0u; pass < 2u; pass++) {
    ctool_u32 offset = 0u;
    ctool_u32 count = 0u;
    while (offset < destination.size) {
      ctool_string_t component;
      ctool_fat16_name_t candidate;
      ctool_status_t status;
      ctool_u32 start;
      while (offset < destination.size && fat16_name_separator(destination.data[offset])) offset++;
      start = offset;
      while (offset < destination.size && !fat16_name_separator(destination.data[offset])) offset++;
      if (offset == start) continue;
      component.data = destination.data + start;
      component.size = offset - start;
      status = ctool_fat16_project_component(component, profile, &candidate);
      if (status != CTOOL_OK) return status;
      if (pass != 0u) components_out[count] = candidate;
      count++;
    }
    if (pass == 0u) {
      if (count == 0u) return CTOOL_ERR_PATH;
      needed = count;
      if (components_out == (ctool_fat16_name_t *)0) { *count_out = needed; return CTOOL_OK; }
      if (needed > capacity) { *count_out = needed; return CTOOL_ERR_LIMIT; }
    }
  }
  *count_out = needed;
  return CTOOL_OK;
}
