#include "contract_parse_internal.h"
#include "artifact_size_policy.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARTIFACT_COUNT 16u
#define FIXED_ARTIFACT_COUNT 10u










typedef struct {
  text_t path;
  text_t producer;
  text_t reason;
  uint64_t exact_bytes;
} policy_entry_t;

typedef struct {
  policy_entry_t entries[ARTIFACT_COUNT];
  size_t count;
} policy_t;

typedef struct {
  uint64_t sizes[SEED_ARTIFACT_COUNT];
  int seen[SEED_ARTIFACT_COUNT];
  uint64_t source_input_count;
  text_t parent_manifest_sha256;
  text_t parent_source_revision;
  text_t source_revision;
  text_t source_snapshot_sha256;
} seed_manifest_t;

typedef struct {
  uint64_t sizes[SEED_ARTIFACT_COUNT];
  text_t digests[SEED_ARTIFACT_COUNT];
  int seen[SEED_ARTIFACT_COUNT];
} windows_manifest_t;

typedef struct {
  byte_slice_t path;
  uint32_t kind;
  uint64_t size;
} observation_t;

typedef struct {
  byte_slice_t path;
  uint32_t kind;
  uint64_t size;
  byte_slice_t digest;
} windows_observation_t;

static const unsigned char request_magic[8] = {
    'C', 'U', 'P', 'S', 'I', 'Z', 'E', '2'};
static const char policy_schema[] = "cupid.artifact-size-policy.v1";
static const char seed_schema[] = "cupid.bootstrap-seed.v2";
static const char windows_seed_schema[] = "cupid.execution-seed.v2";
static const char preceding_parent_revision[] =
    "83d00ce70e5607dc5c011bb97c6478121f24a21c";
static const char preceding_linux_parent_manifest[] =
    "a11c8af08eb1170d040dc6b361c30df321c088fcb4ae5becd6c2864995380622";
static const char preceding_windows_parent_manifest[] =
    "f5124cbddbeb55a61ce2f8ae93923daae512d6fec6732a532b1e8f0d15bed590";
static const char active_parent_revision[] =
    "142a9737f618ab8500308576a1c222501d639e5f";
static const char active_linux_parent_manifest[] =
    "7eeb40dcb6a66fbd6f3e5cc1798695d5b2895c8e1f693451684a9864f1733b52";
static const char active_windows_parent_manifest[] =
    "2d2cb287d90dd942b95629472e72f74013d8fcc4da64187fe87c0bcd0973cccd";


static const char *const windows_seed_files[SEED_ARTIFACT_COUNT] = {
    "cupidasm.exe", "cupidc.exe", "cupiddis.exe", "cupidld.exe",
    "cupidobj.exe", "cupidbuild.exe"};
static const int windows_seed_producers[SEED_ARTIFACT_COUNT] = {1, 1, 0, 1,
                                                                0, 0};
static const int seed_producers[SEED_ARTIFACT_COUNT] = {1, 1, 0, 1, 0, 0};
static const char *const seed_owners[SEED_ARTIFACT_COUNT] = {
    "CupidASM", "CupidC", "CupidDis", "CupidLD", "CupidObj", "CupidBuild"};
static const char *const fixed_paths[FIXED_ARTIFACT_COUNT] = {
    "boot/boot.bin",
    "bootstrap/seeds/i386-windows/cupidasm.exe",
    "bootstrap/seeds/i386-windows/cupidc.exe",
    "bootstrap/seeds/i386-windows/cupiddis.exe",
    "bootstrap/seeds/i386-windows/cupidld.exe",
    "bootstrap/seeds/i386-windows/cupidobj.exe",
    "bootstrap/seeds/i386-windows/cupidbuild.exe",
    "kernel/kernel.bin",
    "kernel/kernel.elf",
    "kernel/kernel.elf.pass1"};
static const char *const fixed_owners[FIXED_ARTIFACT_COUNT] = {
    "CupidASM", "CupidASM", "CupidC", "CupidDis", "CupidLD",
    "CupidObj", "CupidBuild", "CupidObj", "CupidLD", "CupidLD"};










static int slice_equals_literal(const byte_slice_t *slice,
                                const char *literal) {
  size_t length = strlen(literal);
  return slice->size == length &&
         memcmp(slice->bytes, literal, length) == 0;
}

static int text_equals_text(const text_t *left, const text_t *right) {
  return left->size == right->size &&
         memcmp(left->bytes, right->bytes, left->size) == 0;
}

static int parent_pair_matches(const text_t *manifest,
                               const text_t *revision,
                               const char *preceding_manifest,
                               const char *active_manifest) {
  return (cupid_contract_text_equals_literal(manifest, preceding_manifest) &&
          cupid_contract_text_equals_literal(revision, preceding_parent_revision)) ||
         (cupid_contract_text_equals_literal(manifest, active_manifest) &&
          cupid_contract_text_equals_literal(revision, active_parent_revision));
}

static int lower_hex_valid(const unsigned char *bytes, size_t size,
                           size_t expected_size) {
  size_t index;
  if (size != expected_size) {
    return 0;
  }
  for (index = 0u; index < size; index++) {
    if (!((bytes[index] >= (unsigned char)'0' &&
           bytes[index] <= (unsigned char)'9') ||
          (bytes[index] >= (unsigned char)'a' &&
           bytes[index] <= (unsigned char)'f'))) {
      return 0;
    }
  }
  return 1;
}





















static int json_parse_boolean(error_context_t *context, json_reader_t *reader, int *result) {
  cupid_contract_json_skip_space(reader);
  if (cupid_contract_json_match_literal(reader, "true")) {
    *result = 1;
    return 1;
  }
  if (cupid_contract_json_match_literal(reader, "false")) {
    *result = 0;
    return 1;
  }
  return cupid_contract_set_error(context, "a JSON boolean is required");
}



static int ascii_space(unsigned char value) {
  return value == (unsigned char)' ' || value == (unsigned char)'\t' ||
         value == (unsigned char)'\n' || value == (unsigned char)'\r' ||
         value == (unsigned char)'\f' || value == (unsigned char)'\v';
}





