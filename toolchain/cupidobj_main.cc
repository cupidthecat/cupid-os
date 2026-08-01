#include "ctool.h"
#include "ctool_host.h"
#include "cupidobj.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <direct.h>
#else
#include <unistd.h>
#endif

#define CUPIDOBJ_HOST_SOURCE_BYTES 67108864u
#define CUPIDOBJ_HOST_OUTPUT_BYTES 67108864u
#define CUPIDOBJ_HOST_ARENA_BYTES 268435456u

typedef struct {
  ctool_obj_operation_t operation;
  const char *input;
  const char *output;
  const char *identity;
  const char *stem;
  const char *section;
  const char *const *bin_paths;
  ctool_u32 bin_count;
  const char *const *header_paths;
  ctool_u32 header_count;
  const char *const *browser_paths;
  ctool_u32 browser_count;
  const char *const *ctxt_paths;
  ctool_u32 ctxt_count;
  const char *const *doc_asset_paths;
  ctool_u32 doc_asset_count;
  const char *const *home_asset_paths;
  ctool_u32 home_asset_count;
  const char *const *demo_paths;
  ctool_u32 demo_count;
  ctool_obj_install_source_kind_t install_kind;
  ctool_bool readonly;
} cupidobj_cli_t;

static void cupidobj_usage(FILE *stream) {
  (void)fprintf(
      stream,
      "usage: cupidobj wrap INPUT -o OUTPUT [--identity NAME | --stem NAME] "
      "[--section NAME] [--readonly]\n"
      "       cupidobj wrap-text INPUT -o OUTPUT "
      "[--identity NAME | --stem NAME] [--section NAME] [--readonly]\n"
      "       cupidobj flat INPUT -o OUTPUT\n"
      "       cupidobj install-source bin [--bin PATH...] "
      "[--headers PATH...] [--browser PATH...] -o OUTPUT\n"
      "       cupidobj install-source docs [--ctxt PATH...] "
      "[--doc-assets PATH...] [--home-assets PATH...] -o OUTPUT\n"
      "       cupidobj install-source demos --demos PATH... -o OUTPUT\n");
}

