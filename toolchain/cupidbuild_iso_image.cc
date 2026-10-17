#include "cupidbuild_iso_image.h"
#include <string.h>

#define ISO_IMAGE_BLOCK 2048u
#define ISO_IMAGE_IDENTIFIERS 1024u

typedef struct {
  ctool_string_t path;
  ctool_string_t name;
  const ctool_source_t *source;
  ctool_u32 parent;
  ctool_u32 directory;
  ctool_u32 number;
  ctool_u32 extent;
  ctool_u32 size;
  ctool_u32 child_start;
  ctool_u32 child_count;
  ctool_u32 directory_children;
  ctool_u32 identifier_size;
  unsigned char identifier[16];
} iso_image_node_t;

typedef struct {
  const ctool_source_t *image;
  ctool_u32 offset;
} iso_image_check_t;

static int iso_image_error(char *error, ctool_u32 capacity, const char *message) {
  ctool_u32 index = 0u;
  if (error != (char *)0 && capacity != 0u) {
    while (index + 1u < capacity && message[index] != 0) {
      error[index] = message[index]; index++;
    }
    error[index] = 0;
  }
  return 0;
}

static unsigned char iso_image_fold(unsigned char byte) {
  return byte >= 'A' && byte <= 'Z' ? (unsigned char)(byte + 32u) : byte;
}

static int iso_image_compare(ctool_string_t a, ctool_string_t b, int folded) {
  ctool_u32 index;
  ctool_u32 size = a.size < b.size ? a.size : b.size;
  for (index = 0u; index < size; index++) {
    unsigned char left = (unsigned char)a.data[index];
    unsigned char right = (unsigned char)b.data[index];
    if (folded) { left = iso_image_fold(left); right = iso_image_fold(right); }
    if (left != right) return left < right ? -1 : 1;
  }
  return a.size == b.size ? 0 : a.size < b.size ? -1 : 1;
}

static void iso_image_u16(unsigned char *out, ctool_u32 value, int big) {
  out[big ? 1u : 0u] = (unsigned char)value;
  out[big ? 0u : 1u] = (unsigned char)(value >> 8u);
}

static void iso_image_u32(unsigned char *out, ctool_u32 value, int big) {
  ctool_u32 index;
  for (index = 0u; index < 4u; index++)
    out[big ? 3u - index : index] = (unsigned char)(value >> (index * 8u));
}

static void iso_image_both16(unsigned char *out, ctool_u32 value) {
  iso_image_u16(out, value, 0); iso_image_u16(out + 2u, value, 1);
}

static void iso_image_both32(unsigned char *out, ctool_u32 value) {
  iso_image_u32(out, value, 0); iso_image_u32(out + 4u, value, 1);
}

static int iso_image_bytes(iso_image_check_t *check, const void *expected,
                            ctool_u32 size) {
  if (size > check->image->contents.size - check->offset ||
      (size != 0u && memcmp(check->image->contents.data + check->offset, expected, size) != 0))
    return 0;
  check->offset += size;
  return 1;
}

static int iso_image_zeros(iso_image_check_t *check, ctool_u32 size) {
  ctool_u32 index;
  if (size > check->image->contents.size - check->offset) return 0;
  for (index = 0u; index < size; index++)
    if (check->image->contents.data[check->offset + index] != 0u) return 0;
  check->offset += size;
  return 1;
}

static ctool_u32 iso_image_padding(ctool_u32 size) {
  return (ISO_IMAGE_BLOCK - size % ISO_IMAGE_BLOCK) % ISO_IMAGE_BLOCK;
}

