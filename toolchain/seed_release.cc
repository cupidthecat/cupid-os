#include "seed_release.h"
#include "contract_parse_internal.h"
#include <string.h>

static int fail(error_context_t *error, const char *message) {
  return cupid_contract_set_error(error, message);
}

static int take(error_context_t *error, json_reader_t *reader,
                unsigned char character) {
  return cupid_contract_json_take(error, reader, character);
}

static int is_next(json_reader_t *reader, unsigned char character) {
  cupid_contract_json_skip_space(reader);
  return reader->position < reader->size &&
         reader->bytes[reader->position] == character;
}

static int parse_hex(error_context_t *error, json_reader_t *reader,
                     char *output, size_t length) {
  text_t text = {0};
  size_t index;
  int ok = cupid_contract_json_parse_string(error, reader, &text);
  if (ok && text.size != length) {
    ok = fail(error, "release identity has an invalid hexadecimal length");
  }
  for (index = 0u; ok && index < length; index++) {
    unsigned char value = text.bytes[index];
    if (!((value >= '0' && value <= '9') ||
          (value >= 'a' && value <= 'f'))) {
      ok = fail(error, "release identity must use lowercase hexadecimal");
    }
  }
  if (ok) {
    memcpy(output, text.bytes, length);
    output[length] = '\0';
  }
  cupid_contract_text_release(&text);
  return ok;
}

static int parse_artifact(error_context_t *error, json_reader_t *reader,
                          cupid_seed_release_t *result, unsigned int *seen) {
  static const char *const keys[4] = {"name", "format", "size", "sha256"};
  cupid_seed_release_artifact_t artifact;
  unsigned int fields = 0u;
  int role = -1;
  int format = -1;
  int ok;
  memset(&artifact, 0, sizeof(artifact));
  if (!take(error, reader, '{')) return 0;
  while (!is_next(reader, '}')) {
    text_t key = {0};
    size_t index;
    if (fields && !take(error, reader, ',')) return 0;
    if (!cupid_contract_json_parse_string(error, reader, &key)) return 0;
    for (index = 0u; index < 4u; index++) {
      if (cupid_contract_text_equals_literal(&key, keys[index])) break;
    }
    cupid_contract_text_release(&key);
    if (index == 4u || (fields & (1u << index))) {
      return fail(error, "release artifact has an unknown or duplicate field");
    }
    fields |= 1u << index;
    if (!take(error, reader, ':')) return 0;
    if (index < 2u) {
      text_t value = {0};
      ok = cupid_contract_json_parse_string(error, reader, &value);
      if (ok && index == 0u) {
        role = cupid_contract_seed_index_for_name(&value);
        if (role < 0) ok = fail(error, "release artifact role is unknown");
      } else if (ok) {
        if (cupid_contract_text_equals_literal(&value, "elf32")) format = 0;
        else if (cupid_contract_text_equals_literal(&value, "pe32")) format = 1;
        else ok = fail(error, "release artifact format is unknown");
      }
      cupid_contract_text_release(&value);
      if (!ok) return 0;
    } else if (index == 2u) {
      if (!cupid_contract_json_parse_positive_u64(error, reader, &artifact.size)) return 0;
      if (artifact.size > 67108864u) {
        return fail(error, "release artifact exceeds the seed image limit");
      }
    } else if (!parse_hex(error, reader, artifact.sha256, 64u)) return 0;
  }
  if (!take(error, reader, '}')) return 0;
  if (fields != 15u) return fail(error, "release artifact fields are incomplete");
  {
    unsigned int bit = 1u << ((unsigned int)format * 6u + (unsigned int)role);
    if (*seen & bit) return fail(error, "release artifact is duplicated");
    *seen |= bit;
    result->artifacts[format][role] = artifact;
  }
  return 1;
}

static int parse_artifacts(error_context_t *error, json_reader_t *reader,
                           cupid_seed_release_t *result) {
  unsigned int seen = 0u;
  unsigned int count = 0u;
  if (!take(error, reader, '[')) return 0;
  while (!is_next(reader, ']')) {
    if (count >= 12u) return fail(error, "release contains too many artifacts");
    if (count && !take(error, reader, ',')) return 0;
    if (!parse_artifact(error, reader, result, &seen)) return 0;
    count++;
  }
  if (!take(error, reader, ']')) return 0;
  if (seen != 4095u) return fail(error, "release requires both complete six-tool cohorts");
  return 1;
}

