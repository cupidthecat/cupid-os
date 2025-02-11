// filesystem/path.h
#ifndef PATH_H
#define PATH_H

#include "fs.h"

int resolve_relative_path(int base, const char* path);
int resolve_path(const char* path);

#endif
