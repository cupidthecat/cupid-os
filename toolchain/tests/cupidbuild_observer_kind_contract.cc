#include "cupidbuild_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c) do { if (!(c)) { fprintf(stderr, "check failed at %d\n", __LINE__); cupidbuild_host_observer_close(observer); return 1; } } while (0)

static int unhex(const char *input, char *output, size_t capacity) {
  size_t count = 0u;
  while (*input != 0) {
    unsigned int value = 0u;
    unsigned int index;
    for (index = 0u; index < 2u; index++) {
      unsigned char digit = (unsigned char)*input++;
      if (digit >= '0' && digit <= '9') value = value * 16u + digit - '0';
      else if (digit >= 'a' && digit <= 'f') value = value * 16u + digit - 'a' + 10u;
      else return 0;
    }
    if (value == 0u || count + 1u >= capacity) return 0;
    output[count++] = (char)value;
  }
  output[count] = 0;
  return 1;
}

static int capture_tree(cupidbuild_host_observer_t *observer, int deep) {
  cupidbuild_host_entry_kind_t kind;
  unsigned char *bytes;
  uint64_t size;
  char names[512][16];
  const char *members[512];
  char path[128];
  unsigned int i;
  unsigned int count = deep ? 505u : 512u;
  const char *directories[] = {"d0", "d0/d1", "d0/d1/d2", "d0/d1/d2/d3",
    "d0/d1/d2/d3/d4", "d0/d1/d2/d3/d4/d5", "d0/d1/d2/d3/d4/d5/d6"};
  for (i = 0u; i < count; i++) {
    snprintf(names[i], sizeof(names[i]), "f%03u.bin", i); members[i] = names[i];
  }
  if (deep) {
    const char *child[1];
    child[0] = "d0";
    if (!cupidbuild_host_observer_directory(observer, "", child, 1u)) return 0;
    for (i = 0u; i < 7u; i++) {
      char component[8];
      if (!cupidbuild_host_observer_kind(observer, directories[i], &kind) ||
          kind != CUPIDBUILD_ENTRY_DIRECTORY) return 0;
      if (i == 6u) {
        if (!cupidbuild_host_observer_directory(observer, directories[i], members, count)) return 0;
      } else {
        snprintf(component, sizeof(component), "d%u", i + 1u); child[0] = component;
        if (!cupidbuild_host_observer_directory(observer, directories[i], child, 1u)) return 0;
      }
    }
  } else if (!cupidbuild_host_observer_directory(observer, "", members, count)) return 0;
  for (i = 0u; i < count; i++) {
    snprintf(path, sizeof(path), "%s%s", deep ? "d0/d1/d2/d3/d4/d5/d6/" : "", names[i]);
    if (!cupidbuild_host_observer_kind(observer, path, &kind) || kind != CUPIDBUILD_ENTRY_FILE) return 0;
    if (!cupidbuild_host_observer_file(observer, path, 1u, &bytes, &size)) return 0;
    if (size != 1u || bytes[0] != 'x') { free(bytes); return 0; } free(bytes);
  }
  return cupidbuild_host_observer_require_unchanged(observer);
}