static int parse_manifest_artifact(error_context_t *context, json_reader_t *reader, text_t *name,
                                   text_t *file, uint64_t *size,
                                   int *producer) {
  text_t digest = {(unsigned char *)0, 0u};
  unsigned int fields = 0u;
  int ok = 1;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return 0;
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    reader->position++;
    return cupid_contract_set_error(context, "seed manifest artifact fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    ok = cupid_contract_json_parse_string(context, reader, &key) &&
         cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      break;
    }
    if (cupid_contract_text_equals_literal(&key, "name")) {
      if ((fields & 1u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "seed manifest artifact name is duplicated");
      }
      fields |= 1u;
      ok = cupid_contract_json_parse_string(context, reader, name);
    } else if (cupid_contract_text_equals_literal(&key, "file")) {
      if ((fields & 2u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "seed manifest artifact file is duplicated");
      }
      fields |= 2u;
      ok = cupid_contract_json_parse_string(context, reader, file);
    } else if (cupid_contract_text_equals_literal(&key, "size")) {
      if ((fields & 4u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "seed manifest artifact size is duplicated");
      }
      fields |= 4u;
      ok = cupid_contract_json_parse_positive_u64(context, reader, size);
    } else if (cupid_contract_text_equals_literal(&key, "sha256")) {
      if ((fields & 8u) != 0u) {
        cupid_contract_text_release(&key);
        ok = cupid_contract_set_error(context, "seed manifest artifact digest is duplicated");
        break;
      }
      fields |= 8u;
      ok = cupid_contract_json_parse_string(context, reader, &digest);
      if (ok && !lower_hex_valid(digest.bytes, digest.size, 64u)) {
        ok = cupid_contract_set_error(context, "seed manifest artifact digest is invalid");
      }
    } else if (cupid_contract_text_equals_literal(&key, "producer")) {
      if ((fields & 16u) != 0u) {
        cupid_contract_text_release(&key);
        ok = cupid_contract_set_error(context, "seed manifest artifact producer is duplicated");
        break;
      }
      fields |= 16u;
      ok = json_parse_boolean(context, reader, producer);
    } else {
      ok = cupid_contract_set_error(context, "seed manifest artifact fields differ");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      break;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      ok = 0;
      break;
    }
  }
  if (ok && fields != 31u) {
    ok = cupid_contract_set_error(context, "seed manifest artifact fields are missing");
  }
  cupid_contract_text_release(&digest);
  return ok;
}

static int parse_manifest_artifacts(error_context_t *context, json_reader_t *reader,
                                    seed_manifest_t *manifest) {
  size_t count = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'[')) {
    return 0;
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    reader->position++;
    return cupid_contract_set_error(context, "seed manifest artifacts are missing");
  }
  for (;;) {
    text_t name = {(unsigned char *)0, 0u};
    text_t file = {(unsigned char *)0, 0u};
    uint64_t size = 0u;
    int producer = 0;
    int seed_index;
    int ok;
    if (count >= SEED_ARTIFACT_COUNT) {
      return cupid_contract_set_error(context, "seed manifest has too many artifacts");
    }
    ok = parse_manifest_artifact(context, reader, &name, &file, &size, &producer);
    if (!ok) {
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      return 0;
    }
    seed_index = cupid_contract_seed_index_for_name(&name);
    if (seed_index < 0) {
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      return cupid_contract_set_error(context, "seed manifest has an unknown tool artifact");
    }
    if (manifest->seen[(size_t)seed_index] != 0) {
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      return cupid_contract_set_error(context, "seed manifest tool artifact is duplicated");
    }
    if (!cupid_contract_text_equals_literal(&file, cupid_contract_seed_files[(size_t)seed_index])) {
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      return cupid_contract_set_error(context, "seed manifest artifact filename differs");
    }
    if (producer != seed_producers[(size_t)seed_index]) {
      cupid_contract_text_release(&name);
      cupid_contract_text_release(&file);
      return cupid_contract_set_error(context, "seed manifest artifact producer differs");
    }
    manifest->seen[(size_t)seed_index] = 1;
    manifest->sizes[(size_t)seed_index] = size;
    count++;
    cupid_contract_text_release(&name);
    cupid_contract_text_release(&file);
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (count != SEED_ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "seed manifest does not contain six artifacts");
  }
  return 1;
}

static int parse_expected_text(error_context_t *context, json_reader_t *reader, const char *expected,
                               const char *error);

static int parse_seed_producer_lineage(error_context_t *context, json_reader_t *reader) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return 0;
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "assembly") && (fields & 1u) == 0u) {
      fields |= 1u;
      ok = parse_expected_text(context,
          reader, "stage-three CupidASM from the checked-seed bootstrap",
          "seed manifest producer lineage differs");
    } else if (cupid_contract_text_equals_literal(&key, "c") && (fields & 2u) == 0u) {
      fields |= 2u;
      ok = parse_expected_text(context,
          reader, "stage-three CupidC from the checked-seed bootstrap",
          "seed manifest producer lineage differs");
    } else if (cupid_contract_text_equals_literal(&key, "link") && (fields & 4u) == 0u) {
      fields |= 4u;
      ok = parse_expected_text(context,
          reader, "stage-three CupidLD from the checked-seed bootstrap",
          "seed manifest producer lineage differs");
    } else {
      ok = cupid_contract_set_error(context, "seed manifest producer lineage fields differ");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 7u) {
    return cupid_contract_set_error(context, "seed manifest producer lineage fields are missing");
  }
  return 1;
}

static int parse_seed_provenance(error_context_t *context, json_reader_t *reader,
                                 seed_manifest_t *manifest) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return 0;
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "artifact_generation") &&
        (fields & 1u) == 0u) {
      fields |= 1u;
      ok = parse_expected_text(context, reader, "paired-stage-four-six-tool",
                               "seed manifest provenance differs");
    } else if (cupid_contract_text_equals_literal(&key, "fixed_point_command") &&
               (fields & 2u) == 0u) {
      fields |= 2u;
      ok = parse_expected_text(context, reader, "make bootstrap-from-seed",
                               "seed manifest provenance differs");
    } else if (cupid_contract_text_equals_literal(&key, "fixed_point_result") &&
               (fields & 4u) == 0u) {
      fields |= 4u;
      ok = parse_expected_text(context, reader, "pass",
                               "seed manifest provenance differs");
    } else if (cupid_contract_text_equals_literal(&key, "parent_seed_manifest_sha256") &&
               (fields & 8u) == 0u) {
      fields |= 8u;
      ok = cupid_contract_json_parse_string(context, reader, &manifest->parent_manifest_sha256);
      if (ok && !lower_hex_valid(manifest->parent_manifest_sha256.bytes,
                                 manifest->parent_manifest_sha256.size, 64u)) {
        ok = cupid_contract_set_error(context, "seed manifest parent digest is invalid");
      }
    } else if (cupid_contract_text_equals_literal(&key, "parent_seed_source_revision") &&
               (fields & 16u) == 0u) {
      fields |= 16u;
      ok = cupid_contract_json_parse_string(context, reader, &manifest->parent_source_revision);
      if (ok && !lower_hex_valid(manifest->parent_source_revision.bytes,
                                 manifest->parent_source_revision.size, 40u)) {
        ok = cupid_contract_set_error(context, "seed manifest parent revision is invalid");
      }
    } else if (cupid_contract_text_equals_literal(&key, "producer_lineage") &&
               (fields & 32u) == 0u) {
      fields |= 32u;
      ok = parse_seed_producer_lineage(context, reader);
    } else if (cupid_contract_text_equals_literal(&key, "seed_generation") &&
               (fields & 64u) == 0u) {
      fields |= 64u;
      ok = parse_expected_text(context, reader, "stage-four",
                               "seed manifest provenance differs");
    } else if (cupid_contract_text_equals_literal(&key, "source_input_count") &&
               (fields & 128u) == 0u) {
      uint64_t value = 0u;
      fields |= 128u;
      ok = cupid_contract_json_parse_positive_u64(context, reader, &value);
      if (ok && value != 59u && value != 61u && value != 66u && value != 73u) {
        ok = cupid_contract_set_error(context, "seed manifest source input count differs");
      }
      if (ok) {
        manifest->source_input_count = value;
      }
    } else if (cupid_contract_text_equals_literal(&key, "source_revision") &&
               (fields & 256u) == 0u) {
      fields |= 256u;
      ok = cupid_contract_json_parse_string(context, reader, &manifest->source_revision);
      if (ok && !lower_hex_valid(manifest->source_revision.bytes,
                                 manifest->source_revision.size, 40u)) {
        ok = cupid_contract_set_error(context, "seed manifest source revision is invalid");
      }
    } else if (cupid_contract_text_equals_literal(&key, "source_snapshot_sha256") &&
               (fields & 512u) == 0u) {
      fields |= 512u;
      ok = cupid_contract_json_parse_string(context, reader, &manifest->source_snapshot_sha256);
      if (ok && !lower_hex_valid(manifest->source_snapshot_sha256.bytes,
                                 manifest->source_snapshot_sha256.size, 64u)) {
        ok = cupid_contract_set_error(context, "seed manifest source snapshot is invalid");
      }
    } else {
      ok = cupid_contract_set_error(context, "seed manifest provenance fields differ");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 1023u) {
    return cupid_contract_set_error(context, "seed manifest provenance fields are missing");
  }
  if (!parent_pair_matches(&manifest->parent_manifest_sha256,
                           &manifest->parent_source_revision,
                           preceding_linux_parent_manifest,
                           active_linux_parent_manifest)) {
    return cupid_contract_set_error(context, "seed manifest parent provenance differs");
  }
  return 1;
}

