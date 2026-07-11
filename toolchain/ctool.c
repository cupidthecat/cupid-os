#include "ctool.h"

#define CTOOL_U32_MAX 4294967295u
#define CTOOL_MAX_ALIGNMENT 4096u

#if defined(__SIZEOF_POINTER__) && (__SIZEOF_POINTER__ == 8)
typedef unsigned long long ctool_uintptr_t;
#else
typedef unsigned int ctool_uintptr_t;
#endif

typedef struct ctool_arena_block {
  struct ctool_arena_block *previous;
  ctool_u32 allocation_bytes;
  ctool_u32 payload_bytes;
  ctool_u32 used;
  ctool_u32 generation;
} ctool_arena_block_t;

struct ctool_arena {
  ctool_allocator_t allocator;
  ctool_arena_block_t *current;
  ctool_u32 block_bytes;
  ctool_u32 byte_limit;
  ctool_u32 committed_bytes;
  ctool_u32 next_block_generation;
  ctool_u32 allocation_bytes;
};

struct ctool_buffer {
  ctool_allocator_t allocator;
  ctool_u8 *data;
  ctool_u32 size;
  ctool_u32 capacity;
  ctool_u32 byte_limit;
  ctool_u32 allocation_bytes;
};

struct ctool_job {
  ctool_job_config_t config;
  ctool_arena_t *arena;
  ctool_diagnostic_t *diagnostics;
  char *diagnostic_paths;
  char *diagnostic_messages;
  ctool_u32 diagnostic_count;
  ctool_u32 diagnostic_path_stride;
  ctool_u32 diagnostic_message_stride;
  ctool_u32 diagnostic_allocation_bytes;
  ctool_bool has_errors;
  ctool_u32 allocation_bytes;
};

static void ctool_zero(void *destination, ctool_u32 size) {
  ctool_u8 *bytes = (ctool_u8 *)destination;
  ctool_u32 index;
  for (index = 0; index < size; index++) {
    bytes[index] = 0;
  }
}

static void ctool_copy(void *destination, const void *source, ctool_u32 size) {
  ctool_u8 *to = (ctool_u8 *)destination;
  const ctool_u8 *from = (const ctool_u8 *)source;
  ctool_u32 index;
  for (index = 0; index < size; index++) {
    to[index] = from[index];
  }
}

