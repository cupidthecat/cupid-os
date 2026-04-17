//help: TempleOS-style AutoComplete over /god/Vocab.DD
//help: Usage: auto <prefix>
//help: Lists all dictionary words that start with <prefix>.

#define AU_VOCAB_PATH "/god/Vocab.DD"
#define AU_BUF_MAX 65535
#define AU_MAX_HITS 64

char au_vocab[65536];

int au_tolower(int c) {
    if (c >= 65 && c <= 90) return c + 32;
    return c;
}

int au_starts_with_ci(char *s, int slen, char *prefix) {
    int i = 0;
    while (prefix[i]) {
        if (i >= slen) return 0;
        if (au_tolower(s[i]) != au_tolower(prefix[i])) return 0;
        i = i + 1;
    }
    return 1;
}

void main() {
    char prefix[64];
    const char *raw;
    int n;
    int plen = 0;
    int i = 0;
    int hits = 0;
    int col = 0;

    raw = get_args();
    if (raw) {
        while (raw[plen] && raw[plen] != ' ' && plen < 63) {
            prefix[plen] = raw[plen];
            plen = plen + 1;
        }
        prefix[plen] = 0;
    } else {
        prefix[0] = 0;
    }
    if (plen <= 0) {
        println("Usage: auto <prefix>");
        return;
    }

    n = vfs_read_text(AU_VOCAB_PATH, au_vocab, AU_BUF_MAX);
    if (n <= 0) {
        println("auto: failed to read /god/Vocab.DD");
        return;
    }
    au_vocab[n] = 0;

    while (i < n && hits < AU_MAX_HITS) {
        int ls = i;
        int le = i;
        while (le < n && au_vocab[le] != 10 && au_vocab[le] != 13) le = le + 1;
        if (le > ls && au_starts_with_ci(au_vocab + ls, le - ls, prefix)) {
            int k = 0;
            int L = le - ls;
            while (k < L) { putchar(au_vocab[ls + k]); k = k + 1; }
            if (col >= 3) { putchar(10); col = 0; }
            else { putchar(9); col = col + 1; }
            hits = hits + 1;
        }
        i = le;
        while (i < n && (au_vocab[i] == 10 || au_vocab[i] == 13)) i = i + 1;
    }
    if (col != 0) putchar(10);
    print("+++ ");
    print_int(hits);
    println(" match(es) +++");
}