static void seed_manifest_release(seed_manifest_t *manifest) {
  cupid_contract_text_release(&manifest->parent_manifest_sha256);
  cupid_contract_text_release(&manifest->parent_source_revision);
  cupid_contract_text_release(&manifest->source_revision);
  cupid_contract_text_release(&manifest->source_snapshot_sha256);
}

static int parse_seed_manifest(error_context_t *context, byte_slice_t source,
                               seed_manifest_t *manifest) {
  json_reader_t reader = {source.bytes, source.size, 0u};
  unsigned int fields = 0u;
  (void)memset(manifest, 0, sizeof(*manifest));
  if (!cupid_contract_json_take(context, &reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "seed manifest is not a JSON object");
  }
  cupid_contract_json_skip_space(&reader);
  if (reader.position < reader.size &&
      reader.bytes[reader.position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "seed manifest fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, &reader, &key) &&
             cupid_contract_json_take(context, &reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "schema")) {
      text_t schema = {(unsigned char *)0, 0u};
      if ((fields & 1u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "seed manifest schema is duplicated");
      }
      fields |= 1u;
      ok = cupid_contract_json_parse_string(context, &reader, &schema);
      if (ok && !cupid_contract_text_equals_literal(&schema, seed_schema)) {
        ok = cupid_contract_set_error(context, "seed manifest schema differs");
      }
      cupid_contract_text_release(&schema);
    } else if (cupid_contract_text_equals_literal(&key, "artifacts")) {
      if ((fields & 2u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "seed manifest artifacts are duplicated");
      }
      fields |= 2u;
      ok = parse_manifest_artifacts(context, &reader, manifest);
    } else if (cupid_contract_text_equals_literal(&key, "provenance")) {
      if ((fields & 4u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "seed manifest provenance is duplicated");
      }
      fields |= 4u;
      ok = parse_seed_provenance(context, &reader, manifest);
    } else {
      ok = cupid_contract_json_skip_value(context, &reader, 1u);
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    cupid_contract_json_skip_space(&reader);
    if (reader.position < reader.size &&
        reader.bytes[reader.position] == (unsigned char)'}') {
      reader.position++;
      break;
    }
    if (!cupid_contract_json_take(context, &reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 7u) {
    return cupid_contract_set_error(context, "seed manifest fields are missing");
  }
  return cupid_contract_json_finish(context, &reader);
}

static int parse_windows_manifest_artifact(error_context_t *context, json_reader_t *reader,
                                           windows_manifest_t *manifest) {
  text_t name = {(unsigned char *)0, 0u};
  text_t file = {(unsigned char *)0, 0u};
  text_t digest = {(unsigned char *)0, 0u};
  uint64_t size = 0u;
  unsigned int fields = 0u;
  int producer = 0;
  int seed_index;
  int ok = 1;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return 0;
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    ok = cupid_contract_json_parse_string(context, reader, &key) &&
         cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      break;
    }
    if (cupid_contract_text_equals_literal(&key, "name") && (fields & 1u) == 0u) {
      fields |= 1u;
      ok = cupid_contract_json_parse_string(context, reader, &name);
    } else if (cupid_contract_text_equals_literal(&key, "file") && (fields & 2u) == 0u) {
      fields |= 2u;
      ok = cupid_contract_json_parse_string(context, reader, &file);
    } else if (cupid_contract_text_equals_literal(&key, "size") && (fields & 4u) == 0u) {
      fields |= 4u;
      ok = cupid_contract_json_parse_positive_u64(context, reader, &size);
    } else if (cupid_contract_text_equals_literal(&key, "sha256") &&
               (fields & 8u) == 0u) {
      fields |= 8u;
      ok = cupid_contract_json_parse_string(context, reader, &digest);
      if (ok && !lower_hex_valid(digest.bytes, digest.size, 64u)) {
        ok = cupid_contract_set_error(context, "Windows seed artifact digest is invalid");
      }
    } else if (cupid_contract_text_equals_literal(&key, "producer") &&
               (fields & 16u) == 0u) {
      fields |= 16u;
      ok = json_parse_boolean(context, reader, &producer);
    } else {
      ok = cupid_contract_set_error(context, "Windows seed artifact fields differ");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      break;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      ok = 0;
      break;
    }
  }
  if (ok && fields != 31u) {
    ok = cupid_contract_set_error(context, "Windows seed artifact fields are missing");
  }
  seed_index = ok ? cupid_contract_seed_index_for_name(&name) : -1;
  if (ok && seed_index < 0) {
    ok = cupid_contract_set_error(context, "Windows seed manifest has an unknown tool artifact");
  }
  if (ok && manifest->seen[(size_t)seed_index] != 0) {
    ok = cupid_contract_set_error(context, "Windows seed manifest tool artifact is duplicated");
  }
  if (ok && !cupid_contract_text_equals_literal(
                &file, windows_seed_files[(size_t)seed_index])) {
    ok = cupid_contract_set_error(context, "Windows seed manifest artifact filename differs");
  }
  if (ok && producer != windows_seed_producers[(size_t)seed_index]) {
    ok = cupid_contract_set_error(context, "Windows seed manifest artifact producer differs");
  }
  if (ok) {
    manifest->seen[(size_t)seed_index] = 1;
    manifest->sizes[(size_t)seed_index] = size;
    manifest->digests[(size_t)seed_index] = digest;
    digest.bytes = (unsigned char *)0;
    digest.size = 0u;
  }
  cupid_contract_text_release(&name);
  cupid_contract_text_release(&file);
  cupid_contract_text_release(&digest);
  return ok;
}

static int parse_windows_manifest_artifacts(error_context_t *context, json_reader_t *reader,
                                            windows_manifest_t *manifest) {
  size_t count = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'[')) {
    return 0;
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    reader->position++;
    return cupid_contract_set_error(context, "Windows seed manifest artifacts are missing");
  }
  for (;;) {
    if (count >= SEED_ARTIFACT_COUNT) {
      return cupid_contract_set_error(context, "Windows seed manifest has too many artifacts");
    }
    if (!parse_windows_manifest_artifact(context, reader, manifest)) {
      return 0;
    }
    count++;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (count != SEED_ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "Windows seed manifest does not contain six artifacts");
  }
  return 1;
}

