/* Test-only launcher for the private compiler's fixed-address i386 ELF oracle. */
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned long U32;
typedef void *HANDLE;
#define API __declspec(dllimport) __stdcall
API U16 *GetCommandLineW(void);
API HANDLE CreateFileW(const U16 *, U32, U32, void *, U32, U32, HANDLE);
API U32 GetFileSize(HANDLE, U32 *);
API int ReadFile(HANDLE, void *, U32, U32 *, void *);
API int CloseHandle(HANDLE);
API void *VirtualAlloc(void *, U32, U32, U32);
API HANDLE GetStdHandle(U32);
API int WriteFile(HANDLE, const void *, U32, U32 *, void *);
API int FlushInstructionCache(HANDLE, const void *, U32);
API HANDLE GetCurrentProcess(void);
API void ExitProcess(U32);

typedef struct {
  U8 ident[16]; U16 type, machine; U32 version, entry, phoff, shoff, flags;
  U16 ehsize, phsize, phnum, shsize, shnum, shstr;
} ElfHeader;
typedef struct { U32 type, offset, address, physical, filesz, memsz, flags, align; } ElfProgram;
_Static_assert(sizeof(ElfHeader) == 52, "ELF32 header size");
_Static_assert(sizeof(ElfProgram) == 32, "ELF32 program size");
_Static_assert(sizeof(void *) == 4, "i386 pointer size");
static U16 arguments[3][32768];
extern int native_execute(U32, U32);

static int parse_arguments(void) {
  U16 *source = GetCommandLineW();
  U32 argument;
  for (argument = 0; argument < 3; ++argument) {
    U32 size = 0;
    int quoted;
    while (*source == ' ' || *source == '\t') ++source;
    if (!*source) return 0;
    quoted = *source == '"';
    if (quoted) ++source;
    while (*source && (quoted ? *source != '"' : *source != ' ' && *source != '\t')) {
      if (size == 32767) return 0;
      arguments[argument][size++] = *source++;
    }
    arguments[argument][size] = 0;
    if (quoted) { if (*source != '"') return 0; ++source; }
  }
  while (*source == ' ' || *source == '\t') ++source;
  return *source == 0;
}

static void native_print(const char *text) {
  U32 size = 0, written = 0;
  while (text[size]) ++size;
  if (!WriteFile(GetStdHandle((U32)-12), text, size, &written, (void *)0) || written != size)
    ExitProcess(74);
}

void start(void) {
  U32 index, size, file_high = 0, received, entry = 0, low = 0xffffffffUL, high = 0;
  U32 bytes_offset, programs_size, base, span;
  int entry_loaded = 0, print_loaded = 0;
  HANDLE file;
  U8 *bytes, *region;
  ElfHeader *header;
  if (!parse_arguments()) ExitProcess(64);
  if (arguments[2][0] != '0' || arguments[2][1] != 'x') ExitProcess(64);
  for (index = 2; arguments[2][index]; ++index) {
    U32 digit = arguments[2][index];
    if (digit >= '0' && digit <= '9') digit -= '0';
    else if (digit >= 'a' && digit <= 'f') digit = digit - 'a' + 10;
    else ExitProcess(64);
    if (index > 9) ExitProcess(64);
    entry = (entry << 4) | digit;
  }
  if (index == 2) ExitProcess(64);
  file = CreateFileW(arguments[1], 0x80000000UL, 1, (void *)0, 3, 0x80, (HANDLE)0);
  if (file == (HANDLE)-1) ExitProcess(65);
  size = GetFileSize(file, &file_high);
  if (file_high || size < sizeof(ElfHeader) || size > 0x800000UL) ExitProcess(66);
  bytes = VirtualAlloc((void *)0, size, 0x3000, 4);
  if (!bytes || !ReadFile(file, bytes, size, &received, (void *)0) || received != size) ExitProcess(66);
  CloseHandle(file);
  header = (ElfHeader *)bytes;
  if (header->ident[0] != 127 || header->ident[1] != 'E' || header->ident[2] != 'L' ||
      header->ident[3] != 'F' || header->ident[4] != 1 || header->ident[5] != 1 ||
      header->ident[6] != 1 || header->version != 1 || header->ehsize != sizeof(ElfHeader) ||
      header->type != 2 || header->machine != 3 || header->phsize != sizeof(ElfProgram) ||
      header->phnum == 0 || header->phnum > 64) ExitProcess(67);
  programs_size = header->phnum * sizeof(ElfProgram);
  if (header->phoff > size || programs_size > size - header->phoff) ExitProcess(67);
  for (index = 0; index < header->phnum; ++index) {
    ElfProgram *program = (ElfProgram *)(bytes + header->phoff + index * sizeof(ElfProgram));
    if (program->type == 2 || program->type == 3) ExitProcess(67);
    if (program->type != 1) continue;
    if (program->filesz > program->memsz || program->offset > size ||
        program->filesz > size - program->offset || program->address < 0x00ff0000UL ||
        program->address >= 0x02000000UL || program->memsz > 0x02000000UL - program->address)
      ExitProcess(67);
    if ((program->flags & 1) && entry >= program->address &&
        entry - program->address < program->filesz) entry_loaded = 1;
    if ((program->flags & 1) && program->address <= 0x01001100UL &&
        0x01001100UL - program->address <= program->filesz &&
        program->filesz - (0x01001100UL - program->address) >= 5) print_loaded = 1;
    {
      U32 previous;
      for (previous = 0; previous < index; ++previous) {
        ElfProgram *other = (ElfProgram *)(bytes + header->phoff + previous * sizeof(ElfProgram));
        if (other->type == 1 && other->memsz && program->memsz &&
            program->address < other->address + other->memsz &&
            other->address < program->address + program->memsz) ExitProcess(67);
      }
    }
    if (program->address < low) low = program->address;
    if (program->address + program->memsz > high) high = program->address + program->memsz;
  }
  if (low >= high || !entry_loaded || !print_loaded) ExitProcess(67);
  base = low & ~0xffffUL;
  span = (high - base + 0xffffUL) & ~0xffffUL;
  region = VirtualAlloc((void *)base, span, 0x3000, 0x40);
  if (region != (U8 *)base) ExitProcess(68);
  for (index = 0; index < header->phnum; ++index) {
    ElfProgram *program = (ElfProgram *)(bytes + header->phoff + index * sizeof(ElfProgram));
    U32 item;
    if (program->type != 1) continue;
    for (item = 0; item < program->filesz; ++item)
      ((U8 *)program->address)[item] = bytes[program->offset + item];
  }
  bytes_offset = 0x01001100UL;
  {
    U8 *trampoline = (U8 *)bytes_offset;
    trampoline[0] = 0xe9;
    *(U32 *)(trampoline + 1) = (U32)native_print - bytes_offset - 5;
  }
  if (!FlushInstructionCache(GetCurrentProcess(), region, span)) ExitProcess(68);
  {
    U8 *stack = VirtualAlloc((void *)0, 0x800000, 0x3000, 4);
    if (!stack) ExitProcess(69);
    ExitProcess((U32)native_execute(entry, (U32)(stack + 0x800000)));
  }
}