static int parse_release(error_context_t *error, json_reader_t *reader,
                         cupid_seed_release_t *result) {
  static const char *const keys[10] = {
    "schema", "source_revision", "source_snapshot_sha256", "source_input_count",
    "parent_source_revision", "parent_linux_manifest_sha256",
    "parent_windows_manifest_sha256", "linux_plan_sha256",
    "windows_plan_sha256", "artifacts"
  };
  unsigned int fields = 0u;
  if (!take(error, reader, '{')) return 0;
  while (!is_next(reader, '}')) {
    text_t key = {0};
    size_t index;
    char *output = 0;
    size_t length = 64u;
    if (fields && !take(error, reader, ',')) return 0;
    if (!cupid_contract_json_parse_string(error, reader, &key)) return 0;
    for (index = 0u; index < 10u; index++) {
      if (cupid_contract_text_equals_literal(&key, keys[index])) break;
    }
    cupid_contract_text_release(&key);
    if (index == 10u || (fields & (1u << index))) {
      return fail(error, "release has an unknown or duplicate field");
    }
    fields |= 1u << index;
    if (!take(error, reader, ':')) return 0;
    if (index == 0u) {
      text_t schema = {0};
      int ok = cupid_contract_json_parse_string(error, reader, &schema);
      if (ok && !cupid_contract_text_equals_literal(&schema, "cupid.seed-release.v1")) {
        ok = fail(error, "release schema is unsupported");
      }
      cupid_contract_text_release(&schema);
      if (!ok) return 0;
    } else if (index == 3u) {
      uint64_t count = 0u;
      if (!cupid_contract_json_parse_positive_u64(error, reader, &count)) return 0;
      if (count > 4294967295u) return fail(error, "release source count exceeds 32 bits");
      result->source_input_count = (uint32_t)count;
    } else if (index == 9u) {
      if (!parse_artifacts(error, reader, result)) return 0;
    } else {
      if (index == 1u) { output = result->source_revision; length = 40u; }
      else if (index == 2u) output = result->source_snapshot_sha256;
      else if (index == 4u) { output = result->parent_source_revision; length = 40u; }
      else if (index == 5u) output = result->parent_linux_manifest_sha256;
      else if (index == 6u) output = result->parent_windows_manifest_sha256;
      else if (index == 7u) output = result->linux_plan_sha256;
      else output = result->windows_plan_sha256;
      if (!parse_hex(error, reader, output, length)) return 0;
    }
  }
  if (!take(error, reader, '}')) return 0;
  if (fields != 1023u) return fail(error, "release fields are incomplete");
  return cupid_contract_json_finish(error, reader);
}

int cupid_seed_release_parse(const unsigned char *bytes, size_t size,
                             cupid_seed_release_t *result,
                             char *error, size_t error_capacity) {
  error_context_t context;
  json_reader_t reader;
  int ok;
  context.bytes = error;
  context.capacity = error_capacity;
  context.has_error = 0;
  if (result) memset(result, 0, sizeof(*result));
  if (error && error_capacity) error[0] = '\0';
  if (!bytes || !result || (error_capacity && !error)) {
    /* Do not pass invalid storage to the diagnostic writer. */
    if (!error) context.capacity = 0u;
    return fail(&context, "invalid release parser arguments");
  }
  if (!size || size > 65536u) return fail(&context, "release input must contain 1 to 65536 bytes");
  reader.bytes = bytes;
  reader.size = size;
  reader.position = 0u;
  ok = parse_release(&context, &reader, result);
  if (!ok) memset(result, 0, sizeof(*result));
  return ok;
}

static int match_text(error_context_t *error, json_reader_t *reader,
                      const char *expected) {
  text_t value = {0};
  int ok = cupid_contract_json_parse_string(error, reader, &value);
  if (ok && !cupid_contract_text_equals_literal(&value, expected)) {
    ok = fail(error, "manifest text differs from release identity");
  }
  cupid_contract_text_release(&value);
  return ok;
}

