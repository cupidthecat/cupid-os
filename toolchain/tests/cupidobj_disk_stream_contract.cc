#include "ctool_host.h"
#include "cupidobj.h"

#include <stdio.h>
#include <string.h>

typedef struct {
  const ctool_u8 *memory;
  ctool_u64 size;
  ctool_u64 next;
  ctool_u64 highest;
  ctool_u32 calls;
  ctool_u32 fail_call;
  ctool_bool sequential;
} source_t;

typedef struct {
  const ctool_u8 *reference;
  ctool_u64 extent;
  ctool_u64 seen;
  ctool_u64 highest;
  ctool_u64 kernel_size;
  ctool_u64 kernel_end;
  ctool_u64 fat_start;
  ctool_u64 first_fat;
  ctool_u64 second_fat;
  ctool_u32 fat_lba;
  ctool_u32 fat_sectors;
  ctool_u32 calls;
  ctool_u32 fail_call;
  ctool_u32 last_count;
  ctool_u8 mbr[512];
  ctool_u8 bpb[512];
  const ctool_u8 *kernel;
} sink_t;

static void put16(ctool_u8 *bytes, ctool_u32 at, ctool_u32 value) {
  bytes[at] = (ctool_u8)value;
  bytes[at + 1u] = (ctool_u8)(value >> 8u);
}

static void put32(ctool_u8 *bytes, ctool_u32 at, ctool_u32 value) {
  put16(bytes, at, value);
  put16(bytes, at + 2u, value >> 16u);
}

static ctool_u8 boot_byte(ctool_u64 offset) {
  return (ctool_u8)((offset * 37u + 11u) & 255u);
}

static ctool_status_t read_source(void *context, ctool_u64 offset,
                                  ctool_u8 *bytes, ctool_u32 count) {
  source_t *source = (source_t *)context;
  source->calls++;
  if (count == 0u || count > CTOOL_OBJ_DISK_TEMPLATE_CHUNK_BYTES ||
      offset > source->size || (ctool_u64)count > source->size - offset ||
      (source->sequential != CTOOL_FALSE && offset != source->next)) {
    return CTOOL_ERR_INTERNAL;
  }
  if (source->fail_call == source->calls) {
    bytes[0u] = 0xedu;
    return CTOOL_ERR_IO;
  }
  if (source->memory != (const ctool_u8 *)0) {
    (void)memcpy(bytes, source->memory + (ctool_u32)offset, (size_t)count);
  } else {
    (void)memset(bytes, 0x6b, (size_t)count);
  }
  source->next = offset + count;
  if (offset > source->highest) source->highest = offset;
  return CTOOL_OK;
}

