#include "ctool.h"
#include "ctool_host.h"
#include "cupiddis.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CUPIDDIS_HOST_SOURCE_BYTES 67108864u
#define CUPIDDIS_HOST_ARENA_BYTES 134217728u

typedef struct {
  ctool_bool raw;
  ctool_bool nm;
  ctool_bool have_mode;
  ctool_bool have_base;
  ctool_bool have_views;
  ctool_bool require_known;
  ctool_bool require_local_targets;
  ctool_bool require_source_edges;
  ctool_bool require_code_anchors;
  ctool_x86_mode_t mode;
  ctool_u32 base_address;
  ctool_u32 views;
  ctool_dis_raw_range_t *raw_ranges;
  ctool_u32 range_boundary_count;
  ctool_u32 range_capacity;
  ctool_bool mapped_ranges;
  ctool_dis_raw_edge_t *raw_edges;
  ctool_u32 raw_edge_count;
  ctool_bool raw_edge_metadata_present;
  const char *range_map;
  ctool_u32 range_map_size;
  const char *input;
  const char **inputs;
  ctool_u32 input_count;
  ctool_u32 input_capacity;
} cupiddis_cli_t;

static void cupiddis_usage(FILE *stream) {
  (void)fprintf(
      stream,
      "usage: cupiddis [--headers] [--sections] [--symbols] "
      "[--relocations] [--disassemble] [--all] [--nm] FILE\n"
      "       cupiddis --require-known [--require-local-targets] "
      "[--require-source-edges] [--require-code-anchors] "
      "[--headers] [--sections] "
      "[--symbols] [--relocations] [--disassemble] [--all] "
      "FILE [FILE...]\n"
      "       cupiddis --raw --mode 16|32 "
      "[--range-at OFFSET:16|32|data]... "
      "[--mode-at OFFSET:16|32]... --base ADDRESS FILE\n"
      "       cupiddis --raw --range-map MAP FILE\n"
      "       cupiddis --require-known --raw --mode 16|32 "
      "[--require-local-targets] "
      "[--range-at OFFSET:16|32|data]... "
      "[--mode-at OFFSET:16|32]... --base ADDRESS FILE [FILE...]\n"
      "       cupiddis --require-known --raw --range-map MAP "
      "[--require-local-targets] [--require-source-edges] "
      "FILE [FILE...]\n");
}

static int cupiddis_parse_u32_span(const char *text, size_t size,
                                   ctool_u32 *value_out) {
  ctool_u32 base = 10u;
  ctool_u32 value = 0u;
  size_t index = 0u;
  if (text == (const char *)0 || size == 0u ||
      value_out == (ctool_u32 *)0) {
    return 0;
  }
  if (size >= 2u && text[0] == '0' &&
      (text[1] == 'x' || text[1] == 'X')) {
    base = 16u;
    index = 2u;
    if (index == size) {
      return 0;
    }
  }
  while (index < size) {
    ctool_u32 digit;
    char character = text[index];
    if (character >= '0' && character <= '9') {
      digit = (ctool_u32)(character - '0');
    } else if (character >= 'a' && character <= 'f') {
      digit = 10u + (ctool_u32)(character - 'a');
    } else if (character >= 'A' && character <= 'F') {
      digit = 10u + (ctool_u32)(character - 'A');
    } else {
      return 0;
    }
    if (digit >= base || value > (4294967295u - digit) / base) {
      return 0;
    }
    value = value * base + digit;
    index++;
  }
  *value_out = value;
  return 1;
}

static int cupiddis_parse_u32(const char *text, ctool_u32 *value_out) {
  return text == (const char *)0
             ? 0
             : cupiddis_parse_u32_span(text, strlen(text), value_out);
}

static int cupiddis_parse_mode(const char *text, ctool_x86_mode_t *mode_out) {
  if (text == (const char *)0 || mode_out == (ctool_x86_mode_t *)0) {
    return 0;
  }
  if (strcmp(text, "16") == 0) {
    *mode_out = CTOOL_X86_MODE_16;
    return 1;
  }
  if (strcmp(text, "32") == 0) {
    *mode_out = CTOOL_X86_MODE_32;
    return 1;
  }
  return 0;
}

static int cupiddis_parse_range_kind(
    const char *text, ctool_bool allow_data,
    ctool_dis_raw_range_kind_t *kind_out) {
  ctool_x86_mode_t mode;
  if (text == (const char *)0 ||
      kind_out == (ctool_dis_raw_range_kind_t *)0) {
    return 0;
  }
  if (cupiddis_parse_mode(text, &mode) != 0) {
    *kind_out = mode == CTOOL_X86_MODE_16 ? CTOOL_DIS_RAW_RANGE_CODE16
                                          : CTOOL_DIS_RAW_RANGE_CODE32;
    return 1;
  }
  if (allow_data == CTOOL_TRUE && strcmp(text, "data") == 0) {
    *kind_out = CTOOL_DIS_RAW_RANGE_DATA;
    return 1;
  }
  return 0;
}

static int cupiddis_parse_range_change(const char *text,
                                       ctool_bool allow_data,
                                       ctool_dis_raw_range_t *range_out) {
  const char *separator;
  size_t offset_size;
  if (text == (const char *)0 || range_out == (ctool_dis_raw_range_t *)0) {
    return 0;
  }
  separator = strchr(text, ':');
  if (separator == (const char *)0 || separator == text ||
      separator[1] == '\0' || strchr(separator + 1, ':') != (char *)0) {
    return 0;
  }
  offset_size = (size_t)(separator - text);
  return cupiddis_parse_u32_span(text, offset_size, &range_out->offset) != 0 &&
                 cupiddis_parse_range_kind(separator + 1, allow_data,
                                           &range_out->kind) != 0
             ? 1
             : 0;
}

static int cupiddis_append_range_change(cupiddis_cli_t *cli,
                                         ctool_dis_raw_range_t range) {
  ctool_u32 required;
  if (cli->range_boundary_count > 4294967293u) {
    return 0;
  }
  required = cli->range_boundary_count + 2u;
  if (required > cli->range_capacity) {
    ctool_u32 capacity =
        cli->range_capacity == 0u ? 4u : cli->range_capacity;
    ctool_dis_raw_range_t *resized;
    size_t allocation_size;
    while (capacity < required) {
      if (capacity > 2147483647u) {
        capacity = required;
        break;
      }
      capacity *= 2u;
    }
    allocation_size = (size_t)capacity * sizeof(*resized);
    if (capacity != 0u &&
        allocation_size / sizeof(*resized) != (size_t)capacity) {
      return 0;
    }
    resized = (ctool_dis_raw_range_t *)realloc(
        cli->raw_ranges, allocation_size);
    if (resized == (ctool_dis_raw_range_t *)0) {
      return 0;
    }
    cli->raw_ranges = resized;
    cli->range_capacity = capacity;
  }
  cli->raw_ranges[cli->range_boundary_count + 1u] = range;
  cli->range_boundary_count++;
  return 1;
}

