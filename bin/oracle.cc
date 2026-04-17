//help: TempleOS-style 7-word GodSpeak oracle
//help: Usage: oracle [count]
//help: Prints <count> random words from /god/Vocab.DD as one sentence (default 7).

#define OR_VOCAB_PATH "/god/Vocab.DD"
#define OR_BUF_MAX 65535
#define OR_DEFAULT_WORDS 7
#define OR_MAX_WORDS 32

char or_vocab[65536];
U32 or_seed;

int or_rand() {
    or_seed = or_seed * 1103515245 + 12345;
    return (int)((or_seed >> 1) & 0x7FFFFFFF);
}

void or_seed_once() {
    if (or_seed == 0) {
        int t = uptime_ms();
        or_seed = (U32)(t ^ 0x0DD1EC0DE);
        if (or_seed == 0) or_seed = 1;
    }
}

int or_line_count(char *buf, int n) {
    int i = 0;
    int c = 0;
    while (i < n) {
        int ls = i;
        int le = i;
        while (le < n && buf[le] != 10 && buf[le] != 13) le = le + 1;
        if (le > ls) c = c + 1;
        i = le;
        while (i < n && (buf[i] == 10 || buf[i] == 13)) i = i + 1;
    }
    return c;
}

int or_nth_line(char *buf, int n, int target, char *out, int out_max) {
    int i = 0;
    int idx = 0;
    while (i < n) {
        int ls = i;
        int le = i;
        while (le < n && buf[le] != 10 && buf[le] != 13) le = le + 1;
        if (le > ls) {
            if (idx == target) {
                int l = le - ls;
                if (l > out_max - 1) l = out_max - 1;
                int k = 0;
                while (k < l) { out[k] = buf[ls + k]; k = k + 1; }
                out[l] = 0;
                return l;
            }
            idx = idx + 1;
        }
        i = le;
        while (i < n && (buf[i] == 10 || buf[i] == 13)) i = i + 1;
    }
    return 0;
}

void main() {
    char word[96];
    int n;
    int total;
    int count = OR_DEFAULT_WORDS;
    int k = 0;

    or_seed_once();
    n = vfs_read_text(OR_VOCAB_PATH, or_vocab, OR_BUF_MAX);
    if (n <= 0) {
        println("oracle: failed to read /god/Vocab.DD");
        return;
    }
    or_vocab[n] = 0;
    total = or_line_count(or_vocab, n);
    if (total <= 0) {
        println("oracle: vocabulary empty");
        return;
    }

    print(">>> ");
    while (k < count) {
        int pick = or_rand() % total;
        if (or_nth_line(or_vocab, n, pick, word, 96) > 0) {
            print(word);
            print(" ");
        }
        k = k + 1;
    }
    println("<<<");
}
