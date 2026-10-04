"""Exercise terminal completion against the real serial driver under contention."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STUBS = {
    "bkl.h": "#include \"types.h\"\nbool bkl_is_initialized(void);\nvoid bkl_lock(void);\nvoid bkl_unlock(void);\n",
    "kernel.h": "void print(const char *text);\n",
    "ports.h": "#include \"types.h\"\nuint8_t inb(uint16_t port);\nvoid outb(uint16_t port,uint8_t byte);\n",
    "string.h": "#ifndef SERIAL_TEST_STRING_H\n#define SERIAL_TEST_STRING_H\n#include <stddef.h>\nsize_t strlen(const char *);\nvoid *memcpy(void *,const void *,size_t);\nchar *strstr(const char *,const char *);\nint strcmp(const char *,const char *);\nint strncmp(const char *,const char *,size_t);\nint fmt_f(char *,int,double,int);\nint fmt_e(char *,int,double,int);\nint fmt_g(char *,int,double,int);\n#endif\n",
    "timer.h": "#include \"types.h\"\nuint32_t timer_get_uptime_ms(void);\n",
    "types.h": "#ifndef SERIAL_TEST_TYPES_H\n#define SERIAL_TEST_TYPES_H\n#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#endif\n"
}

CONTRACT = r"""
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include "serial.h"
static volatile int gui_pending_command_state=1;
static char gui_pending_command[256]="exec /disk/hello";
static int executed_commands,printed_prompts;
static void gui_exec_command(const char *input) {
  if (gui_pending_command_state!=2 || strcmp(input,"exec /disk/hello")!=0) abort();
  executed_commands++;
}
static void shell_gui_print_prompt(void) {
  if (gui_pending_command_state!=0 || gui_pending_command[0]!='\0') abort();
  printed_prompts++;
}
#include "pending.cc"
static pthread_mutex_t bkl;
static pthread_mutex_t events = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event = PTHREAD_COND_INITIALIZER;
static pthread_t first_thread, peer_thread;
static int initialized = 1;
static int gate, peer_waiting, peer_io, peer_done;
static unsigned int lock_calls;
static char wire[4096];
static size_t used;
static const char *case_name;
bool bkl_is_initialized(void) { return initialized != 0; }
void bkl_lock(void) {
  int status = pthread_mutex_trylock(&bkl);
  (void)__atomic_add_fetch(&lock_calls,1u,__ATOMIC_RELAXED);
  if (status == EBUSY) {
    (void)pthread_mutex_lock(&events);
    if (pthread_equal(pthread_self(), peer_thread)) {
      peer_waiting = 1;
      (void)pthread_cond_broadcast(&event);
    }
    (void)pthread_mutex_unlock(&events);
    if (pthread_mutex_lock(&bkl) != 0) abort();
  } else if (status != 0) abort();
}
void bkl_unlock(void) { if (pthread_mutex_unlock(&bkl) != 0) abort(); }
uint8_t inb(uint16_t port) { (void)port; return 0x20u; }
void outb(uint16_t port, uint8_t byte) {
  if (port != SERIAL_COM1) return;
  (void)pthread_mutex_lock(&events);
  if (used + 1u >= sizeof(wire)) abort();
  wire[used++] = (char)byte; wire[used] = '\0';
  if (gate && pthread_equal(pthread_self(),peer_thread)) {
    peer_io = 1; (void)pthread_cond_broadcast(&event);
  }
  if (initialized && !gate && pthread_equal(pthread_self(),first_thread) && used == 20u) {
    gate = 1; (void)pthread_cond_broadcast(&event);
    while (!peer_waiting && !peer_io) (void)pthread_cond_wait(&event,&events);
    if (peer_io) while (!peer_done) (void)pthread_cond_wait(&event,&events);
  }
  (void)pthread_mutex_unlock(&events);
}
uint32_t timer_get_uptime_ms(void) { return 1234u; }
void print(const char *text) { (void)text; }
int fmt_f(char *out,int size,double value,int precision) { return snprintf(out,(size_t)size,"%.*f",precision<0?6:precision,value); }
int fmt_e(char *out,int size,double value,int precision) { return snprintf(out,(size_t)size,"%.*e",precision<0?6:precision,value); }
int fmt_g(char *out,int size,double value,int precision) { return snprintf(out,(size_t)size,"%.*g",precision<0?6:precision,value); }
static void *peer(void *unused) {
  (void)unused;
  (void)pthread_mutex_lock(&events);
  while (!gate) (void)pthread_cond_wait(&event,&events);
  (void)pthread_mutex_unlock(&events);
  if (strcmp(case_name,"printf-shell")==0 || strcmp(case_name,"checksum-shell")==0) {
    if (shell_gui_run_pending_command()!=1) abort();
  }
  else if (strstr(case_name,"-char") != NULL) serial_write_char('@');
  else if (strstr(case_name,"-printf") != NULL)
    serial_printf("[terminal] command %s\n","complete");
  else serial_write_string("[terminal] command complete\n");
  (void)pthread_mutex_lock(&events);peer_done = 1;
  (void)pthread_cond_broadcast(&event);(void)pthread_mutex_unlock(&events);
  return NULL;
}
int main(int argc,char **argv) {
  pthread_mutexattr_t attributes;
  const char *prefix, *suffix;
  char expected[1024];
  if (argc != 2) return 2;
  case_name=argv[1];
  (void)pthread_mutexattr_init(&attributes);
  (void)pthread_mutexattr_settype(&attributes,PTHREAD_MUTEX_RECURSIVE);
  (void)pthread_mutex_init(&bkl,&attributes);
  first_thread=pthread_self();
  if (strcmp(case_name,"idle-shell")==0) {
    gate=1;gui_pending_command_state=0;
    if (shell_gui_run_pending_command()!=0 || used!=0 || lock_calls!=0 ||
        executed_commands!=0 || printed_prompts!=0) return 1;
    (void)puts("idle terminal neither executes nor emits completion");return 0;
  }
  if (strcmp(case_name,"boot")==0) {
    initialized=0;
    serial_write_string("boot\n");serial_write_char('!');serial_printf(" pid=%u\n",4u);
    if (strcmp(wire,"boot\r\n! pid=4\r\n")!=0 || lock_calls!=0) return 1;
    (void)puts("boot serial works before BKL initialization");return 0;
  }
  if (pthread_create(&peer_thread,NULL,peer,NULL)!=0) return 2;
  if (strcmp(case_name,"checksum-shell")==0) {
    prefix="[elf-syscall] pid=4 op=print bytes=62 fnv1a=0xdeadbeef\r\n";
    serial_printf("[elf-syscall] pid=%u op=print bytes=%u fnv1a=0x%08x\n",4u,62u,0xdeadbeefu);
  } else if (strncmp(case_name,"klog-",5u)==0) {
    prefix="[1.234] trace pid=4 value=4\r\n";
    klog(LOG_INFO,"trace pid=%u value=%u",4u,4u);
  } else {
    prefix="[elf-syscall] pid=4 op=print_int value=4\r\n";
    if (strncmp(case_name,"string-",7u)==0)
      serial_write_string("[elf-syscall] pid=4 op=print_int value=4\n");
    else serial_printf("[elf-syscall] pid=%u op=print_int value=%u\n",4u,4u);
  }
  (void)pthread_join(peer_thread,NULL);
  if (strcmp(case_name,"printf-shell")==0 || strcmp(case_name,"checksum-shell")==0) {
    if (gui_pending_command_state!=0 || gui_pending_command[0]!='\0' ||
        executed_commands!=1 || printed_prompts!=1) return 1;
  }
  suffix=strstr(case_name,"-char")!=NULL?"@":"[terminal] command complete\r\n";
  (void)snprintf(expected,sizeof(expected),"%s%s",prefix,suffix);
  if (strcmp(wire,expected)!=0) {
    (void)fprintf(stderr,"interleaved serial bytes in %s:\n%s\n",case_name,wire);return 1;
  }
  (void)printf("atomic %s bytes=%u\n",case_name,(unsigned int)used);return 0;
}