static int cupiddis_append_input(cupiddis_cli_t *cli, const char *input) {
  ctool_u32 required;
  if (cli->input_count == 4294967295u) {
    return 0;
  }
  required = cli->input_count + 1u;
  if (required > cli->input_capacity) {
    ctool_u32 capacity =
        cli->input_capacity == 0u ? 4u : cli->input_capacity;
    const char **resized;
    size_t allocation_size;
    while (capacity < required) {
      if (capacity > 2147483647u) {
        capacity = required;
        break;
      }
      capacity *= 2u;
    }
    allocation_size = (size_t)capacity * sizeof(*resized);
    if (capacity != 0u &&
        allocation_size / sizeof(*resized) != (size_t)capacity) {
      return 0;
    }
    resized = (const char **)realloc(cli->inputs, allocation_size);
    if (resized == (const char **)0) {
      return 0;
    }
    cli->inputs = resized;
    cli->input_capacity = capacity;
  }
  cli->inputs[cli->input_count] = input;
  cli->input_count++;
  if (cli->input == (const char *)0) {
    cli->input = input;
  }
  return 1;
}

typedef struct {
  const char *data;
  size_t size;
} cupiddis_span_t;

static int cupiddis_span_equal(cupiddis_span_t span, const char *text) {
  size_t text_size = strlen(text);
  return span.size == text_size &&
                 memcmp(span.data, text, text_size) == 0
             ? 1
             : 0;
}

static int cupiddis_tokenize_line(const char *line, size_t line_size,
                                  cupiddis_span_t *tokens,
                                  size_t token_capacity,
                                  size_t *token_count_out) {
  size_t cursor = 0u;
  size_t count = 0u;
  while (cursor < line_size) {
    size_t start;
    while (cursor < line_size &&
           (line[cursor] == ' ' || line[cursor] == '\t')) {
      cursor++;
    }
    if (cursor == line_size) {
      break;
    }
    if (count == token_capacity) {
      return 0;
    }
    start = cursor;
    while (cursor < line_size && line[cursor] != ' ' &&
           line[cursor] != '\t') {
      cursor++;
    }
    tokens[count].data = line + start;
    tokens[count].size = cursor - start;
    count++;
  }
  *token_count_out = count;
  return 1;
}

static int cupiddis_read_range_map(const char *path, char **contents_out,
                                   size_t *size_out) {
  FILE *stream;
  long length;
  char *contents;
  size_t total = 0u;
#if defined(_WIN32)
  stream = (FILE *)0;
  if (fopen_s(&stream, path, "rb") != 0) {
    stream = (FILE *)0;
  }
#else
  stream = fopen(path, "rb");
#endif
  if (stream == (FILE *)0) {
    return 0;
  }
  if (fseek(stream, 0l, SEEK_END) != 0) {
    (void)fclose(stream);
    return 0;
  }
  length = ftell(stream);
  if (length < 0l || length > 1048576l || fseek(stream, 0l, 0) != 0) {
    (void)fclose(stream);
    return 0;
  }
  contents = (char *)malloc((size_t)length + 1u);
  if (contents == (char *)0) {
    (void)fclose(stream);
    return 0;
  }
  while (total < (size_t)length) {
    size_t count = fread(contents + total, 1u, (size_t)length - total,
                         stream);
    if (count == 0u) {
      free(contents);
      (void)fclose(stream);
      return 0;
    }
    total += count;
  }
  if (fclose(stream) != 0) {
    free(contents);
    return 0;
  }
  contents[total] = '\0';
  *contents_out = contents;
  *size_out = total;
  return 1;
}

static int cupiddis_map_kind(cupiddis_span_t token,
                             ctool_dis_raw_range_kind_t *kind_out) {
  if (cupiddis_span_equal(token, "code16")) {
    *kind_out = CTOOL_DIS_RAW_RANGE_CODE16;
    return 1;
  }
  if (cupiddis_span_equal(token, "code32")) {
    *kind_out = CTOOL_DIS_RAW_RANGE_CODE32;
    return 1;
  }
  if (cupiddis_span_equal(token, "data")) {
    *kind_out = CTOOL_DIS_RAW_RANGE_DATA;
    return 1;
  }
  return 0;
}

static int cupiddis_map_edge_kind(cupiddis_span_t token,
                                  ctool_dis_raw_edge_kind_t *kind_out) {
  if (cupiddis_span_equal(token, "relative")) {
    *kind_out = CTOOL_DIS_RAW_EDGE_RELATIVE;
    return 1;
  }
  if (cupiddis_span_equal(token, "far")) {
    *kind_out = CTOOL_DIS_RAW_EDGE_FAR;
    return 1;
  }
  if (cupiddis_span_equal(token, "indirect")) {
    *kind_out = CTOOL_DIS_RAW_EDGE_INDIRECT;
    return 1;
  }
  return 0;
}

static int cupiddis_map_edge_class(
    cupiddis_span_t token, ctool_dis_raw_edge_class_t *class_out) {
  if (cupiddis_span_equal(token, "local")) {
    *class_out = CTOOL_DIS_RAW_EDGE_LOCAL;
    return 1;
  }
  if (cupiddis_span_equal(token, "external")) {
    *class_out = CTOOL_DIS_RAW_EDGE_EXTERNAL;
    return 1;
  }
  if (cupiddis_span_equal(token, "unprovable")) {
    *class_out = CTOOL_DIS_RAW_EDGE_UNPROVABLE;
    return 1;
  }
  return 0;
}

static int cupiddis_map_u32_or_none(cupiddis_span_t token,
                                    ctool_u32 *value_out) {
  if (cupiddis_span_equal(token, "-")) {
    *value_out = CTOOL_DIS_RAW_EDGE_NO_TARGET;
    return 1;
  }
  return cupiddis_parse_u32_span(token.data, token.size, value_out);
}

static int cupiddis_map_mode_or_unknown(cupiddis_span_t token,
                                        ctool_x86_mode_t *mode_out) {
  ctool_u32 value;
  if (cupiddis_span_equal(token, "unknown")) {
    *mode_out = (ctool_x86_mode_t)0;
    return 1;
  }
  if (!cupiddis_parse_u32_span(token.data, token.size, &value) ||
      (value != 16u && value != 32u)) {
    return 0;
  }
  *mode_out = (ctool_x86_mode_t)value;
  return 1;
}

