//help: AC97 audio smoke tests
//help: Usage: audiotest <sine|opl|pan|sweep>

void main() {
    char *args;
    I32 present;

    args = (char*)get_args();

    if (strlen(args) == 0) {
        serial_write_string("Usage: audiotest <sine|opl|pan|sweep>\n");
        return;
    }

    if (strcmp(args, "sine") == 0) {
        present = ac97_is_present_int();
        if (present == 0) {
            serial_write_string("[SKIP] audiotest sine: no AC97 device\n");
            return;
        }
        ac97_smoke_sine();
        return;
    }

    if (strcmp(args, "opl") == 0) {
        serial_write_string("[SKIP] audiotest opl: not implemented (Task 8)\n");
        return;
    }

    if (strcmp(args, "pan") == 0) {
        ac97_smoke_pan();
        return;
    }

    if (strcmp(args, "sweep") == 0) {
        ac97_smoke_sweep();
        return;
    }

    serial_write_string("audiotest: unknown subcommand\n");
}

main();