/* This oracle describes disk bytes independently of the shared producer. */
static void init_sink(sink_t *sink, ctool_u32 image, ctool_u32 fat_lba,
                      ctool_u32 fat_sectors, ctool_u32 cluster,
                      ctool_u64 kernel_size, const ctool_u8 *kernel) {
  ctool_u32 partition = image - fat_lba;
  (void)memset(sink, 0, sizeof(*sink));
  sink->fat_lba = fat_lba;
  sink->fat_sectors = fat_sectors;
  sink->extent = ((ctool_u64)fat_lba + 1u + 2u * fat_sectors + 32u) * 512u;
  sink->kernel_size = kernel_size;
  sink->kernel_end = 2560u + kernel_size;
  sink->fat_start = (ctool_u64)fat_lba * 512u;
  sink->first_fat = sink->fat_start + 512u;
  sink->second_fat = sink->first_fat + (ctool_u64)fat_sectors * 512u;
  sink->kernel = kernel;
  sink->mbr[446u] = 0x80u;
  sink->mbr[447u] = sink->mbr[451u] = 0xfeu;
  sink->mbr[448u] = sink->mbr[449u] = 0xffu;
  sink->mbr[452u] = sink->mbr[453u] = 0xffu;
  sink->mbr[450u] = 6u;
  put32(sink->mbr, 454u, fat_lba);
  put32(sink->mbr, 458u, partition);
  sink->mbr[510u] = 0x55u;
  sink->mbr[511u] = 0xaau;
  sink->bpb[0u] = 0xebu;
  sink->bpb[1u] = 0x3cu;
  sink->bpb[2u] = 0x90u;
  (void)memcpy(sink->bpb + 3u, "CUPIDOS ", 8u);
  put16(sink->bpb, 11u, 512u);
  sink->bpb[13u] = (ctool_u8)cluster;
  put16(sink->bpb, 14u, 1u);
  sink->bpb[16u] = 2u;
  put16(sink->bpb, 17u, 512u);
  put16(sink->bpb, 19u, partition < 65536u ? partition : 0u);
  sink->bpb[21u] = 0xf8u;
  put16(sink->bpb, 22u, fat_sectors);
  put16(sink->bpb, 24u, 63u);
  put16(sink->bpb, 26u, 255u);
  put32(sink->bpb, 28u, fat_lba);
  put32(sink->bpb, 32u, partition < 65536u ? 0u : partition);
  sink->bpb[36u] = 0x80u;
  sink->bpb[38u] = 0x29u;
  put32(sink->bpb, 39u, 0x0c001d05u);
  (void)memcpy(sink->bpb + 43u, "CUPIDOS    ", 11u);
  (void)memcpy(sink->bpb + 54u, "FAT16   ", 8u);
  sink->bpb[510u] = 0x55u;
  sink->bpb[511u] = 0xaau;
}

static ctool_u8 expected_byte(const sink_t *sink, ctool_u64 position) {
  ctool_u64 fat = (ctool_u64)sink->fat_lba * 512u;
  ctool_u64 first = fat + 512u;
  ctool_u64 second = first + (ctool_u64)sink->fat_sectors * 512u;
  if (position < 446u || (position >= 512u && position < 2560u))
    return boot_byte(position);
  if (position < 512u) return sink->mbr[(ctool_u32)position];
  if (position >= 2560u && position - 2560u < sink->kernel_size) {
    return sink->kernel != (const ctool_u8 *)0
        ? sink->kernel[(ctool_u32)(position - 2560u)] : (ctool_u8)0x6bu;
  }
  if (position >= fat && position - fat < 512u)
    return sink->bpb[(ctool_u32)(position - fat)];
  if ((position >= first && position - first < 4u) ||
      (position >= second && position - second < 4u)) {
    return position == first || position == second ? (ctool_u8)0xf8u
                                                   : (ctool_u8)0xffu;
  }
  return 0u;
}