static int cupiddis_load_range_map(cupiddis_cli_t *cli,
                                   const char **message_out) {
  char *contents = (char *)0;
  size_t contents_size = 0u;
  size_t cursor = 0u;
  ctool_bool have_schema = CTOOL_FALSE;
  ctool_bool schema_v2 = CTOOL_FALSE;
  ctool_bool have_size = CTOOL_FALSE;
  ctool_bool have_base = CTOOL_FALSE;
  ctool_bool have_edge_count = CTOOL_FALSE;
  ctool_u32 expected_size = 0u;
  ctool_u32 expected_edge_count = 0u;
  ctool_u32 base = 0u;
  ctool_dis_raw_range_t *ranges = (ctool_dis_raw_range_t *)0;
  ctool_u32 range_count = 0u;
  ctool_u32 range_capacity = 0u;
  ctool_dis_raw_edge_t *edges = (ctool_dis_raw_edge_t *)0;
  ctool_u32 edge_count = 0u;
  ctool_u32 edge_capacity = 0u;
  int success = 0;
  *message_out = "raw range map could not be read";
  if (!cupiddis_read_range_map(cli->range_map, &contents,
                               &contents_size)) {
    return 0;
  }
  while (cursor < contents_size) {
    size_t line_start = cursor;
    size_t line_size;
    cupiddis_span_t tokens[8];
    size_t token_count = 0u;
    while (cursor < contents_size && contents[cursor] != '\n') {
      cursor++;
    }
    line_size = cursor - line_start;
    if (line_size != 0u && contents[line_start + line_size - 1u] == '\r') {
      line_size--;
    }
    if (cursor < contents_size) {
      cursor++;
    }
    if (!cupiddis_tokenize_line(contents + line_start, line_size, tokens,
                                8u, &token_count) || token_count == 0u) {
      *message_out = "raw range map contains an invalid line";
      goto done;
    }
    if (have_schema == CTOOL_FALSE) {
      if (token_count != 1u ||
          (!cupiddis_span_equal(tokens[0], "cupid.raw-map.v1") &&
           !cupiddis_span_equal(tokens[0], "cupid.raw-map.v2"))) {
        *message_out = "raw range map has an unsupported schema";
        goto done;
      }
      schema_v2 = cupiddis_span_equal(tokens[0], "cupid.raw-map.v2")
                      ? CTOOL_TRUE
                      : CTOOL_FALSE;
      have_schema = CTOOL_TRUE;
      continue;
    }
    if (cupiddis_span_equal(tokens[0], "size")) {
      if (token_count != 2u || have_size == CTOOL_TRUE ||
          !cupiddis_parse_u32_span(tokens[1].data, tokens[1].size,
                                   &expected_size) ||
          expected_size == 0u) {
        *message_out = "raw range map requires one nonzero size";
        goto done;
      }
      have_size = CTOOL_TRUE;
      continue;
    }
    if (cupiddis_span_equal(tokens[0], "base")) {
      if (token_count != 2u || have_base == CTOOL_TRUE ||
          !cupiddis_parse_u32_span(tokens[1].data, tokens[1].size,
                                   &base)) {
        *message_out = "raw range map requires one base address";
        goto done;
      }
      have_base = CTOOL_TRUE;
      continue;
    }
    if (cupiddis_span_equal(tokens[0], "edges")) {
      if (schema_v2 == CTOOL_FALSE || token_count != 2u ||
          have_edge_count == CTOOL_TRUE ||
          !cupiddis_parse_u32_span(tokens[1].data, tokens[1].size,
                                   &expected_edge_count)) {
        *message_out = "raw range map v2 requires one edge count";
        goto done;
      }
      have_edge_count = CTOOL_TRUE;
      continue;
    }
    if (cupiddis_span_equal(tokens[0], "range")) {
      ctool_dis_raw_range_t range;
      ctool_dis_raw_range_t *resized;
      ctool_u32 capacity;
      size_t allocation_size;
      if (token_count != 3u ||
          !cupiddis_parse_u32_span(tokens[1].data, tokens[1].size,
                                   &range.offset)) {
        *message_out = "raw range map contains an invalid range start";
        goto done;
      }
      if (!cupiddis_map_kind(tokens[2], &range.kind)) {
        *message_out =
            "raw range kind must be code16, code32, or data";
        goto done;
      }
      if (range_count != 0u &&
          range.offset <= ranges[range_count - 1u].offset) {
        *message_out = "raw range starts must increase";
        goto done;
      }
      if (range_count == 4294967295u) {
        *message_out = "raw range map has too many ranges";
        goto done;
      }
      if (range_count == range_capacity) {
        capacity = range_capacity == 0u ? 4u : range_capacity;
        if (capacity > 2147483647u) {
          capacity = range_count + 1u;
        } else {
          capacity *= 2u;
        }
        allocation_size = (size_t)capacity * sizeof(*resized);
        if (allocation_size / sizeof(*resized) != (size_t)capacity) {
          *message_out = "raw range map has too many ranges";
          goto done;
        }
        resized = (ctool_dis_raw_range_t *)realloc(ranges,
                                                    allocation_size);
        if (resized == (ctool_dis_raw_range_t *)0) {
          *message_out = "raw range map exceeds the host limit";
          goto done;
        }
        ranges = resized;
        range_capacity = capacity;
      }
      ranges[range_count++] = range;
      continue;
    }
    if (cupiddis_span_equal(tokens[0], "edge")) {
      ctool_dis_raw_edge_t edge;
      ctool_dis_raw_edge_t *resized;
      ctool_u32 capacity;
      size_t allocation_size;
      if (schema_v2 == CTOOL_FALSE || token_count != 8u ||
          !cupiddis_parse_u32_span(tokens[1].data, tokens[1].size,
                                   &edge.source_offset)) {
        *message_out = "raw range map contains an invalid edge source";
        goto done;
      }
      if (!cupiddis_map_edge_kind(tokens[2], &edge.kind)) {
        *message_out = "raw edge kind must be relative, far, or indirect";
        goto done;
      }
      if (!cupiddis_map_edge_class(tokens[3], &edge.class_id)) {
        *message_out =
            "raw edge class must be local, external, or unprovable";
        goto done;
      }
      if (!cupiddis_map_u32_or_none(tokens[4], &edge.target_offset) ||
          !cupiddis_map_u32_or_none(tokens[5], &edge.target_address) ||
          !cupiddis_map_mode_or_unknown(tokens[6], &edge.target_mode) ||
          !cupiddis_map_u32_or_none(tokens[7], &edge.target_segment)) {
        *message_out = "raw range map contains an invalid edge target";
        goto done;
      }
      if ((edge.class_id == CTOOL_DIS_RAW_EDGE_LOCAL &&
           (cupiddis_span_equal(tokens[4], "-") ||
            cupiddis_span_equal(tokens[5], "-") ||
            cupiddis_span_equal(tokens[6], "unknown") ||
            cupiddis_span_equal(tokens[7], "-"))) ||
          (edge.class_id == CTOOL_DIS_RAW_EDGE_EXTERNAL &&
           (!cupiddis_span_equal(tokens[4], "-") ||
            cupiddis_span_equal(tokens[5], "-") ||
            cupiddis_span_equal(tokens[6], "unknown") ||
            cupiddis_span_equal(tokens[7], "-"))) ||
          (edge.class_id == CTOOL_DIS_RAW_EDGE_UNPROVABLE &&
           (!cupiddis_span_equal(tokens[4], "-") ||
            !cupiddis_span_equal(tokens[5], "-") ||
            !cupiddis_span_equal(tokens[6], "unknown") ||
            !cupiddis_span_equal(tokens[7], "-")))) {
        *message_out =
            "raw range map edge fields disagree with their class";
        goto done;
      }
      if (edge_count != 0u &&
          edge.source_offset <= edges[edge_count - 1u].source_offset) {
        *message_out = "raw edge sources must increase without overlap";
        goto done;
      }
      if (edge_count == 4294967295u) {
        *message_out = "raw range map has too many edges";
        goto done;
      }
      if (edge_count == edge_capacity) {
        capacity = edge_capacity == 0u ? 4u : edge_capacity;
        if (capacity > 2147483647u) {
          capacity = edge_count + 1u;
        } else {
          capacity *= 2u;
        }
        allocation_size = (size_t)capacity * sizeof(*resized);
        if (allocation_size / sizeof(*resized) != (size_t)capacity) {
          *message_out = "raw range map has too many edges";
          goto done;
        }
        resized = (ctool_dis_raw_edge_t *)realloc(edges, allocation_size);
        if (resized == (ctool_dis_raw_edge_t *)0) {
          *message_out = "raw range map exceeds the host limit";
          goto done;
        }
        edges = resized;
        edge_capacity = capacity;
      }
      edges[edge_count++] = edge;
      continue;
    }
    *message_out = "raw range map contains an unknown record";
    goto done;
  }
  if (have_schema == CTOOL_FALSE) {
    *message_out = "raw range map has an unsupported schema";
    goto done;
  }
  if (have_size == CTOOL_FALSE) {
    *message_out = "raw range map requires one size";
    goto done;
  }
  if (have_base == CTOOL_FALSE) {
    *message_out = "raw range map requires one base address";
    goto done;
  }
  if (schema_v2 == CTOOL_TRUE && have_edge_count == CTOOL_FALSE) {
    *message_out = "raw range map v2 requires one edge count";
    goto done;
  }
  if (schema_v2 == CTOOL_FALSE && edge_count != 0u) {
    *message_out = "raw range map v1 cannot contain edge records";
    goto done;
  }
  if (schema_v2 == CTOOL_TRUE && edge_count != expected_edge_count) {
    *message_out = "raw range map edge count does not match its records";
    goto done;
  }
  if (range_count == 0u) {
    *message_out = "raw range map requires at least one range";
    goto done;
  }
  if (ranges[0].offset != 0u) {
    *message_out = "raw range map must start at offset zero";
    goto done;
  }
  if (ranges[range_count - 1u].offset >= expected_size) {
    *message_out = "raw range start is outside the mapped image";
    goto done;
  }
  {
    ctool_u32 edge_index;
    for (edge_index = 0u; edge_index < edge_count; edge_index++) {
      const ctool_dis_raw_edge_t *edge = &edges[edge_index];
      ctool_dis_raw_range_kind_t source_kind = CTOOL_DIS_RAW_RANGE_DATA;
      ctool_u32 range_index;
      if (edge->source_offset >= expected_size) {
        *message_out = "raw edge source is outside the mapped image";
        goto done;
      }
      for (range_index = 0u; range_index < range_count; range_index++) {
        if (ranges[range_index].offset > edge->source_offset) {
          break;
        }
        source_kind = ranges[range_index].kind;
      }
      if (source_kind == CTOOL_DIS_RAW_RANGE_DATA) {
        *message_out = "raw edge source must be inside code";
        goto done;
      }
      if (edge->class_id == CTOOL_DIS_RAW_EDGE_LOCAL) {
        ctool_dis_raw_range_kind_t target_kind = CTOOL_DIS_RAW_RANGE_DATA;
        if (edge->kind == CTOOL_DIS_RAW_EDGE_INDIRECT ||
            edge->target_offset == CTOOL_DIS_RAW_EDGE_NO_TARGET ||
            (edge->target_mode != CTOOL_X86_MODE_16 &&
             edge->target_mode != CTOOL_X86_MODE_32) ||
            edge->target_offset >= expected_size ||
            base > 4294967295u - edge->target_offset ||
            base + edge->target_offset != edge->target_address) {
          *message_out = "raw local edge target is inconsistent";
          goto done;
        }
        for (range_index = 0u; range_index < range_count; range_index++) {
          if (ranges[range_index].offset > edge->target_offset) {
            break;
          }
          target_kind = ranges[range_index].kind;
        }
        if (target_kind == CTOOL_DIS_RAW_RANGE_DATA ||
            (target_kind == CTOOL_DIS_RAW_RANGE_CODE16
                 ? CTOOL_X86_MODE_16
                 : CTOOL_X86_MODE_32) != edge->target_mode) {
          *message_out = "raw local edge target mode disagrees with ranges";
          goto done;
        }
        if ((edge->kind == CTOOL_DIS_RAW_EDGE_RELATIVE &&
             edge->target_segment != 0u) ||
            edge->target_segment == CTOOL_DIS_RAW_EDGE_NO_TARGET) {
          *message_out = "raw local edge segment is inconsistent";
          goto done;
        }
      } else if (edge->class_id == CTOOL_DIS_RAW_EDGE_EXTERNAL) {
        if (edge->kind == CTOOL_DIS_RAW_EDGE_INDIRECT ||
            edge->target_offset != CTOOL_DIS_RAW_EDGE_NO_TARGET ||
            (edge->target_mode != CTOOL_X86_MODE_16 &&
             edge->target_mode != CTOOL_X86_MODE_32) ||
            edge->target_segment == CTOOL_DIS_RAW_EDGE_NO_TARGET ||
            (edge->kind == CTOOL_DIS_RAW_EDGE_RELATIVE &&
             edge->target_segment != 0u)) {
          *message_out = "raw external edge target is inconsistent";
          goto done;
        }
        if (edge->target_address >= base &&
            edge->target_address - base < expected_size) {
          *message_out =
              "raw external edge target is inside the mapped image";
          goto done;
        }
      } else if (edge->kind != CTOOL_DIS_RAW_EDGE_INDIRECT ||
                 edge->target_offset != CTOOL_DIS_RAW_EDGE_NO_TARGET ||
                 edge->target_address != CTOOL_DIS_RAW_EDGE_NO_TARGET ||
                 edge->target_mode != (ctool_x86_mode_t)0 ||
                 edge->target_segment != CTOOL_DIS_RAW_EDGE_NO_TARGET) {
        *message_out = "raw unprovable edge target must be unknown";
        goto done;
      }
    }
  }
  cli->raw_ranges = ranges;
  cli->range_capacity = range_capacity;
  cli->range_boundary_count = range_count - 1u;
  cli->mapped_ranges = CTOOL_TRUE;
  cli->range_map_size = expected_size;
  cli->base_address = base;
  cli->raw_edges = edges;
  cli->raw_edge_count = edge_count;
  cli->raw_edge_metadata_present = schema_v2;
  ranges = (ctool_dis_raw_range_t *)0;
  edges = (ctool_dis_raw_edge_t *)0;
  success = 1;

done:
  free(ranges);
  free(edges);
  free(contents);
  return success;
}