"""

@unittest.skipIf(os.name == "nt", "the deterministic thread fixture uses POSIX pthreads")
@unittest.skipUnless(shutil.which("clang"), "native Clang is required for the serial fixture")
class TerminalSerialRecordTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix="cupid-terminal-record-")
        cls.addClassCleanup(cls.work.cleanup)
        folder = Path(cls.work.name)
        stubs = folder / "stubs"
        stubs.mkdir()
        for name, source in STUBS.items():
            (stubs / name).write_text(source, encoding="utf-8")
        for name in ("serial.cc", "serial.h"):
            (folder / name).write_bytes((ROOT / "drivers" / name).read_bytes())
        shell = (ROOT / "kernel/lang/shell.cc").read_text(encoding="utf-8")
        bodies = re.findall(
            r"^int shell_gui_run_pending_command\(void\) \{.*?^\}",
            shell, re.MULTILINE | re.DOTALL,
        )
        if len(bodies) != 1:
            raise AssertionError("expected one actual pending-command implementation")
        (folder / "pending.cc").write_text(bodies[0] + "\n", encoding="utf-8")
        (folder / "contract.cc").write_text(CONTRACT, encoding="utf-8")
        cls.binary = folder / "contract"
        result = subprocess.run(
            [shutil.which("clang"), "-O0", "-std=c11", "-Wall", "-Wextra",
             "-Werror", "-Wno-pointer-to-int-cast", "-pthread", "-x", "c",
             "-I", str(stubs), "-I", str(folder), str(folder / "serial.cc"),
             str(folder / "contract.cc"), "-o", str(cls.binary)],
            capture_output=True, text=True, timeout=60,
        )
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def check_case(self, name):
        result = subprocess.run(
            [str(self.binary), name], capture_output=True, text=True, timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_completion_preserves_the_pid_and_integer_record(self):
        self.check_case("printf-shell")

    def test_completion_preserves_the_byte_count_and_checksum_record(self):
        self.check_case("checksum-shell")

    def test_idle_terminal_neither_executes_nor_emits_completion(self):
        self.check_case("idle-shell")

    def test_formatted_records_remain_serialized(self):
        self.check_case("printf-printf")

    def test_serial_output_works_before_lock_initialization(self):
        self.check_case("boot")