/* Classify and clip once per span; the u32 loops still inspect every byte. */
static int validate_spans(const sink_t *sink, ctool_u64 offset,
                           const ctool_u8 *bytes, ctool_u32 count) {
  static const ctool_u8 fat_initial[4] = {0xf8u, 0xffu, 0xffu, 0xffu};
  ctool_u32 done = 0u;
  ctool_u64 callback_end;
  if (count == 0u || count > CTOOL_OBJ_DISK_TEMPLATE_CHUNK_BYTES ||
      offset > sink->extent || (ctool_u64)count > sink->extent - offset)
    return 0;
  callback_end = offset + count;
  while (done < count) {
    ctool_u64 position = offset + done;
    ctool_u64 end;
    const ctool_u8 *expected = (const ctool_u8 *)0;
    ctool_u8 fill = 0u;
    ctool_bool boot_pattern = CTOOL_FALSE;
    ctool_u32 span;
    ctool_u32 index;
    if (position < 446u) {
      end = 446u;
      boot_pattern = CTOOL_TRUE;
    } else if (position < 512u) {
      end = 512u;
      expected = sink->mbr + (ctool_u32)position;
    } else if (position < 2560u) {
      end = 2560u;
      boot_pattern = CTOOL_TRUE;
    } else if (position < sink->kernel_end) {
      end = sink->kernel_end;
      if (sink->kernel != (const ctool_u8 *)0) {
        if (position - 2560u > (ctool_u64)0xffffffffu) return 0;
        expected = sink->kernel + (ctool_u32)(position - 2560u);
      } else {
        fill = 0x6bu;
      }
    } else if (position < sink->fat_start) {
      end = sink->fat_start;
    } else if (position < sink->first_fat) {
      end = sink->first_fat;
      expected = sink->bpb + (ctool_u32)(position - sink->fat_start);
    } else if (position < sink->first_fat + 4u) {
      end = sink->first_fat + 4u;
      expected = fat_initial + (ctool_u32)(position - sink->first_fat);
    } else if (position < sink->second_fat) {
      end = sink->second_fat;
    } else if (position < sink->second_fat + 4u) {
      end = sink->second_fat + 4u;
      expected = fat_initial + (ctool_u32)(position - sink->second_fat);
    } else {
      end = sink->extent;
    }
    if (end > callback_end) end = callback_end;
    if (end <= position) return 0;
    span = (ctool_u32)(end - position);
    if (expected != (const ctool_u8 *)0) {
      for (index = 0u; index < span; index++) {
        if (bytes[done + index] != expected[index]) return 0;
      }
    } else if (boot_pattern != CTOOL_FALSE) {
      ctool_u32 boot_offset = (ctool_u32)position;
      for (index = 0u; index < span; index++) {
        ctool_u8 value =
            (ctool_u8)(((boot_offset + index) * 37u + 11u) & 255u);
        if (bytes[done + index] != value) return 0;
      }
    } else {
      for (index = 0u; index < span; index++) {
        if (bytes[done + index] != fill) return 0;
      }
    }
    done += span;
  }
  return 1;
}

static ctool_status_t write_sink(void *context, ctool_u64 offset,
                                 const ctool_u8 *bytes, ctool_u32 count) {
  sink_t *sink = (sink_t *)context;
  sink->calls++;
  if (offset != sink->seen || count == 0u ||
      count > CTOOL_OBJ_DISK_TEMPLATE_CHUNK_BYTES || offset > sink->extent ||
      (ctool_u64)count > sink->extent - offset) return CTOOL_ERR_INTERNAL;
  if (sink->fail_call == sink->calls) {
    /* A callback can physically consume part of a chunk before failing. */
    sink->seen += count / 2u;
    return CTOOL_ERR_IO;
  }
  if (sink->reference != (const ctool_u8 *)0) {
    if (memcmp(bytes, sink->reference + (ctool_u32)offset, (size_t)count) != 0)
      return CTOOL_ERR_INTERNAL;
  } else if (!validate_spans(sink, offset, bytes, count)) {
    return CTOOL_ERR_INTERNAL;
  }
  sink->seen += count;
  sink->highest = offset;
  sink->last_count = count;
  return CTOOL_OK;
}

static void init_request(ctool_obj_disk_template_stream_request_t *request,
                         source_t *boot, source_t *kernel, sink_t *sink,
                         const ctool_u8 *boot_memory,
                         const ctool_u8 *kernel_memory, ctool_u64 kernel_size,
                         ctool_u32 image, ctool_u32 fat_lba) {
  (void)memset(request, 0, sizeof(*request));
  (void)memset(boot, 0, sizeof(*boot));
  (void)memset(kernel, 0, sizeof(*kernel));
  boot->memory = boot_memory;
  boot->size = 2560u;
  kernel->memory = kernel_memory;
  kernel->size = kernel_size;
  kernel->sequential = CTOOL_TRUE;
  request->bootloader.context = boot;
  request->bootloader.size = boot->size;
  request->bootloader.read_exact = read_source;
  request->kernel.context = kernel;
  request->kernel.size = kernel_size;
  request->kernel.read_exact = read_source;
  request->image_sectors = image;
  request->fat_start_lba = fat_lba;
  request->output_context = sink;
  request->write_exact = write_sink;
}

