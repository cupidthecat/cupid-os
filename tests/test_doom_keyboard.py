import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

MENU_STUBS = r'''
static int saveStringEnter, saveCharIndex, saveSlot, vanilla_keyboard_mapping, saved;
static char savegamestrings[10][SAVESTRINGSIZE], saveOldString[SAVESTRINGSIZE];
static void M_StringCopy(char *out, const char *in, size_t size) {
    snprintf(out, size, "%s", in);
}
static int M_StringWidth(const char *text) { return (int)strlen(text) * 8; }
static void M_DoSave(int slot) { require(slot == 0, "wrong save slot"); saved++; }
'''

HARNESS = r'''
static void require(int value, const char *message) {
    if (!value) { fprintf(stderr, "%s\n", message); exit(91); }
}

static void reset(void) {
    s_kbd_head = 0u;
    s_kbd_tail = 0u;
    shiftdown = 0;
}

static unsigned char take(int expected_pressed) {
    int pressed = 42;
    unsigned char key = 42;
    require(DG_GetKey(&pressed, &key) == 1, "queued key was lost");
    require(pressed == expected_pressed, "pressed/released state changed");
    UpdateShiftStatus(pressed, key);
    return key;
}

static void unchanged_empty(void) {
    int pressed = 42;
    unsigned char key = 42;
    require(DG_GetKey(&pressed, &key) == 0, "empty ring produced a key");
    require(pressed == 42 && key == 42, "empty read changed caller outputs");
}

typedef struct { unsigned char scan, key; } key_case_t;

static const key_case_t letters[] = {
    {0x1e,'a'}, {0x30,'b'}, {0x2e,'c'}, {0x20,'d'}, {0x12,'e'},
    {0x21,'f'}, {0x22,'g'}, {0x23,'h'}, {0x17,'i'}, {0x24,'j'},
    {0x25,'k'}, {0x26,'l'}, {0x32,'m'}, {0x31,'n'}, {0x18,'o'},
    {0x19,'p'}, {0x10,'q'}, {0x13,'r'}, {0x1f,'s'}, {0x14,'t'},
    {0x16,'u'}, {0x2f,'v'}, {0x11,'w'}, {0x2d,'x'}, {0x15,'y'},
    {0x2c,'z'}
};

static const key_case_t printable[] = {
    {0x02,'1'}, {0x03,'2'}, {0x04,'3'}, {0x05,'4'}, {0x06,'5'},
    {0x07,'6'}, {0x08,'7'}, {0x09,'8'}, {0x0a,'9'}, {0x0b,'0'},
    {0x0c,'-'}, {0x0d,'='}, {0x1a,'['}, {0x1b,']'}, {0x27,';'},
    {0x28,'\''}, {0x29,'`'}, {0x2b,'\\'}, {0x33,','}, {0x34,'.'},
    {0x35,'/'}
};

static const key_case_t controls[] = {
    {0x4b,KEY_LEFTARROW}, {0x4d,KEY_RIGHTARROW},
    {0x48,KEY_UPARROW}, {0x50,KEY_DOWNARROW}, {0x1d,KEY_FIRE},
    {0x39,KEY_USE}, {0x2a,KEY_RSHIFT}, {0x36,KEY_RSHIFT}, {0x38,KEY_RALT},
    {0x01,KEY_ESCAPE}, {0x1c,KEY_ENTER}, {0x0f,KEY_TAB}, {0x0e,KEY_BACKSPACE},
    {0x3b,KEY_F1}, {0x3c,KEY_F2}, {0x3d,KEY_F3}, {0x3e,KEY_F4},
    {0x3f,KEY_F5}, {0x40,KEY_F6}, {0x41,KEY_F7}, {0x42,KEY_F8},
    {0x43,KEY_F9}, {0x44,KEY_F10}, {0x57,KEY_F11}, {0x58,KEY_F12}
};

static void pairs(const key_case_t *cases, size_t count) {
    reset();
    for (size_t index = 0; index < count; index++) {
        on_kbd(cases[index].scan, true, 0);
        require(take(1) == cases[index].key, "physical key produced wrong Doom code");
        on_kbd(cases[index].scan, false, 0);
        require(take(0) == cases[index].key, "release produced wrong Doom code");
        unchanged_empty();
    }
}

static void text(void) {
    reset();
    on_kbd(0x2a, true, 0);
    require(take(1) == KEY_RSHIFT, "left Shift did not reach Doom shift tracking");
    on_kbd(0x1e, true, 0);
    require(GetTypedChar(take(1)) == 'A', "Shift+letter did not produce uppercase text");
    on_kbd(0x02, true, 0);
    require(GetTypedChar(take(1)) == '!', "shifted punctuation changed");
    on_kbd(0x39, true, 0);
    require(GetTypedChar(take(1)) == ' ', "Use key did not retain typed Space");
    on_kbd(0x2a, false, 0);
    require(take(0) == KEY_RSHIFT, "Shift release changed");
    on_kbd(0x39, true, 0);
    require(GetTypedChar(take(1)) == ' ', "unshifted Space changed");
    on_kbd(0x1e, true, 0);
    require(GetTypedChar(take(1)) == 'a', "Shift state remained stuck");
    on_kbd(0x0e, true, 0);
    require(take(1) == KEY_BACKSPACE, "Backspace editing key was lost");
    unchanged_empty();
}

static void ignored(void) {
    reset();
    for (int index = 0; index < 300; index++) on_kbd(0x60, false, 0);
    on_kbd(0x80, true, 0);
    on_kbd(0xff, false, 0);
    on_kbd(0x48, true, 0);
    require(take(1) == KEY_UPARROW, "unknown/extended prefix starved a valid key");
    on_kbd(0x60, false, 0);
    on_kbd(0x48, false, 0);
    require(take(0) == KEY_UPARROW, "extended release prefix starved key-up");
    unchanged_empty();
}

static void ring(void) {
    reset();
    for (int index = 0; index < 1024; index++) {
        on_kbd(0x02, true, 0);
        require(take(1) == '1', "wrapped ring changed a key");
    }
    for (int index = 0; index < 255; index++) on_kbd(0x02, true, 0);
    on_kbd(0x1c, true, 0);
    for (int index = 0; index < 255; index++)
        require(take(1) == '1', "overflow overwrote unread input");
    unchanged_empty();
    on_kbd(0x02, false, 0);
    require(take(0) == '1', "ring failed after overflow recovery");
    unchanged_empty();
}


static void save_name(void) {
    for (int mapping = 0; mapping <= 1; mapping++) {
        reset(); saveSlot = 0; saveStringEnter = 1; saveCharIndex = 0;
        vanilla_keyboard_mapping = mapping; saved = 0;
        memset(savegamestrings, 0, sizeof(savegamestrings));
        M_StringCopy(saveOldString, "OLD", sizeof(saveOldString));
        const unsigned char scans[] = {0x2e,0x16,0x19,0x17,0x20,0x39,0x1e,0x09,0x0e,0x02};
        for (size_t i = 0; i < sizeof(scans); i++) {
            on_kbd(scans[i], true, 0); unsigned char key = take(1);
            require(menu_save_event(key, GetTypedChar(key)), "save menu ignored input");
            on_kbd(scans[i], false, 0); (void)take(0);
        }
        require(!strcmp(savegamestrings[0], "CUPID A1"), "save menu lost a letter, space or edit");
        require(menu_save_event(KEY_ENTER, 0), "save menu ignored Enter");
        require(saved == 1 && saveStringEnter == 0, "save menu did not commit exactly once");
        saveStringEnter = 1;
        require(menu_save_event(KEY_ESCAPE, 0), "save menu ignored Escape");
        require(!strcmp(savegamestrings[0], "OLD") && saved == 1, "Escape did not preserve old name");
        saveStringEnter = 1; saveCharIndex = 0; savegamestrings[0][0] = 0;
        require(menu_save_event(KEY_RALT, 0), "save menu ignored control");
        require(saveCharIndex == 0, "nonprinting control entered save name");
        require(menu_save_event('1', '!'), "save menu ignored shifted digit");
        require(savegamestrings[0][0] == (mapping ? '1' : '!'), "vanilla shift policy changed");
    }
}

int main(int argc, char **argv) {
    require(argc == 2, "one contract name is required");
    if (!strcmp(argv[1], "letters")) pairs(letters, sizeof(letters)/sizeof(letters[0]));
    else if (!strcmp(argv[1], "printable")) pairs(printable, sizeof(printable)/sizeof(printable[0]));
    else if (!strcmp(argv[1], "controls")) pairs(controls, sizeof(controls)/sizeof(controls[0]));
    else if (!strcmp(argv[1], "text")) text();
    else if (!strcmp(argv[1], "ignored")) ignored();
    else if (!strcmp(argv[1], "ring")) ring();
    else if (!strcmp(argv[1], "save")) save_name();
    else require(0, "unknown contract");
    puts("doom-keyboard-ok");
    return 0;
}
'''


