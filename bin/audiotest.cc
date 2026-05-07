//help: AC97 audio smoke tests
//help: Usage: audiotest <sine|opl|pan|sweep|all>

void main() {
    char *args;
    I32 present;

    args = (char*)get_args();

    if (strlen(args) == 0) {
        serial_write_string("Usage: audiotest <sine|opl|pan|sweep|all>\n");
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
        opl_smoke();
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

    if (strcmp(args, "all") == 0) {
        audiotest_all();
        return;
    }

    serial_write_string("audiotest: unknown subcommand\n");
}

main();