static int complete(const ctool_obj_disk_template_stream_request_t *request,
                     sink_t *sink, source_t *kernel,
                     ctool_obj_disk_template_stream_result_t *result) {
  ctool_status_t status = ctool_obj_stream_disk_template(request, result);
  ctool_u32 remainder = (ctool_u32)(sink->extent % 65536u);
  return status == CTOOL_OK && result->bytes == sink->extent &&
      result->attempted_write == CTOOL_TRUE && sink->seen == sink->extent &&
      sink->last_count == (remainder != 0u ? remainder : 65536u) &&
      kernel->next == kernel->size;
}

static int run_success(const char *name, ctool_u32 image, ctool_u32 fat_lba,
                        ctool_u32 fat_sectors, ctool_u32 cluster,
                        ctool_u64 kernel_size, int parity) {
  ctool_u8 boot_memory[2560];
  ctool_u8 kernel_memory[1536];
  source_t boot;
  source_t kernel;
  sink_t sink;
  ctool_obj_disk_template_stream_request_t request;
  ctool_obj_disk_template_stream_result_t result;
  ctool_host_adapter_t adapter;
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_buffer_t *buffer = (ctool_buffer_t *)0;
  ctool_source_t boot_source;
  ctool_source_t kernel_source;
  ctool_obj_request_t buffered_request;
  ctool_obj_result_t buffered_result;
  ctool_arena_mark_t before;
  ctool_arena_mark_t after;
  ctool_status_t status;
  ctool_u32 index;
  int ok;
  for (index = 0u; index < (ctool_u32)sizeof(boot_memory); index++)
    boot_memory[index] = boot_byte(index);
  (void)memset(kernel_memory, 0x6b, sizeof(kernel_memory));
  init_sink(&sink, image, fat_lba, fat_sectors, cluster, kernel_size,
            kernel_size <= sizeof(kernel_memory) ? kernel_memory : (const ctool_u8 *)0);
  init_request(&request, &boot, &kernel, &sink, boot_memory, sink.kernel,
               kernel_size, image, fat_lba);
  if (kernel_size == 0u) request.kernel.read_exact = 0;
  if (parity != 0) {
    status = ctool_host_adapter_init(&adapter, ".");
    if (status != CTOOL_OK) return 1;
    config = ctool_host_job_config(&adapter, ctool_default_limits());
    status = ctool_job_open(&config, &job);
    if (status != CTOOL_OK) return 1;
    status = ctool_job_open_buffer(job, 64u, config.limits.output_bytes, &buffer);
    if (status != CTOOL_OK) {
      ctool_job_close(job);
      return 1;
    }
    boot_source.path.text = ctool_string("/boot.bin");
    boot_source.contents = ctool_bytes(boot_memory, (ctool_u32)sizeof(boot_memory));
    kernel_source.path.text = ctool_string("/kernel.bin");
    kernel_source.contents = ctool_bytes(kernel_memory, (ctool_u32)kernel_size);
    (void)memset(&buffered_request, 0, sizeof(buffered_request));
    buffered_request.operation = CTOOL_OBJ_BUILD_DISK_TEMPLATE;
    buffered_request.input = &boot_source;
    buffered_request.as.disk_template.kernel = &kernel_source;
    buffered_request.as.disk_template.image_sectors = image;
    buffered_request.as.disk_template.fat_start_lba = fat_lba;
    before = ctool_arena_mark(ctool_job_arena(job));
    status = ctool_obj_transform(job, &buffered_request, buffer, &buffered_result);
    after = ctool_arena_mark(ctool_job_arena(job));
    if (status != CTOOL_OK || buffered_result.bytes.size != sink.extent ||
        before.owner != after.owner || before.block != after.block ||
        before.used != after.used || before.generation != after.generation) {
      ctool_buffer_close(buffer);
      ctool_job_close(job);
      return 1;
    }
    /* Check every buffered byte with the independent oracle before parity. */
    for (index = 0u; index < buffered_result.bytes.size; index++) {
      if (buffered_result.bytes.data[index] != expected_byte(&sink, index)) {
        ctool_buffer_close(buffer);
        ctool_job_close(job);
        return 1;
      }
    }
    /* Both independent oracles cover every buffered byte before stream parity. */
    for (index = 0u; index < buffered_result.bytes.size;) {
      ctool_u32 count = buffered_result.bytes.size - index;
      if (count > CTOOL_OBJ_DISK_TEMPLATE_CHUNK_BYTES)
        count = CTOOL_OBJ_DISK_TEMPLATE_CHUNK_BYTES;
      if (!validate_spans(&sink, index, buffered_result.bytes.data + index,
                           count)) {
        ctool_buffer_close(buffer);
        ctool_job_close(job);
        return 1;
      }
      index += count;
    }
    sink.reference = buffered_result.bytes.data;
  }
  ok = complete(&request, &sink, &kernel, &result);
  if (strcmp(name, "wide-u64") == 0) {
    ok = ok && sink.highest > (ctool_u64)0xffffffffu &&
        kernel.highest > (ctool_u64)0xffffffffu;
  }
  (void)printf("{\"case\":\"%s\",\"ok\":%s,\"bytes\":%llu,"
               "\"writes\":%u,\"last_count\":%u,\"highest_write\":%llu,"
               "\"kernel_bytes\":%llu,\"highest_read\":%llu}\n",
               name, ok != 0 ? "true" : "false", result.bytes, sink.calls,
               sink.last_count, sink.highest, kernel.next, kernel.highest);
  if (buffer != (ctool_buffer_t *)0) ctool_buffer_close(buffer);
  if (job != (ctool_job_t *)0) ctool_job_close(job);
  return ok != 0 ? 0 : 1;
}

