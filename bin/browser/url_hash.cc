/* url_hash.cc - 32-bit FNV-1a + font cache path helpers.
 *
 * Hash quality target: this is single-user, single-host OS with a
 * 64 MiB cache cap (~1000 entries max). 32-bit collision probability
 * at 1000 entries is ~1 in 4M, well below failure modes that matter.
 * Plan called for 64-bit but cupidc has no native u64; the simpler
 * single-word path is in keeping with the rest of bin/browser.
 *
 * Output paths: /cache/fonts/<hex8>.bin and <hex8>.meta.*/

unsigned int url_hash32(char *url) {
    unsigned int h = 2166136261u;
    int i = 0;
    while (url[i]) {
        h = h ^ (unsigned int)(unsigned char)url[i];
        h = h * 16777619u;
        i = i + 1;
    }
    return h;
}

static int u_hex_nibble(int v) {
    if (v < 10) return '0' + v;
    return 'a' + (v - 10);
}

void url_hash_hex(char *url, char *out8) {
    unsigned int h = url_hash32(url);
    int i;
    for (i = 0; i < 8; i = i + 1) {
        int sh = (7 - i) * 4;
        out8[i] = (char)u_hex_nibble((int)((h >> (unsigned int)sh) & 0xFu));
    }
    out8[8] = 0;
}

/* Builds /cache/fonts/<hex8><suffix>. suffix is e.g. ".bin", ".meta",
 * ".bin.tmp" - caller picks. out must hold at least 32 bytes.*/
void url_cache_path(char *url, char *suffix, char *out, int max) {
    char hex[9];
    url_hash_hex(url, hex);
    int p = 0;
    char *prefix = "/cache/fonts/";
    int i = 0;
    while (prefix[i] && p < max - 1) { out[p] = prefix[i]; p = p + 1; i = i + 1; }
    i = 0;
    while (hex[i] && p < max - 1) { out[p] = hex[i]; p = p + 1; i = i + 1; }
    i = 0;
    while (suffix[i] && p < max - 1) { out[p] = suffix[i]; p = p + 1; i = i + 1; }
    out[p] = 0;
}