static int cupiddis_take_value(int argc, char **argv, int *index,
                               const char *argument, const char *prefix,
                               const char **value_out) {
  size_t prefix_size = strlen(prefix);
  if (strncmp(argument, prefix, prefix_size) == 0 &&
      argument[prefix_size] == '=') {
    *value_out = argument + prefix_size + 1u;
    return 1;
  }
  if (strcmp(argument, prefix) == 0) {
    if (*index + 1 >= argc) {
      return -1;
    }
    *index = *index + 1;
    *value_out = argv[*index];
    return 1;
  }
  return 0;
}

static int cupiddis_parse_cli(int argc, char **argv, cupiddis_cli_t *cli) {
  int index;
  (void)memset(cli, 0, sizeof(*cli));
  for (index = 1; index < argc; index++) {
    const char *argument = argv[index];
    const char *value = (const char *)0;
    int taken;
    if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
      return -1;
    }
    if (strcmp(argument, "--raw") == 0) {
      cli->raw = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--nm") == 0 || strcmp(argument, "-n") == 0) {
      cli->nm = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--require-known") == 0) {
      cli->require_known = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--require-local-targets") == 0) {
      cli->require_local_targets = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--require-source-edges") == 0) {
      cli->require_source_edges = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--require-code-anchors") == 0) {
      cli->require_code_anchors = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--headers") == 0) {
      cli->views |= CTOOL_DIS_VIEW_HEADER;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--sections") == 0) {
      cli->views |= CTOOL_DIS_VIEW_SECTIONS;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--symbols") == 0) {
      cli->views |= CTOOL_DIS_VIEW_SYMBOLS;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--relocations") == 0) {
      cli->views |= CTOOL_DIS_VIEW_RELOCATIONS;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--disassemble") == 0) {
      cli->views |= CTOOL_DIS_VIEW_DISASSEMBLY;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    if (strcmp(argument, "--all") == 0) {
      cli->views = CTOOL_DIS_VIEW_ALL;
      cli->have_views = CTOOL_TRUE;
      continue;
    }
    taken = cupiddis_take_value(argc, argv, &index, argument, "--mode",
                                &value);
    if (taken != 0) {
      if (taken < 0 || cli->have_mode == CTOOL_TRUE ||
          cupiddis_parse_mode(value, &cli->mode) == 0) {
        return 0;
      }
      cli->have_mode = CTOOL_TRUE;
      continue;
    }
    taken = cupiddis_take_value(argc, argv, &index, argument, "--base",
                                &value);
    if (taken != 0) {
      if (taken < 0 || cli->have_base == CTOOL_TRUE ||
          cupiddis_parse_u32(value, &cli->base_address) == 0) {
        return 0;
      }
      cli->have_base = CTOOL_TRUE;
      continue;
    }
    taken = cupiddis_take_value(argc, argv, &index, argument, "--range-map",
                                &value);
    if (taken != 0) {
      if (taken < 0 || cli->range_map != (const char *)0 ||
          value[0] == '\0') {
        return 0;
      }
      cli->range_map = value;
      continue;
    }
    taken = cupiddis_take_value(argc, argv, &index, argument, "--mode-at",
                                &value);
    if (taken != 0) {
      ctool_dis_raw_range_t range;
      if (taken < 0 ||
          cupiddis_parse_range_change(value, CTOOL_FALSE, &range) == 0 ||
          cupiddis_append_range_change(cli, range) == 0) {
        return 0;
      }
      continue;
    }
    taken = cupiddis_take_value(argc, argv, &index, argument, "--range-at",
                                &value);
    if (taken != 0) {
      ctool_dis_raw_range_t range;
      if (taken < 0 ||
          cupiddis_parse_range_change(value, CTOOL_TRUE, &range) == 0 ||
          cupiddis_append_range_change(cli, range) == 0) {
        return 0;
      }
      continue;
    }
    if (argument[0] == '-') {
      return 0;
    }
    if (cupiddis_append_input(cli, argument) == 0) {
      return 0;
    }
  }
  if (cli->input_count == 0u ||
      (cli->require_known == CTOOL_FALSE && cli->input_count != 1u)) {
    return 0;
  }
  if (cli->require_local_targets == CTOOL_TRUE &&
      cli->require_known == CTOOL_FALSE) {
    return 0;
  }
  if (cli->require_source_edges == CTOOL_TRUE &&
      (cli->require_known == CTOOL_FALSE || cli->raw == CTOOL_FALSE ||
       cli->range_map == (const char *)0)) {
    return 0;
  }
  if (cli->require_code_anchors == CTOOL_TRUE &&
      (cli->require_known == CTOOL_FALSE || cli->raw == CTOOL_TRUE)) {
    return 0;
  }
  if (cli->raw == CTOOL_TRUE) {
    if (cli->nm == CTOOL_TRUE ||
        (cli->have_views == CTOOL_TRUE &&
         cli->views != CTOOL_DIS_VIEW_DISASSEMBLY)) {
      return 0;
    }
    if (cli->range_map != (const char *)0) {
      if (cli->have_mode == CTOOL_TRUE || cli->have_base == CTOOL_TRUE ||
          cli->range_boundary_count != 0u) {
        return 0;
      }
    } else if (cli->have_mode == CTOOL_FALSE ||
               cli->have_base == CTOOL_FALSE) {
      return 0;
    }
    if (cli->have_views == CTOOL_FALSE) {
      cli->views = CTOOL_DIS_VIEW_DISASSEMBLY;
    }
    if (cli->range_boundary_count != 0u) {
      cli->raw_ranges[0].offset = 0u;
      cli->raw_ranges[0].kind = cli->mode == CTOOL_X86_MODE_16
                                    ? CTOOL_DIS_RAW_RANGE_CODE16
                                    : CTOOL_DIS_RAW_RANGE_CODE32;
      cli->mapped_ranges = CTOOL_TRUE;
    }
  } else {
    if (cli->have_mode == CTOOL_TRUE || cli->have_base == CTOOL_TRUE ||
        cli->range_boundary_count != 0u || cli->range_map != (const char *)0 ||
        (cli->nm == CTOOL_TRUE && cli->have_views == CTOOL_TRUE) ||
        (cli->nm == CTOOL_TRUE && cli->require_known == CTOOL_TRUE)) {
      return 0;
    }
    if (cli->nm == CTOOL_TRUE) {
      cli->views = CTOOL_DIS_VIEW_SYMBOLS;
    } else if (cli->have_views == CTOOL_FALSE) {
      cli->views = CTOOL_DIS_VIEW_ALL;
    }
  }
  if (cli->require_known == CTOOL_TRUE) {
    cli->views |= CTOOL_DIS_VIEW_DISASSEMBLY;
  }
  return 1;
}

