import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opened = source.index("{", start)
    depth = 1
    end = opened + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


HARNESS = r'''
static int locked, locks, errors, mkdir_calls, serial_calls;
static unsigned int s_kbd_head, s_kbd_tail;
static keyboard_state_t keyboard_state;
typedef void (*kbd_event_cb)(uint8_t, bool, void *);
static kbd_event_cb s_kbd_sub_cb;
static void *s_kbd_sub_ctx;
static jmp_buf error_env;
static void require(int ok, const char *why) {
    if (!ok) { fprintf(stderr, "%s\n", why); exit(91); }
}
static void bkl_lock(void) { require(!locked, "nested handoff lock"); locked = 1; locks++; }
static void bkl_unlock(void) { require(locked, "unlock without lock"); locked = 0; }
static void on_kbd(uint8_t sc, bool pressed, void *ctx) { (void)sc; (void)pressed; (void)ctx; }
static int vfs_mkdir(const char *name) {
    require(!locked && !strcmp(name,"/home/doom"), "mkdir in input critical section");
    mkdir_calls++; return 0;
}
static void serial_write_string(const char *text) {
    require(!locked && strstr(text,"platform shim ready"), "wrong init completion"); serial_calls++;
}
static void I_Error(const char *text) {
    require(!locked && strstr(text,"subscriber is busy"), "busy failure held lock");
    errors++; longjmp(error_env, 1);
}
'''

WRAPPERS = r'''
static int keyboard_subscribe(kbd_event_cb cb, void *ctx) {
    require(locked, "subscribe outside BKL"); return driver_subscribe(cb,ctx);
}
static void keyboard_unsubscribe(void) {
    require(locked, "unsubscribe outside BKL"); driver_unsubscribe();
}
static void keyboard_discard_events(void) {
    require(locked, "discard outside BKL"); driver_discard();
}
'''

CASES = r'''
static void fill(void) {
    memset(&keyboard_state,0xa5,sizeof(keyboard_state));
    keyboard_state.buffer.head = 79; keyboard_state.buffer.tail = 9;
    keyboard_state.buffer.count = 70;
}
static void preserved(keyboard_state_t before) {
    before.buffer.head = before.buffer.tail = before.buffer.count = 0;
    require(!memcmp(&before,&keyboard_state,sizeof(before)), "discard changed keys, modifiers or payload");
}
int main(int argc, char **argv) {
    require(argc==2,"one case required");
    if (!strcmp(argv[1],"discard")) {
        for (int count=0;count<256;count++) {
            fill(); keyboard_state.buffer.count=(uint8_t)count;
            keyboard_state_t before=keyboard_state;
            bkl_lock(); keyboard_discard_events(); bkl_unlock(); preserved(before);
        }
    } else if (!strcmp(argv[1],"handoff")) {
        fill(); keyboard_state_t before=keyboard_state;
        DG_Init(); require(s_input_owned && s_kbd_sub_cb==on_kbd,"Doom did not own subscription");
        require(s_kbd_head==0 && s_kbd_tail==0 && mkdir_calls==1 && serial_calls==1,"init incomplete");
        preserved(before); fill(); before=keyboard_state;
        cup_input_stop(); preserved(before);
        require(!s_input_owned && !s_kbd_sub_cb && !s_kbd_sub_ctx && !locked && locks==2,"handoff incomplete");
        fill(); before=keyboard_state; cup_input_stop();
        require(!memcmp(&before,&keyboard_state,sizeof(before)),"second stop discarded new owner's input");
    } else if (!strcmp(argv[1],"busy")) {
        fill(); keyboard_state_t before=keyboard_state;
        bkl_lock(); require(keyboard_subscribe(on_kbd,(void*)7)==0,"fixture subscription failed"); bkl_unlock();
        if (!setjmp(error_env)) { DG_Init(); require(0,"busy subscription was accepted"); }
        require(errors==1 && !s_input_owned && !locked,"failed init stole ownership");
        cup_input_stop(); require(s_kbd_sub_ctx==(void*)7,"cleanup removed original subscriber");
        require(!memcmp(&before,&keyboard_state,sizeof(before)),"busy init or cleanup discarded unrelated input");
        require(!mkdir_calls && !serial_calls,"failed init reported readiness");
    } else if (!strcmp(argv[1],"unowned")) {
        fill(); keyboard_state_t before=keyboard_state; cup_input_stop();
        require(!memcmp(&before,&keyboard_state,sizeof(before)),"unowned cleanup discarded input");
        require(!s_kbd_sub_cb && !s_input_owned && !locked,"unowned cleanup changed ownership");
    } else require(0,"unknown case");
    puts("doom-input-handoff-ok"); return 0;
}
'''


def contract_source():
    driver = (ROOT / "drivers/keyboard.cc").read_text(encoding="utf-8")
    shim = (ROOT / "kernel/doom/doomgeneric_cupidos.cc").read_text(encoding="utf-8")
    types = (ROOT / "kernel/core/types.h").read_text(encoding="utf-8")
    types = types[types.index("#define KEYBOARD_BUFFER_SIZE"):types.index("// Timer calibration")]
    subscriber = function(driver,"int keyboard_subscribe(").replace("keyboard_subscribe(","driver_subscribe(")
    subscriber += function(driver,"void keyboard_unsubscribe(").replace("keyboard_unsubscribe(","driver_unsubscribe(")
    discard = function(driver,"void keyboard_discard_events(").replace("keyboard_discard_events(","driver_discard(")
    stop = "static bool s_input_owned = false;\n" + function(shim,"static void cup_input_stop(")
    init = function(shim,"void DG_Init(")
    return ("#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n"
            "#include <stdlib.h>\n#include <string.h>\n#include <setjmp.h>\n"
            + types + HARNESS + subscriber + discard + WRAPPERS + stop + init + CASES)


class DoomInputHandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("native compiler unavailable")
        cls.directory = tempfile.TemporaryDirectory(prefix=".doom-input-handoff-", dir=ROOT / "toolchain")
        cls.addClassCleanup(cls.directory.cleanup)
        source = Path(cls.directory.name) / "handoff.cc"
        source.write_text(contract_source(), encoding="utf-8")
        cls.program = Path(cls.directory.name) / ("handoff.exe" if os.name == "nt" else "handoff")
        result = subprocess.run([compiler,"-x","c","-std=c11","-O2","-Wall","-Werror",
                                 str(source),"-o",str(cls.program)],capture_output=True,text=True,timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, name):
        result = subprocess.run([str(self.program),name],capture_output=True,text=True,timeout=5)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout,"doom-input-handoff-ok\n")
        self.assertEqual(result.stderr,"")

    def test_discard_preserves_keys_modifiers_and_event_storage(self):
        self.run_case("discard")

    def test_owned_handoff_clears_both_queues_and_is_idempotent(self):
        self.run_case("handoff")

    def test_busy_subscription_preserves_the_existing_owner_and_queue(self):
        self.run_case("busy")

    def test_unowned_cleanup_preserves_pending_input(self):
        self.run_case("unowned")


if __name__ == "__main__":
    unittest.main()
