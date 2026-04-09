//help: TempleOS-style random vocabulary output
//help: Usage: godspeak
//help: Prints "Cupid says: <random word>" using /god/Vocab.DD.

#define GS_VOCAB_PATH "/god/Vocab.DD"
#define GS_BUF_MAX 65535

char gs_vocab[65536];
U32 gs_seed;

int gs_rand() {
    gs_seed = gs_seed * 1103515245 + 12345;
    return (int)((gs_seed >> 1) & 0x7FFFFFFF);
}

void gs_seed_once() {
    if (gs_seed == 0) {
        int t = uptime_ms();
        gs_seed = (U32)(t ^ 0x5EEDC0DE);
        if (gs_seed == 0) gs_seed = 1;
    }
}

int gs_pick_random_nonempty_line(char *buf, int n, char *out, int out_max) {
    int i = 0;
    int seen = 0;
    int pick_start = -1;
    int pick_len = 0;

    while (i < n) {
        int ls = i;
        int le = i;

        while (le < n && buf[le] != 10 && buf[le] != 13) {
            le = le + 1;
        }

        if (le > ls) {
            seen = seen + 1;
            if ((gs_rand() % seen) == 0) {
                pick_start = ls;
                pick_len = le - ls;
            }
        }

        i = le;
        while (i < n && (buf[i] == 10 || buf[i] == 13)) {
            i = i + 1;
        }
    }

    if (pick_start < 0 || pick_len <= 0) return 0;
    if (pick_len > out_max - 1) pick_len = out_max - 1;

    i = 0;
    while (i < pick_len) {
        out[i] = buf[pick_start + i];
        i = i + 1;
    }
    out[pick_len] = 0;
    return 1;
}

void main() {
    char chosen[128];
    int n;

    gs_seed_once();

    n = vfs_read_text(GS_VOCAB_PATH, gs_vocab, GS_BUF_MAX);
    if (n <= 0) {
        println("godspeak: failed to read /god/Vocab.DD");
        return;
    }
    gs_vocab[n] = 0;

    if (!gs_pick_random_nonempty_line(gs_vocab, n, chosen, 128)) {
        println("godspeak: vocabulary is empty");
        return;
    }

    print("Cupid says: ");
    println(chosen);
}