static void iso_image_base_identifier(iso_image_node_t *node, ctool_u32 sequence) {
  unsigned char stem[8];
  unsigned char extension[3];
  unsigned char suffix[5];
  ctool_u32 stem_size;
  ctool_u32 extension_size = 0u;
  ctool_u32 suffix_size = 0u;
  ctool_u32 split = node->name.size;
  ctool_u32 index;
  if (!node->directory && node->name.data[0] != '.' &&
      node->name.data[node->name.size - 1u] != '.') {
    for (index = 0u; index < node->name.size; index++)
      if (node->name.data[index] == '.') split = index;
  }
  stem_size = split < 8u ? split : 8u;
  for (index = 0u; index < stem_size; index++) {
    unsigned char byte = (unsigned char)node->name.data[index];
    if (byte >= 'a' && byte <= 'z') byte = (unsigned char)(byte - 32u);
    stem[index] = ((byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_') ? byte : '_';
  }
  if (split < node->name.size) {
    extension_size = node->name.size - split - 1u;
    if (extension_size > 3u) extension_size = 3u;
    for (index = 0u; index < extension_size; index++) {
      unsigned char byte = (unsigned char)node->name.data[split + 1u + index];
      if (byte >= 'a' && byte <= 'z') byte = (unsigned char)(byte - 32u);
      extension[index] = ((byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') || byte == '_') ? byte : '_';
    }
  }
  if (sequence != 0u) {
    ctool_u32 value = sequence;
    unsigned char reversed[4];
    ctool_u32 digits = 0u;
    do { reversed[digits++] = (unsigned char)('0' + value % 10u); value /= 10u; } while (value != 0u);
    suffix[0] = '_'; suffix_size = digits + 1u;
    for (index = 0u; index < digits; index++) suffix[index + 1u] = reversed[digits - index - 1u];
    if (stem_size > 8u - suffix_size) stem_size = 8u - suffix_size;
  }
  (void)memcpy(node->identifier, stem, stem_size);
  (void)memcpy(node->identifier + stem_size, suffix, suffix_size);
  node->identifier_size = stem_size + suffix_size;
  if (!node->directory) {
    node->identifier[node->identifier_size++] = '.';
    (void)memcpy(node->identifier + node->identifier_size, extension, extension_size);
    node->identifier_size += extension_size;
    node->identifier[node->identifier_size++] = ';'; node->identifier[node->identifier_size++] = '1';
  }
}

static int iso_image_identifier_compare(const iso_image_node_t *a,
                                         const iso_image_node_t *b) {
  ctool_string_t left;
  ctool_string_t right;
  left.data = (const char *)a->identifier; left.size = a->identifier_size;
  right.data = (const char *)b->identifier; right.size = b->identifier_size;
  return iso_image_compare(left, right, 0);
}

static int iso_image_identifier_unused(iso_image_node_t *nodes, ctool_u32 *used,
                                        ctool_u32 index) {
  ctool_u32 hash = 2166136261u;
  ctool_u32 byte;
  ctool_u32 slot;
  for (byte = 0u; byte < nodes[index].identifier_size; byte++)
    hash = (hash ^ nodes[index].identifier[byte]) * 16777619u;
  slot = hash & (ISO_IMAGE_IDENTIFIERS - 1u);
  while (used[slot] != 0u) {
    if (iso_image_identifier_compare(&nodes[index], &nodes[used[slot]]) == 0) return 0;
    slot = (slot + 1u) & (ISO_IMAGE_IDENTIFIERS - 1u);
  }
  used[slot] = index;
  return 1;
}

static ctool_u32 iso_image_record_size(const iso_image_node_t *node,
                                        ctool_u32 identifier_size,
                                        int root_dot, int child, int primary) {
  ctool_u32 size = 33u + identifier_size + (identifier_size % 2u == 0u ? 1u : 0u);
  if (!primary) size += 62u + (root_dot ? 35u : 0u) + (child ? 5u + node->name.size : 0u);
  return size + size % 2u;
}

static ctool_u32 iso_image_directory_size(iso_image_node_t *nodes, ctool_u32 *children,
                                          ctool_u32 owner) {
  ctool_u32 size = iso_image_record_size(&nodes[owner], 1u, owner == 0u, 0, 0);
  ctool_u32 index;
  size += iso_image_record_size(&nodes[nodes[owner].parent], 1u, 0, 0, 0);
  for (index = 0u; index < nodes[owner].child_count; index++) {
    iso_image_node_t *child = &nodes[children[nodes[owner].child_start + index]];
    ctool_u32 record = iso_image_record_size(child, child->identifier_size, 0, 1, 0);
    if (record > ISO_IMAGE_BLOCK - size % ISO_IMAGE_BLOCK) size += iso_image_padding(size);
    size += record;
  }
  return size + iso_image_padding(size);
}

static void iso_image_date(unsigned char *out) {
  static const unsigned char date[7] = {100u, 1u, 1u, 0u, 0u, 0u, 0u};
  (void)memcpy(out, date, sizeof(date));
}

static ctool_u32 iso_image_record(unsigned char *out, const iso_image_node_t *node,
                                   const unsigned char *identifier, ctool_u32 identifier_size,
                                   int root_dot, int child, int primary,
                                   ctool_u32 continuation, ctool_u32 continuation_size) {
  ctool_u32 size = iso_image_record_size(node, identifier_size, root_dot, child, primary);
  ctool_u32 offset = 33u + identifier_size + (identifier_size % 2u == 0u ? 1u : 0u);
  (void)memset(out, 0, size);
  out[0] = (unsigned char)size;
  iso_image_both32(out + 2u, node->extent); iso_image_both32(out + 10u, node->size);
  iso_image_date(out + 18u); out[25] = node->directory ? 2u : 0u;
  iso_image_both16(out + 28u, 1u); out[32] = (unsigned char)identifier_size;
  (void)memcpy(out + 33u, identifier, identifier_size);
  if (primary) return size;
  if (root_dot) {
    static const unsigned char sp[7] = {'S', 'P', 7u, 1u, 0xbeu, 0xefu, 0u};
    (void)memcpy(out + offset, sp, sizeof(sp)); offset += 7u;
  }
  out[offset] = 'P'; out[offset + 1u] = 'X'; out[offset + 2u] = 36u; out[offset + 3u] = 1u;
  iso_image_both32(out + offset + 4u, node->directory ? 0040555u : 0100444u);
  iso_image_both32(out + offset + 12u, node->directory ? 2u + node->directory_children : 1u);
  offset += 36u;
  out[offset] = 'T'; out[offset + 1u] = 'F'; out[offset + 2u] = 26u;
  out[offset + 3u] = 1u; out[offset + 4u] = 0x0eu;
  iso_image_date(out + offset + 5u); iso_image_date(out + offset + 12u); iso_image_date(out + offset + 19u);
  offset += 26u;
  if (root_dot) {
    out[offset] = 'C'; out[offset + 1u] = 'E'; out[offset + 2u] = 28u; out[offset + 3u] = 1u;
    iso_image_both32(out + offset + 4u, continuation);
    iso_image_both32(out + offset + 20u, continuation_size); offset += 28u;
  }
  if (child) {
    out[offset] = 'N'; out[offset + 1u] = 'M'; out[offset + 2u] = (unsigned char)(5u + node->name.size);
    out[offset + 3u] = 1u; out[offset + 4u] = 0u;
    (void)memcpy(out + offset + 5u, node->name.data, node->name.size);
  }
  return size;
}

static ctool_u32 iso_image_continuation(unsigned char *out) {
  static const char identifier[] = "RRIP_1991A";
  static const char description[] = "THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS";
  static const char source[] = "PLEASE CONTACT DISC PUBLISHER FOR SPECIFICATION SOURCE.  SEE PUBLISHER IDENTIFIER IN PRIMARY VOLUME DESCRIPTOR FOR CONTACT INFORMATION.";
  ctool_u32 a = sizeof(identifier) - 1u;
  ctool_u32 b = sizeof(description) - 1u;
  ctool_u32 c = sizeof(source) - 1u;
  out[0] = 'E'; out[1] = 'R'; out[2] = (unsigned char)(8u + a + b + c); out[3] = 1u;
  out[4] = (unsigned char)a; out[5] = (unsigned char)b; out[6] = (unsigned char)c; out[7] = 1u;
  (void)memcpy(out + 8u, identifier, a); (void)memcpy(out + 8u + a, description, b);
  (void)memcpy(out + 8u + a + b, source, c);
  return 8u + a + b + c;
}

static void iso_image_descriptor_text(unsigned char *out, ctool_u32 offset,
                                       ctool_u32 width, const char *text) {
  size_t size = strlen(text);
  (void)memset(out + offset, ' ', width); (void)memcpy(out + offset, text, size);
}

static void iso_image_descriptor(unsigned char *out, iso_image_node_t *root,
                                  ctool_u32 blocks, ctool_u32 path_size,
                                  ctool_u32 big_path_block) {
  static const unsigned char zero[1] = {0u};
  static const char date[17] = "2000010100000000";
  static const char unspecified[17] = "0000000000000000";
  (void)memset(out, 0, ISO_IMAGE_BLOCK); (void)memcpy(out, "\001CD001\001", 7u);
  iso_image_descriptor_text(out, 8u, 32u, "CUPID OS");
  iso_image_descriptor_text(out, 40u, 32u, "CUPID_OS_TEST");
  iso_image_both32(out + 80u, blocks);
  iso_image_both16(out + 120u, 1u); iso_image_both16(out + 124u, 1u);
  iso_image_both16(out + 128u, ISO_IMAGE_BLOCK); iso_image_both32(out + 132u, path_size);
  iso_image_u32(out + 140u, 18u, 0); iso_image_u32(out + 148u, big_path_block, 1);
  (void)iso_image_record(out + 156u, root, zero, 1u, 0, 0, 1, 0u, 0u);
  iso_image_descriptor_text(out, 190u, 128u, "CUPID_OS_TEST_FIXTURE");
  iso_image_descriptor_text(out, 318u, 128u, "CUPID OS");
  iso_image_descriptor_text(out, 446u, 128u, "CUPID OS REPOSITORY HOSTBUILD");
  iso_image_descriptor_text(out, 574u, 128u, "CUPID OS DETERMINISTIC ISO9660 AUTHOR");
  iso_image_descriptor_text(out, 702u, 37u, ""); iso_image_descriptor_text(out, 739u, 37u, "");
  iso_image_descriptor_text(out, 776u, 37u, "");
  (void)memcpy(out + 813u, date, 17u); (void)memcpy(out + 830u, date, 17u);
  (void)memcpy(out + 847u, unspecified, 17u); (void)memcpy(out + 864u, date, 17u); out[881u] = 1u;
}

static int iso_image_table(iso_image_check_t *check, iso_image_node_t *nodes,
                             const ctool_u32 *directories, ctool_u32 count, int big) {
  unsigned char record[20];
  ctool_u32 start = check->offset;
  ctool_u32 index;
  for (index = 0u; index < count; index++) {
    iso_image_node_t *node = &nodes[directories[index]];
    ctool_u32 size = 8u + node->identifier_size + node->identifier_size % 2u;
    (void)memset(record, 0, size); record[0] = (unsigned char)node->identifier_size;
    iso_image_u32(record + 2u, node->extent, big);
    iso_image_u16(record + 6u, nodes[node->parent].number, big);
    (void)memcpy(record + 8u, node->identifier, node->identifier_size);
    if (!iso_image_bytes(check, record, size)) return 0;
  }
  return iso_image_zeros(check, iso_image_padding(check->offset - start));
}

static int iso_image_directory(iso_image_check_t *check, iso_image_node_t *nodes,
                                 const ctool_u32 *children, ctool_u32 owner,
                                 ctool_u32 continuation, ctool_u32 continuation_size) {
  unsigned char record[256];
  unsigned char special[1];
  ctool_u32 start = check->offset;
  ctool_u32 index;
  for (index = 0u; index < nodes[owner].child_count + 2u; index++) {
    iso_image_node_t *target;
    const unsigned char *identifier;
    ctool_u32 identifier_size;
    ctool_u32 size;
    if (index < 2u) {
      target = &nodes[index == 0u ? owner : nodes[owner].parent];
      special[0] = (unsigned char)index; identifier = special; identifier_size = 1u;
    } else {
      target = &nodes[children[nodes[owner].child_start + index - 2u]];
      identifier = target->identifier; identifier_size = target->identifier_size;
    }
    size = iso_image_record(record, target, identifier, identifier_size,
        owner == 0u && index == 0u, index >= 2u, 0, continuation, continuation_size);
    if (size > ISO_IMAGE_BLOCK - (check->offset - start) % ISO_IMAGE_BLOCK &&
        !iso_image_zeros(check, iso_image_padding(check->offset - start))) return 0;
    if (!iso_image_bytes(check, record, size)) return 0;
  }
  return iso_image_zeros(check, iso_image_padding(check->offset - start));
}

int cupidbuild_iso_image_validate(
    ctool_arena_t *arena, const ctool_source_t *manifest,
    const ctool_obj_iso_fixture_request_t *inventory, const ctool_source_t *image,
    cupidbuild_iso_image_report_t *report, char *error, ctool_u32 error_capacity) {
  cupidbuild_iso_image_report_t candidate;
  ctool_arena_mark_t mark;
  iso_image_node_t *nodes = (iso_image_node_t *)0;
  ctool_u32 *order = (ctool_u32 *)0;
  ctool_u32 *children = (ctool_u32 *)0;
  ctool_u32 *directories = (ctool_u32 *)0;
  ctool_u32 *used = (ctool_u32 *)0;
  ctool_u32 count;
  ctool_u32 index;
  ctool_u32 child_count = 0u;
  ctool_u32 directory_count = 1u;
  ctool_u32 path_size = 0u;
  ctool_u32 path_blocks;
  ctool_u64 next_block;
  ctool_u32 continuation;
  unsigned char descriptor[ISO_IMAGE_BLOCK];
  unsigned char extension[255];
  ctool_u32 extension_size;
  const char *failure = (const char *)0;
  iso_image_check_t check;
  if (report != (cupidbuild_iso_image_report_t *)0) (void)memset(report, 0, sizeof(*report));
  if (error != (char *)0 && error_capacity != 0u) error[0] = 0;
  if (arena == (ctool_arena_t *)0 || report == (cupidbuild_iso_image_report_t *)0 ||
      (error == (char *)0 && error_capacity != 0u) || image == (const ctool_source_t *)0 ||
      image->contents.data == (const ctool_u8 *)0)
    return iso_image_error(error, error_capacity, "ISO image, arena and report are required");
  (void)memset(&candidate, 0, sizeof(candidate));
  if (!cupidbuild_iso_inventory_validate(manifest, inventory, &candidate.inventory, error, error_capacity)) return 0;
  mark = ctool_arena_mark(arena); count = inventory->entry_count + 1u;
  if (ctool_arena_alloc_zero(arena, count, sizeof(*nodes), 8u, (void **)&nodes) != CTOOL_OK ||
      ctool_arena_alloc_zero(arena, count, sizeof(*order), 4u, (void **)&order) != CTOOL_OK ||
      ctool_arena_alloc_zero(arena, count, sizeof(*children), 4u, (void **)&children) != CTOOL_OK ||
      ctool_arena_alloc_zero(arena, count, sizeof(*directories), 4u, (void **)&directories) != CTOOL_OK ||
      ctool_arena_alloc_zero(arena, ISO_IMAGE_IDENTIFIERS, sizeof(*used), 4u, (void **)&used) != CTOOL_OK) {
    failure = "ISO image layout exceeds arena storage"; goto done;
  }
  nodes[0].directory = 1u; nodes[0].number = 1u; nodes[0].identifier_size = 1u;
  for (index = 1u; index < count; index++) {
    const ctool_obj_iso_fixture_entry_t *entry = &inventory->entries[index - 1u];
    ctool_u32 start = entry->path.size;
    ctool_u32 position = index - 1u;
    ctool_string_t parent;
    nodes[index].path = entry->path; nodes[index].directory = entry->kind == CTOOL_OBJ_ISO_FIXTURE_DIRECTORY;
    nodes[index].source = entry->source;
    nodes[index].size = nodes[index].directory ? 0u : entry->source->contents.size;
    while (start != 0u && entry->path.data[start - 1u] != '/') start--;
    nodes[index].name.data = entry->path.data + start; nodes[index].name.size = entry->path.size - start;
    parent = entry->path; parent.size = start == 0u ? 0u : start - 1u;
    if (parent.size != 0u) {
      ctool_u32 other;
      for (other = 0u; other < inventory->entry_count; other++)
        if (iso_image_compare(parent, inventory->entries[other].path, 0) == 0) { nodes[index].parent = other + 1u; break; }
    }
    while (position != 0u && iso_image_compare(entry->path, nodes[order[position - 1u]].path, 1) < 0) {
      order[position] = order[position - 1u]; position--;
    }
    order[position] = index;
  }
  for (index = 0u; index < count; index++) {
    ctool_u32 item;
    nodes[index].child_start = child_count;
    (void)memset(used, 0, ISO_IMAGE_IDENTIFIERS * sizeof(*used));
    for (item = 0u; item + 1u < count; item++) {
      ctool_u32 child = order[item];
      ctool_u32 sequence = 0u;
      if (nodes[child].parent != index) continue;
      do { iso_image_base_identifier(&nodes[child], sequence++); }
      while (!iso_image_identifier_unused(nodes, used, child));
      children[child_count++] = child; nodes[index].child_count++;
      nodes[index].directory_children += nodes[child].directory;
    }
    for (item = 1u; item < nodes[index].child_count; item++) {
      ctool_u32 position = item;
      ctool_u32 child = children[nodes[index].child_start + item];
      while (position != 0u && iso_image_identifier_compare(&nodes[child],
          &nodes[children[nodes[index].child_start + position - 1u]]) < 0) {
        children[nodes[index].child_start + position] = children[nodes[index].child_start + position - 1u]; position--;
      }
      children[nodes[index].child_start + position] = child;
    }
  }
  for (index = 0u; index < directory_count; index++) {
    ctool_u32 owner = directories[index];
    ctool_u32 child;
    for (child = 0u; child < nodes[owner].child_count; child++) {
      ctool_u32 node = children[nodes[owner].child_start + child];
      if (nodes[node].directory) { nodes[node].number = directory_count + 1u; directories[directory_count++] = node; }
    }
    nodes[owner].size = iso_image_directory_size(nodes, children, owner);
    path_size += 8u + nodes[owner].identifier_size + nodes[owner].identifier_size % 2u;
  }
  path_blocks = (path_size + ISO_IMAGE_BLOCK - 1u) / ISO_IMAGE_BLOCK;
  next_block = 18u + 2u * path_blocks;
  for (index = 0u; index < directory_count; index++) {
    nodes[directories[index]].extent = (ctool_u32)next_block;
    next_block += nodes[directories[index]].size / ISO_IMAGE_BLOCK;
  }
  continuation = (ctool_u32)next_block++;
  for (index = 0u; index + 1u < count; index++) {
    iso_image_node_t *node = &nodes[order[index]];
    if (!node->directory && node->size != 0u) {
      if (next_block > 0xffffffffu) { failure = "ISO image extent exceeds 32-bit storage"; goto done; }
      node->extent = (ctool_u32)next_block;
      next_block += ((ctool_u64)node->size + ISO_IMAGE_BLOCK - 1u) / ISO_IMAGE_BLOCK;
    }
  }
  if (next_block * ISO_IMAGE_BLOCK > 0xffffffffu) { failure = "ISO image exceeds 32-bit source storage"; goto done; }
  candidate.blocks = (ctool_u32)next_block; candidate.image_bytes = candidate.blocks * ISO_IMAGE_BLOCK;
  candidate.path_table_bytes = path_size; candidate.continuation_block = continuation;
  if (image->contents.size != candidate.image_bytes) { failure = "ISO image size differs from captured inventory"; goto done; }
  check.image = image; check.offset = 0u;
  extension_size = iso_image_continuation(extension);
  iso_image_descriptor(descriptor, &nodes[0], candidate.blocks, path_size, 18u + path_blocks);
  if (!iso_image_zeros(&check, 16u * ISO_IMAGE_BLOCK) ||
      !iso_image_bytes(&check, descriptor, ISO_IMAGE_BLOCK)) { failure = "ISO system area or primary descriptor differs"; goto done; }
  (void)memset(descriptor, 0, sizeof(descriptor)); (void)memcpy(descriptor, "\377CD001\001", 7u);
  if (!iso_image_bytes(&check, descriptor, ISO_IMAGE_BLOCK)) { failure = "ISO descriptor terminator differs"; goto done; }
  if (!iso_image_table(&check, nodes, directories, directory_count, 0) ||
      !iso_image_table(&check, nodes, directories, directory_count, 1)) { failure = "ISO path tables or padding differ"; goto done; }
  for (index = 0u; index < directory_count; index++) {
    if (!iso_image_directory(&check, nodes, children, directories[index], continuation, extension_size)) {
      failure = "ISO directory records or padding differ"; goto done;
    }
  }
  if (!iso_image_bytes(&check, extension, extension_size) ||
      !iso_image_zeros(&check, ISO_IMAGE_BLOCK - extension_size)) { failure = "ISO Rock Ridge continuation or padding differs"; goto done; }
  for (index = 0u; index + 1u < count; index++) {
    iso_image_node_t *node = &nodes[order[index]];
    if (!node->directory && node->size != 0u &&
        (!iso_image_bytes(&check, node->source->contents.data, node->size) ||
         !iso_image_zeros(&check, iso_image_padding(node->size)))) { failure = "ISO file payload or padding differs"; goto done; }
  }
  if (check.offset != candidate.image_bytes) failure = "ISO image has unaccounted bytes";
done:
  if (ctool_arena_rewind(arena, mark) != CTOOL_OK) failure = "ISO image arena rewind failed";
  if (failure != (const char *)0) return iso_image_error(error, error_capacity, failure);
  *report = candidate;
  return 1;
}