int main(int argc, char **argv) {
  cupidbuild_host_observer_t *observer = NULL;
  cupidbuild_host_entry_kind_t kind = CUPIDBUILD_ENTRY_DIRECTORY;
  unsigned char *bytes = NULL;
  uint64_t size = 99u;
  unsigned char resume;
  char root[8192];
  char logical[8192];
  const char *mode;
  if (argc != 4) return 2;
  mode = argv[2];
  REQUIRE(unhex(argv[1], root, sizeof(root)) && unhex(argv[3], logical, sizeof(logical)));
  REQUIRE(cupidbuild_host_observer_open(root, &observer));
  if (strcmp(mode, "tree") == 0) {
    REQUIRE(capture_tree(observer, strcmp(logical, "deep") == 0));
  } else if (strcmp(mode, "file") == 0 || strcmp(mode, "directory") == 0) {
    REQUIRE(cupidbuild_host_observer_kind(observer, logical, &kind));
    REQUIRE(kind == (strcmp(mode, "file") == 0 ? CUPIDBUILD_ENTRY_FILE : CUPIDBUILD_ENTRY_DIRECTORY));
    REQUIRE(cupidbuild_host_observer_require_unchanged(observer));
  } else if (strcmp(mode, "wait") == 0 || strcmp(mode, "payload") == 0 ||
             strcmp(mode, "members") == 0 || strcmp(mode, "repeat") == 0) {
    REQUIRE(cupidbuild_host_observer_kind(observer, logical, &kind));
    REQUIRE(kind != CUPIDBUILD_ENTRY_NONE);
    if (strcmp(mode, "payload") == 0) {
      REQUIRE(kind == CUPIDBUILD_ENTRY_FILE);
      REQUIRE(cupidbuild_host_observer_file(observer, logical, 64u, &bytes, &size));
      REQUIRE(size == 3u && memcmp(bytes, "one", 3u) == 0); free(bytes);
    }
    if (strcmp(mode, "members") == 0) {
      REQUIRE(kind == CUPIDBUILD_ENTRY_DIRECTORY);
      REQUIRE(cupidbuild_host_observer_directory(observer, logical, NULL, 0u));
    }
    REQUIRE(cupidbuild_host_observer_require_unchanged(observer));
    printf("ready\n"); fflush(stdout);
    REQUIRE(fread(&resume, 1u, 1u, stdin) == 1u);
    if (strcmp(mode, "repeat") == 0) {
      kind = CUPIDBUILD_ENTRY_DIRECTORY;
      if (resume == 'p') {
        REQUIRE(cupidbuild_host_observer_kind(observer, logical, &kind));
        REQUIRE(kind == CUPIDBUILD_ENTRY_FILE);
      } else {
        REQUIRE(!cupidbuild_host_observer_kind(observer, logical, &kind));
        REQUIRE(kind == CUPIDBUILD_ENTRY_NONE);
      }
    }
    REQUIRE((cupidbuild_host_observer_require_unchanged(observer) != 0) == (resume == 'p'));
  } else if (strcmp(mode, "null-observer") == 0) {
    REQUIRE(!cupidbuild_host_observer_kind(NULL, logical, &kind));
    REQUIRE(kind == CUPIDBUILD_ENTRY_NONE);
    REQUIRE(cupidbuild_host_observer_require_unchanged(observer));
  } else {
    if (strcmp(mode, "null-path") == 0)
      REQUIRE(!cupidbuild_host_observer_kind(observer, NULL, &kind));
    else if (strcmp(mode, "null-out") == 0)
      REQUIRE(!cupidbuild_host_observer_kind(observer, logical, NULL));
    else if (strcmp(mode, "bad-utf8") == 0)
      REQUIRE(!cupidbuild_host_observer_kind(observer, "\xc0\xaf", &kind));
    else if (strcmp(mode, "file-strict") == 0) {
      REQUIRE(!cupidbuild_host_observer_file(observer, logical, 0u, NULL, &size));
      REQUIRE(size == 0u); kind = CUPIDBUILD_ENTRY_NONE;
    } else if (strcmp(mode, "directory-strict") == 0) {
      REQUIRE(!cupidbuild_host_observer_directory(observer, logical, NULL, 0u));
      kind = CUPIDBUILD_ENTRY_NONE;
    } else if (strcmp(mode, "reject") == 0)
      REQUIRE(!cupidbuild_host_observer_kind(observer, logical, &kind));
    else { cupidbuild_host_observer_close(observer); return 2; }
    if (strcmp(mode, "null-out") != 0) REQUIRE(kind == CUPIDBUILD_ENTRY_NONE);
    REQUIRE(cupidbuild_host_observer_error(observer)[0] != 0);
    REQUIRE(!cupidbuild_host_observer_require_unchanged(observer));
    kind = CUPIDBUILD_ENTRY_DIRECTORY;
    REQUIRE(!cupidbuild_host_observer_kind(observer, "nested/file", &kind));
    REQUIRE(kind == CUPIDBUILD_ENTRY_NONE);
    REQUIRE(!cupidbuild_host_observer_file(observer, "nested/file", 0u, NULL, &size));
    REQUIRE(size == 0u);
  }
  return cupidbuild_host_observer_close(observer) ? 0 : 3;
}
