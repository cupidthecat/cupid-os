//help: TempleOS-style scripture oracle
//help: Usage: bible
//help: Prints a random KJV verse from Psalms/Proverbs (/god/Psalms.DD).

#define BIBLE_PATH "/god/Psalms.DD"
#define BIBLE_BUF_MAX 524287

char bi_buf[524288];
U32 bi_seed;

int bi_rand() {
    bi_seed = bi_seed * 1103515245 + 12345;
    return (int)((bi_seed >> 1) & 0x7FFFFFFF);
}

void bi_seed_once() {
    if (bi_seed == 0) {
        int t = uptime_ms();
        bi_seed = (U32)(t ^ 0xB1BE1EED);
        if (bi_seed == 0) bi_seed = 1;
    }
}

int bi_pick_verse(char *buf, int n, char *out, int out_max) {
    int i = 0;
    int seen = 0;
    int ps = -1;
    int pl = 0;

    while (i < n) {
        int ls = i;
        int le = i;
        while (le < n && buf[le] != 10 && buf[le] != 13) le = le + 1;
        if (le > ls) {
            seen = seen + 1;
            if ((bi_rand() % seen) == 0) {
                ps = ls;
                pl = le - ls;
            }
        }
        i = le;
        while (i < n && (buf[i] == 10 || buf[i] == 13)) i = i + 1;
    }

    if (ps < 0 || pl <= 0) return 0;
    if (pl > out_max - 1) pl = out_max - 1;
    i = 0;
    while (i < pl) { out[i] = buf[ps + i]; i = i + 1; }
    out[pl] = 0;
    return 1;
}

void main() {
    char verse[512];
    int n;
    bi_seed_once();
    n = vfs_read_text(BIBLE_PATH, bi_buf, BIBLE_BUF_MAX);
    if (n <= 0) {
        println("bible: failed to read /god/Psalms.DD");
        return;
    }
    bi_buf[n] = 0;
    if (!bi_pick_verse(bi_buf, n, verse, 512)) {
        println("bible: scripture empty");
        return;
    }
    print("+++ ");
    print(verse);
    println(" +++");
}