static int parse_expected_text(error_context_t *context, json_reader_t *reader, const char *expected,
                               const char *error) {
  text_t value = {(unsigned char *)0, 0u};
  int ok = cupid_contract_json_parse_string(context, reader, &value);
  if (ok && !cupid_contract_text_equals_literal(&value, expected)) {
    ok = cupid_contract_set_error(context, error);
  }
  cupid_contract_text_release(&value);
  return ok;
}

static int parse_expected_text_pair(error_context_t *context, json_reader_t *reader, const char *first,
                                    const char *second, const char *error) {
  text_t value = {(unsigned char *)0, 0u};
  int ok = cupid_contract_json_parse_string(context, reader, &value);
  if (ok && !cupid_contract_text_equals_literal(&value, first) &&
      !cupid_contract_text_equals_literal(&value, second)) {
    ok = cupid_contract_set_error(context, error);
  }
  cupid_contract_text_release(&value);
  return ok;
}

static int parse_windows_target(error_context_t *context, json_reader_t *reader) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return 0;
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "abi") && (fields & 1u) == 0u) {
      fields |= 1u;
      ok = parse_expected_text(context, reader, "windows-stdcall-imports",
                               "Windows seed manifest target differs");
    } else if (cupid_contract_text_equals_literal(&key, "architecture") &&
               (fields & 2u) == 0u) {
      fields |= 2u;
      ok = parse_expected_text(context, reader, "i386",
                               "Windows seed manifest target differs");
    } else if (cupid_contract_text_equals_literal(&key, "byte_order") &&
               (fields & 4u) == 0u) {
      fields |= 4u;
      ok = parse_expected_text(context, reader, "little",
                               "Windows seed manifest target differs");
    } else if (cupid_contract_text_equals_literal(&key, "entry") &&
               (fields & 8u) == 0u) {
      uint64_t value = 0u;
      fields |= 8u;
      ok = cupid_contract_json_parse_positive_u64(context, reader, &value);
      if (ok && value != 4198400u) {
        ok = cupid_contract_set_error(context, "Windows seed manifest target differs");
      }
    } else if (cupid_contract_text_equals_literal(&key, "linkage") &&
               (fields & 16u) == 0u) {
      fields |= 16u;
      ok = parse_expected_text(context, reader, "kernel32-imports",
                               "Windows seed manifest target differs");
    } else if (cupid_contract_text_equals_literal(&key, "operating_system") &&
               (fields & 32u) == 0u) {
      fields |= 32u;
      ok = parse_expected_text(context, reader, "windows",
                               "Windows seed manifest target differs");
    } else if (cupid_contract_text_equals_literal(&key, "pe_class") &&
               (fields & 64u) == 0u) {
      uint64_t value = 0u;
      fields |= 64u;
      ok = cupid_contract_json_parse_positive_u64(context, reader, &value);
      if (ok && value != 32u) {
        ok = cupid_contract_set_error(context, "Windows seed manifest target differs");
      }
    } else {
      ok = cupid_contract_set_error(context, "Windows seed manifest target fields differ");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 127u) {
    return cupid_contract_set_error(context, "Windows seed manifest target fields are missing");
  }
  return 1;
}

static int parse_windows_producer_lineage(error_context_t *context, json_reader_t *reader) {
  unsigned int fields = 0u;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return 0;
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "assembly") && (fields & 1u) == 0u) {
      fields |= 1u;
      ok = parse_expected_text(context,
          reader,
          "native stage-three CupidASM from the checked i386 Windows bootstrap",
          "Windows seed manifest producer lineage differs");
    } else if (cupid_contract_text_equals_literal(&key, "c") && (fields & 2u) == 0u) {
      fields |= 2u;
      ok = parse_expected_text(context,
          reader,
          "native stage-three CupidC from the checked i386 Windows bootstrap",
          "Windows seed manifest producer lineage differs");
    } else if (cupid_contract_text_equals_literal(&key, "link") && (fields & 4u) == 0u) {
      fields |= 4u;
      ok = parse_expected_text(context,
          reader,
          "native stage-three CupidLD from the checked i386 Windows bootstrap",
          "Windows seed manifest producer lineage differs");
    } else {
      ok = cupid_contract_set_error(context, "Windows seed manifest producer lineage fields differ");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 7u) {
    return cupid_contract_set_error(context, "Windows seed manifest producer lineage fields are missing");
  }
  return 1;
}