static int rejected(ctool_obj_disk_template_stream_request_t *request,
                      source_t *boot, source_t *kernel, sink_t *sink,
                      ctool_status_t expected) {
  ctool_obj_disk_template_stream_result_t result;
  ctool_status_t status;
  result.bytes = 99u;
  result.attempted_write = CTOOL_TRUE;
  status = ctool_obj_stream_disk_template(request, &result);
  return status == expected && result.bytes == 0u &&
      result.attempted_write == CTOOL_FALSE && boot->calls == 0u &&
      kernel->calls == 0u && sink->calls == 0u;
}

static int run_admission(void) {
  ctool_u8 memory[2560] = {0};
  source_t boot;
  source_t kernel;
  sink_t sink;
  ctool_obj_disk_template_stream_request_t request;
  ctool_obj_disk_template_stream_request_t bad;
  ctool_u32 checks = 0u;
  int ok = 1;
  init_sink(&sink, 4208u, 8u, 17u, 1u, 0u, (const ctool_u8 *)0);
  init_request(&request, &boot, &kernel, &sink, memory, (const ctool_u8 *)0,
               0u, 4208u, 8u);
  ok &= rejected(0, &boot, &kernel, &sink, CTOOL_ERR_INVALID_ARGUMENT); checks++;
  ok &= ctool_obj_stream_disk_template(&request, 0) == CTOOL_ERR_INVALID_ARGUMENT; checks++;
  bad = request; bad.write_exact = 0;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_INVALID_ARGUMENT); checks++;
  bad = request; bad.bootloader.read_exact = 0;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_INVALID_ARGUMENT); checks++;
  bad = request; bad.kernel.size = 1u; bad.kernel.read_exact = 0;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_INVALID_ARGUMENT); checks++;
  bad = request; bad.bootloader.size = 2559u;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_INPUT); checks++;
  bad = request; bad.fat_start_lba = 5u;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_INPUT); checks++;
  bad = request; bad.fat_start_lba = bad.image_sectors;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_INPUT); checks++;
  bad = request; bad.image_sectors = 100u;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_INPUT); checks++;
  bad = request; bad.kernel.size = 1537u;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_INPUT); checks++;
  bad = request; bad.kernel.size = (ctool_u64)-1;
  ok &= rejected(&bad, &boot, &kernel, &sink, CTOOL_ERR_OVERFLOW); checks++;
  ok &= run_success("admission-recovery", 4208u, 8u, 17u, 1u, 8u, 0) == 0;
  (void)printf("{\"case\":\"admission\",\"ok\":%s,\"rejections\":%u}\n",
               ok != 0 ? "true" : "false", checks);
  return ok != 0 ? 0 : 1;
}