static ctool_bool cupidobj_is_wrap(ctool_obj_operation_t operation) {
  return operation == CTOOL_OBJ_WRAP_BINARY ||
                 operation == CTOOL_OBJ_WRAP_TEXT
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static int cupidobj_take_value(int argc, char **argv, int *index,
                               const char *argument, const char *option,
                               const char **value_out) {
  size_t option_size = strlen(option);
  if (strcmp(argument, option) == 0) {
    if (*index + 1 >= argc) {
      return -1;
    }
    *index = *index + 1;
    *value_out = argv[*index];
    return 1;
  }
  if (strncmp(argument, option, option_size) == 0 &&
      argument[option_size] == '=') {
    *value_out = argument + option_size + 1u;
    return 1;
  }
  return 0;
}

static int cupidobj_parse_cli(int argc, char **argv, cupidobj_cli_t *cli) {
  int index;
  ctool_bool have_readonly = CTOOL_FALSE;
  (void)memset(cli, 0, sizeof(*cli));
  if (argc == 2 &&
      (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
    return -1;
  }
  if (argc < 2) {
    return 0;
  }
  if (strcmp(argv[1], "wrap") == 0) {
    cli->operation = CTOOL_OBJ_WRAP_BINARY;
  } else if (strcmp(argv[1], "wrap-text") == 0) {
    cli->operation = CTOOL_OBJ_WRAP_TEXT;
  } else if (strcmp(argv[1], "flat") == 0) {
    cli->operation = CTOOL_OBJ_EXTRACT_FLAT;
  } else if (strcmp(argv[1], "install-source") == 0) {
    if (argc < 3) {
      return 0;
    }
    cli->operation = CTOOL_OBJ_GENERATE_INSTALL_SOURCE;
    if (strcmp(argv[2], "demos") == 0) {
      cli->install_kind = CTOOL_OBJ_INSTALL_DEMOS;
    } else if (strcmp(argv[2], "bin") == 0) {
      cli->install_kind = CTOOL_OBJ_INSTALL_BIN;
    } else if (strcmp(argv[2], "docs") == 0) {
      cli->install_kind = CTOOL_OBJ_INSTALL_DOCS;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
  for (index = cli->operation == CTOOL_OBJ_GENERATE_INSTALL_SOURCE ? 3 : 2;
       index < argc; index++) {
    const char *argument = argv[index];
    const char *value = (const char *)0;
    int taken;
    if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
      return -1;
    }
    if (strcmp(argument, "--demos") == 0) {
      int first;
      if (cli->operation != CTOOL_OBJ_GENERATE_INSTALL_SOURCE ||
          cli->install_kind != CTOOL_OBJ_INSTALL_DEMOS ||
          cli->demo_paths != (const char *const *)0) {
        return 0;
      }
      first = index + 1;
      index = first;
      while (index < argc && argv[index][0] != '-') {
        index++;
      }
      if (index == first) {
        return 0;
      }
      cli->demo_paths = (const char *const *)&argv[first];
      cli->demo_count = (ctool_u32)(index - first);
      index--;
      continue;
    }
    if (strcmp(argument, "--bin") == 0 ||
        strcmp(argument, "--headers") == 0 ||
        strcmp(argument, "--browser") == 0) {
      const char *const **paths_out;
      ctool_u32 *count_out;
      int first;
      if (cli->operation != CTOOL_OBJ_GENERATE_INSTALL_SOURCE ||
          cli->install_kind != CTOOL_OBJ_INSTALL_BIN) {
        return 0;
      }
      if (strcmp(argument, "--bin") == 0) {
        paths_out = &cli->bin_paths;
        count_out = &cli->bin_count;
      } else if (strcmp(argument, "--headers") == 0) {
        paths_out = &cli->header_paths;
        count_out = &cli->header_count;
      } else {
        paths_out = &cli->browser_paths;
        count_out = &cli->browser_count;
      }
      if (*paths_out != (const char *const *)0) {
        return 0;
      }
      first = index + 1;
      index = first;
      while (index < argc && argv[index][0] != '-') {
        index++;
      }
      if (index == first) {
        return 0;
      }
      *paths_out = (const char *const *)&argv[first];
      *count_out = (ctool_u32)(index - first);
      index--;
      continue;
    }
    if (strcmp(argument, "--ctxt") == 0 ||
        strcmp(argument, "--doc-assets") == 0 ||
        strcmp(argument, "--home-assets") == 0) {
      const char *const **paths_out;
      ctool_u32 *count_out;
      int first;
      if (cli->operation != CTOOL_OBJ_GENERATE_INSTALL_SOURCE ||
          cli->install_kind != CTOOL_OBJ_INSTALL_DOCS) {
        return 0;
      }
      if (strcmp(argument, "--ctxt") == 0) {
        paths_out = &cli->ctxt_paths;
        count_out = &cli->ctxt_count;
      } else if (strcmp(argument, "--doc-assets") == 0) {
        paths_out = &cli->doc_asset_paths;
        count_out = &cli->doc_asset_count;
      } else {
        paths_out = &cli->home_asset_paths;
        count_out = &cli->home_asset_count;
      }
      if (*paths_out != (const char *const *)0) {
        return 0;
      }
      first = index + 1;
      index = first;
      while (index < argc && argv[index][0] != '-') {
        index++;
      }
      if (index == first) {
        return 0;
      }
      *paths_out = (const char *const *)&argv[first];
      *count_out = (ctool_u32)(index - first);
      index--;
      continue;
    }
    taken = cupidobj_take_value(argc, argv, &index, argument, "-o", &value);
    if (taken != 0) {
      if (taken < 0 || cli->output != (const char *)0 || value[0] == '\0') {
        return 0;
      }
      cli->output = value;
      continue;
    }
    taken = cupidobj_take_value(argc, argv, &index, argument, "--identity",
                                &value);
    if (taken != 0) {
      if (taken < 0 || cli->identity != (const char *)0 ||
          value[0] == '\0') {
        return 0;
      }
      cli->identity = value;
      continue;
    }
    taken = cupidobj_take_value(argc, argv, &index, argument, "--stem",
                                &value);
    if (taken != 0) {
      if (taken < 0 || cli->stem != (const char *)0 || value[0] == '\0') {
        return 0;
      }
      cli->stem = value;
      continue;
    }
    taken = cupidobj_take_value(argc, argv, &index, argument, "--section",
                                &value);
    if (taken != 0) {
      if (taken < 0 || cli->section != (const char *)0 || value[0] == '\0') {
        return 0;
      }
      cli->section = value;
      continue;
    }
    if (strcmp(argument, "--readonly") == 0) {
      if (have_readonly == CTOOL_TRUE) {
        return 0;
      }
      cli->readonly = CTOOL_TRUE;
      have_readonly = CTOOL_TRUE;
      continue;
    }
    if (cli->operation == CTOOL_OBJ_GENERATE_INSTALL_SOURCE ||
        argument[0] == '-' || cli->input != (const char *)0) {
      return 0;
    }
    cli->input = argument;
  }
  if (cli->operation == CTOOL_OBJ_GENERATE_INSTALL_SOURCE) {
    if (cli->install_kind == CTOOL_OBJ_INSTALL_DEMOS) {
      if (cli->demo_paths == (const char *const *)0 ||
          cli->demo_count == 0u) {
        return 0;
      }
      cli->input = cli->demo_paths[0];
    } else if (cli->install_kind == CTOOL_OBJ_INSTALL_BIN) {
      if (cli->bin_count + cli->header_count + cli->browser_count == 0u) {
        return 0;
      }
      if (cli->bin_count != 0u) {
        cli->input = cli->bin_paths[0];
      } else if (cli->header_count != 0u) {
        cli->input = cli->header_paths[0];
      } else {
        cli->input = cli->browser_paths[0];
      }
    } else {
      if (cli->ctxt_count + cli->doc_asset_count + cli->home_asset_count ==
          0u) {
        return 0;
      }
      if (cli->ctxt_count != 0u) {
        cli->input = cli->ctxt_paths[0];
      } else if (cli->doc_asset_count != 0u) {
        cli->input = cli->doc_asset_paths[0];
      } else {
        cli->input = cli->home_asset_paths[0];
      }
    }
  }
  if (cli->input == (const char *)0 || cli->output == (const char *)0) {
    return 0;
  }
  if (cli->operation == CTOOL_OBJ_GENERATE_INSTALL_SOURCE) {
    if (cli->identity != (const char *)0 || cli->stem != (const char *)0 ||
        cli->section != (const char *)0 || cli->readonly == CTOOL_TRUE) {
      return 0;
    }
  } else if (cli->operation == CTOOL_OBJ_EXTRACT_FLAT) {
    if (cli->identity != (const char *)0 || cli->stem != (const char *)0 ||
        cli->section != (const char *)0 || cli->readonly == CTOOL_TRUE) {
      return 0;
    }
  } else if (cli->identity != (const char *)0 &&
             cli->stem != (const char *)0) {
    return 0;
  } else if (cli->section == (const char *)0) {
    cli->section = ".data";
  }
  return 1;
}

static ctool_bool cupidobj_native_separator(char character) {
  return character == '/' || character == '\\' ? CTOOL_TRUE : CTOOL_FALSE;
}

static char *cupidobj_working_directory(void) {
  size_t capacity = 256u;
  while (capacity <= 1048576u) {
    char *directory = (char *)malloc(capacity);
    if (directory == (char *)0) {
      return (char *)0;
    }
#if defined(_WIN32)
    if (_getcwd(directory, (int)capacity) != (char *)0) {
      return directory;
    }
#else
    if (getcwd(directory, capacity) != (char *)0) {
      return directory;
    }
#endif
    if (errno != ERANGE) {
      free(directory);
      return (char *)0;
    }
    free(directory);
    capacity *= 2u;
  }
  return (char *)0;
}

static char *cupidobj_logical_path(const char *path) {
  char *directory = (char *)0;
  char *native = (char *)0;
  char *logical;
  size_t path_size;
  size_t directory_size = 0u;
  size_t native_size;
  size_t logical_start;
  size_t index;
  ctool_bool absolute = CTOOL_FALSE;
  if (path == (const char *)0 || path[0] == '\0') {
    return (char *)0;
  }
  path_size = strlen(path);
#if defined(_WIN32)
  if (cupidobj_native_separator(path[0]) == CTOOL_TRUE &&
      cupidobj_native_separator(path[1]) == CTOOL_TRUE) {
    return (char *)0;
  }
  if (path_size >= 3u && path[1] == ':' &&
      cupidobj_native_separator(path[2]) == CTOOL_TRUE) {
    absolute = CTOOL_TRUE;
  } else if (path_size >= 2u && path[1] == ':') {
    return (char *)0;
  }
#else
  if (path[0] == '/') {
    absolute = CTOOL_TRUE;
  }
#endif
  if (absolute == CTOOL_FALSE) {
    directory = cupidobj_working_directory();
    if (directory == (char *)0) {
      return (char *)0;
    }
    directory_size = strlen(directory);
#if defined(_WIN32)
    if (cupidobj_native_separator(path[0]) == CTOOL_TRUE) {
      if (directory_size < 2u || directory[1] != ':') {
        free(directory);
        return (char *)0;
      }
      directory_size = 2u;
    }
#endif
  }
  if (path_size > (size_t)-1 - directory_size - 2u) {
    free(directory);
    return (char *)0;
  }
  native_size = absolute == CTOOL_TRUE
                    ? path_size
                    : directory_size + 1u + path_size;
  native = (char *)malloc(native_size + 1u);
  if (native == (char *)0) {
    free(directory);
    return (char *)0;
  }
  if (absolute == CTOOL_TRUE) {
    (void)memcpy(native, path, path_size + 1u);
  } else {
    (void)memcpy(native, directory, directory_size);
    native[directory_size] = '/';
    (void)memcpy(native + directory_size + 1u, path, path_size + 1u);
  }
  free(directory);
#if defined(_WIN32)
  logical_start = 1u;
#else
  logical_start = 0u;
#endif
  logical = (char *)malloc(native_size + logical_start + 1u);
  if (logical == (char *)0) {
    free(native);
    return (char *)0;
  }
  if (logical_start != 0u) {
    logical[0] = '/';
  }
  for (index = 0u; index < native_size; index++) {
    logical[logical_start + index] =
        cupidobj_native_separator(native[index]) == CTOOL_TRUE
            ? '/'
            : native[index];
  }
  logical[logical_start + native_size] = '\0';
  free(native);
  return logical;
}

static ctool_bool cupidobj_ascii_alnum(unsigned char character) {
  return ((character >= (unsigned char)'a' &&
           character <= (unsigned char)'z') ||
          (character >= (unsigned char)'A' &&
           character <= (unsigned char)'Z') ||
          (character >= (unsigned char)'0' &&
           character <= (unsigned char)'9'))
             ? CTOOL_TRUE
             : CTOOL_FALSE;
}

static char *cupidobj_stem(const cupidobj_cli_t *cli) {
  static const char prefix[] = "_binary_";
  const char *identity;
  size_t identity_size;
  size_t prefix_size = sizeof(prefix) - 1u;
  size_t index;
  char *stem;
  if (cli->stem != (const char *)0) {
    size_t stem_size = strlen(cli->stem);
    stem = (char *)malloc(stem_size + 1u);
    if (stem != (char *)0) {
      (void)memcpy(stem, cli->stem, stem_size + 1u);
    }
    return stem;
  }
  identity = cli->identity != (const char *)0 ? cli->identity : cli->input;
  identity_size = strlen(identity);
  if (identity_size > (size_t)-1 - prefix_size - 1u) {
    return (char *)0;
  }
  stem = (char *)malloc(prefix_size + identity_size + 1u);
  if (stem == (char *)0) {
    return (char *)0;
  }
  (void)memcpy(stem, prefix, prefix_size);
  for (index = 0u; index < identity_size; index++) {
    unsigned char character = (unsigned char)identity[index];
    stem[prefix_size + index] =
        cupidobj_ascii_alnum(character) == CTOOL_TRUE ? (char)character : '_';
  }
  stem[prefix_size + identity_size] = '\0';
  return stem;
}

static char *cupidobj_symbol(const char *stem, const char *suffix) {
  size_t stem_size = strlen(stem);
  size_t suffix_size = strlen(suffix);
  char *symbol;
  if (stem_size > (size_t)-1 - suffix_size - 1u) {
    return (char *)0;
  }
  symbol = (char *)malloc(stem_size + suffix_size + 1u);
  if (symbol == (char *)0) {
    return (char *)0;
  }
  (void)memcpy(symbol, stem, stem_size);
  (void)memcpy(symbol + stem_size, suffix, suffix_size + 1u);
  return symbol;
}

typedef struct {
  ctool_obj_request_t request;
  ctool_obj_result_t result;
} cupidobj_invocation_context_t;

static ctool_status_t cupidobj_invoke_body(ctool_invocation_t *invocation,
                                           void *user_data) {
  cupidobj_invocation_context_t *context =
      (cupidobj_invocation_context_t *)user_data;
  if (invocation == (ctool_invocation_t *)0 ||
      context == (cupidobj_invocation_context_t *)0) {
    return CTOOL_ERR_INVALID_ARGUMENT;
  }
  context->request.input = invocation->input;
  return ctool_obj_transform(invocation->job, &context->request,
                             invocation->output, &context->result);
}

int main(int argc, char **argv) {
  cupidobj_cli_t cli;
  ctool_host_adapter_t adapter;
  ctool_limits_t limits = ctool_default_limits();
  ctool_job_config_t config;
  ctool_invocation_request_t invocation_request;
  ctool_invocation_result_t invocation_result;
  cupidobj_invocation_context_t context;
  char *logical_input = (char *)0;
  char *logical_output = (char *)0;
  char *stem = (char *)0;
  char *start_symbol = (char *)0;
  char *end_symbol = (char *)0;
  char *size_symbol = (char *)0;
  ctool_string_t *demo_paths = (ctool_string_t *)0;
  ctool_string_t *bin_paths = (ctool_string_t *)0;
  ctool_string_t *header_paths = (ctool_string_t *)0;
  ctool_string_t *browser_paths = (ctool_string_t *)0;
  ctool_string_t *ctxt_paths = (ctool_string_t *)0;
  ctool_string_t *doc_asset_paths = (ctool_string_t *)0;
  ctool_string_t *home_asset_paths = (ctool_string_t *)0;
  ctool_status_t status;
  int parsed = cupidobj_parse_cli(argc, argv, &cli);
  int exit_code = 1;
  if (parsed < 0) {
    cupidobj_usage(stdout);
    return 0;
  }
  if (parsed == 0) {
    cupidobj_usage(stderr);
    return 2;
  }
  if (cupidobj_is_wrap(cli.operation) == CTOOL_TRUE) {
    stem = cupidobj_stem(&cli);
    if (stem != (char *)0) {
      start_symbol = cupidobj_symbol(stem, "_start");
      end_symbol = cupidobj_symbol(stem, "_end");
      size_symbol = cupidobj_symbol(stem, "_size");
    }
    if (stem == (char *)0 || start_symbol == (char *)0 ||
        end_symbol == (char *)0 || size_symbol == (char *)0) {
      (void)fprintf(stderr, "cupidobj: symbol allocation failed\n");
      goto done;
    }
  }
  logical_input = cupidobj_logical_path(cli.input);
  logical_output = cupidobj_logical_path(cli.output);
  if (logical_input == (char *)0 || logical_output == (char *)0) {
    (void)fprintf(stderr, "cupidobj: invalid input or output path\n");
    goto done;
  }
  limits.source_bytes = CUPIDOBJ_HOST_SOURCE_BYTES;
  limits.output_bytes = CUPIDOBJ_HOST_OUTPUT_BYTES;
  limits.arena_bytes = CUPIDOBJ_HOST_ARENA_BYTES;
#if defined(_WIN32)
  status = ctool_host_adapter_init(&adapter, "");
#else
  status = ctool_host_adapter_init(&adapter, "/");
#endif
  config = ctool_host_job_config(&adapter, limits);
  if (status != CTOOL_OK) {
    (void)fprintf(stderr, "cupidobj: job setup failed (%s)\n",
                  ctool_status_name(status));
    goto done;
  }
  (void)memset(&context, 0, sizeof(context));
  context.request.operation = cli.operation;
  if (cupidobj_is_wrap(cli.operation) == CTOOL_TRUE) {
    context.request.as.wrap_binary.section_name = ctool_string(cli.section);
    context.request.as.wrap_binary.section_flags = CTOOL_ELF32_SHF_ALLOC;
    if (cli.readonly == CTOOL_FALSE) {
      context.request.as.wrap_binary.section_flags |= CTOOL_ELF32_SHF_WRITE;
    }
    context.request.as.wrap_binary.section_alignment = 1u;
    context.request.as.wrap_binary.start_symbol = ctool_string(start_symbol);
    context.request.as.wrap_binary.end_symbol = ctool_string(end_symbol);
    context.request.as.wrap_binary.size_symbol = ctool_string(size_symbol);
  } else if (cli.operation == CTOOL_OBJ_GENERATE_INSTALL_SOURCE) {
    ctool_u32 index;
    if (cli.demo_count != 0u) {
      demo_paths = (ctool_string_t *)malloc(
          (size_t)cli.demo_count * sizeof(*demo_paths));
    }
    if (cli.bin_count != 0u) {
      bin_paths = (ctool_string_t *)malloc(
          (size_t)cli.bin_count * sizeof(*bin_paths));
    }
    if (cli.header_count != 0u) {
      header_paths = (ctool_string_t *)malloc(
          (size_t)cli.header_count * sizeof(*header_paths));
    }
    if (cli.browser_count != 0u) {
      browser_paths = (ctool_string_t *)malloc(
          (size_t)cli.browser_count * sizeof(*browser_paths));
    }
    if (cli.ctxt_count != 0u) {
      ctxt_paths = (ctool_string_t *)malloc(
          (size_t)cli.ctxt_count * sizeof(*ctxt_paths));
    }
    if (cli.doc_asset_count != 0u) {
      doc_asset_paths = (ctool_string_t *)malloc(
          (size_t)cli.doc_asset_count * sizeof(*doc_asset_paths));
    }
    if (cli.home_asset_count != 0u) {
      home_asset_paths = (ctool_string_t *)malloc(
          (size_t)cli.home_asset_count * sizeof(*home_asset_paths));
    }
    if ((cli.demo_count != 0u && demo_paths == (ctool_string_t *)0) ||
        (cli.bin_count != 0u && bin_paths == (ctool_string_t *)0) ||
        (cli.header_count != 0u && header_paths == (ctool_string_t *)0) ||
        (cli.browser_count != 0u && browser_paths == (ctool_string_t *)0) ||
        (cli.ctxt_count != 0u && ctxt_paths == (ctool_string_t *)0) ||
        (cli.doc_asset_count != 0u &&
         doc_asset_paths == (ctool_string_t *)0) ||
        (cli.home_asset_count != 0u &&
         home_asset_paths == (ctool_string_t *)0)) {
      (void)fprintf(stderr, "cupidobj: installation list allocation failed\n");
      goto done;
    }
    for (index = 0u; index < cli.demo_count; index++) {
      demo_paths[index] = ctool_string(cli.demo_paths[index]);
    }
    for (index = 0u; index < cli.bin_count; index++) {
      bin_paths[index] = ctool_string(cli.bin_paths[index]);
    }
    for (index = 0u; index < cli.header_count; index++) {
      header_paths[index] = ctool_string(cli.header_paths[index]);
    }
    for (index = 0u; index < cli.browser_count; index++) {
      browser_paths[index] = ctool_string(cli.browser_paths[index]);
    }
    for (index = 0u; index < cli.ctxt_count; index++) {
      ctxt_paths[index] = ctool_string(cli.ctxt_paths[index]);
    }
    for (index = 0u; index < cli.doc_asset_count; index++) {
      doc_asset_paths[index] = ctool_string(cli.doc_asset_paths[index]);
    }
    for (index = 0u; index < cli.home_asset_count; index++) {
      home_asset_paths[index] = ctool_string(cli.home_asset_paths[index]);
    }
    context.request.as.install_source.kind = cli.install_kind;
    context.request.as.install_source.bin_paths = bin_paths;
    context.request.as.install_source.bin_count = cli.bin_count;
    context.request.as.install_source.header_paths = header_paths;
    context.request.as.install_source.header_count = cli.header_count;
    context.request.as.install_source.browser_paths = browser_paths;
    context.request.as.install_source.browser_count = cli.browser_count;
    context.request.as.install_source.ctxt_paths = ctxt_paths;
    context.request.as.install_source.ctxt_count = cli.ctxt_count;
    context.request.as.install_source.doc_asset_paths = doc_asset_paths;
    context.request.as.install_source.doc_asset_count = cli.doc_asset_count;
    context.request.as.install_source.home_asset_paths = home_asset_paths;
    context.request.as.install_source.home_asset_count = cli.home_asset_count;
    context.request.as.install_source.demo_paths = demo_paths;
    context.request.as.install_source.demo_count = cli.demo_count;
  }
  invocation_request.input_path = ctool_string(logical_input);
  invocation_request.output_path = ctool_string(logical_output);
  (void)memset(&invocation_result, 0, sizeof(invocation_result));
  status = ctool_invoke(&config, &invocation_request, cupidobj_invoke_body,
                        &context, &invocation_result);
  if (status != CTOOL_OK) {
    if (invocation_result.diagnostic_count != 0u) {
      /* ctool_invoke has already rendered the ordered diagnostics. */
    } else if (invocation_result.body_status == CTOOL_ERR_NOT_FOUND) {
      (void)fprintf(stderr, "cupidobj: cannot load %s (%s)\n", cli.input,
                    ctool_status_name(invocation_result.body_status));
    } else if (invocation_result.body_status == CTOOL_OK &&
               invocation_result.output_committed == CTOOL_FALSE) {
      (void)fprintf(stderr, "cupidobj: cannot write %s (%s)\n", cli.output,
                    ctool_status_name(status));
    } else {
      (void)fprintf(stderr, "cupidobj: transformation failed (%s)\n",
                    ctool_status_name(status));
    }
    goto done;
  }
  exit_code = 0;

done:
  free(home_asset_paths);
  free(doc_asset_paths);
  free(ctxt_paths);
  free(browser_paths);
  free(header_paths);
  free(bin_paths);
  free(demo_paths);
  free(logical_output);
  free(logical_input);
  free(size_symbol);
  free(end_symbol);
  free(start_symbol);
  free(stem);
  return exit_code;
}