static int match_artifact(error_context_t *error, json_reader_t *reader,
                          const cupid_seed_release_t *release,
                          unsigned int format, unsigned int *seen) {
  static const char *const keys[5] = {"name", "file", "size", "sha256", "producer"};
  static const char *const pe_files[6] = {
    "cupidasm.exe", "cupidc.exe", "cupiddis.exe", "cupidld.exe", "cupidobj.exe", "cupidbuild.exe"
  };
  static const int producers[6] = {1, 1, 0, 1, 0, 0};
  text_t name = {0};
  text_t file = {0};
  char digest[65];
  uint64_t size = 0u;
  int producer = -1;
  int role = -1;
  unsigned int fields = 0u;
  int ok = take(error, reader, '{');
  while (ok && !is_next(reader, '}')) {
    text_t key = {0};
    size_t index;
    if (fields && !take(error, reader, ',')) { ok = 0; break; }
    if (!cupid_contract_json_parse_string(error, reader, &key)) { ok = 0; break; }
    for (index = 0u; index < 5u; index++) {
      if (cupid_contract_text_equals_literal(&key, keys[index])) break;
    }
    cupid_contract_text_release(&key);
    if (index == 5u || (fields & (1u << index))) {
      ok = fail(error, "manifest artifact has an unknown or duplicate field");
      break;
    }
    fields |= 1u << index;
    if (!take(error, reader, ':')) { ok = 0; break; }
    if (index == 0u) ok = cupid_contract_json_parse_string(error, reader, &name);
    else if (index == 1u) ok = cupid_contract_json_parse_string(error, reader, &file);
    else if (index == 2u) ok = cupid_contract_json_parse_positive_u64(error, reader, &size);
    else if (index == 3u) ok = parse_hex(error, reader, digest, 64u);
    else {
      cupid_contract_json_skip_space(reader);
      if (cupid_contract_json_match_literal(reader, "true")) producer = 1;
      else if (cupid_contract_json_match_literal(reader, "false")) producer = 0;
      else ok = fail(error, "manifest artifact producer must be boolean");
    }
  }
  if (ok) ok = take(error, reader, '}');
  if (ok && fields != 31u) ok = fail(error, "manifest artifact fields are incomplete");
  if (ok) {
    role = cupid_contract_seed_index_for_name(&name);
    if (role < 0 || (*seen & (1u << (unsigned int)role))) {
      ok = fail(error, "manifest artifact role is unknown or duplicated");
    }
  }
  if (ok && (!cupid_contract_text_equals_literal(&file,
              format == 1u ? cupid_contract_seed_files[role] : pe_files[role]) ||
             producer != producers[role])) {
    ok = fail(error, "manifest artifact filename or producer differs");
  }
  if (ok) {
    const cupid_seed_release_artifact_t *pin = &release->artifacts[format - 1u][role];
    if (size != pin->size || memcmp(digest, pin->sha256, 65u)) {
      ok = fail(error, "manifest artifact differs from release identity");
    }
  }
  if (ok) *seen |= 1u << (unsigned int)role;
  cupid_contract_text_release(&name);
  cupid_contract_text_release(&file);
  return ok;
}

static int match_artifacts(error_context_t *error, json_reader_t *reader,
                           const cupid_seed_release_t *release,
                           unsigned int format) {
  unsigned int seen = 0u;
  unsigned int count = 0u;
  if (!take(error, reader, '[')) return 0;
  while (!is_next(reader, ']')) {
    if (count == 6u) return fail(error, "manifest contains too many artifacts");
    if (count && !take(error, reader, ',')) return 0;
    if (!match_artifact(error, reader, release, format, &seen)) return 0;
    count++;
  }
  if (!take(error, reader, ']')) return 0;
  if (seen != 63u) return fail(error, "manifest requires the complete six-tool cohort");
  return 1;
}

enum {
  RELEASE_TEXT, RELEASE_COUNT, RELEASE_OBJECT, RELEASE_DIGEST,
  RELEASE_ARTIFACTS, RELEASE_PROVENANCE
};

typedef struct {
  const char *key;
  unsigned int kind;
  const char *expected;
} release_field_t;

static int match_provenance(error_context_t *error, json_reader_t *reader,
                            const cupid_seed_release_t *release,
                            unsigned int format);

static int match_fields(error_context_t *error, json_reader_t *reader,
                        const cupid_seed_release_t *release, unsigned int format,
                        const release_field_t *fields, size_t field_count) {
  unsigned int seen = 0u;
  if (!take(error, reader, '{')) return 0;
  while (!is_next(reader, '}')) {
    text_t key = {0};
    size_t index;
    int ok = 0;
    if (seen && !take(error, reader, ',')) return 0;
    if (!cupid_contract_json_parse_string(error, reader, &key)) return 0;
    for (index = 0u; index < field_count; index++) {
      if (cupid_contract_text_equals_literal(&key, fields[index].key)) break;
    }
    cupid_contract_text_release(&key);
    if (index == field_count || (seen & (1u << index))) {
      return fail(error, "manifest has an unknown or duplicate field");
    }
    seen |= 1u << index;
    if (!take(error, reader, ':')) return 0;
    if (fields[index].kind == RELEASE_TEXT) {
      ok = match_text(error, reader, fields[index].expected);
    } else if (fields[index].kind == RELEASE_COUNT) {
      uint64_t count = 0u;
      ok = cupid_contract_json_parse_positive_u64(error, reader, &count);
      if (ok && count != release->source_input_count) {
        ok = fail(error, "manifest source count differs from release identity");
      }
    } else if (fields[index].kind == RELEASE_OBJECT) {
      if (!is_next(reader, '{')) return fail(error, "manifest object is required");
      ok = cupid_contract_json_skip_value(error, reader, 1u);
    } else if (fields[index].kind == RELEASE_DIGEST) {
      char digest[65];
      ok = parse_hex(error, reader, digest, 64u);
    } else if (fields[index].kind == RELEASE_ARTIFACTS) {
      ok = match_artifacts(error, reader, release, format);
    } else if (fields[index].kind == RELEASE_PROVENANCE) {
      ok = match_provenance(error, reader, release, format);
    }
    if (!ok) return 0;
  }
  if (!take(error, reader, '}')) return 0;
  if (seen != (1u << field_count) - 1u) {
    return fail(error, "manifest fields are incomplete");
  }
  return 1;
}