static ctool_bool ctool_add_overflows(ctool_u32 left, ctool_u32 right) {
  return left > CTOOL_U32_MAX - right ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool ctool_multiply_overflows(ctool_u32 left, ctool_u32 right) {
  if (left == 0u) {
    return CTOOL_FALSE;
  }
  return right > CTOOL_U32_MAX / left ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_bool ctool_valid_allocator(ctool_allocator_t allocator) {
  return allocator.allocate != (void *(*)(void *, ctool_u32))0 &&
                 allocator.release !=
                     (void (*)(void *, void *, ctool_u32))0
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

ctool_string_t ctool_string(const char *text) {
  ctool_string_t result;
  ctool_u32 size = 0;
  result.data = text;
  if (text != (const char *)0) {
    while (size != CTOOL_U32_MAX && text[size] != '\0') {
      size++;
    }
  }
  result.size = size;
  return result;
}

ctool_bytes_t ctool_bytes(const void *data, ctool_u32 size) {
  ctool_bytes_t result;
  result.data = (const ctool_u8 *)data;
  result.size = size;
  return result;
}

ctool_limits_t ctool_default_limits(void) {
  ctool_limits_t limits;
  limits.arena_block_bytes = 16384u;
  limits.arena_bytes = 8u * 1024u * 1024u;
  limits.source_bytes = 4u * 1024u * 1024u;
  limits.output_bytes = 16u * 1024u * 1024u;
  limits.path_bytes = 4096u;
  limits.diagnostic_count = 256u;
  limits.diagnostic_message_bytes = 4096u;
  return limits;
}

const char *ctool_status_name(ctool_status_t status) {
  switch (status) {
  case CTOOL_OK:
    return "ok";
  case CTOOL_ERR_INVALID_ARGUMENT:
    return "invalid_argument";
  case CTOOL_ERR_INPUT:
    return "input";
  case CTOOL_ERR_NOT_FOUND:
    return "not_found";
  case CTOOL_ERR_IO:
    return "io";
  case CTOOL_ERR_NO_MEMORY:
    return "no_memory";
  case CTOOL_ERR_LIMIT:
    return "limit";
  case CTOOL_ERR_OVERFLOW:
    return "overflow";
  case CTOOL_ERR_PATH:
    return "path";
  case CTOOL_ERR_PATH_ESCAPE:
    return "path_escape";
  case CTOOL_ERR_UNSUPPORTED:
    return "unsupported";
  case CTOOL_ERR_INTERNAL:
    return "internal";
  default:
    return "unknown";
  }
}

ctool_status_t ctool_arena_open(ctool_allocator_t allocator,
                                ctool_u32 block_bytes,
                                ctool_u32 byte_limit,
                                ctool_arena_t **arena_out) {
  ctool_arena_t *arena;
  ctool_u32 object_bytes = (ctool_u32)sizeof(ctool_arena_t);
  if (arena_out == (ctool_arena_t **)0 ||
      ctool_valid_allocator(allocator) == CTOOL_FALSE || block_bytes == 0u ||
      byte_limit == 0u || block_bytes > byte_limit) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *arena_out = (ctool_arena_t *)0;
  arena = (ctool_arena_t *)allocator.allocate(allocator.context, object_bytes);
  if (arena == (ctool_arena_t *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  ctool_zero(arena, object_bytes);
  arena->allocator = allocator;
  arena->block_bytes = block_bytes;
  arena->byte_limit = byte_limit;
  arena->next_block_generation = 1u;
  arena->allocation_bytes = object_bytes;
  *arena_out = arena;
  return CTOOL_OK;
}

static void ctool_arena_release_blocks(ctool_arena_t *arena) {
  ctool_arena_block_t *block = arena->current;
  while (block != (ctool_arena_block_t *)0) {
    ctool_arena_block_t *previous = block->previous;
    arena->allocator.release(arena->allocator.context, block,
                             block->allocation_bytes);
    block = previous;
  }
  arena->current = (ctool_arena_block_t *)0;
  arena->committed_bytes = 0u;
}

void ctool_arena_close(ctool_arena_t *arena) {
  ctool_allocator_t allocator;
  ctool_u32 allocation_bytes;
  if (arena == (ctool_arena_t *)0) {
    return;
  }
  allocator = arena->allocator;
  allocation_bytes = arena->allocation_bytes;
  ctool_arena_release_blocks(arena);
  allocator.release(allocator.context, arena, allocation_bytes);
}

static ctool_status_t ctool_arena_add_block(ctool_arena_t *arena,
                                            ctool_u32 bytes,
                                            ctool_u32 alignment) {
  ctool_u32 minimum_payload;
  ctool_u32 payload;
  ctool_u32 header_bytes = (ctool_u32)sizeof(ctool_arena_block_t);
  ctool_u32 allocation_bytes;
  ctool_arena_block_t *block;
  if (arena->next_block_generation == CTOOL_U32_MAX) {
    return CTOOL_ERR_OVERFLOW;
  }
  if (ctool_add_overflows(bytes, alignment - 1u) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  minimum_payload = bytes + alignment - 1u;
  payload = arena->block_bytes;
  if (payload < minimum_payload) {
    payload = minimum_payload;
  }
  if (payload > arena->byte_limit - arena->committed_bytes) {
    return CTOOL_ERR_LIMIT;
  }
  if (ctool_add_overflows(header_bytes, payload) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  allocation_bytes = header_bytes + payload;
  block = (ctool_arena_block_t *)arena->allocator.allocate(
      arena->allocator.context, allocation_bytes);
  if (block == (ctool_arena_block_t *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  block->previous = arena->current;
  block->allocation_bytes = allocation_bytes;
  block->payload_bytes = payload;
  block->used = 0u;
  block->generation = arena->next_block_generation;
  arena->next_block_generation++;
  ctool_zero(block + 1, payload);
  arena->current = block;
  arena->committed_bytes += payload;
  return CTOOL_OK;
}

ctool_status_t ctool_arena_alloc(ctool_arena_t *arena, ctool_u32 bytes,
                                 ctool_u32 alignment, void **allocation_out) {
  ctool_arena_block_t *block;
  ctool_uintptr_t address;
  ctool_uintptr_t aligned;
  ctool_u32 padding;
  ctool_status_t status;
  if (allocation_out == (void **)0 || arena == (ctool_arena_t *)0 ||
      bytes == 0u || alignment == 0u ||
      (alignment & (alignment - 1u)) != 0u ||
      alignment > CTOOL_MAX_ALIGNMENT) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *allocation_out = (void *)0;
  block = arena->current;
  if (block != (ctool_arena_block_t *)0) {
    address = (ctool_uintptr_t)(void *)(block + 1) + block->used;
    aligned = (address + (ctool_uintptr_t)(alignment - 1u)) &
              ~((ctool_uintptr_t)alignment - 1u);
    padding = (ctool_u32)(aligned - address);
    if (padding <= block->payload_bytes - block->used &&
        bytes <= block->payload_bytes - block->used - padding) {
      block->used += padding + bytes;
      *allocation_out = (void *)aligned;
      return CTOOL_OK;
    }
  }
  status = ctool_arena_add_block(arena, bytes, alignment);
  if (status != CTOOL_OK) {
    return status;
  }
  block = arena->current;
  address = (ctool_uintptr_t)(void *)(block + 1);
  aligned = (address + (ctool_uintptr_t)(alignment - 1u)) &
            ~((ctool_uintptr_t)alignment - 1u);
  padding = (ctool_u32)(aligned - address);
  block->used = padding + bytes;
  *allocation_out = (void *)aligned;
  return CTOOL_OK;
}

ctool_status_t ctool_arena_alloc_zero(ctool_arena_t *arena, ctool_u32 count,
                                      ctool_u32 element_bytes,
                                      ctool_u32 alignment,
                                      void **allocation_out) {
  ctool_u32 total;
  ctool_status_t status;
  if (allocation_out == (void **)0 || count == 0u || element_bytes == 0u) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (ctool_multiply_overflows(count, element_bytes) == CTOOL_TRUE) {
    *allocation_out = (void *)0;
    return CTOOL_ERR_OVERFLOW;
  }
  total = count * element_bytes;
  status = ctool_arena_alloc(arena, total, alignment, allocation_out);
  if (status == CTOOL_OK) {
    ctool_zero(*allocation_out, total);
  }
  return status;
}

ctool_arena_mark_t ctool_arena_mark(const ctool_arena_t *arena) {
  ctool_arena_mark_t mark;
  mark.owner = arena;
  mark.block = (void *)0;
  mark.used = 0u;
  mark.generation = 0u;
  if (arena != (const ctool_arena_t *)0) {
    mark.block = arena->current;
    mark.used = arena->current != (ctool_arena_block_t *)0
                    ? arena->current->used
                    : 0u;
    mark.generation = arena->current != (ctool_arena_block_t *)0
                          ? arena->current->generation
                          : 0u;
  }
  return mark;
}

ctool_status_t ctool_arena_rewind(ctool_arena_t *arena,
                                  ctool_arena_mark_t mark) {
  ctool_arena_block_t *block;
  ctool_arena_block_t *target = (ctool_arena_block_t *)mark.block;
  if (arena == (ctool_arena_t *)0 || mark.owner != arena ||
      (target == (ctool_arena_block_t *)0 && mark.generation != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  block = arena->current;
  while (block != (ctool_arena_block_t *)0 && block != target) {
    block = block->previous;
  }
  if (target != (ctool_arena_block_t *)0 &&
      (block == (ctool_arena_block_t *)0 ||
       target->generation != mark.generation || mark.used > target->used)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  while (arena->current != target) {
    ctool_arena_block_t *released = arena->current;
    arena->current = released->previous;
    arena->committed_bytes -= released->payload_bytes;
    arena->allocator.release(arena->allocator.context, released,
                             released->allocation_bytes);
  }
  if (target != (ctool_arena_block_t *)0) {
    ctool_zero((ctool_u8 *)(void *)(target + 1) + mark.used,
               target->used - mark.used);
    target->used = mark.used;
  }
  return CTOOL_OK;
}

ctool_status_t ctool_arena_copy_bytes(ctool_arena_t *arena,
                                      ctool_bytes_t input,
                                      ctool_bytes_t *copy_out) {
  void *copy;
  ctool_status_t status;
  if (copy_out == (ctool_bytes_t *)0 ||
      (input.data == (const ctool_u8 *)0 && input.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  copy_out->data = (const ctool_u8 *)0;
  copy_out->size = 0u;
  if (input.size == 0u) {
    return CTOOL_OK;
  }
  status = ctool_arena_alloc(arena, input.size, 1u, &copy);
  if (status != CTOOL_OK) {
    return status;
  }
  ctool_copy(copy, input.data, input.size);
  copy_out->data = (const ctool_u8 *)copy;
  copy_out->size = input.size;
  return CTOOL_OK;
}

ctool_status_t ctool_arena_copy_string(ctool_arena_t *arena,
                                       ctool_string_t input,
                                       ctool_string_t *copy_out) {
  char *copy;
  ctool_status_t status;
  if (copy_out == (ctool_string_t *)0 ||
      (input.data == (const char *)0 && input.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  copy_out->data = (const char *)0;
  copy_out->size = 0u;
  if (ctool_add_overflows(input.size, 1u) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  status = ctool_arena_alloc(arena, input.size + 1u, 1u, (void **)&copy);
  if (status != CTOOL_OK) {
    return status;
  }
  if (input.size != 0u) {
    ctool_copy(copy, input.data, input.size);
  }
  copy[input.size] = '\0';
  copy_out->data = copy;
  copy_out->size = input.size;
  return CTOOL_OK;
}

ctool_status_t ctool_buffer_open(ctool_allocator_t allocator,
                                 ctool_u32 initial_capacity,
                                 ctool_u32 byte_limit,
                                 ctool_buffer_t **buffer_out) {
  ctool_buffer_t *buffer;
  ctool_u32 object_bytes = (ctool_u32)sizeof(ctool_buffer_t);
  if (buffer_out == (ctool_buffer_t **)0 ||
      ctool_valid_allocator(allocator) == CTOOL_FALSE || byte_limit == 0u ||
      initial_capacity > byte_limit) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *buffer_out = (ctool_buffer_t *)0;
  buffer = (ctool_buffer_t *)allocator.allocate(allocator.context, object_bytes);
  if (buffer == (ctool_buffer_t *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  ctool_zero(buffer, object_bytes);
  buffer->allocator = allocator;
  buffer->byte_limit = byte_limit;
  buffer->allocation_bytes = object_bytes;
  if (initial_capacity != 0u) {
    buffer->data =
        (ctool_u8 *)allocator.allocate(allocator.context, initial_capacity);
    if (buffer->data == (ctool_u8 *)0) {
      allocator.release(allocator.context, buffer, object_bytes);
      return CTOOL_ERR_NO_MEMORY;
    }
    ctool_zero(buffer->data, initial_capacity);
    buffer->capacity = initial_capacity;
  }
  *buffer_out = buffer;
  return CTOOL_OK;
}

void ctool_buffer_close(ctool_buffer_t *buffer) {
  ctool_allocator_t allocator;
  ctool_u32 object_bytes;
  if (buffer == (ctool_buffer_t *)0) {
    return;
  }
  allocator = buffer->allocator;
  object_bytes = buffer->allocation_bytes;
  if (buffer->data != (ctool_u8 *)0) {
    allocator.release(allocator.context, buffer->data, buffer->capacity);
  }
  allocator.release(allocator.context, buffer, object_bytes);
}

void ctool_buffer_clear(ctool_buffer_t *buffer) {
  if (buffer != (ctool_buffer_t *)0 && buffer->data != (ctool_u8 *)0) {
    ctool_zero(buffer->data, buffer->size);
    buffer->size = 0u;
  }
}

ctool_bytes_t ctool_buffer_view(const ctool_buffer_t *buffer) {
  ctool_bytes_t view;
  view.data = (const ctool_u8 *)0;
  view.size = 0u;
  if (buffer != (const ctool_buffer_t *)0) {
    view.data = buffer->data;
    view.size = buffer->size;
  }
  return view;
}

ctool_u32 ctool_buffer_mark(const ctool_buffer_t *buffer) {
  return buffer != (const ctool_buffer_t *)0 ? buffer->size : 0u;
}

ctool_status_t ctool_buffer_rewind(ctool_buffer_t *buffer, ctool_u32 mark) {
  if (buffer == (ctool_buffer_t *)0 || mark > buffer->size) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (mark < buffer->size) {
    ctool_zero(buffer->data + mark, buffer->size - mark);
  }
  buffer->size = mark;
  return CTOOL_OK;
}

static ctool_status_t ctool_buffer_grow(ctool_buffer_t *buffer,
                                        ctool_u32 required) {
  ctool_u32 capacity;
  ctool_u8 *replacement;
  if (required <= buffer->capacity) {
    return CTOOL_OK;
  }
  if (required > buffer->byte_limit) {
    return CTOOL_ERR_LIMIT;
  }
  capacity = buffer->capacity;
  if (capacity == 0u) {
    capacity = buffer->byte_limit < 16u ? buffer->byte_limit : 16u;
  }
  while (capacity < required) {
    if (capacity > buffer->byte_limit / 2u) {
      capacity = buffer->byte_limit;
    } else {
      capacity *= 2u;
    }
  }
  replacement = (ctool_u8 *)buffer->allocator.allocate(
      buffer->allocator.context, capacity);
  if (replacement == (ctool_u8 *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  ctool_zero(replacement, capacity);
  if (buffer->size != 0u) {
    ctool_copy(replacement, buffer->data, buffer->size);
  }
  if (buffer->data != (ctool_u8 *)0) {
    buffer->allocator.release(buffer->allocator.context, buffer->data,
                              buffer->capacity);
  }
  buffer->data = replacement;
  buffer->capacity = capacity;
  return CTOOL_OK;
}

ctool_status_t ctool_buffer_reserve_zero(ctool_buffer_t *buffer,
                                         ctool_u32 bytes,
                                         ctool_u32 *offset_out,
                                         ctool_mut_bytes_t *reserved_out) {
  ctool_u32 required;
  ctool_status_t status;
  if (buffer == (ctool_buffer_t *)0 || offset_out == (ctool_u32 *)0 ||
      reserved_out == (ctool_mut_bytes_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *offset_out = 0u;
  reserved_out->data = (ctool_u8 *)0;
  reserved_out->size = 0u;
  if (ctool_add_overflows(buffer->size, bytes) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  required = buffer->size + bytes;
  status = ctool_buffer_grow(buffer, required);
  if (status != CTOOL_OK) {
    return status;
  }
  *offset_out = buffer->size;
  if (bytes != 0u) {
    ctool_zero(buffer->data + buffer->size, bytes);
    reserved_out->data = buffer->data + buffer->size;
  }
  reserved_out->size = bytes;
  buffer->size = required;
  return CTOOL_OK;
}

ctool_status_t ctool_buffer_append(ctool_buffer_t *buffer,
                                   ctool_bytes_t bytes) {
  ctool_u32 offset;
  ctool_u32 source_offset = 0u;
  ctool_mut_bytes_t reserved;
  ctool_status_t status;
  ctool_bool internal_source = CTOOL_FALSE;
  if (buffer == (ctool_buffer_t *)0 ||
      (bytes.data == (const ctool_u8 *)0 && bytes.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (buffer->data != (ctool_u8 *)0 && bytes.data != (const ctool_u8 *)0) {
    ctool_uintptr_t base = (ctool_uintptr_t)(void *)buffer->data;
    ctool_uintptr_t source = (ctool_uintptr_t)(const void *)bytes.data;
    if (source >= base && source - base <= buffer->capacity) {
      ctool_uintptr_t difference = source - base;
      if (difference > buffer->size ||
          bytes.size > buffer->size - (ctool_u32)difference) {
        return CTOOL_ERR_INVALID_ARGUMENT;
      }
      source_offset = (ctool_u32)difference;
      internal_source = CTOOL_TRUE;
    }
  }
  status = ctool_buffer_reserve_zero(buffer, bytes.size, &offset, &reserved);
  if (status == CTOOL_OK && bytes.size != 0u) {
    const ctool_u8 *source = internal_source == CTOOL_TRUE
                                 ? buffer->data + source_offset
                                 : bytes.data;
    ctool_copy(reserved.data, source, bytes.size);
  }
  return status;
}

ctool_status_t ctool_buffer_fill(ctool_buffer_t *buffer, ctool_u8 value,
                                 ctool_u32 count) {
  ctool_u32 offset;
  ctool_mut_bytes_t reserved;
  ctool_u32 index;
  ctool_status_t status =
      ctool_buffer_reserve_zero(buffer, count, &offset, &reserved);
  if (status != CTOOL_OK) {
    return status;
  }
  for (index = 0; index < count; index++) {
    reserved.data[index] = value;
  }
  return CTOOL_OK;
}

ctool_status_t ctool_buffer_put_u8(ctool_buffer_t *buffer, ctool_u8 value) {
  return ctool_buffer_append(buffer, ctool_bytes(&value, 1u));
}

ctool_status_t ctool_buffer_put_le16(ctool_buffer_t *buffer, ctool_u16 value) {
  ctool_u8 bytes[2];
  bytes[0] = (ctool_u8)(value & 0xffu);
  bytes[1] = (ctool_u8)((value >> 8u) & 0xffu);
  return ctool_buffer_append(buffer, ctool_bytes(bytes, 2u));
}

ctool_status_t ctool_buffer_put_le32(ctool_buffer_t *buffer, ctool_u32 value) {
  ctool_u8 bytes[4];
  ctool_u32 index;
  for (index = 0; index < 4u; index++) {
    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);
  }
  return ctool_buffer_append(buffer, ctool_bytes(bytes, 4u));
}

ctool_status_t ctool_buffer_put_le64(ctool_buffer_t *buffer, ctool_u64 value) {
  ctool_u8 bytes[8];
  ctool_u32 index;
  for (index = 0; index < 8u; index++) {
    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);
  }
  return ctool_buffer_append(buffer, ctool_bytes(bytes, 8u));
}

static ctool_status_t ctool_buffer_patch(ctool_buffer_t *buffer,
                                         ctool_u32 offset,
                                         const ctool_u8 *bytes,
                                         ctool_u32 count) {
  if (buffer == (ctool_buffer_t *)0 || bytes == (const ctool_u8 *)0 ||
      offset > buffer->size || count > buffer->size - offset) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  ctool_copy(buffer->data + offset, bytes, count);
  return CTOOL_OK;
}

ctool_status_t ctool_buffer_patch_u8(ctool_buffer_t *buffer,
                                     ctool_u32 offset, ctool_u8 value) {
  return ctool_buffer_patch(buffer, offset, &value, 1u);
}

ctool_status_t ctool_buffer_patch_le16(ctool_buffer_t *buffer,
                                       ctool_u32 offset, ctool_u16 value) {
  ctool_u8 bytes[2];
  bytes[0] = (ctool_u8)(value & 0xffu);
  bytes[1] = (ctool_u8)((value >> 8u) & 0xffu);
  return ctool_buffer_patch(buffer, offset, bytes, 2u);
}

ctool_status_t ctool_buffer_patch_le32(ctool_buffer_t *buffer,
                                       ctool_u32 offset, ctool_u32 value) {
  ctool_u8 bytes[4];
  ctool_u32 index;
  for (index = 0; index < 4u; index++) {
    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);
  }
  return ctool_buffer_patch(buffer, offset, bytes, 4u);
}

ctool_status_t ctool_buffer_patch_le64(ctool_buffer_t *buffer,
                                       ctool_u32 offset, ctool_u64 value) {
  ctool_u8 bytes[8];
  ctool_u32 index;
  for (index = 0; index < 8u; index++) {
    bytes[index] = (ctool_u8)((value >> (index * 8u)) & 0xffu);
  }
  return ctool_buffer_patch(buffer, offset, bytes, 8u);
}

static ctool_bool ctool_is_separator(char value) {
  return value == '/' || value == '\\' ? CTOOL_TRUE : CTOOL_FALSE;
}

static ctool_status_t ctool_path_apply(char *output, ctool_u32 *length,
                                       ctool_string_t spelling) {
  ctool_u32 cursor = 0u;
  while (cursor < spelling.size) {
    ctool_u32 start;
    ctool_u32 segment_size;
    while (cursor < spelling.size &&
           ctool_is_separator(spelling.data[cursor]) == CTOOL_TRUE) {
      cursor++;
    }
    start = cursor;
    while (cursor < spelling.size &&
           ctool_is_separator(spelling.data[cursor]) == CTOOL_FALSE) {
      if (spelling.data[cursor] == '\0') {
        return CTOOL_ERR_PATH;
      }
      cursor++;
    }
    segment_size = cursor - start;
    if (segment_size == 0u ||
        (segment_size == 1u && spelling.data[start] == '.')) {
      continue;
    }
    if (segment_size == 2u && spelling.data[start] == '.' &&
        spelling.data[start + 1u] == '.') {
      if (*length == 1u) {
        return CTOOL_ERR_PATH_ESCAPE;
      }
      while (*length > 1u && output[*length - 1u] != '/') {
        (*length)--;
      }
      if (*length > 1u) {
        (*length)--;
      }
      continue;
    }
    if (*length > 1u) {
      output[*length] = '/';
      (*length)++;
    }
    ctool_copy(output + *length, spelling.data + start, segment_size);
    *length += segment_size;
  }
  return CTOOL_OK;
}

ctool_status_t ctool_path_root(ctool_arena_t *arena, ctool_path_t *path_out) {
  return ctool_path_resolve(arena, (const ctool_path_t *)0,
                            ctool_string("/"), 1u, path_out);
}

ctool_status_t ctool_path_resolve(ctool_arena_t *arena,
                                  const ctool_path_t *base_directory,
                                  ctool_string_t spelling,
                                  ctool_u32 max_bytes,
                                  ctool_path_t *path_out) {
  ctool_bool absolute;
  ctool_u32 base_size;
  ctool_u32 allocation_size;
  ctool_u32 length = 1u;
  char *output;
  ctool_status_t status;
  ctool_arena_mark_t mark;
  if (arena == (ctool_arena_t *)0 || path_out == (ctool_path_t *)0 ||
      max_bytes == 0u ||
      (spelling.data == (const char *)0 && spelling.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  path_out->text.data = (const char *)0;
  path_out->text.size = 0u;
  absolute = spelling.size != 0u &&
                     ctool_is_separator(spelling.data[0]) == CTOOL_TRUE
                 ? CTOOL_TRUE
                 : CTOOL_FALSE;
  if (absolute == CTOOL_FALSE && base_directory == (const ctool_path_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  base_size = absolute == CTOOL_TRUE ? 0u : base_directory->text.size;
  if ((base_size != 0u && base_directory->text.data == (const char *)0) ||
      spelling.size > max_bytes || base_size > max_bytes) {
    return CTOOL_ERR_LIMIT;
  }
  if (ctool_add_overflows(base_size, spelling.size) == CTOOL_TRUE ||
      ctool_add_overflows(base_size + spelling.size, 2u) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  allocation_size = base_size + spelling.size + 2u;
  mark = ctool_arena_mark(arena);
  status = ctool_arena_alloc(arena, allocation_size, 1u, (void **)&output);
  if (status != CTOOL_OK) {
    return status;
  }
  output[0] = '/';
  if (absolute == CTOOL_FALSE) {
    status = ctool_path_apply(output, &length, base_directory->text);
  }
  if (status == CTOOL_OK) {
    status = ctool_path_apply(output, &length, spelling);
  }
  if (status == CTOOL_OK && length > max_bytes) {
    status = CTOOL_ERR_LIMIT;
  }
  if (status != CTOOL_OK) {
    (void)ctool_arena_rewind(arena, mark);
    return status;
  }
  output[length] = '\0';
  path_out->text.data = output;
  path_out->text.size = length;
  return CTOOL_OK;
}

ctool_status_t ctool_path_parent(const ctool_path_t *path,
                                 ctool_path_t *parent_out) {
  ctool_u32 cursor;
  if (path == (const ctool_path_t *)0 || parent_out == (ctool_path_t *)0 ||
      path->text.data == (const char *)0 || path->text.size == 0u ||
      path->text.data[0] != '/') {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  cursor = path->text.size;
  while (cursor > 1u && path->text.data[cursor - 1u] != '/') {
    cursor--;
  }
  parent_out->text.data = path->text.data;
  parent_out->text.size = cursor > 1u ? cursor - 1u : 1u;
  return CTOOL_OK;
}

ctool_string_t ctool_path_basename(const ctool_path_t *path) {
  ctool_string_t basename;
  ctool_u32 cursor;
  basename.data = (const char *)0;
  basename.size = 0u;
  if (path == (const ctool_path_t *)0 || path->text.data == (const char *)0 ||
      path->text.size == 0u) {
    return basename;
  }
  cursor = path->text.size;
  while (cursor > 0u && path->text.data[cursor - 1u] != '/') {
    cursor--;
  }
  if (path->text.size == 1u) {
    basename = path->text;
  } else {
    basename.data = path->text.data + cursor;
    basename.size = path->text.size - cursor;
  }
  return basename;
}

ctool_bool ctool_path_equal(const ctool_path_t *left,
                            const ctool_path_t *right) {
  ctool_u32 index;
  if (left == (const ctool_path_t *)0 || right == (const ctool_path_t *)0 ||
      left->text.size != right->text.size ||
      (left->text.size != 0u &&
       (left->text.data == (const char *)0 ||
        right->text.data == (const char *)0))) {
    return CTOOL_FALSE;
  }
  for (index = 0; index < left->text.size; index++) {
    if (left->text.data[index] != right->text.data[index]) {
      return CTOOL_FALSE;
    }
  }
  return CTOOL_TRUE;
}

ctool_bool ctool_path_is_canonical(const ctool_path_t *path) {
  ctool_u32 cursor;
  ctool_u32 segment_start = 1u;
  if (path == (const ctool_path_t *)0 || path->text.data == (const char *)0 ||
      path->text.size == 0u || path->text.data[0] != '/' ||
      (path->text.size > 1u && path->text.data[path->text.size - 1u] == '/')) {
    return CTOOL_FALSE;
  }
  if (path->text.size == 1u) {
    return CTOOL_TRUE;
  }
  for (cursor = 1u; cursor <= path->text.size; cursor++) {
    ctool_bool boundary =
        cursor == path->text.size || path->text.data[cursor] == '/'
            ? CTOOL_TRUE
            : CTOOL_FALSE;
    if (cursor < path->text.size &&
        (path->text.data[cursor] == '\0' || path->text.data[cursor] == '\\')) {
      return CTOOL_FALSE;
    }
    if (boundary == CTOOL_TRUE) {
      ctool_u32 segment_size = cursor - segment_start;
      if (segment_size == 0u ||
          (segment_size == 1u && path->text.data[segment_start] == '.') ||
          (segment_size == 2u && path->text.data[segment_start] == '.' &&
           path->text.data[segment_start + 1u] == '.')) {
        return CTOOL_FALSE;
      }
      segment_start = cursor + 1u;
    }
  }
  return CTOOL_TRUE;
}

static ctool_bool ctool_valid_job_config(const ctool_job_config_t *config) {
  if (config == (const ctool_job_config_t *)0 ||
      ctool_valid_allocator(config->allocator) == CTOOL_FALSE ||
      config->files.file_size ==
          (ctool_status_t(*)(void *, ctool_string_t, ctool_u32 *))0 ||
      config->files.read_exact ==
          (ctool_status_t(*)(void *, ctool_string_t, ctool_u8 *, ctool_u32))0 ||
      config->files.write_all ==
          (ctool_status_t(*)(void *, ctool_string_t, ctool_bytes_t))0 ||
      config->diagnostics.write ==
          (ctool_status_t(*)(void *, ctool_bytes_t))0 ||
      config->limits.arena_block_bytes == 0u ||
      config->limits.arena_bytes == 0u ||
      config->limits.arena_block_bytes > config->limits.arena_bytes ||
      config->limits.source_bytes == 0u ||
      config->limits.output_bytes == 0u || config->limits.path_bytes == 0u ||
      config->limits.diagnostic_count == 0u ||
      config->limits.diagnostic_message_bytes == 0u) {
    return CTOOL_FALSE;
  }
  return CTOOL_TRUE;
}

ctool_status_t ctool_job_open(const ctool_job_config_t *config,
                              ctool_job_t **job_out) {
  ctool_job_t *job;
  ctool_u32 object_bytes = (ctool_u32)sizeof(ctool_job_t);
  ctool_u32 diagnostics_bytes;
  ctool_u32 path_stride;
  ctool_u32 message_stride;
  ctool_u32 paths_bytes;
  ctool_u32 messages_bytes;
  ctool_u32 diagnostic_allocation_bytes;
  ctool_u8 *diagnostic_storage;
  ctool_status_t status;
  if (job_out == (ctool_job_t **)0 ||
      ctool_valid_job_config(config) == CTOOL_FALSE) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *job_out = (ctool_job_t *)0;
  if (ctool_add_overflows(config->limits.path_bytes, 1u) == CTOOL_TRUE ||
      ctool_add_overflows(config->limits.diagnostic_message_bytes, 1u) ==
          CTOOL_TRUE ||
      ctool_multiply_overflows(
          config->limits.diagnostic_count,
          (ctool_u32)sizeof(ctool_diagnostic_t)) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  path_stride = config->limits.path_bytes + 1u;
  message_stride = config->limits.diagnostic_message_bytes + 1u;
  if (ctool_multiply_overflows(config->limits.diagnostic_count,
                               path_stride) == CTOOL_TRUE ||
      ctool_multiply_overflows(config->limits.diagnostic_count,
                               message_stride) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  diagnostics_bytes = config->limits.diagnostic_count *
                      (ctool_u32)sizeof(ctool_diagnostic_t);
  paths_bytes = config->limits.diagnostic_count * path_stride;
  messages_bytes = config->limits.diagnostic_count * message_stride;
  if (ctool_add_overflows(diagnostics_bytes, paths_bytes) == CTOOL_TRUE ||
      ctool_add_overflows(diagnostics_bytes + paths_bytes,
                          messages_bytes) == CTOOL_TRUE) {
    return CTOOL_ERR_OVERFLOW;
  }
  diagnostic_allocation_bytes =
      diagnostics_bytes + paths_bytes + messages_bytes;
  job = (ctool_job_t *)config->allocator.allocate(config->allocator.context,
                                                   object_bytes);
  if (job == (ctool_job_t *)0) {
    return CTOOL_ERR_NO_MEMORY;
  }
  ctool_zero(job, object_bytes);
  job->config = *config;
  job->allocation_bytes = object_bytes;
  status = ctool_arena_open(config->allocator, config->limits.arena_block_bytes,
                            config->limits.arena_bytes, &job->arena);
  if (status != CTOOL_OK) {
    config->allocator.release(config->allocator.context, job, object_bytes);
    return status;
  }
  diagnostic_storage = (ctool_u8 *)config->allocator.allocate(
      config->allocator.context, diagnostic_allocation_bytes);
  if (diagnostic_storage == (ctool_u8 *)0) {
    ctool_arena_close(job->arena);
    config->allocator.release(config->allocator.context, job, object_bytes);
    return CTOOL_ERR_NO_MEMORY;
  }
  ctool_zero(diagnostic_storage, diagnostic_allocation_bytes);
  job->diagnostics = (ctool_diagnostic_t *)(void *)diagnostic_storage;
  job->diagnostic_paths =
      (char *)(void *)(diagnostic_storage + diagnostics_bytes);
  job->diagnostic_messages =
      (char *)(void *)(diagnostic_storage + diagnostics_bytes + paths_bytes);
  job->diagnostic_path_stride = path_stride;
  job->diagnostic_message_stride = message_stride;
  job->diagnostic_allocation_bytes = diagnostic_allocation_bytes;
  *job_out = job;
  return CTOOL_OK;
}

void ctool_job_close(ctool_job_t *job) {
  ctool_allocator_t allocator;
  ctool_u32 object_bytes;
  if (job == (ctool_job_t *)0) {
    return;
  }
  allocator = job->config.allocator;
  object_bytes = job->allocation_bytes;
  if (job->diagnostics != (ctool_diagnostic_t *)0) {
    allocator.release(allocator.context, job->diagnostics,
                      job->diagnostic_allocation_bytes);
  }
  ctool_arena_close(job->arena);
  allocator.release(allocator.context, job, object_bytes);
}

ctool_arena_t *ctool_job_arena(ctool_job_t *job) {
  return job != (ctool_job_t *)0 ? job->arena : (ctool_arena_t *)0;
}

ctool_status_t ctool_job_open_buffer(ctool_job_t *job,
                                     ctool_u32 initial_capacity,
                                     ctool_u32 byte_limit,
                                     ctool_buffer_t **buffer_out) {
  if (job == (ctool_job_t *)0 || byte_limit > job->config.limits.output_bytes) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  return ctool_buffer_open(job->config.allocator, initial_capacity, byte_limit,
                           buffer_out);
}

ctool_status_t ctool_job_load_source(ctool_job_t *job,
                                     const ctool_path_t *path,
                                     ctool_source_t *source_out) {
  ctool_u32 size;
  ctool_u8 *contents;
  ctool_status_t status;
  ctool_arena_mark_t mark;
  ctool_string_t path_copy;
  if (job == (ctool_job_t *)0 || path == (const ctool_path_t *)0 ||
      source_out == (ctool_source_t *)0 ||
      ctool_path_is_canonical(path) == CTOOL_FALSE ||
      path->text.size > job->config.limits.path_bytes) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  source_out->path.text.data = (const char *)0;
  source_out->path.text.size = 0u;
  source_out->contents.data = (const ctool_u8 *)0;
  source_out->contents.size = 0u;
  mark = ctool_arena_mark(job->arena);
  status = ctool_arena_copy_string(job->arena, path->text, &path_copy);
  if (status == CTOOL_OK) {
    status = job->config.files.file_size(job->config.files.context, path_copy,
                                         &size);
  }
  if (status == CTOOL_OK && size > job->config.limits.source_bytes) {
    status = CTOOL_ERR_LIMIT;
  }
  if (status == CTOOL_OK && ctool_add_overflows(size, 1u) == CTOOL_TRUE) {
    status = CTOOL_ERR_OVERFLOW;
  }
  if (status == CTOOL_OK) {
    status = ctool_arena_alloc(job->arena, size + 1u, 1u, (void **)&contents);
  }
  if (status == CTOOL_OK && size != 0u) {
    status = job->config.files.read_exact(job->config.files.context, path_copy,
                                          contents, size);
  }
  if (status != CTOOL_OK) {
    (void)ctool_arena_rewind(job->arena, mark);
    return status;
  }
  contents[size] = 0u;
  source_out->path.text = path_copy;
  source_out->contents.data = contents;
  source_out->contents.size = size;
  return CTOOL_OK;
}

ctool_status_t ctool_job_write(ctool_job_t *job, const ctool_path_t *path,
                               ctool_bytes_t contents) {
  ctool_string_t path_copy;
  ctool_status_t status;
  if (job == (ctool_job_t *)0 || path == (const ctool_path_t *)0 ||
      ctool_path_is_canonical(path) == CTOOL_FALSE ||
      path->text.size > job->config.limits.path_bytes ||
      (contents.data == (const ctool_u8 *)0 && contents.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  status = ctool_arena_copy_string(job->arena, path->text, &path_copy);
  if (status != CTOOL_OK) {
    return status;
  }
  return job->config.files.write_all(job->config.files.context, path_copy,
                                     contents);
}

ctool_status_t ctool_job_emit(ctool_job_t *job,
                              const ctool_diagnostic_t *diagnostic) {
  ctool_diagnostic_t copy;
  char *path;
  char *message;
  if (job == (ctool_job_t *)0 ||
      diagnostic == (const ctool_diagnostic_t *)0 ||
      diagnostic->severity > CTOOL_DIAG_FATAL ||
      (diagnostic->path.data == (const char *)0 &&
       diagnostic->path.size != 0u) ||
      diagnostic->message.data == (const char *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  if (diagnostic->path.size > job->config.limits.path_bytes ||
      diagnostic->message.size >
      job->config.limits.diagnostic_message_bytes) {
    return CTOOL_ERR_LIMIT;
  }
  if (job->diagnostic_count >= job->config.limits.diagnostic_count) {
    return CTOOL_ERR_LIMIT;
  }
  copy = *diagnostic;
  path = job->diagnostic_paths +
         job->diagnostic_count * job->diagnostic_path_stride;
  message = job->diagnostic_messages +
            job->diagnostic_count * job->diagnostic_message_stride;
  ctool_copy(path, diagnostic->path.data, diagnostic->path.size);
  path[diagnostic->path.size] = '\0';
  ctool_copy(message, diagnostic->message.data, diagnostic->message.size);
  message[diagnostic->message.size] = '\0';
  copy.path.data = path;
  copy.message.data = message;
  job->diagnostics[job->diagnostic_count] = copy;
  job->diagnostic_count++;
  if (copy.severity == CTOOL_DIAG_ERROR ||
      copy.severity == CTOOL_DIAG_FATAL) {
    job->has_errors = CTOOL_TRUE;
  }
  return CTOOL_OK;
}

ctool_u32 ctool_job_diagnostic_count(const ctool_job_t *job) {
  return job != (const ctool_job_t *)0 ? job->diagnostic_count : 0u;
}

const ctool_diagnostic_t *ctool_job_diagnostic(const ctool_job_t *job,
                                               ctool_u32 index) {
  if (job == (const ctool_job_t *)0 || index >= job->diagnostic_count) {
    return (const ctool_diagnostic_t *)0;
  }
  return &job->diagnostics[index];
}

ctool_bool ctool_job_has_errors(const ctool_job_t *job) {
  return job != (const ctool_job_t *)0 ? job->has_errors : CTOOL_FALSE;
}

static ctool_status_t ctool_sink_write(ctool_text_sink_t sink,
                                       const void *data, ctool_u32 size) {
  return sink.write(sink.context, ctool_bytes(data, size));
}

static ctool_status_t ctool_sink_literal(ctool_text_sink_t sink,
                                         const char *text) {
  ctool_string_t string = ctool_string(text);
  return ctool_sink_write(sink, string.data, string.size);
}

static ctool_u32 ctool_decimal(char *output, ctool_u32 value) {
  char reverse[10];
  ctool_u32 count = 0u;
  ctool_u32 index;
  do {
    reverse[count] = (char)('0' + (char)(value % 10u));
    count++;
    value /= 10u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    output[index] = reverse[count - index - 1u];
  }
  return count;
}

static ctool_u32 ctool_hex(char *output, ctool_u32 value) {
  static const char digits[] = "0123456789ABCDEF";
  char reverse[8];
  ctool_u32 count = 0u;
  ctool_u32 index;
  do {
    reverse[count] = digits[value & 0x0fu];
    count++;
    value >>= 4u;
  } while (value != 0u);
  for (index = 0u; index < count; index++) {
    output[index] = reverse[count - index - 1u];
  }
  return count;
}

static const char *ctool_severity_name(ctool_diag_severity_t severity) {
  switch (severity) {
  case CTOOL_DIAG_NOTE:
    return "note";
  case CTOOL_DIAG_WARNING:
    return "warning";
  case CTOOL_DIAG_ERROR:
    return "error";
  case CTOOL_DIAG_FATAL:
    return "fatal";
  default:
    return "unknown";
  }
}

ctool_status_t ctool_job_render_diagnostics(const ctool_job_t *job) {
  ctool_u32 index;
  ctool_status_t status;
  if (job == (const ctool_job_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  for (index = 0u; index < job->diagnostic_count; index++) {
    const ctool_diagnostic_t *diagnostic = &job->diagnostics[index];
    char number[10];
    ctool_u32 number_size;
    ctool_string_t path = diagnostic->path;
    if (path.size == 0u) {
      path = ctool_string("<toolchain>");
    }
    status = ctool_sink_write(job->config.diagnostics, path.data, path.size);
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_sink_literal(job->config.diagnostics, ":");
    if (status != CTOOL_OK) {
      return status;
    }
    number_size = ctool_decimal(number, diagnostic->line);
    status = ctool_sink_write(job->config.diagnostics, number, number_size);
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_sink_literal(job->config.diagnostics, ":");
    if (status != CTOOL_OK) {
      return status;
    }
    number_size = ctool_decimal(number, diagnostic->column);
    status = ctool_sink_write(job->config.diagnostics, number, number_size);
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_sink_literal(job->config.diagnostics, ": ");
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_sink_literal(job->config.diagnostics,
                                ctool_severity_name(diagnostic->severity));
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_sink_literal(job->config.diagnostics, " CT");
    if (status != CTOOL_OK) {
      return status;
    }
    number_size = ctool_hex(number, diagnostic->code);
    status = ctool_sink_write(job->config.diagnostics, number, number_size);
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_sink_literal(job->config.diagnostics, ": ");
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_sink_write(job->config.diagnostics, diagnostic->message.data,
                              diagnostic->message.size);
    if (status != CTOOL_OK) {
      return status;
    }
    status = ctool_sink_literal(job->config.diagnostics, "\n");
    if (status != CTOOL_OK) {
      return status;
    }
  }
  return CTOOL_OK;
}

ctool_status_t ctool_invoke(const ctool_job_config_t *config,
                            const ctool_invocation_request_t *request,
                            ctool_invocation_body_t body, void *user_data,
                            ctool_invocation_result_t *result_out) {
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *output = (ctool_buffer_t *)0;
  ctool_path_t root;
  ctool_path_t input_path;
  ctool_path_t output_path;
  ctool_source_t source;
  ctool_invocation_t invocation;
  ctool_invocation_result_t result;
  ctool_status_t status;
  ctool_status_t render_status;
  ctool_bool has_output;
  ctool_zero(&result, (ctool_u32)sizeof(result));
  result.body_status = CTOOL_ERR_INVALID_ARGUMENT;
  if (request == (const ctool_invocation_request_t *)0 ||
      body == (ctool_invocation_body_t)0 ||
      result_out == (ctool_invocation_result_t *)0 ||
      request->input_path.data == (const char *)0 ||
      request->input_path.size == 0u ||
      (request->output_path.data == (const char *)0 &&
       request->output_path.size != 0u)) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  *result_out = result;
  status = ctool_job_open(config, &job);
  if (status != CTOOL_OK) {
    result.body_status = status;
    *result_out = result;
    return status;
  }
  status = ctool_path_root(job->arena, &root);
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(job->arena, &root, request->input_path,
                                config->limits.path_bytes, &input_path);
  }
  has_output = request->output_path.size != 0u ? CTOOL_TRUE : CTOOL_FALSE;
  if (status == CTOOL_OK && has_output == CTOOL_TRUE) {
    status = ctool_path_resolve(job->arena, &root, request->output_path,
                                config->limits.path_bytes, &output_path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &input_path, &source);
  }
  if (status == CTOOL_OK) {
    ctool_u32 initial_capacity =
        config->limits.output_bytes < 256u ? config->limits.output_bytes : 256u;
    status = ctool_job_open_buffer(job, initial_capacity,
                                   config->limits.output_bytes, &output);
  }
  if (status == CTOOL_OK) {
    invocation.job = job;
    invocation.input = &source;
    invocation.output = output;
    status = body(&invocation, user_data);
    result.body_status = status;
    result.input_bytes = source.contents.size;
    result.output_bytes = ctool_buffer_view(output).size;
    result.diagnostic_count = job->diagnostic_count;
    if (status == CTOOL_OK && job->has_errors == CTOOL_TRUE) {
      status = CTOOL_ERR_INPUT;
    }
    if (status == CTOOL_OK && has_output == CTOOL_TRUE) {
      status = ctool_job_write(job, &output_path, ctool_buffer_view(output));
      if (status == CTOOL_OK) {
        result.output_committed = CTOOL_TRUE;
      }
    }
  } else {
    result.body_status = status;
  }
  render_status = ctool_job_render_diagnostics(job);
  if (status == CTOOL_OK && render_status != CTOOL_OK) {
    status = render_status;
  }
  result.diagnostic_count = job->diagnostic_count;
  if (output != (ctool_buffer_t *)0) {
    ctool_buffer_close(output);
  }
  ctool_job_close(job);
  *result_out = result;
  return status;
}