def keyboard_contract_source(root=ROOT):
    shim = (root / "kernel/doom/doomgeneric_cupidos.cc").read_text(encoding="utf-8")
    engine = (root / "kernel/doom/src/i_input.cc").read_text(encoding="utf-8")
    ring = shim[shim.index("#define INPUT_RING"):shim.index("static bool s_input_owned")]
    get_key = shim[shim.index("int DG_GetKey("):shim.index("/* DG_SetWindowTitle */")]
    typed = engine[engine.index("static const char shiftxform[]"):engine.index("void I_GetEvent(")]
    menu = (root / "kernel/doom/src/m_menu.cc").read_text(encoding="utf-8")
    save_input = menu[menu.index("    if (saveStringEnter)", menu.index("    // Save Game string input")):menu.index("    // Take care of any messages")]
    save_function = "static int menu_save_event(int key, int ch) {\n" + save_input + "return 0;\n}\n"
    return ('#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n'
            '#include <stdlib.h>\n#include <string.h>\n#include <ctype.h>\n#include "doomtype.h"\n'
            '#include "hu_stuff.h"\n#include "p_saveg.h"\n'
            '#include "doomkeys.h"\nstatic int shiftdown = 0;\n'
            + ring + get_key + typed + "static void require(int value, const char *message);\n"
            + MENU_STUBS + save_function + HARNESS)


class DoomKeyboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            raise unittest.SkipTest("native compiler unavailable")
        cls.directory = tempfile.TemporaryDirectory(prefix=".doom-keyboard-", dir=ROOT / "toolchain")
        cls.addClassCleanup(cls.directory.cleanup)
        source = Path(cls.directory.name) / "keyboard.cc"
        source.write_text(keyboard_contract_source(), encoding="utf-8")
        cls.program = Path(cls.directory.name) / ("keyboard.exe" if os.name == "nt" else "keyboard")
        command = [compiler, "-x", "c", "-std=c11", "-O2", "-Wall", "-Werror",
                   "-I", str(ROOT / "kernel/doom/src"), str(source), "-o", str(cls.program)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_contract(self, name):
        result = subprocess.run([str(self.program), name], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "doom-keyboard-ok\n")
        self.assertEqual(result.stderr, "")

    def test_alphabet_make_and_break(self):
        self.run_contract("letters")

    def test_digits_and_punctuation_make_and_break(self):
        self.run_contract("printable")

    def test_game_controls_and_function_keys_match_engine_codes(self):
        self.run_contract("controls")

    def test_shift_space_and_backspace_reach_text_input(self):
        self.run_contract("text")

    def test_ignored_keys_cannot_fill_or_starve_the_ring(self):
        self.run_contract("ignored")

    def test_save_name_accepts_space_and_edits_under_both_keyboard_policies(self):
        self.run_contract("save")

    def test_wrap_overflow_and_release_preserve_pending_input(self):
        self.run_contract("ring")


if __name__ == "__main__":
    unittest.main()