static ctool_status_t cupiddis_stdout_write(void *context,
                                            ctool_bytes_t text) {
  FILE *stream = (FILE *)context;
  size_t written;
  if (text.size == 0u) {
    return CTOOL_OK;
  }
  written = fwrite(text.data, 1u, (size_t)text.size, stream);
  return written == (size_t)text.size ? CTOOL_OK : CTOOL_ERR_IO;
}

static ctool_status_t cupiddis_flush_output(ctool_job_t *job,
                                             ctool_string_t path) {
  ctool_diagnostic_t diagnostic;
  ctool_status_t emitted;
  if (fflush(stdout) == 0 && ferror(stdout) == 0) {
    return CTOOL_OK;
  }
  diagnostic.severity = CTOOL_DIAG_ERROR;
  diagnostic.code = CTOOL_DIS_DIAG_OUTPUT;
  diagnostic.path = path;
  diagnostic.line = 0u;
  diagnostic.column = 0u;
  diagnostic.message =
      ctool_string("CupidDis could not complete report output");
  emitted = ctool_job_emit(job, &diagnostic);
  return emitted == CTOOL_OK ? CTOOL_ERR_IO : emitted;
}

static int cupiddis_split_path(const char *path, char **root_out,
                               const char **name_out) {
  size_t size = strlen(path);
  size_t separator = size;
  size_t root_size;
  char *root;
  while (separator != 0u) {
    char character = path[separator - 1u];
    if (character == '/' || character == '\\') {
      separator--;
      break;
    }
    separator--;
  }
  if (size == 0u || (path[size - 1u] == '/' || path[size - 1u] == '\\')) {
    return 0;
  }
  if (separator == 0u && path[0] != '/' && path[0] != '\\') {
    root = (char *)malloc(2u);
    if (root == (char *)0) {
      return 0;
    }
    root[0] = '.';
    root[1] = '\0';
    *name_out = path;
  } else {
    root_size = separator;
    if (separator == 0u ||
        (separator == 2u && path[1] == ':')) {
      root_size++;
    }
    root = (char *)malloc(root_size + 1u);
    if (root == (char *)0) {
      return 0;
    }
    (void)memcpy(root, path, root_size);
    root[root_size] = '\0';
    *name_out = path + separator + 1u;
  }
  *root_out = root;
  return 1;
}