static int run_failures(int source_failure) {
  ctool_u8 memory[2560];
  source_t boot;
  source_t kernel;
  sink_t sink;
  ctool_obj_disk_template_stream_request_t request;
  ctool_obj_disk_template_stream_result_t result;
  ctool_u32 index;
  ctool_u32 trial;
  ctool_u32 trials = source_failure != 0 ? 2u : 3u;
  ctool_u64 kernel_size = source_failure != 0 ? 131072u : 8u;
  int ok = 1;
  for (index = 0u; index < (ctool_u32)sizeof(memory); index++)
    memory[index] = boot_byte(index);
  for (trial = 0u; trial < trials; trial++) {
    ctool_status_t status;
    init_sink(&sink, 409600u, 20480u, 190u, 8u, kernel_size, (const ctool_u8 *)0);
    init_request(&request, &boot, &kernel, &sink, memory, (const ctool_u8 *)0,
                 kernel_size, 409600u, 20480u);
    if (source_failure != 0) {
      if (trial == 0u) boot.fail_call = 1u;
      else kernel.fail_call = 2u;
    } else {
      sink.fail_call = trial == 0u ? 1u : trial == 1u ? 81u : 164u;
    }
    result.bytes = 99u;
    result.attempted_write = CTOOL_FALSE;
    status = ctool_obj_stream_disk_template(&request, &result);
    ok &= status == CTOOL_ERR_IO && result.bytes == 0u &&
        result.attempted_write == ((source_failure != 0 && trial == 0u)
            ? CTOOL_FALSE : CTOOL_TRUE);
    if (source_failure != 0) {
      ok &= sink.calls == (trial == 0u ? 0u : 1u);
    } else {
      ok &= sink.calls == sink.fail_call;
    }
    init_sink(&sink, 409600u, 20480u, 190u, 8u, kernel_size, (const ctool_u8 *)0);
    init_request(&request, &boot, &kernel, &sink, memory, (const ctool_u8 *)0,
                 kernel_size, 409600u, 20480u);
    ok &= complete(&request, &sink, &kernel, &result);
  }
  (void)printf("{\"case\":\"%s\",\"ok\":%s,\"failures\":%u,\"recoveries\":%u}\n",
               source_failure != 0 ? "source-failures" : "sink-failures",
               ok != 0 ? "true" : "false", trials, trials);
  return ok != 0 ? 0 : 1;
}

int main(int argc, char **argv) {
  const char *name;
  if (argc != 2) return 2;
  name = argv[1];
  if (strcmp(name, "parity-small") == 0)
    return run_success(name, 4208u, 8u, 17u, 1u, 8u, 1);
  if (strcmp(name, "parity-active") == 0)
    return run_success(name, 409600u, 20480u, 190u, 8u, 8u, 1);
  if (strcmp(name, "fat-cycle") == 0)
    return run_success(name, 8304u, 16u, 17u, 2u, 8u, 1);
  if (strcmp(name, "exact-fit") == 0)
    return run_success(name, 4208u, 8u, 17u, 1u, 1536u, 1);
  if (strcmp(name, "empty-kernel") == 0)
    return run_success(name, 4208u, 8u, 17u, 1u, 0u, 1);
  if (strcmp(name, "chunk-boundary") == 0)
    return run_success(name, 4261u, 61u, 17u, 1u, 8u, 1);
  if (strcmp(name, "large-prefix") == 0)
    return run_success(name, 135272u, 131072u, 17u, 1u, 8u, 0);
  if (strcmp(name, "wide-u64") == 0)
    return run_success(name, 8393064u, 8388864u, 17u, 1u,
                        (ctool_u64)8388864u * 512u - 2560u, 0);
  if (strcmp(name, "admission") == 0) return run_admission();
  if (strcmp(name, "sink-failures") == 0) return run_failures(0);
  if (strcmp(name, "source-failures") == 0) return run_failures(1);
  return 2;
}
