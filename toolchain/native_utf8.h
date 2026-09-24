#ifndef CUPID_NATIVE_UTF8_H
#define CUPID_NATIVE_UTF8_H
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
DWORD cupid_native_utf8_attributes(const char *);
HANDLE cupid_native_utf8_open(const char *, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
BOOL cupid_native_utf8_delete(const char *);
BOOL cupid_native_utf8_move(const char *, const char *, DWORD);
BOOL cupid_native_utf8_process(const char *, char *, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, const char *, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
char *cupid_native_utf8_fullpath(char *, const char *, size_t);
char *cupid_native_utf8_getcwd(char *, int);
errno_t cupid_native_utf8_fopen_s(FILE **, const char *, const char *);
FILE *cupid_native_utf8_fopen(const char *, const char *);
char *cupid_native_utf8_getenv(const char *);
char *cupid_native_utf8_from_wide(const wchar_t *, size_t);
void cupid_native_utf8_release_environment(void);
int cupid_tool_utf8_main(int argc, char **argv);
#ifndef CUPID_NATIVE_UTF8_IMPLEMENTATION
#define GetFileAttributesA cupid_native_utf8_attributes
#define CreateFileA cupid_native_utf8_open
#define DeleteFileA cupid_native_utf8_delete
#define MoveFileExA cupid_native_utf8_move
#define CreateProcessA cupid_native_utf8_process
#define _fullpath cupid_native_utf8_fullpath
#define _getcwd cupid_native_utf8_getcwd
#define fopen_s cupid_native_utf8_fopen_s
#define fopen cupid_native_utf8_fopen
#define getenv cupid_native_utf8_getenv
#endif
#endif