static int cupiddis_is_elf(ctool_bytes_t bytes) {
  return bytes.size >= 4u && bytes.data[0] == 0x7fu &&
                 bytes.data[1] == (ctool_u8)'E' &&
                 bytes.data[2] == (ctool_u8)'L' &&
                 bytes.data[3] == (ctool_u8)'F'
             ? 1
             : 0;
}

static void cupiddis_make_request(const cupiddis_cli_t *cli,
                                   ctool_dis_request_t *request) {
  (void)memset(request, 0, sizeof(*request));
  request->input = cli->raw == CTOOL_TRUE ? CTOOL_DIS_INPUT_RAW
                                          : CTOOL_DIS_INPUT_ELF32;
  request->views = cli->views;
  request->policies = 0u;
  if (cli->require_local_targets == CTOOL_TRUE) {
    request->policies |= CTOOL_DIS_POLICY_LOCAL_RELATIVE_TARGETS;
  }
  if (cli->require_source_edges == CTOOL_TRUE) {
    request->policies |= CTOOL_DIS_POLICY_SOURCE_CONTROL_EDGES;
  }
  if (cli->require_code_anchors == CTOOL_TRUE) {
    request->policies |= CTOOL_DIS_POLICY_CODE_ANCHORS;
  }
  request->raw_mode = cli->mapped_ranges == CTOOL_FALSE
                          ? cli->mode
                          : CTOOL_DIS_RAW_RANGE_MAP;
  request->raw_base_address = cli->base_address;
  if (cli->mapped_ranges == CTOOL_TRUE) {
    request->raw_ranges = cli->raw_ranges;
    request->raw_range_count = cli->range_boundary_count + 1u;
  }
  request->raw_edges = cli->raw_edges;
  request->raw_edge_count = cli->raw_edge_count;
  request->raw_edge_metadata_present = cli->raw_edge_metadata_present;
}

static int cupiddis_range_map_matches(const cupiddis_cli_t *cli,
                                      const ctool_source_t *source,
                                      const char *input) {
  if (cli->range_map == (const char *)0 ||
      cli->range_map_size == source->contents.size) {
    return 1;
  }
  (void)fprintf(stderr,
                "cupiddis: %s: raw range map expects %u bytes, input has "
                "%u\n",
                input, (unsigned int)cli->range_map_size,
                (unsigned int)source->contents.size);
  return 0;
}