static int parse_windows_provenance(error_context_t *context, json_reader_t *reader,
                                    const seed_manifest_t *seed_manifest,
                                    const byte_slice_t *seed_manifest_digest) {
  text_t execution_parent_manifest = {(unsigned char *)0, 0u};
  text_t execution_parent_revision = {(unsigned char *)0, 0u};
  text_t plan_manifest_digest = {(unsigned char *)0, 0u};
  text_t plan_parent_manifest = {(unsigned char *)0, 0u};
  text_t plan_parent_revision = {(unsigned char *)0, 0u};
  text_t source_revision = {(unsigned char *)0, 0u};
  text_t source_snapshot = {(unsigned char *)0, 0u};
  unsigned int fields = 0u;
  int ok = 1;
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return 0;
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    ok = cupid_contract_json_parse_string(context, reader, &key) &&
         cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      break;
    }
    if (cupid_contract_text_equals_literal(&key, "artifact_generation") &&
        (fields & 1u) == 0u) {
      fields |= 1u;
      ok = parse_expected_text(context, reader,
                               "paired-stage-four-six-tool-native-windows",
                               "Windows seed manifest provenance differs");
    } else if (cupid_contract_text_equals_literal(&key, "fixed_point_command") &&
               (fields & 2u) == 0u) {
      fields |= 2u;
      ok = parse_expected_text(context, reader, "make bootstrap-windows-from-seed",
                               "Windows seed manifest provenance differs");
    } else if (cupid_contract_text_equals_literal(&key, "fixed_point_result") &&
               (fields & 4u) == 0u) {
      fields |= 4u;
      ok = parse_expected_text(context, reader, "pass",
                               "Windows seed manifest provenance differs");
    } else if (cupid_contract_text_equals_literal(
                   &key, "linux_candidate_build_plan_sha256") &&
               (fields & 8u) == 0u) {
      fields |= 8u;
      ok = parse_expected_text(context,
          reader,
          (seed_manifest->source_input_count == 66u || seed_manifest->source_input_count == 73u)
              ? "fc1c7634d4cb6a9106c523fe7c5c82f38e2b8e3eb3b3dbce9166e93daa4116fe"
              : "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd",
          "Windows seed Linux build plan differs");
    } else if (cupid_contract_text_equals_literal(&key, "native_build_plan_sha256") &&
               (fields & 16u) == 0u) {
      fields |= 16u;
      if (seed_manifest->source_input_count == 73u) {
        ok = parse_expected_text(context, reader,
            "a31575236059b77a47bb58c79072754258c4762d30105319c451e407b7353f99",
            "Windows seed native build plan differs");
      } else if (seed_manifest->source_input_count == 66u) {
        ok = parse_expected_text(context, reader,
            "70158fd9780990ec0cd0ed1c4da1af9f22f8acbcb483324693fd46c2362177b9",
            "Windows seed native build plan differs");
      } else {
        ok = parse_expected_text_pair(context,
          reader,
          "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14",
          "98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669",
          "Windows seed native build plan differs");
      }
    } else if (cupid_contract_text_equals_literal(
                   &key, "parent_execution_seed_manifest_sha256") &&
               (fields & 32u) == 0u) {
      fields |= 32u;
      ok = cupid_contract_json_parse_string(context, reader, &execution_parent_manifest);
      if (ok && !lower_hex_valid(execution_parent_manifest.bytes,
                                 execution_parent_manifest.size, 64u)) {
        ok = cupid_contract_set_error(context, "Windows seed execution parent digest is invalid");
      }
    } else if (cupid_contract_text_equals_literal(
                   &key, "parent_execution_seed_source_revision") &&
               (fields & 64u) == 0u) {
      fields |= 64u;
      ok = cupid_contract_json_parse_string(context, reader, &execution_parent_revision);
      if (ok && !lower_hex_valid(execution_parent_revision.bytes,
                                 execution_parent_revision.size, 40u)) {
        ok = cupid_contract_set_error(context, "Windows seed execution parent revision is invalid");
      }
    } else if (cupid_contract_text_equals_literal(
                   &key, "parent_plan_seed_manifest_sha256") &&
               (fields & 128u) == 0u) {
      fields |= 128u;
      ok = cupid_contract_json_parse_string(context, reader, &plan_parent_manifest);
      if (ok && !lower_hex_valid(plan_parent_manifest.bytes,
                                 plan_parent_manifest.size, 64u)) {
        ok = cupid_contract_set_error(context, "Windows seed plan parent digest is invalid");
      }
    } else if (cupid_contract_text_equals_literal(
                   &key, "parent_plan_seed_source_revision") &&
               (fields & 256u) == 0u) {
      fields |= 256u;
      ok = cupid_contract_json_parse_string(context, reader, &plan_parent_revision);
      if (ok && !lower_hex_valid(plan_parent_revision.bytes,
                                 plan_parent_revision.size, 40u)) {
        ok = cupid_contract_set_error(context, "Windows seed plan parent revision is invalid");
      }
    } else if (cupid_contract_text_equals_literal(&key, "plan_seed_manifest_sha256") &&
               (fields & 512u) == 0u) {
      fields |= 512u;
      ok = cupid_contract_json_parse_string(context, reader, &plan_manifest_digest);
      if (ok && !lower_hex_valid(plan_manifest_digest.bytes,
                                 plan_manifest_digest.size, 64u)) {
        ok = cupid_contract_set_error(context, "Windows seed plan manifest digest is invalid");
      }
    } else if (cupid_contract_text_equals_literal(&key, "producer_lineage") &&
               (fields & 1024u) == 0u) {
      fields |= 1024u;
      ok = parse_windows_producer_lineage(context, reader);
    } else if (cupid_contract_text_equals_literal(&key, "source_input_count") &&
               (fields & 2048u) == 0u) {
      uint64_t value = 0u;
      fields |= 2048u;
      ok = cupid_contract_json_parse_positive_u64(context, reader, &value);
      if (ok && value != 59u && value != 61u && value != 66u && value != 73u) {
        ok = cupid_contract_set_error(context, "Windows seed manifest source input count differs");
      }
      if (ok && value != seed_manifest->source_input_count) {
        ok = cupid_contract_set_error(context, "Windows and Linux seed source input counts differ");
      }
    } else if (cupid_contract_text_equals_literal(&key, "source_revision") &&
               (fields & 4096u) == 0u) {
      fields |= 4096u;
      ok = cupid_contract_json_parse_string(context, reader, &source_revision);
      if (ok && !lower_hex_valid(source_revision.bytes,
                                 source_revision.size, 40u)) {
        ok = cupid_contract_set_error(context, "Windows seed source revision is invalid");
      }
    } else if (cupid_contract_text_equals_literal(&key, "source_snapshot_sha256") &&
               (fields & 8192u) == 0u) {
      fields |= 8192u;
      ok = cupid_contract_json_parse_string(context, reader, &source_snapshot);
      if (ok && !lower_hex_valid(source_snapshot.bytes,
                                 source_snapshot.size, 64u)) {
        ok = cupid_contract_set_error(context, "Windows seed source snapshot is invalid");
      }
    } else {
      ok = cupid_contract_set_error(context, "Windows seed manifest provenance fields differ");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      break;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      ok = 0;
      break;
    }
  }
  if (ok && fields != 16383u) {
    ok = cupid_contract_set_error(context, "Windows seed manifest provenance fields are missing");
  }
  if (ok && !parent_pair_matches(&execution_parent_manifest,
                                 &execution_parent_revision,
                                 preceding_windows_parent_manifest,
                                 active_windows_parent_manifest)) {
    ok = cupid_contract_set_error(context, "Windows seed execution parent provenance differs");
  }
  if (ok && !parent_pair_matches(&plan_parent_manifest,
                                 &plan_parent_revision,
                                 preceding_linux_parent_manifest,
                                 active_linux_parent_manifest)) {
    ok = cupid_contract_set_error(context, "Windows seed plan parent provenance differs");
  }
  if (ok && !text_equals_text(&execution_parent_revision,
                              &plan_parent_revision)) {
    ok = cupid_contract_set_error(context, "Windows seed parent generations differ");
  }
  if (ok && !cupid_contract_slice_equals_text(seed_manifest_digest,
                               &plan_manifest_digest)) {
    ok = cupid_contract_set_error(context, "Windows seed plan manifest differs");
  }
  if (ok && !text_equals_text(&source_revision,
                               &seed_manifest->source_revision)) {
    ok = cupid_contract_set_error(context, "Windows seed source revision differs");
  }
  if (ok && !text_equals_text(&source_snapshot,
                              &seed_manifest->source_snapshot_sha256)) {
    ok = cupid_contract_set_error(context, "Windows seed source snapshot differs");
  }
  cupid_contract_text_release(&execution_parent_manifest);
  cupid_contract_text_release(&execution_parent_revision);
  cupid_contract_text_release(&plan_manifest_digest);
  cupid_contract_text_release(&plan_parent_manifest);
  cupid_contract_text_release(&plan_parent_revision);
  cupid_contract_text_release(&source_revision);
  cupid_contract_text_release(&source_snapshot);
  return ok;
}

static void windows_manifest_release(windows_manifest_t *manifest) {
  size_t index;
  for (index = 0u; index < SEED_ARTIFACT_COUNT; index++) {
    cupid_contract_text_release(&manifest->digests[index]);
  }
}