static int match_provenance(error_context_t *error, json_reader_t *reader,
                            const cupid_seed_release_t *release,
                            unsigned int format) {
  const release_field_t linux_fields[10] = {
    {"artifact_generation", RELEASE_TEXT, "paired-stage-four-six-tool"},
    {"fixed_point_command", RELEASE_TEXT, "make bootstrap-from-seed"},
    {"fixed_point_result", RELEASE_TEXT, "pass"},
    {"parent_seed_manifest_sha256", RELEASE_TEXT, release->parent_linux_manifest_sha256},
    {"parent_seed_source_revision", RELEASE_TEXT, release->parent_source_revision},
    {"producer_lineage", RELEASE_OBJECT, 0},
    {"seed_generation", RELEASE_TEXT, "stage-four"},
    {"source_input_count", RELEASE_COUNT, 0},
    {"source_revision", RELEASE_TEXT, release->source_revision},
    {"source_snapshot_sha256", RELEASE_TEXT, release->source_snapshot_sha256}
  };
  const release_field_t windows_fields[14] = {
    {"artifact_generation", RELEASE_TEXT, "paired-stage-four-six-tool-native-windows"},
    {"fixed_point_command", RELEASE_TEXT, "make bootstrap-windows-from-seed"},
    {"fixed_point_result", RELEASE_TEXT, "pass"},
    {"linux_candidate_build_plan_sha256", RELEASE_TEXT, release->linux_plan_sha256},
    {"native_build_plan_sha256", RELEASE_TEXT, release->windows_plan_sha256},
    {"parent_execution_seed_manifest_sha256", RELEASE_TEXT, release->parent_windows_manifest_sha256},
    {"parent_execution_seed_source_revision", RELEASE_TEXT, release->parent_source_revision},
    {"parent_plan_seed_manifest_sha256", RELEASE_TEXT, release->parent_linux_manifest_sha256},
    {"parent_plan_seed_source_revision", RELEASE_TEXT, release->parent_source_revision},
    {"plan_seed_manifest_sha256", RELEASE_DIGEST, 0},
    {"producer_lineage", RELEASE_OBJECT, 0},
    {"source_input_count", RELEASE_COUNT, 0},
    {"source_revision", RELEASE_TEXT, release->source_revision},
    {"source_snapshot_sha256", RELEASE_TEXT, release->source_snapshot_sha256}
  };
  return match_fields(error, reader, release, format,
                       format == 1u ? linux_fields : windows_fields,
                       format == 1u ? 10u : 14u);
}

int cupid_seed_release_match_manifest(
    const unsigned char *release_bytes, size_t release_size,
    const unsigned char *manifest_bytes, size_t manifest_size,
    unsigned int format, char *error, size_t error_capacity) {
  cupid_seed_release_t release;
  error_context_t context = {error, error_capacity, 0};
  json_reader_t reader = {manifest_bytes, manifest_size, 0u};
  const release_field_t linux_fields[6] = {
    {"schema", RELEASE_TEXT, "cupid.bootstrap-seed.v2"},
    {"artifacts", RELEASE_ARTIFACTS, 0},
    {"provenance", RELEASE_PROVENANCE, 0},
    {"target", RELEASE_OBJECT, 0},
    {"build_plan", RELEASE_OBJECT, 0},
    {"build_plan_sha256", RELEASE_TEXT, release.linux_plan_sha256}
  };
  const release_field_t windows_fields[4] = {
    {"schema", RELEASE_TEXT, "cupid.execution-seed.v2"},
    {"artifacts", RELEASE_ARTIFACTS, 0},
    {"provenance", RELEASE_PROVENANCE, 0},
    {"target", RELEASE_OBJECT, 0}
  };
  if (!cupid_seed_release_parse(release_bytes, release_size, &release,
                                 error, error_capacity)) return 0;
  if ((format != 1u && format != 2u) || !manifest_bytes ||
      !manifest_size || manifest_size > 1048576u) {
    return fail(&context, "invalid release manifest arguments");
  }
  if (!match_fields(&context, &reader, &release, format,
                     format == 1u ? linux_fields : windows_fields,
                     format == 1u ? 6u : 4u)) return 0;
  return cupid_contract_json_finish(&context, &reader);
}