static int cupiddis_check_known_input(const cupiddis_cli_t *cli,
                                       const ctool_x86_decoder_t *decoder,
                                       const char *input) {
  char *native_root = (char *)0;
  const char *logical_name = (const char *)0;
  ctool_host_adapter_t adapter;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_path_t root;
  ctool_path_t input_path;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  ctool_status_t status;
  int failed = 1;
  if (!cupiddis_split_path(input, &native_root, &logical_name)) {
    (void)fprintf(stderr, "cupiddis: %s: invalid input path\n", input);
    return 1;
  }
  limits.source_bytes = CUPIDDIS_HOST_SOURCE_BYTES;
  limits.arena_bytes = CUPIDDIS_HOST_ARENA_BYTES;
  status = ctool_host_adapter_init(&adapter, native_root);
  config = ctool_host_job_config(&adapter, limits);
  if (status == CTOOL_OK) {
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_path_root(ctool_job_arena(job), &root);
  }
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string(logical_name), limits.path_bytes,
                                &input_path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &input_path, &source);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupiddis: cannot load %s (%s)\n", input,
                  ctool_status_name(status));
    goto done;
  }
  if (!cupiddis_range_map_matches(cli, &source, input)) {
    goto done;
  }
  if (cli->raw == CTOOL_FALSE && !cupiddis_is_elf(source.contents)) {
    (void)fprintf(stderr,
                  "cupiddis: %s: input is not ELF32; raw input requires "
                  "--raw\n",
                  input);
    goto done;
  }
  cupiddis_make_request(cli, &request);
  status = ctool_dis_inspect_indexed(job, decoder, &source, &request,
                                     &report);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupiddis: %s: inspection failed (%s)\n", input,
                  ctool_status_name(status));
    if (ctool_job_diagnostic_count(job) != 0u) {
      (void)ctool_job_render_diagnostics(job);
    }
    goto done;
  }
  failed = 0;
  if (report.decode_summary.unknown_count != 0u ||
      report.decode_summary.invalid_count != 0u ||
      report.decode_summary.truncated_count != 0u ||
      report.decode_summary.unmatched_executable_relocation_count != 0u) {
    if (report.decode_summary.executable_relocation_count == 0u) {
      (void)fprintf(
          stderr,
          "cupiddis: %s: code check failed: %llu known, %llu unknown, "
          "%llu invalid, %llu truncated\n",
          input, (unsigned long long)report.decode_summary.known_count,
          (unsigned long long)report.decode_summary.unknown_count,
          (unsigned long long)report.decode_summary.invalid_count,
          (unsigned long long)report.decode_summary.truncated_count);
    } else {
      (void)fprintf(
          stderr,
          "cupiddis: %s: code check failed: %llu known, %llu unknown, "
          "%llu invalid, %llu truncated, %llu of %llu executable "
          "relocations unmatched\n",
          input, (unsigned long long)report.decode_summary.known_count,
          (unsigned long long)report.decode_summary.unknown_count,
          (unsigned long long)report.decode_summary.invalid_count,
          (unsigned long long)report.decode_summary.truncated_count,
          (unsigned long long)
              report.decode_summary.unmatched_executable_relocation_count,
          (unsigned long long)
              report.decode_summary.executable_relocation_count);
    }
    failed = 1;
  }
  if (cli->require_local_targets == CTOOL_TRUE) {
    ctool_u64 invalid_targets;
    if (cli->raw == CTOOL_TRUE) {
      invalid_targets =
          report.decode_summary.direct_relative_outside_image_count +
          report.decode_summary.direct_relative_data_count +
          report.decode_summary.direct_relative_wrong_mode_count +
          report.decode_summary.direct_relative_mid_instruction_count;
    } else if (report.elf32.file_type == CTOOL_ELF32_ET_EXEC) {
      invalid_targets =
          report.decode_summary.direct_relative_outside_image_count +
          report.decode_summary.direct_relative_data_count +
          report.decode_summary.direct_relative_mid_instruction_count;
    } else {
      invalid_targets =
          report.decode_summary.direct_relative_outside_section_count +
          report.decode_summary.direct_relative_mid_instruction_count;
    }
    if (invalid_targets != 0u) {
      if (cli->raw == CTOOL_TRUE) {
        (void)fprintf(
            stderr,
            "cupiddis: %s: local target check failed: %llu of %llu direct "
            "relative targets invalid (%llu outside image, %llu in data, "
            "%llu wrong mode, %llu mid-instruction)\n",
            input, (unsigned long long)invalid_targets,
            (unsigned long long)
                report.decode_summary.direct_relative_target_count,
            (unsigned long long)
                report.decode_summary.direct_relative_outside_image_count,
            (unsigned long long)
                report.decode_summary.direct_relative_data_count,
            (unsigned long long)
                report.decode_summary.direct_relative_wrong_mode_count,
            (unsigned long long)
                report.decode_summary.direct_relative_mid_instruction_count);
      } else if (report.elf32.file_type == CTOOL_ELF32_ET_EXEC) {
        (void)fprintf(
            stderr,
            "cupiddis: %s: local target check failed: %llu of %llu direct "
            "relative targets invalid (%llu outside loaded image, %llu in "
            "loaded bytes without file-backed executable code, %llu "
            "mid-instruction)\n",
            input, (unsigned long long)invalid_targets,
            (unsigned long long)
                report.decode_summary.direct_relative_target_count,
            (unsigned long long)
                report.decode_summary.direct_relative_outside_image_count,
            (unsigned long long)
                report.decode_summary.direct_relative_data_count,
            (unsigned long long)
                report.decode_summary.direct_relative_mid_instruction_count);
      } else {
        (void)fprintf(
            stderr,
            "cupiddis: %s: local target check failed: %llu of %llu direct "
            "relative targets invalid (%llu outside section, "
            "%llu mid-instruction)\n",
            input, (unsigned long long)invalid_targets,
            (unsigned long long)
                report.decode_summary.direct_relative_target_count,
            (unsigned long long)
                report.decode_summary.direct_relative_outside_section_count,
            (unsigned long long)
                report.decode_summary.direct_relative_mid_instruction_count);
      }
      failed = 1;
    }
  }
  if (cli->require_source_edges == CTOOL_TRUE) {
    ctool_u64 invalid_edges =
        report.decode_summary.source_control_edge_invalid_count;
    if (invalid_edges != 0u) {
      (void)fprintf(
          stderr,
          "cupiddis: %s: source control-edge check failed: %llu of %llu "
          "edges invalid (%llu source mismatch, %llu target mismatch, "
          "%llu target-mode mismatch, %llu explicitly unprovable)\n",
          input, (unsigned long long)invalid_edges,
          (unsigned long long)
              report.decode_summary.source_control_edge_count,
          (unsigned long long)
              report.decode_summary.source_control_edge_source_mismatch_count,
          (unsigned long long)
              report.decode_summary.source_control_edge_target_mismatch_count,
          (unsigned long long)
              report.decode_summary.source_control_edge_target_mode_mismatch_count,
          (unsigned long long)
              report.decode_summary.source_control_edge_unprovable_count);
      failed = 1;
    }
  }
  if (cli->require_code_anchors == CTOOL_TRUE) {
    ctool_u64 invalid_anchors =
        report.decode_summary.code_anchor_outside_executable_count +
        report.decode_summary.code_anchor_mid_instruction_count;
    if (invalid_anchors != 0u) {
      const char *outside =
          report.elf32.file_type == CTOOL_ELF32_ET_REL
              ? "outside executable PROGBITS"
              : "outside file-backed executable code";
      (void)fprintf(stderr,
                    "cupiddis: %s: code anchor check failed: %llu of %llu "
                    "code anchors invalid (%llu %s, %llu mid-instruction)\n",
                    input, (unsigned long long)invalid_anchors,
                    (unsigned long long)
                        report.decode_summary.code_anchor_count,
                    (unsigned long long)report.decode_summary
                        .code_anchor_outside_executable_count,
                    outside,
                    (unsigned long long)report.decode_summary
                        .code_anchor_mid_instruction_count);
      failed = 1;
    }
  }