static int parse_windows_manifest(error_context_t *context, byte_slice_t source,
                                  const seed_manifest_t *seed_manifest,
                                  const byte_slice_t *seed_manifest_digest,
                                  windows_manifest_t *manifest) {
  json_reader_t reader = {source.bytes, source.size, 0u};
  unsigned int fields = 0u;
  (void)memset(manifest, 0, sizeof(*manifest));
  if (!cupid_contract_json_take(context, &reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "Windows seed manifest is not a JSON object");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, &reader, &key) &&
             cupid_contract_json_take(context, &reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "schema") && (fields & 1u) == 0u) {
      fields |= 1u;
      ok = parse_expected_text(context, &reader, windows_seed_schema,
                               "Windows seed manifest schema differs");
    } else if (cupid_contract_text_equals_literal(&key, "artifacts") &&
               (fields & 2u) == 0u) {
      fields |= 2u;
      ok = parse_windows_manifest_artifacts(context, &reader, manifest);
    } else if (cupid_contract_text_equals_literal(&key, "provenance") &&
               (fields & 4u) == 0u) {
      fields |= 4u;
      ok = parse_windows_provenance(context, &reader, seed_manifest,
                                    seed_manifest_digest);
    } else if (cupid_contract_text_equals_literal(&key, "target") &&
               (fields & 8u) == 0u) {
      fields |= 8u;
      ok = parse_windows_target(context, &reader);
    } else {
      ok = cupid_contract_set_error(context, "Windows seed manifest fields differ");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    cupid_contract_json_skip_space(&reader);
    if (reader.position < reader.size &&
        reader.bytes[reader.position] == (unsigned char)'}') {
      reader.position++;
      break;
    }
    if (!cupid_contract_json_take(context, &reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 15u) {
    return cupid_contract_set_error(context, "Windows seed manifest fields are missing");
  }
  return cupid_contract_json_finish(context, &reader);
}

static void policy_entry_release(policy_entry_t *entry) {
  cupid_contract_text_release(&entry->path);
  cupid_contract_text_release(&entry->producer);
  cupid_contract_text_release(&entry->reason);
  entry->exact_bytes = 0u;
}

static void policy_release(policy_t *policy) {
  size_t index;
  for (index = 0u; index < policy->count; index++) {
    policy_entry_release(&policy->entries[index]);
  }
  policy->count = 0u;
}

static int policy_reason_valid(const text_t *reason) {
  size_t index;
  if (reason->size == 0u || ascii_space(reason->bytes[0]) ||
      ascii_space(reason->bytes[reason->size - 1u])) {
    return 0;
  }
  for (index = 0u; index < reason->size; index++) {
    if (reason->bytes[index] == (unsigned char)'\r' ||
        reason->bytes[index] == (unsigned char)'\n') {
      return 0;
    }
  }
  return 1;
}

static int parse_policy_entry(error_context_t *context, json_reader_t *reader, policy_entry_t *entry) {
  unsigned int fields = 0u;
  (void)memset(entry, 0, sizeof(*entry));
  if (!cupid_contract_json_take(context, reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "policy artifact is not a JSON object");
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "policy artifact fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, reader, &key) &&
             cupid_contract_json_take(context, reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "path")) {
      if ((fields & 1u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "policy artifact path is duplicated");
      }
      fields |= 1u;
      ok = cupid_contract_json_parse_string(context, reader, &entry->path);
    } else if (cupid_contract_text_equals_literal(&key, "producer")) {
      if ((fields & 2u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "policy artifact producer is duplicated");
      }
      fields |= 2u;
      ok = cupid_contract_json_parse_string(context, reader, &entry->producer);
    } else if (cupid_contract_text_equals_literal(&key, "reason")) {
      if ((fields & 4u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "policy artifact reason is duplicated");
      }
      fields |= 4u;
      ok = cupid_contract_json_parse_string(context, reader, &entry->reason);
    } else if (cupid_contract_text_equals_literal(&key, "exact_bytes")) {
      if ((fields & 8u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "policy artifact exact size is duplicated");
      }
      fields |= 8u;
      ok = cupid_contract_json_parse_positive_u64(context, reader, &entry->exact_bytes);
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, "policy artifact has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)'}') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 15u) {
    return cupid_contract_set_error(context, "policy artifact fields are missing");
  }
  if (!cupid_contract_logical_path_valid(entry->path.bytes, entry->path.size)) {
    return cupid_contract_set_error(context, "policy artifact path is unsafe");
  }
  if (entry->producer.size == 0u) {
    return cupid_contract_set_error(context, "policy artifact producer is empty");
  }
  if (!policy_reason_valid(&entry->reason)) {
    return cupid_contract_set_error(context, "policy artifact reason is invalid");
  }
  return 1;
}

static int parse_policy_artifacts(error_context_t *context, json_reader_t *reader, policy_t *policy) {
  if (!cupid_contract_json_take(context, reader, (unsigned char)'[')) {
    return 0;
  }
  cupid_contract_json_skip_space(reader);
  if (reader->position < reader->size &&
      reader->bytes[reader->position] == (unsigned char)']') {
    reader->position++;
    return cupid_contract_set_error(context, "policy artifacts are missing");
  }
  for (;;) {
    policy_entry_t entry;
    if (policy->count >= ARTIFACT_COUNT) {
      return cupid_contract_set_error(context, "policy has too many artifacts");
    }
    if (!parse_policy_entry(context, reader, &entry)) {
      policy_entry_release(&entry);
      return 0;
    }
    policy->entries[policy->count] = entry;
    policy->count++;
    cupid_contract_json_skip_space(reader);
    if (reader->position < reader->size &&
        reader->bytes[reader->position] == (unsigned char)']') {
      reader->position++;
      break;
    }
    if (!cupid_contract_json_take(context, reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (policy->count != ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "policy does not contain sixteen artifacts");
  }
  return 1;
}

static int parse_policy(error_context_t *context, byte_slice_t source, policy_t *policy) {
  json_reader_t reader = {source.bytes, source.size, 0u};
  unsigned int fields = 0u;
  (void)memset(policy, 0, sizeof(*policy));
  if (!cupid_contract_json_take(context, &reader, (unsigned char)'{')) {
    return cupid_contract_set_error(context, "policy is not a JSON object");
  }
  cupid_contract_json_skip_space(&reader);
  if (reader.position < reader.size &&
      reader.bytes[reader.position] == (unsigned char)'}') {
    return cupid_contract_set_error(context, "policy fields are missing");
  }
  for (;;) {
    text_t key = {(unsigned char *)0, 0u};
    int ok = cupid_contract_json_parse_string(context, &reader, &key) &&
             cupid_contract_json_take(context, &reader, (unsigned char)':');
    if (!ok) {
      cupid_contract_text_release(&key);
      return 0;
    }
    if (cupid_contract_text_equals_literal(&key, "schema")) {
      text_t schema = {(unsigned char *)0, 0u};
      if ((fields & 1u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "policy schema is duplicated");
      }
      fields |= 1u;
      ok = cupid_contract_json_parse_string(context, &reader, &schema);
      if (ok && !cupid_contract_text_equals_literal(&schema, policy_schema)) {
        ok = cupid_contract_set_error(context, "policy schema differs");
      }
      cupid_contract_text_release(&schema);
    } else if (cupid_contract_text_equals_literal(&key, "artifacts")) {
      if ((fields & 2u) != 0u) {
        cupid_contract_text_release(&key);
        return cupid_contract_set_error(context, "policy artifacts are duplicated");
      }
      fields |= 2u;
      ok = parse_policy_artifacts(context, &reader, policy);
    } else {
      cupid_contract_text_release(&key);
      return cupid_contract_set_error(context, "policy has an unknown field");
    }
    cupid_contract_text_release(&key);
    if (!ok) {
      return 0;
    }
    cupid_contract_json_skip_space(&reader);
    if (reader.position < reader.size &&
        reader.bytes[reader.position] == (unsigned char)'}') {
      reader.position++;
      break;
    }
    if (!cupid_contract_json_take(context, &reader, (unsigned char)',')) {
      return 0;
    }
  }
  if (fields != 3u) {
    return cupid_contract_set_error(context, "policy fields are missing");
  }
  return cupid_contract_json_finish(context, &reader);
}

static size_t manifest_parent_size(const byte_slice_t *manifest_path) {
  size_t position = manifest_path->size;
  while (position > 0u) {
    position--;
    if (manifest_path->bytes[position] == (unsigned char)'/') {
      return position;
    }
  }
  return 0u;
}

static int text_matches_seed_path(const text_t *path,
                                  const byte_slice_t *manifest_path,
                                  const char *filename) {
  size_t parent_size = manifest_parent_size(manifest_path);
  size_t filename_size = strlen(filename);
  if (parent_size == 0u) {
    return path->size == filename_size &&
           memcmp(path->bytes, filename, filename_size) == 0;
  }
  return path->size == parent_size + 1u + filename_size &&
         memcmp(path->bytes, manifest_path->bytes, parent_size) == 0 &&
         path->bytes[parent_size] == (unsigned char)'/' &&
         memcmp(path->bytes + parent_size + 1u, filename, filename_size) == 0;
}

static int validate_policy(error_context_t *context, policy_t *policy, const seed_manifest_t *manifest,
                           const windows_manifest_t *windows_manifest,
                           const byte_slice_t *manifest_path,
                           uint64_t *total) {
  int matched[ARTIFACT_COUNT];
  uint64_t sum = 0u;
  uint64_t maximum = ~(uint64_t)0u;
  size_t policy_index;
  (void)memset(matched, 0, sizeof(matched));
  for (policy_index = 0u; policy_index < policy->count; policy_index++) {
    policy_entry_t *entry = &policy->entries[policy_index];
    int expected_index = -1;
    const char *expected_owner = (const char *)0;
    uint64_t seed_size = 0u;
    size_t index;
    if (policy_index > 0u &&
        cupid_contract_text_compare(&policy->entries[policy_index - 1u].path, &entry->path) >=
            0) {
      return cupid_contract_set_error(context, "policy artifacts are not in canonical order");
    }
    for (index = 0u; index < FIXED_ARTIFACT_COUNT; index++) {
      if (cupid_contract_text_equals_literal(&entry->path, fixed_paths[index])) {
        expected_index = (int)index;
        expected_owner = fixed_owners[index];
        break;
      }
    }
    if (expected_index < 0) {
      for (index = 0u; index < SEED_ARTIFACT_COUNT; index++) {
        if (text_matches_seed_path(&entry->path, manifest_path,
                                   cupid_contract_seed_files[index])) {
          expected_index = (int)(FIXED_ARTIFACT_COUNT + index);
          expected_owner = seed_owners[index];
          seed_size = manifest->sizes[index];
          break;
        }
      }
    }
    if (expected_index < 0 || expected_owner == (const char *)0) {
      return cupid_contract_set_error(context, "policy has an unknown artifact path");
    }
    if (matched[(size_t)expected_index] != 0) {
      return cupid_contract_set_error(context, "policy artifact path is duplicated");
    }
    matched[(size_t)expected_index] = 1;
    if (!cupid_contract_text_equals_literal(&entry->producer, expected_owner)) {
      return cupid_contract_set_error(context, "policy artifact producer differs");
    }
    if ((size_t)expected_index >= FIXED_ARTIFACT_COUNT &&
        entry->exact_bytes != seed_size) {
      return cupid_contract_set_error(context, "policy seed size differs from the selected manifest");
    }
    if (expected_index >= 1 &&
        expected_index <= (int)SEED_ARTIFACT_COUNT &&
        entry->exact_bytes !=
            windows_manifest->sizes[(size_t)expected_index - 1u]) {
      return cupid_contract_set_error(context, "policy Windows seed size differs from the manifest");
    }
    if (sum > maximum - entry->exact_bytes) {
      return cupid_contract_set_error(context, "policy exact byte total exceeds the unsigned range");
    }
    sum += entry->exact_bytes;
  }
  for (policy_index = 0u; policy_index < ARTIFACT_COUNT; policy_index++) {
    if (matched[policy_index] == 0) {
      return cupid_contract_set_error(context, "policy is missing a required artifact");
    }
  }
  *total = sum;
  return 1;
}







static int validate_observations(error_context_t *context, const observation_t *observations,
                                 const policy_t *policy) {
  int matched[ARTIFACT_COUNT];
  size_t observation_index;
  (void)memset(matched, 0, sizeof(matched));
  for (observation_index = 0u; observation_index < ARTIFACT_COUNT;
       observation_index++) {
    const observation_t *observation = &observations[observation_index];
    size_t policy_index;
    int found = -1;
    if (observation->kind != 1u) {
      return cupid_contract_set_error(context, "artifact observation is not a regular file");
    }
    for (policy_index = 0u; policy_index < policy->count; policy_index++) {
      if (cupid_contract_slice_equals_text(&observation->path,
                            &policy->entries[policy_index].path)) {
        found = (int)policy_index;
        break;
      }
    }
    if (found < 0) {
      return cupid_contract_set_error(context, "artifact observation has an unknown path");
    }
    if (matched[(size_t)found] != 0) {
      return cupid_contract_set_error(context, "artifact observation path is duplicated");
    }
    matched[(size_t)found] = 1;
    if (observation->size != policy->entries[(size_t)found].exact_bytes) {
      return cupid_contract_set_error(context, "artifact observation size differs from policy");
    }
  }
  for (observation_index = 0u; observation_index < ARTIFACT_COUNT;
       observation_index++) {
    if (matched[observation_index] == 0) {
      return cupid_contract_set_error(context, "artifact observation is missing");
    }
  }
  return 1;
}

static int validate_windows_observations(error_context_t *context,
    const windows_observation_t *observations,
    const windows_manifest_t *manifest) {
  int matched[SEED_ARTIFACT_COUNT];
  size_t observation_index;
  (void)memset(matched, 0, sizeof(matched));
  for (observation_index = 0u; observation_index < SEED_ARTIFACT_COUNT;
       observation_index++) {
    const windows_observation_t *observation =
        &observations[observation_index];
    size_t seed_index;
    int found = -1;
    if (observation->kind != 1u) {
      return cupid_contract_set_error(context, "Windows seed observation is not a regular file");
    }
    for (seed_index = 0u; seed_index < SEED_ARTIFACT_COUNT; seed_index++) {
      if (slice_equals_literal(&observation->path,
                               fixed_paths[seed_index + 1u])) {
        found = (int)seed_index;
        break;
      }
    }
    if (found < 0) {
      return cupid_contract_set_error(context, "Windows seed observation has an unknown path");
    }
    if (matched[(size_t)found] != 0) {
      return cupid_contract_set_error(context, "Windows seed observation path is duplicated");
    }
    matched[(size_t)found] = 1;
    if (observation->size != manifest->sizes[(size_t)found]) {
      return cupid_contract_set_error(context, "Windows seed artifact size differs from observation");
    }
    if (!cupid_contract_slice_equals_text(&observation->digest,
                           &manifest->digests[(size_t)found])) {
      return cupid_contract_set_error(context, "Windows seed artifact digest differs from observation");
    }
  }
  for (observation_index = 0u; observation_index < SEED_ARTIFACT_COUNT;
       observation_index++) {
    if (matched[observation_index] == 0) {
      return cupid_contract_set_error(context, "Windows seed observation is missing");
    }
  }
  return 1;
}

static int validate_request(error_context_t *context, const byte_slice_t *request, uint64_t *total) {
  binary_reader_t reader = {request->bytes, request->size, 0u};
  byte_slice_t policy_source;
  byte_slice_t manifest_path;
  byte_slice_t manifest_source;
  byte_slice_t manifest_digest;
  byte_slice_t windows_manifest_path;
  byte_slice_t windows_manifest_source;
  windows_observation_t windows_observations[SEED_ARTIFACT_COUNT];
  observation_t observations[ARTIFACT_COUNT];
  seed_manifest_t manifest;
  windows_manifest_t windows_manifest;
  policy_t policy;
  uint32_t windows_observation_count;
  uint32_t observation_count;
  size_t index;
  int ok = 0;
  (void)memset(&policy, 0, sizeof(policy));
  (void)memset(&manifest, 0, sizeof(manifest));
  (void)memset(&windows_manifest, 0, sizeof(windows_manifest));
  if (reader.size < sizeof(request_magic) ||
      memcmp(reader.bytes, request_magic, sizeof(request_magic)) != 0) {
    return cupid_contract_set_error(context, "request magic differs from CUPSIZE2");
  }
  reader.position = sizeof(request_magic);
  if (!cupid_contract_binary_read_slice(context, &reader, &policy_source) ||
      !cupid_contract_binary_read_slice(context, &reader, &manifest_path) ||
      !cupid_contract_binary_read_slice(context, &reader, &manifest_source) ||
      !cupid_contract_binary_read_slice(context, &reader, &manifest_digest) ||
      !cupid_contract_binary_read_slice(context, &reader, &windows_manifest_path) ||
      !cupid_contract_binary_read_slice(context, &reader, &windows_manifest_source) ||
      !cupid_contract_binary_read_u32(context, &reader, &windows_observation_count)) {
    return 0;
  }
  if (!cupid_contract_logical_path_valid(manifest_path.bytes, manifest_path.size)) {
    return cupid_contract_set_error(context, "seed manifest logical path is unsafe");
  }
  if (!lower_hex_valid(manifest_digest.bytes, manifest_digest.size, 64u)) {
    return cupid_contract_set_error(context, "seed manifest digest observation is invalid");
  }
  if (!cupid_contract_logical_path_valid(windows_manifest_path.bytes,
                          windows_manifest_path.size)) {
    return cupid_contract_set_error(context, "Windows seed manifest logical path is unsafe");
  }
  if (!slice_equals_literal(
          &windows_manifest_path,
          "bootstrap/seeds/i386-windows/manifest.json")) {
    return cupid_contract_set_error(context, "Windows seed manifest logical path differs");
  }
  if (windows_observation_count != SEED_ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "request does not contain six Windows seed observations");
  }
  for (index = 0u; index < SEED_ARTIFACT_COUNT; index++) {
    if (!cupid_contract_binary_read_slice(context, &reader, &windows_observations[index].path) ||
        !cupid_contract_binary_read_u32(context, &reader, &windows_observations[index].kind) ||
        !cupid_contract_binary_read_u64(context, &reader, &windows_observations[index].size) ||
        !cupid_contract_binary_read_slice(context, &reader, &windows_observations[index].digest)) {
      return 0;
    }
    if (!cupid_contract_logical_path_valid(windows_observations[index].path.bytes,
                            windows_observations[index].path.size)) {
      return cupid_contract_set_error(context, "Windows seed observation path is unsafe");
    }
    if (!lower_hex_valid(windows_observations[index].digest.bytes,
                         windows_observations[index].digest.size, 64u)) {
      return cupid_contract_set_error(context, "Windows seed observation digest is invalid");
    }
  }
  if (!cupid_contract_binary_read_u32(context, &reader, &observation_count)) {
    return 0;
  }
  if (observation_count != ARTIFACT_COUNT) {
    return cupid_contract_set_error(context, "request does not contain sixteen artifact observations");
  }
  for (index = 0u; index < ARTIFACT_COUNT; index++) {
    if (!cupid_contract_binary_read_slice(context, &reader, &observations[index].path) ||
        !cupid_contract_binary_read_u32(context, &reader, &observations[index].kind) ||
        !cupid_contract_binary_read_u64(context, &reader, &observations[index].size)) {
      return 0;
    }
    if (!cupid_contract_logical_path_valid(observations[index].path.bytes,
                            observations[index].path.size)) {
      return cupid_contract_set_error(context, "artifact observation path is unsafe");
    }
  }
  if (reader.position != reader.size) {
    return cupid_contract_set_error(context, "request has trailing input");
  }
  if (!parse_seed_manifest(context, manifest_source, &manifest) ||
      !parse_windows_manifest(context, windows_manifest_source, &manifest,
                              &manifest_digest, &windows_manifest) ||
      !parse_policy(context, policy_source, &policy)) {
    policy_release(&policy);
    windows_manifest_release(&windows_manifest);
    seed_manifest_release(&manifest);
    return 0;
  }
  if (validate_policy(context, &policy, &manifest, &windows_manifest, &manifest_path,
                      total) &&
      validate_windows_observations(context, windows_observations,
                                    &windows_manifest) &&
      validate_observations(context, observations, &policy)) {
    ok = 1;
  }
  policy_release(&policy);
  windows_manifest_release(&windows_manifest);
  seed_manifest_release(&manifest);
  return ok;
}

int artifact_size_policy_validate(const unsigned char *bytes, size_t size,
                                  artifact_size_policy_result_t *result,
                                  char *error, size_t error_capacity) {
  error_context_t context = {error, error_capacity, 0};
  byte_slice_t request = {bytes, size};
  uint64_t total = 0u;
  if (result != (artifact_size_policy_result_t *)0) {
    result->artifact_count = 0u;
    result->total_exact_bytes = 0u;
  }
  if (error_capacity != 0u && error == (char *)0) {
    return 0;
  }
  if (error_capacity != 0u) {
    error[0] = '\0';
  }
  if (result == (artifact_size_policy_result_t *)0 ||
      (bytes == (const unsigned char *)0 && size != 0u)) {
    return cupid_contract_set_error(&context, "invalid artifact-size policy API arguments");
  }
  if (!validate_request(&context, &request, &total)) {
    return 0;
  }
  result->artifact_count = ARTIFACT_COUNT;
  result->total_exact_bytes = total;
  return 1;
}