done:
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  free(native_root);
  return failed;
}

int main(int argc, char **argv) {
  cupiddis_cli_t cli;
  char *native_root = (char *)0;
  const char *logical_name = (const char *)0;
  ctool_host_adapter_t adapter;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_job_t *job = (ctool_job_t *)0;
  ctool_path_t root;
  ctool_path_t input_path;
  ctool_source_t source;
  ctool_dis_request_t request;
  ctool_dis_report_t report;
  ctool_text_sink_t output;
  ctool_status_t status;
  int parsed = cupiddis_parse_cli(argc, argv, &cli);
  int exit_code = 1;
  if (parsed < 0) {
    cupiddis_usage(stdout);
    free(cli.raw_ranges);
    free(cli.raw_edges);
    free(cli.inputs);
    return 0;
  }
  if (parsed == 0) {
    cupiddis_usage(stderr);
    free(cli.raw_ranges);
    free(cli.raw_edges);
    free(cli.inputs);
    return 2;
  }
  if (cli.range_map != (const char *)0) {
    const char *message = (const char *)0;
    if (!cupiddis_load_range_map(&cli, &message)) {
      (void)fprintf(stderr, "cupiddis: %s: %s\n", cli.range_map, message);
      free(cli.raw_ranges);
      free(cli.raw_edges);
      free(cli.inputs);
      return 1;
    }
  }
  if (cli.require_known == CTOOL_TRUE) {
    const ctool_x86_decoder_t *decoder =
        (const ctool_x86_decoder_t *)0;
    ctool_u32 index;
    int failed = 0;
    limits.arena_bytes = CUPIDDIS_HOST_ARENA_BYTES;
    status = ctool_host_adapter_init(&adapter, ".");
    config = ctool_host_job_config(&adapter, limits);
    if (status == CTOOL_OK) {
      status = ctool_job_open(&config, &job);
    }
    if (status == CTOOL_OK) {
      status = ctool_x86_decoder_prepare(job, &decoder);
    }
    if (status != CTOOL_OK) {
      (void)fprintf(stderr, "cupiddis: cannot prepare x86 decoder (%s)\n",
                    ctool_status_name(status));
      failed = 1;
    } else {
      for (index = 0u; index < cli.input_count; index++) {
        if (cupiddis_check_known_input(&cli, decoder, cli.inputs[index]) !=
            0) {
          failed = 1;
        }
      }
    }
    if (job != (ctool_job_t *)0) {
      ctool_job_close(job);
    }
    free(cli.raw_ranges);
    free(cli.raw_edges);
    free(cli.inputs);
    return failed;
  }
  if (!cupiddis_split_path(cli.input, &native_root, &logical_name)) {
    (void)fprintf(stderr, "cupiddis: invalid input path\n");
    free(cli.raw_ranges);
    free(cli.raw_edges);
    free(cli.inputs);
    return 1;
  }
  limits.source_bytes = CUPIDDIS_HOST_SOURCE_BYTES;
  limits.arena_bytes = CUPIDDIS_HOST_ARENA_BYTES;
  status = ctool_host_adapter_init(&adapter, native_root);
  config = ctool_host_job_config(&adapter, limits);
  if (status == CTOOL_OK) {
    status = ctool_job_open(&config, &job);
  }
  if (status == CTOOL_OK) {
    status = ctool_path_root(ctool_job_arena(job), &root);
  }
  if (status == CTOOL_OK) {
    status = ctool_path_resolve(ctool_job_arena(job), &root,
                                ctool_string(logical_name), limits.path_bytes,
                                &input_path);
  }
  if (status == CTOOL_OK) {
    status = ctool_job_load_source(job, &input_path, &source);
  }
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupiddis: cannot load %s (%s)\n", cli.input,
                  ctool_status_name(status));
    goto done;
  }
  if (!cupiddis_range_map_matches(&cli, &source, cli.input)) {
    goto done;
  }
  if (cli.raw == CTOOL_FALSE && !cupiddis_is_elf(source.contents)) {
    (void)fprintf(stderr,
                  "cupiddis: input is not ELF32; raw input requires --raw\n");
    goto done;
  }
  cupiddis_make_request(&cli, &request);
  status = ctool_dis_inspect(job, &source, &request, &report);
  output.context = stdout;
  output.write = cupiddis_stdout_write;
  if (status == CTOOL_OK) {
    status = ctool_dis_render(job, &report,
                              cli.nm == CTOOL_TRUE ? CTOOL_DIS_TEXT_NM
                                                   : CTOOL_DIS_TEXT_CUPID,
                              output);
  }
  if (status == CTOOL_OK) {
    status = cupiddis_flush_output(job, source.path.text);
  }
  if (status != CTOOL_OK) {
    if (ctool_job_diagnostic_count(job) != 0u) {
      (void)ctool_job_render_diagnostics(job);
    } else {
      (void)fprintf(stderr, "cupiddis: inspection failed (%s)\n",
                    ctool_status_name(status));
    }
    goto done;
  }
  exit_code = 0;

done:
  if (job != (ctool_job_t *)0) {
    ctool_job_close(job);
  }
  free(native_root);
  free(cli.raw_ranges);
  free(cli.raw_edges);
  free(cli.inputs);
  return exit_code;
}
