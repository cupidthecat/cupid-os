//help: SSH-2 client. Connects to a server, authenticates, runs interactive shell.
//help: Usage: ssh [user@]host[:port]
//help:   user defaults to root, port defaults to 22.
//help:   Negotiates curve25519-sha256 / chacha20-poly1305@openssh.com.
//help:   Host keys: ssh-ed25519, rsa-sha2-512, ecdsa-sha2-nistp256.
//help:   Auth: password, then keyboard-interactive fallback.

enum {
    SOCK_TCP        = 2,

    SSH_PORT        = 22,

    /* SSH msg numbers (RFC 4250 §4.1). */
    SSH_MSG_DISCONNECT                = 1,
    SSH_MSG_IGNORE                    = 2,
    SSH_MSG_UNIMPLEMENTED             = 3,
    SSH_MSG_DEBUG                     = 4,
    SSH_MSG_SERVICE_REQUEST           = 5,
    SSH_MSG_SERVICE_ACCEPT            = 6,
    SSH_MSG_KEXINIT                   = 20,
    SSH_MSG_NEWKEYS                   = 21,
    SSH_MSG_KEX_ECDH_INIT             = 30,
    SSH_MSG_KEX_ECDH_REPLY            = 31,
    SSH_MSG_USERAUTH_REQUEST          = 50,
    SSH_MSG_USERAUTH_FAILURE          = 51,
    SSH_MSG_USERAUTH_SUCCESS          = 52,
    SSH_MSG_USERAUTH_BANNER           = 53,
    SSH_MSG_USERAUTH_INFO_REQUEST     = 60,
    SSH_MSG_USERAUTH_INFO_RESPONSE    = 61,
    SSH_MSG_GLOBAL_REQUEST            = 80,
    SSH_MSG_REQUEST_SUCCESS           = 81,
    SSH_MSG_REQUEST_FAILURE           = 82,
    SSH_MSG_CHANNEL_OPEN              = 90,
    SSH_MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    SSH_MSG_CHANNEL_OPEN_FAILURE      = 92,
    SSH_MSG_CHANNEL_WINDOW_ADJUST     = 93,
    SSH_MSG_CHANNEL_DATA              = 94,
    SSH_MSG_CHANNEL_EXTENDED_DATA     = 95,
    SSH_MSG_CHANNEL_EOF               = 96,
    SSH_MSG_CHANNEL_CLOSE             = 97,
    SSH_MSG_CHANNEL_REQUEST           = 98,
    SSH_MSG_CHANNEL_SUCCESS           = 99,
    SSH_MSG_CHANNEL_FAILURE           = 100,

    /* Buffer sizing. SSH spec caps packet at 35000 bytes; we round up. */
    BUF_CAP     = 65536,
    KS_NAMES    = 1024,
    HOSTKEY_MAX = 4096,
    PKT_PAYLOAD_MAX = 32768,

    /* TCP states. */
    TCPS_ESTABLISHED = 4
};

/* ----- global state ----- */

int  fd = -1;
char *rxbuf  = 0;  /* incoming wire bytes — circular sliding window */
int   rxhead = 0;
int   rxtail = 0;
char *txbuf  = 0;  /* outgoing wire bytes — built per packet */
int   txlen  = 0;
char *pkt_pl = 0;  /* decrypted incoming packet payload */
int   pkt_pl_len = 0;
char *mac_scratch = 0; /* concatenation buffer for Poly1305 MAC verify */

/* Sequence numbers as big-endian 8-byte arrays. Only the low 4 bytes
 * ever matter for normal session lengths; we keep the full 8 bytes for
 * use as the chacha20 nonce. */
char seq_c2s[8];
char seq_s2c[8];

/* Cipher keys derived from KEX. Filled when newkeys_done == 1. */
char key_c2s_main[32];
char key_c2s_hdr[32];
char key_s2c_main[32];
char key_s2c_hdr[32];
int  newkeys_c2s = 0;
int  newkeys_s2c = 0;

/* KEX intermediates. */
char client_kex_priv[32];
char client_kex_pub[32];
char server_kex_pub[32];
char shared_K[32];
char session_id[32];
int  session_id_set = 0;

/* Banner strings (without CRLF). */
char V_C[64];
int  V_C_len = 0;
char V_S[256];
int  V_S_len = 0;

/* Raw KEXINIT payloads (incl. msg type byte) for the exchange hash. */
char *I_C = 0;
int   I_C_len = 0;
char *I_S = 0;
int   I_S_len = 0;

/* Negotiated host key algorithm (after parsing server's KEXINIT). */
int  hk_algo = 0;   /* 1=ssh-ed25519, 2=rsa-sha2-512, 3=ecdsa-sha2-nistp256 */

/* Channel state for the shell session. */
int  local_chan = 0;
int  remote_chan = 0;
int  tx_window = 0;
int  remote_max_packet = 0;
int  rx_window = 1048576;
int  channel_open = 0;
int  channel_eof  = 0;

/* ----- tiny utilities ----- */

void zero_bytes(char *p, int n) { int i = 0; while (i < n) { p[i] = 0; i = i + 1; } }
void copy_bytes(char *d, char *s, int n) { int i = 0; while (i < n) { d[i] = s[i]; i = i + 1; } }
int  bytes_eq(char *a, char *b, int n) {
    int i = 0; while (i < n) { if (a[i] != b[i]) return 0; i = i + 1; } return 1;
}
int  cstrlen(char *s) { int i = 0; while (s[i]) i = i + 1; return i; }
int  cstreq(char *a, char *b) {
    int i = 0; while (a[i] && a[i] == b[i]) i = i + 1; return a[i] == b[i];
}
void cstrcpy_n(char *d, char *s, int max) {
    int i = 0; while (s[i] && i < max - 1) { d[i] = s[i]; i = i + 1; } d[i] = 0;
}

void print_err(char *s) { print("ssh: "); println(s); }

int ssh_debug_io = 1;
int ssh_debug_render = 1;
int ssh_debug_dump_max = 160;

void debug_dump_bytes(char *tag, char *buf, int len) {
    if (!ssh_debug_io) return;
    int max = len;
    if (max > ssh_debug_dump_max) max = ssh_debug_dump_max;
    serial_printf("[ssh-debug] %s len=%d bytes=", tag, len);
    int i = 0;
    while (i < max) {
        int b = buf[i] & 0xFF;
        if (b == 0) serial_printf("<NUL>");
        else if (b == 7) serial_printf("<BEL>");
        else if (b == 8) serial_printf("<BS>");
        else if (b == 9) serial_printf("<TAB>");
        else if (b == 10) serial_printf("<LF>");
        else if (b == 13) serial_printf("<CR>");
        else if (b == 27) serial_printf("<ESC>");
        else if (b >= 32 && b <= 126) {
            char one[2];
            one[0] = b;
            one[1] = 0;
            serial_printf("%s", one);
        } else {
            serial_printf("<0x%x>", b);
        }
        i = i + 1;
    }
    if (len > max) serial_printf("...");
    serial_printf("\n");
}

void seq_incr(char *seq) {
    int i = 7;
    while (i >= 0) {
        int v = (seq[i] & 0xFF) + 1;
        seq[i] = v & 0xFF;
        if (v < 256) return;
        i = i - 1;
    }
}

/* ----- big-endian write/read ----- */

void put_be32(char *p, int v) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8)  & 0xFF;
    p[3] = v & 0xFF;
}
int  get_be32(char *p) {
    int a = p[0] & 0xFF;
    int b = p[1] & 0xFF;
    int c = p[2] & 0xFF;
    int d = p[3] & 0xFF;
    return (a << 24) | (b << 16) | (c << 8) | d;
}

/* ----- SSH wire helpers (append-only into tx buffer) ----- */

int wb_byte(char *out, int off, int v) { out[off] = v & 0xFF; return off + 1; }
int wb_be32(char *out, int off, int v) {
    out[off]     = (v >> 24) & 0xFF;
    out[off + 1] = (v >> 16) & 0xFF;
    out[off + 2] = (v >> 8)  & 0xFF;
    out[off + 3] =  v        & 0xFF;
    return off + 4;
}
int wb_bytes(char *out, int off, char *src, int n) {
    int i = 0; while (i < n) { out[off + i] = src[i]; i = i + 1; }
    return off + n;
}
int wb_string(char *out, int off, char *src, int n) {
    off = wb_be32(out, off, n);
    return wb_bytes(out, off, src, n);
}
int wb_cstr(char *out, int off, char *src) {
    return wb_string(out, off, src, cstrlen(src));
}
int wb_bool(char *out, int off, int v) { out[off] = v ? 1 : 0; return off + 1; }

/* SSH mpint: positive integer, big-endian, with leading 0x00 if high
 * bit of MSB is set. value is given as fixed 32 bytes BE (X25519
 * shared secret). Strips leading zero bytes. */
int wb_mpint_be32(char *out, int off, char *be32) {
    int start = 0;
    while (start < 32 && be32[start] == 0) start = start + 1;
    /* If all zero: empty mpint (length 0). */
    if (start == 32) {
        return wb_be32(out, off, 0);
    }
    int needs_pad = (be32[start] & 0x80) ? 1 : 0;
    int len = 32 - start + needs_pad;
    off = wb_be32(out, off, len);
    if (needs_pad) { out[off] = 0; off = off + 1; }
    int i = start;
    while (i < 32) { out[off] = be32[i]; off = off + 1; i = i + 1; }
    return off;
}

/* ----- TCP I/O ----- */

void tcp_send_all(char *buf, int n) {
    int sent = 0;
    while (sent < n) {
        int r = send(fd, buf + sent, n - sent);
        if (r < 0) { print_err("send failed"); exit(); }
        sent = sent + r;
    }
}

int tcp_recv_exact(char *buf, int n) {
    int got = 0;
    while (got < n) {
        if (sock_state(fd) != TCPS_ESTABLISHED && sock_avail(fd) == 0) {
            return -1;
        }
        if (sock_avail(fd) > 0) {
            int r = recv(fd, buf + got, n - got);
            if (r <= 0) return -1;
            got = got + r;
        } else {
            yield();
        }
    }
    return n;
}

int tcp_recv_some(char *buf, int cap) {
    while (sock_avail(fd) == 0) {
        if (sock_state(fd) != TCPS_ESTABLISHED) return -1;
        yield();
    }
    int max_now = sock_avail(fd);
    if (max_now > cap) max_now = cap;
    return recv(fd, buf, max_now);
}

/* ----- SSH version exchange ----- */

int send_banner() {
    cstrcpy_n(V_C, "SSH-2.0-CupidOS_0.1", 64);
    V_C_len = cstrlen(V_C);
    char line[80];
    int i = 0;
    while (i < V_C_len) { line[i] = V_C[i]; i = i + 1; }
    line[i] = 13; line[i + 1] = 10;
    tcp_send_all(line, V_C_len + 2);
    return 0;
}

int recv_banner() {
    char line[512];
    int n = 0;
    /* Read lines until one starts with "SSH-". */
    while (1) {
        n = 0;
        while (n < 510) {
            char c;
            if (tcp_recv_exact(&c, 1) != 1) return -1;
            if (c == 13) { /* CR */
                char d;
                if (tcp_recv_exact(&d, 1) != 1) return -1;
                if (d == 10) break; /* CRLF */
                /* Treat CR without LF as line end too. */
                break;
            }
            if (c == 10) break; /* LF */
            line[n] = c;
            n = n + 1;
        }
        if (n >= 4 && line[0] == 'S' && line[1] == 'S' && line[2] == 'H' && line[3] == '-') {
            break;
        }
    }
    if (n > 255) n = 255;
    int i = 0; while (i < n) { V_S[i] = line[i]; i = i + 1; }
    V_S_len = n;
    return 0;
}

/* ----- BPP (binary packet protocol) -----
 *
 * Pre-NEWKEYS: clear text. Block size 8. No MAC. Padding randomised
 * (could be zero; we randomise to match standard implementations).
 *
 * Post-NEWKEYS (chacha20-poly1305@openssh.com):
 *   nonce[12] = {0,0,0,0, seq[0..8]}
 *   length_enc  = chacha20(k_hdr, counter=0, nonce, length_be32, 4)
 *   poly_key    = first 32 bytes of chacha20(k_main, counter=0, nonce, zeros, 32)
 *   payload_enc = chacha20(k_main, counter=1, nonce, padded_payload)
 *   tag         = poly1305(poly_key, length_enc || payload_enc)
 */

void build_nonce(char nonce[12], char seq[8]) {
    int i = 0;
    while (i < 4) { nonce[i] = 0; i = i + 1; }
    while (i < 12) { nonce[i] = seq[i - 4]; i = i + 1; }
}

int bpp_send(char *payload, int plen) {
    int block = 8;
    if (newkeys_c2s) {
        /* AEAD framing — length field NOT included in block-size math. */
        int padlen = block - ((1 + plen) % block);
        if (padlen < 4) padlen = padlen + block;
        int total_inner = 1 + plen + padlen; /* padding_length + payload + padding */
        int wire_len = 4 + total_inner + 16;
        if (wire_len > BUF_CAP) return -1;

        /* Build cleartext packet (length || padding_length || payload || padding). */
        char *p = txbuf;
        put_be32(p, total_inner);
        p[4] = padlen & 0xFF;
        int i = 0; while (i < plen) { p[5 + i] = payload[i]; i = i + 1; }
        crypto_random_bytes(p + 5 + plen, padlen);

        /* Encrypt header (4 bytes) under k_hdr, counter=0. */
        char nonce[12]; build_nonce(nonce, seq_c2s);
        chacha20_xor(key_c2s_hdr, 0, nonce, p, p, 4);

        /* Derive Poly1305 key: first 32 of keystream under k_main, ctr=0. */
        char poly_key[64];
        zero_bytes(poly_key, 64);
        chacha20_xor(key_c2s_main, 0, nonce, poly_key, poly_key, 64);

        /* Encrypt payload+padding under k_main, ctr=1. */
        chacha20_xor(key_c2s_main, 1, nonce, p + 4, p + 4, total_inner);

        /* MAC over (length_enc || payload_enc). */
        char tag[16];
        poly1305_auth(tag, p, 4 + total_inner, poly_key);
        i = 0; while (i < 16) { p[4 + total_inner + i] = tag[i]; i = i + 1; }

        tcp_send_all(p, wire_len);
    } else {
        int padlen = block - ((5 + plen) % block);
        if (padlen < 4) padlen = padlen + block;
        int wire_len = 4 + 1 + plen + padlen;
        if (wire_len > BUF_CAP) return -1;

        char *p = txbuf;
        put_be32(p, 1 + plen + padlen);
        p[4] = padlen & 0xFF;
        int i = 0; while (i < plen) { p[5 + i] = payload[i]; i = i + 1; }
        /* Random padding is fine even pre-NEWKEYS. */
        crypto_random_bytes(p + 5 + plen, padlen);
        tcp_send_all(p, wire_len);
    }
    seq_incr(seq_c2s);
    return 0;
}

/* Receive one SSH packet. Decrypts (if AEAD) and returns payload pointer
 * via *out_payload and length via *out_len. Returns 0 on success, -1 on
 * EOF, -2 on protocol/MAC error. Uses static decode buffer (rxbuf reuse
 * is fine because we copy decrypted payload into pkt_pl). */
int bpp_recv(char **out_payload, int *out_len) {
    char nonce[12]; build_nonce(nonce, seq_s2c);

    if (newkeys_s2c) {
        char len_enc[4];
        if (tcp_recv_exact(len_enc, 4) != 4) return -1;
        char len_clear[4];
        chacha20_xor(key_s2c_hdr, 0, nonce, len_enc, len_clear, 4);
        int total_inner = get_be32(len_clear);
        if (total_inner < 1 + 4 || total_inner > PKT_PAYLOAD_MAX) return -2;

        /* Need (total_inner) ciphertext bytes + 16 tag. */
        if (total_inner > BUF_CAP - 20) return -2;
        if (tcp_recv_exact(rxbuf, total_inner + 16) != total_inner + 16) return -1;

        /* Derive poly key from ctr=0 under k_main. */
        char poly_key[64];
        zero_bytes(poly_key, 64);
        chacha20_xor(key_s2c_main, 0, nonce, poly_key, poly_key, 64);

        /* MAC verify over (len_enc || ciphertext). */
        if (4 + total_inner > 32772) return -2;
        int i = 0; while (i < 4) { mac_scratch[i] = len_enc[i]; i = i + 1; }
        i = 0; while (i < total_inner) { mac_scratch[4 + i] = rxbuf[i]; i = i + 1; }
        char tag_expect[16];
        poly1305_auth(tag_expect, mac_scratch, 4 + total_inner, poly_key);
        char *tag_got = rxbuf + total_inner;
        int bad = 0;
        i = 0; while (i < 16) { if (tag_expect[i] != tag_got[i]) bad = 1; i = i + 1; }
        if (bad) return -2;

        /* Decrypt payload + padding. */
        chacha20_xor(key_s2c_main, 1, nonce, rxbuf, rxbuf, total_inner);

        int padlen = rxbuf[0] & 0xFF;
        int pllen = total_inner - 1 - padlen;
        if (pllen < 1) return -2;
        i = 0; while (i < pllen) { pkt_pl[i] = rxbuf[1 + i]; i = i + 1; }
        pkt_pl_len = pllen;
    } else {
        char hdr[5];
        if (tcp_recv_exact(hdr, 5) != 5) return -1;
        int total_inner = get_be32(hdr);
        int padlen = hdr[4] & 0xFF;
        if (total_inner < 1 + padlen + 1 || total_inner > PKT_PAYLOAD_MAX) return -2;
        int pllen = total_inner - 1 - padlen;
        if (tcp_recv_exact(pkt_pl, pllen) != pllen) return -1;
        pkt_pl_len = pllen;
        /* discard padding */
        char pad[256];
        int rem = padlen;
        while (rem > 0) {
            int n = rem > 256 ? 256 : rem;
            if (tcp_recv_exact(pad, n) != n) return -1;
            rem = rem - n;
        }
    }
    seq_incr(seq_s2c);
    *out_payload = pkt_pl;
    *out_len = pkt_pl_len;
    return 0;
}

/* ----- KEXINIT ----- */

int build_kexinit(char *out) {
    int off = 0;
    serial_printf("[ssh] bk: msgtype write off=%d outptr=%p\n", off, out);
    out[off] = SSH_MSG_KEXINIT; off = off + 1;
    serial_printf("[ssh] bk: cookie\n");
    crypto_random_bytes(out + off, 16); off = off + 16;
    serial_printf("[ssh] bk: kex list off=%d\n", off);
    off = wb_cstr(out, off, "curve25519-sha256");
    serial_printf("[ssh] bk: hk list off=%d\n", off);
    off = wb_cstr(out, off, "ssh-ed25519,rsa-sha2-512,ecdsa-sha2-nistp256");
    serial_printf("[ssh] bk: enc c2s off=%d\n", off);
    off = wb_cstr(out, off, "chacha20-poly1305@openssh.com");
    off = wb_cstr(out, off, "chacha20-poly1305@openssh.com");
    serial_printf("[ssh] bk: mac off=%d\n", off);
    off = wb_cstr(out, off, "");
    off = wb_cstr(out, off, "");
    off = wb_cstr(out, off, "none");
    off = wb_cstr(out, off, "none");
    off = wb_cstr(out, off, "");
    off = wb_cstr(out, off, "");
    out[off] = 0; off = off + 1;       /* first_kex_packet_follows */
    off = wb_be32(out, off, 0);        /* reserved */
    serial_printf("[ssh] bk: done off=%d\n", off);
    return off;
}

int contains_token(char *list, int list_len, char *tok) {
    int tl = cstrlen(tok);
    int i = 0;
    while (i <= list_len - tl) {
        int j = 0;
        while (j < tl && list[i + j] == tok[j]) j = j + 1;
        if (j == tl) {
            int prev_ok = (i == 0) || (list[i - 1] == ',');
            int next_ok = (i + tl == list_len) || (list[i + tl] == ',');
            if (prev_ok && next_ok) return 1;
        }
        i = i + 1;
    }
    return 0;
}

int parse_server_kexinit_pickhk(char *pl, int len) {
    /* pl[0] is the SSH_MSG_KEXINIT byte; skip + 16 cookie. */
    int off = 1 + 16;
    /* Skip kex_algorithms */
    int slen = get_be32(pl + off); off = off + 4 + slen;
    /* server_host_key_algorithms */
    slen = get_be32(pl + off); off = off + 4;
    char *hk_list = pl + off;
    int hk_list_len = slen;
    /* Pick first of (ssh-ed25519, rsa-sha2-512, ecdsa-sha2-nistp256) the
     * server supports, preserving our preference order. */
    if (contains_token(hk_list, hk_list_len, "ssh-ed25519")) return 1;
    if (contains_token(hk_list, hk_list_len, "rsa-sha2-512")) return 2;
    if (contains_token(hk_list, hk_list_len, "ecdsa-sha2-nistp256")) return 3;
    return -1;
}

int do_kexinit() {
    serial_printf("[ssh] do_kexinit: stack alloc\n");
    char ourk[KS_NAMES];
    serial_printf("[ssh] do_kexinit: build\n");
    int ourk_len = build_kexinit(ourk);
    serial_printf("[ssh] do_kexinit: built len=%d\n", ourk_len);

    /* Save I_C = our KEXINIT payload (bytes incl. msg type). */
    I_C = kmalloc(ourk_len);
    serial_printf("[ssh] do_kexinit: I_C=%p\n", I_C);
    int i = 0; while (i < ourk_len) { I_C[i] = ourk[i]; i = i + 1; }
    I_C_len = ourk_len;

    serial_printf("[ssh] do_kexinit: bpp_send\n");
    bpp_send(ourk, ourk_len);
    serial_printf("[ssh] do_kexinit: sent, awaiting reply\n");

    char *pl; int pllen;
    if (bpp_recv(&pl, &pllen) != 0) { print_err("kex recv failed"); return -1; }
    serial_printf("[ssh] do_kexinit: recv ok pllen=%d msg=%d\n", pllen, pl[0]);
    if (pllen < 1 || pl[0] != SSH_MSG_KEXINIT) { print_err("expected KEXINIT"); return -1; }

    I_S = kmalloc(pllen);
    i = 0; while (i < pllen) { I_S[i] = pl[i]; i = i + 1; }
    I_S_len = pllen;

    hk_algo = parse_server_kexinit_pickhk(pl, pllen);
    if (hk_algo < 0) { print_err("no compatible host key algorithm"); return -1; }
    return 0;
}

/* ----- X25519 ECDH ----- */

int do_ecdh() {
    serial_printf("[ssh] ecdh: gen priv\n");
    /* Generate clamped 32-byte private scalar. */
    crypto_random_bytes(client_kex_priv, 32);
    client_kex_priv[0] = client_kex_priv[0] & 248;
    client_kex_priv[31] = (client_kex_priv[31] & 127) | 64;

    /* Compute Q_C = x25519(priv, basepoint). The basepoint is {9,0,...,0}. */
    char base[32];
    zero_bytes(base, 32);
    base[0] = 9;
    serial_printf("[ssh] ecdh: x25519 basepoint\n");
    x25519(client_kex_pub, client_kex_priv, base);
    serial_printf("[ssh] ecdh: pub computed\n");

    /* Send SSH_MSG_KEX_ECDH_INIT. */
    serial_printf("[ssh] ecdh: send INIT\n");
    char msg[5 + 32];
    msg[0] = SSH_MSG_KEX_ECDH_INIT;
    put_be32(msg + 1, 32);
    int i = 0; while (i < 32) { msg[5 + i] = client_kex_pub[i]; i = i + 1; }
    bpp_send(msg, 5 + 32);

    /* Receive SSH_MSG_KEX_ECDH_REPLY. */
    serial_printf("[ssh] ecdh: await REPLY\n");
    char *pl; int pllen;
    if (bpp_recv(&pl, &pllen) != 0) { print_err("ecdh recv failed"); return -1; }
    serial_printf("[ssh] ecdh: got reply pllen=%d msg=%d\n", pllen, pl[0]);
    if (pl[0] != SSH_MSG_KEX_ECDH_REPLY) { print_err("expected ECDH_REPLY"); return -1; }

    /* Parse K_S, Q_S, sig — all length-prefixed SSH strings. */
    int off = 1;
    int ks_len = get_be32(pl + off); off = off + 4;
    if (off + ks_len > pllen) return -1;
    char *ks = pl + off; off = off + ks_len;

    int qs_len = get_be32(pl + off); off = off + 4;
    if (qs_len != 32 || off + qs_len > pllen) return -1;
    i = 0; while (i < 32) { server_kex_pub[i] = pl[off + i]; i = i + 1; }
    off = off + 32;

    int sig_len = get_be32(pl + off); off = off + 4;
    if (off + sig_len > pllen) return -1;
    char *sig = pl + off;

    /* Compute K = x25519(priv, Q_S). */
    serial_printf("[ssh] ecdh: x25519 shared K\n");
    x25519(shared_K, client_kex_priv, server_kex_pub);
    serial_printf("[ssh] ecdh: shared K done\n");
    int allzero = 1;
    i = 0; while (i < 32) { if (shared_K[i] != 0) allzero = 0; i = i + 1; }
    if (allzero) { print_err("shared secret is zero"); return -1; }
    serial_printf("[ssh] ecdh: K nonzero, build H\n");

    /* Build exchange hash input. */
    int H_buf_cap = 8192;
    char *Hin = kmalloc(H_buf_cap);
    serial_printf("[ssh] ecdh: Hin=%p cap=%d\n", Hin, H_buf_cap);
    int hoff = 0;
    hoff = wb_string(Hin, hoff, V_C, V_C_len);
    serial_printf("[ssh] ecdh: H+V_C hoff=%d\n", hoff);
    hoff = wb_string(Hin, hoff, V_S, V_S_len);
    serial_printf("[ssh] ecdh: H+V_S hoff=%d\n", hoff);
    hoff = wb_string(Hin, hoff, I_C, I_C_len);
    serial_printf("[ssh] ecdh: H+I_C hoff=%d\n", hoff);
    hoff = wb_string(Hin, hoff, I_S, I_S_len);
    serial_printf("[ssh] ecdh: H+I_S hoff=%d\n", hoff);
    hoff = wb_string(Hin, hoff, ks, ks_len);
    serial_printf("[ssh] ecdh: H+K_S hoff=%d ks_len=%d\n", hoff, ks_len);
    hoff = wb_string(Hin, hoff, client_kex_pub, 32);
    hoff = wb_string(Hin, hoff, server_kex_pub, 32);
    hoff = wb_mpint_be32(Hin, hoff, shared_K);
    serial_printf("[ssh] ecdh: H ready hoff=%d\n", hoff);

    char H[32];
    sha256(Hin, hoff, H);
    serial_printf("[ssh] ecdh: H hash done\n");
    kfree(Hin);

    if (!session_id_set) {
        int j = 0; while (j < 32) { session_id[j] = H[j]; j = j + 1; }
        session_id_set = 1;
    }

    /* Verify host key signature over H. */
    serial_printf("[ssh] ecdh: verify host key alg=%d\n", hk_algo);
    if (verify_host_key(ks, ks_len, H, sig, sig_len) != 0) {
        print_err("host key verification FAILED");
        return -1;
    }
    serial_printf("[ssh] ecdh: host key verified\n");

    /* TOFU prompt with SHA-256 fingerprint of K_S. */
    serial_printf("[ssh] tofu: awaiting host key confirmation\n");
    if (tofu_prompt(ks, ks_len) != 0) {
        print_err("connection aborted by user");
        serial_printf("[ssh] tofu: rejected or read failed\n");
        return -1;
    }
    serial_printf("[ssh] tofu: accepted\n");

    return 0;
}

/* ----- host key verify ----- */

int find_string(char *buf, int len, int *off_io, char **out_ptr, int *out_len) {
    int off = *off_io;
    if (off + 4 > len) return -1;
    int sl = get_be32(buf + off); off = off + 4;
    if (off + sl > len) return -1;
    *out_ptr = buf + off;
    *out_len = sl;
    *off_io = off + sl;
    return 0;
}

int verify_host_key(char *ks, int ks_len, char *H, char *sig_blob, int sig_blob_len) {
    char *ks_alg; int ks_alg_len;
    int ks_off = 0;
    if (find_string(ks, ks_len, &ks_off, &ks_alg, &ks_alg_len) != 0) return -1;

    char *sig_alg; int sig_alg_len;
    int sig_off = 0;
    if (find_string(sig_blob, sig_blob_len, &sig_off, &sig_alg, &sig_alg_len) != 0) return -1;
    char *sig_bytes; int sig_bytes_len;
    if (find_string(sig_blob, sig_blob_len, &sig_off, &sig_bytes, &sig_bytes_len) != 0) return -1;

    if (hk_algo == 1) {
        /* ssh-ed25519: K_S = string "ssh-ed25519" || string pub(32),
         * sig = string "ssh-ed25519" || string raw_sig(64). */
        if (ks_alg_len != 11) return -1;
        if (!bytes_eq(ks_alg, "ssh-ed25519", 11)) return -1;
        char *pub; int pub_len;
        if (find_string(ks, ks_len, &ks_off, &pub, &pub_len) != 0) return -1;
        if (pub_len != 32) return -1;
        if (sig_bytes_len != 64) return -1;
        int ok = ed25519_verify(pub, H, 32, sig_bytes);
        return ok ? 0 : -1;
    } else if (hk_algo == 2) {
        /* rsa-sha2-512: K_S = string "ssh-rsa" || mpint e || mpint n.
         * Sig blob alg = "rsa-sha2-512". Sig bytes = raw RSA signature. */
        if (ks_alg_len != 7 || !bytes_eq(ks_alg, "ssh-rsa", 7)) return -1;
        char *e_bytes; int e_len;
        char *n_bytes; int n_len;
        if (find_string(ks, ks_len, &ks_off, &e_bytes, &e_len) != 0) return -1;
        if (find_string(ks, ks_len, &ks_off, &n_bytes, &n_len) != 0) return -1;
        /* SSH mpints may start with 0x00 padding for sign bit. RSA helper
         * accepts raw big-endian; strip the leading zero. */
        if (e_len > 0 && e_bytes[0] == 0) { e_bytes = e_bytes + 1; e_len = e_len - 1; }
        if (n_len > 0 && n_bytes[0] == 0) { n_bytes = n_bytes + 1; n_len = n_len - 1; }
        /* Compute SHA-512(H). */
        char hH[64];
        sha512(H, 32, hH);
        int ok = rsa_pkcs1v15_verify_sha512(n_bytes, n_len, e_bytes, e_len,
                                            sig_bytes, sig_bytes_len, hH);
        return ok ? 0 : -1;
    } else if (hk_algo == 3) {
        /* ecdsa-sha2-nistp256: K_S = string "ecdsa-sha2-nistp256" ||
         *   string "nistp256" || string Q (uncompressed 0x04||X||Y, 65b).
         * Sig bytes = string (mpint r || mpint s). */
        char *curve; int curve_len;
        if (find_string(ks, ks_len, &ks_off, &curve, &curve_len) != 0) return -1;
        if (curve_len != 8 || !bytes_eq(curve, "nistp256", 8)) return -1;
        char *q; int q_len;
        if (find_string(ks, ks_len, &ks_off, &q, &q_len) != 0) return -1;
        if (q_len != 65 || (q[0] & 0xFF) != 4) return -1;
        /* Sig bytes is a concatenated (mpint r || mpint s). */
        int s_off = 0;
        char *r_bytes; int r_len;
        char *s_bytes; int s_len;
        if (find_string(sig_bytes, sig_bytes_len, &s_off, &r_bytes, &r_len) != 0) return -1;
        if (find_string(sig_bytes, sig_bytes_len, &s_off, &s_bytes, &s_len) != 0) return -1;
        if (r_len > 0 && r_bytes[0] == 0) { r_bytes = r_bytes + 1; r_len = r_len - 1; }
        if (s_len > 0 && s_bytes[0] == 0) { s_bytes = s_bytes + 1; s_len = s_len - 1; }
        /* hash = SHA-256(H). For ecdsa-sha2-nistp256 the data signed is
         * H (which is already a SHA-256), and the inner hash for ECDSA
         * is SHA-256 again per RFC 5656. */
        char hH[32];
        sha256(H, 32, hH);
        int rc = ssh_ecdsa_p256_verify_blob(q, hH, 32, r_bytes, r_len, s_bytes, s_len);
        return (rc == 0) ? 0 : -1;
    }
    return -1;
}

/* ----- Base64 (no padding for fingerprint display) ----- */

void b64_emit(char *out, int *off, int v) {
    char *tab = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    out[*off] = tab[v & 63];
    *off = *off + 1;
}
int b64_encode_nopad(char *in, int in_len, char *out) {
    int i = 0; int off = 0;
    while (i + 3 <= in_len) {
        int a = in[i] & 0xFF, b = in[i+1] & 0xFF, c = in[i+2] & 0xFF;
        b64_emit(out, &off, a >> 2);
        b64_emit(out, &off, ((a & 3) << 4) | (b >> 4));
        b64_emit(out, &off, ((b & 15) << 2) | (c >> 6));
        b64_emit(out, &off, c & 63);
        i = i + 3;
    }
    int rem = in_len - i;
    if (rem == 1) {
        int a = in[i] & 0xFF;
        b64_emit(out, &off, a >> 2);
        b64_emit(out, &off, (a & 3) << 4);
    } else if (rem == 2) {
        int a = in[i] & 0xFF, b = in[i+1] & 0xFF;
        b64_emit(out, &off, a >> 2);
        b64_emit(out, &off, ((a & 3) << 4) | (b >> 4));
        b64_emit(out, &off, (b & 15) << 2);
    }
    out[off] = 0;
    return off;
}

int tofu_prompt(char *ks, int ks_len) {
    char fp[32];
    sha256(ks, ks_len, fp);
    char fp_b64[64];
    b64_encode_nopad(fp, 32, fp_b64);

    print("The authenticity of host can't be established.\n");
    char *alg_name = "host";
    if (hk_algo == 1) alg_name = "ED25519";
    else if (hk_algo == 2) alg_name = "RSA";
    else if (hk_algo == 3) alg_name = "ECDSA";
    print(alg_name);
    print(" key fingerprint is SHA256:");
    print(fp_b64);
    print("\nWARNING: accepting host key without known_hosts storage.\n");
    serial_printf("[ssh] tofu: accepting host key SHA256:%s\n", fp_b64);

    return 0;
}

/* ----- NEWKEYS / key derivation ----- */

void derive_key(char letter, char *out, int out_len) {
    /* K_1 = HASH(K_mpint || H || X || session_id), where X is the letter.
     * K_{n+1} = HASH(K_mpint || H || K_1..K_n).
     * For the initial KEX, H == session_id, so we use session_id throughout. */
    char Kbuf[48];
    int koff = wb_mpint_be32(Kbuf, 0, shared_K);

    char stage[256];   /* mpint_K (≤37) + H(32) + letter(1) + session_id(32) */
    char block[32];
    int produced = 0;

    /* First block. */
    int soff = 0;
    int i = 0;
    while (i < koff)   { stage[soff] = Kbuf[i];        soff = soff + 1; i = i + 1; }
    i = 0;
    while (i < 32)     { stage[soff] = session_id[i];  soff = soff + 1; i = i + 1; }
    stage[soff] = letter; soff = soff + 1;
    i = 0;
    while (i < 32)     { stage[soff] = session_id[i];  soff = soff + 1; i = i + 1; }
    sha256(stage, soff, block);
    int take = 32; if (take > out_len) take = out_len;
    i = 0; while (i < take) { out[i] = block[i]; i = i + 1; }
    produced = take;

    /* Subsequent blocks. */
    while (produced < out_len) {
        soff = 0;
        i = 0; while (i < koff) { stage[soff] = Kbuf[i];       soff = soff + 1; i = i + 1; }
        i = 0; while (i < 32)   { stage[soff] = session_id[i]; soff = soff + 1; i = i + 1; }
        i = 0; while (i < produced) { stage[soff] = out[i];    soff = soff + 1; i = i + 1; }
        sha256(stage, soff, block);
        take = 32; if (produced + take > out_len) take = out_len - produced;
        i = 0; while (i < take) { out[produced + i] = block[i]; i = i + 1; }
        produced = produced + take;
    }
}

int do_newkeys() {
    /* Send NEWKEYS. */
    serial_printf("[ssh] newkeys: send\n");
    char m[1]; m[0] = SSH_MSG_NEWKEYS;
    bpp_send(m, 1);

    /* Receive NEWKEYS. */
    serial_printf("[ssh] newkeys: await server NEWKEYS\n");
    char *pl; int pllen;
    int rr = bpp_recv(&pl, &pllen);
    if (rr != 0) {
        serial_printf("[ssh] newkeys: recv failed rc=%d\n", rr);
        return -1;
    }
    serial_printf("[ssh] newkeys: got pllen=%d msg=%d\n", pllen, pl[0]);
    if (pllen < 1 || pl[0] != SSH_MSG_NEWKEYS) return -1;

    /* Derive 64-byte keys for c2s and s2c. K_main = first 32, K_hdr = next 32. */
    serial_printf("[ssh] newkeys: derive keys\n");
    char keyC[64], keyD[64];
    derive_key('C', keyC, 64);
    derive_key('D', keyD, 64);
    int i = 0;
    while (i < 32) { key_c2s_main[i] = keyC[i]; key_c2s_hdr[i] = keyC[32 + i]; i = i + 1; }
    i = 0;
    while (i < 32) { key_s2c_main[i] = keyD[i]; key_s2c_hdr[i] = keyD[32 + i]; i = i + 1; }

    newkeys_c2s = 1;
    newkeys_s2c = 1;
    serial_printf("[ssh] newkeys: enabled\n");
    return 0;
}

/* ----- userauth ----- */

int do_service_request(char *svc) {
    serial_printf("[ssh] service: request '%s'\n", svc);
    char msg[256];
    int off = 0;
    msg[off] = SSH_MSG_SERVICE_REQUEST; off = off + 1;
    off = wb_cstr(msg, off, svc);
    if (bpp_send(msg, off) != 0) {
        serial_printf("[ssh] service: send failed\n");
        return -1;
    }
    serial_printf("[ssh] service: sent, awaiting accept\n");

    char *pl; int pllen;
    while (1) {
        int rr = bpp_recv(&pl, &pllen);
        if (rr != 0) {
            serial_printf("[ssh] service: recv failed rc=%d\n", rr);
            return -1;
        }
        serial_printf("[ssh] service: got pllen=%d msg=%d\n", pllen, pl[0]);
        if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_DEBUG) continue;
        break;
    }
    if (pl[0] != SSH_MSG_SERVICE_ACCEPT) {
        serial_printf("[ssh] service: expected accept, got %d\n", pl[0]);
        return -1;
    }
    serial_printf("[ssh] service: accepted\n");
    return 0;
}

int parse_failure_methods(char *pl, int pllen, char *methods_out, int cap) {
    /* pl[0] = 51 SSH_MSG_USERAUTH_FAILURE
     * string  authentications that can continue
     * boolean partial_success */
    int off = 1;
    int sl = get_be32(pl + off); off = off + 4;
    if (sl >= cap) sl = cap - 1;
    int i = 0; while (i < sl) { methods_out[i] = pl[off + i]; i = i + 1; }
    methods_out[sl] = 0;
    return sl;
}

int try_password_auth(char *user, char *pw) {
    serial_printf("[ssh] auth: send password request user='%s'\n", user);
    char msg[1024];
    int off = 0;
    msg[off] = SSH_MSG_USERAUTH_REQUEST; off = off + 1;
    off = wb_cstr(msg, off, user);
    off = wb_cstr(msg, off, "ssh-connection");
    off = wb_cstr(msg, off, "password");
    msg[off] = 0; off = off + 1;          /* FALSE (no change_password) */
    off = wb_cstr(msg, off, pw);
    if (bpp_send(msg, off) != 0) {
        serial_printf("[ssh] auth: password send failed\n");
        return -1;
    }
    serial_printf("[ssh] auth: password sent, awaiting response\n");

    char *pl; int pllen;
    while (1) {
        int rr = bpp_recv(&pl, &pllen);
        if (rr != 0) {
            serial_printf("[ssh] auth: recv failed rc=%d\n", rr);
            return -1;
        }
        serial_printf("[ssh] auth: got pllen=%d msg=%d\n", pllen, pl[0]);
        if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_DEBUG) continue;
        if (pl[0] == SSH_MSG_USERAUTH_BANNER) continue;
        break;
    }
    if (pl[0] == SSH_MSG_USERAUTH_SUCCESS) return 0;
    if (pl[0] == SSH_MSG_USERAUTH_FAILURE) {
        char methods[256];
        parse_failure_methods(pl, pllen, methods, 256);
        if (contains_token(methods, cstrlen(methods), "keyboard-interactive")) return 2;
        return 1;
    }
    return -1;
}

int try_kbdint_auth(char *user) {
    serial_printf("[ssh] auth: send keyboard-interactive request user='%s'\n", user);
    char msg[512];
    int off = 0;
    msg[off] = SSH_MSG_USERAUTH_REQUEST; off = off + 1;
    off = wb_cstr(msg, off, user);
    off = wb_cstr(msg, off, "ssh-connection");
    off = wb_cstr(msg, off, "keyboard-interactive");
    off = wb_cstr(msg, off, "");          /* language tag */
    off = wb_cstr(msg, off, "");          /* submethods */
    if (bpp_send(msg, off) != 0) {
        serial_printf("[ssh] auth: kbdint send failed\n");
        return -1;
    }
    serial_printf("[ssh] auth: kbdint sent, awaiting response\n");

    while (1) {
        char *pl; int pllen;
        while (1) {
            int rr = bpp_recv(&pl, &pllen);
            if (rr != 0) {
                serial_printf("[ssh] auth: kbdint recv failed rc=%d\n", rr);
                return -1;
            }
            serial_printf("[ssh] auth: kbdint got pllen=%d msg=%d\n", pllen, pl[0]);
            if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_DEBUG) continue;
            if (pl[0] == SSH_MSG_USERAUTH_BANNER) continue;
            break;
        }
        if (pl[0] == SSH_MSG_USERAUTH_SUCCESS) return 0;
        if (pl[0] == SSH_MSG_USERAUTH_FAILURE) return 1;
        if (pl[0] != SSH_MSG_USERAUTH_INFO_REQUEST) return -1;
        /* Parse INFO_REQUEST and reply. */
        int off2 = 1;
        char *name; int name_len;
        if (find_string(pl, pllen, &off2, &name, &name_len) != 0) return -1;
        char *instr; int instr_len;
        if (find_string(pl, pllen, &off2, &instr, &instr_len) != 0) return -1;
        char *lang; int lang_len;
        if (find_string(pl, pllen, &off2, &lang, &lang_len) != 0) return -1;
        if (off2 + 4 > pllen) return -1;
        int nprompts = get_be32(pl + off2); off2 = off2 + 4;
        if (instr_len > 0) {
            print_n(instr, instr_len);
            print("\n");
        }
        /* Build response. */
        char rsp[1024 + 32];
        int roff = 0;
        rsp[roff] = SSH_MSG_USERAUTH_INFO_RESPONSE; roff = roff + 1;
        roff = wb_be32(rsp, roff, nprompts);
        int k = 0;
        while (k < nprompts) {
            char *pr; int pr_len;
            if (find_string(pl, pllen, &off2, &pr, &pr_len) != 0) return -1;
            if (off2 + 1 > pllen) return -1;
            int echo = pl[off2] & 0xFF; off2 = off2 + 1;
            if (pr_len > 0) print_n(pr, pr_len);
            char ans[256];
            int alen;
            if (echo) alen = read_line(ans, 256);
            else alen = read_password(ans, 256);
            if (alen < 0) alen = 0;
            roff = wb_string(rsp, roff, ans, alen);
            k = k + 1;
        }
        bpp_send(rsp, roff);
    }
}

int do_userauth(char *user) {
    serial_printf("[ssh] auth: begin user='%s'\n", user);
    if (do_service_request("ssh-userauth") != 0) return -1;
    char pw[256];
    serial_printf("[ssh] auth: service ok, waiting for password input\n");
    print("\nssh: enter password in this terminal (input hidden)\npassword: ");
    int pwlen = read_password(pw, 256);
    if (pwlen < 0) {
        serial_printf("[ssh] auth: password input failed\n");
        return -1;
    }
    serial_printf("[ssh] auth: password input len=%d\n", pwlen);
    int r = try_password_auth(user, pw);
    serial_printf("[ssh] auth: password result=%d\n", r);
    if (r == 0) return 0;
    if (r == 2) {
        /* Fallback to keyboard-interactive. */
        return try_kbdint_auth(user);
    }
    if (r == 1) {
        /* Failed; try keyboard-interactive anyway (server may not have
         * advertised it but it's worth attempting). */
        return try_kbdint_auth(user);
    }
    return -1;
}

/* ----- Channel + pty-req + shell ----- */

void send_channel_open() {
    char msg[256];
    int off = 0;
    msg[off] = SSH_MSG_CHANNEL_OPEN; off = off + 1;
    off = wb_cstr(msg, off, "session");
    off = wb_be32(msg, off, local_chan);
    off = wb_be32(msg, off, rx_window);
    off = wb_be32(msg, off, 32768);
    bpp_send(msg, off);
}

void send_pty_req(int cols, int rows) {
    char msg[256];
    char modes[128];
    int moff = 0;
    int off = 0;

    /* SSH RFC 4254 terminal modes. Make the remote PTY look like a normal
     * cooked terminal: echo input, canonical lines, CR->NL input, NL->CRLF
     * output, and Ctrl-C/Backspace defaults. */
    modes[moff] = 1; moff = moff + 1; put_be32(modes + moff, 3); moff = moff + 4;     /* VINTR  ^C */
    modes[moff] = 3; moff = moff + 1; put_be32(modes + moff, 127); moff = moff + 4;   /* VERASE DEL */
    modes[moff] = 4; moff = moff + 1; put_be32(modes + moff, 21); moff = moff + 4;    /* VKILL  ^U */
    modes[moff] = 5; moff = moff + 1; put_be32(modes + moff, 4); moff = moff + 4;     /* VEOF   ^D */
    modes[moff] = 36; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* ICRNL */
    modes[moff] = 38; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* IXON */
    modes[moff] = 50; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* ISIG */
    modes[moff] = 51; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* ICANON */
    modes[moff] = 53; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* ECHO */
    modes[moff] = 54; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* ECHOE */
    modes[moff] = 55; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* ECHOK */
    modes[moff] = 59; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* IEXTEN */
    modes[moff] = 70; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* OPOST */
    modes[moff] = 72; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* ONLCR */
    modes[moff] = 91; moff = moff + 1; put_be32(modes + moff, 1); moff = moff + 4;    /* CS8 */
    modes[moff] = 128; moff = moff + 1; put_be32(modes + moff, 38400); moff = moff + 4; /* ISPEED */
    modes[moff] = 129; moff = moff + 1; put_be32(modes + moff, 38400); moff = moff + 4; /* OSPEED */
    modes[moff] = 0; moff = moff + 1;                                                   /* TTY_OP_END */

    msg[off] = SSH_MSG_CHANNEL_REQUEST; off = off + 1;
    off = wb_be32(msg, off, remote_chan);
    off = wb_cstr(msg, off, "pty-req");
    msg[off] = 1; off = off + 1;          /* want_reply */
    off = wb_cstr(msg, off, "xterm-256color");
    off = wb_be32(msg, off, cols);
    off = wb_be32(msg, off, rows);
    off = wb_be32(msg, off, 0);
    off = wb_be32(msg, off, 0);
    off = wb_string(msg, off, modes, moff);
    bpp_send(msg, off);
}

void send_window_change(int cols, int rows) {
    char msg[80];
    int off = 0;
    serial_printf("[ssh-debug] pty window-change cols=%d rows=%d remote=%d\n",
                  cols, rows, remote_chan);
    msg[off] = SSH_MSG_CHANNEL_REQUEST; off = off + 1;
    off = wb_be32(msg, off, remote_chan);
    off = wb_cstr(msg, off, "window-change");
    msg[off] = 0; off = off + 1;          /* want_reply */
    off = wb_be32(msg, off, cols);
    off = wb_be32(msg, off, rows);
    off = wb_be32(msg, off, 0);
    off = wb_be32(msg, off, 0);
    bpp_send(msg, off);
}

void send_shell_req() {
    char msg[64];
    int off = 0;
    msg[off] = SSH_MSG_CHANNEL_REQUEST; off = off + 1;
    off = wb_be32(msg, off, remote_chan);
    off = wb_cstr(msg, off, "shell");
    msg[off] = 1; off = off + 1;          /* want_reply */
    bpp_send(msg, off);
}

void send_channel_data(char *data, int len) {
    int requested = len;
    int before_window = tx_window;
    if (len > tx_window) len = tx_window;
    if (len > remote_max_packet - 9) len = remote_max_packet - 9;
    if (len <= 0) {
        serial_printf("[ssh-debug] tx channel blocked requested=%d tx_window=%d max_packet=%d\n",
                      requested, tx_window, remote_max_packet);
        return;
    }
    debug_dump_bytes("tx-channel", data, len);
    char hdr[9];
    hdr[0] = SSH_MSG_CHANNEL_DATA;
    put_be32(hdr + 1, remote_chan);
    put_be32(hdr + 5, len);
    char *buf = kmalloc(9 + len);
    int i = 0; while (i < 9) { buf[i] = hdr[i]; i = i + 1; }
    i = 0; while (i < len) { buf[9 + i] = data[i]; i = i + 1; }
    bpp_send(buf, 9 + len);
    kfree(buf);
    tx_window = tx_window - len;
    serial_printf("[ssh-debug] tx channel sent=%d requested=%d window %d->%d\n",
                  len, requested, before_window, tx_window);
}

void send_window_adjust(int n) {
    char msg[16];
    serial_printf("[ssh-debug] rx window-adjust add=%d before=%d\n", n, rx_window);
    msg[0] = SSH_MSG_CHANNEL_WINDOW_ADJUST;
    put_be32(msg + 1, remote_chan);
    put_be32(msg + 5, n);
    bpp_send(msg, 9);
    rx_window = rx_window + n;
}

void send_channel_close() {
    char msg[8];
    msg[0] = SSH_MSG_CHANNEL_CLOSE;
    put_be32(msg + 1, remote_chan);
    bpp_send(msg, 5);
}

void send_disconnect(int reason, char *desc) {
    char msg[256];
    int off = 0;
    msg[off] = SSH_MSG_DISCONNECT; off = off + 1;
    off = wb_be32(msg, off, reason);
    off = wb_cstr(msg, off, desc);
    off = wb_cstr(msg, off, "");          /* language */
    bpp_send(msg, off);
}

int wait_for_channel_open() {
    char *pl; int pllen;
    while (1) {
        if (bpp_recv(&pl, &pllen) != 0) return -1;
        if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_DEBUG) continue;
        if (pl[0] == SSH_MSG_GLOBAL_REQUEST) {
            /* Ignore; reply with REQUEST_FAILURE if want_reply. */
            int off2 = 1;
            int nl = get_be32(pl + off2); off2 = off2 + 4 + nl;
            int want = pl[off2] & 0xFF;
            if (want) {
                char rep[1]; rep[0] = SSH_MSG_REQUEST_FAILURE;
                bpp_send(rep, 1);
            }
            continue;
        }
        if (pl[0] == SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
            remote_chan = get_be32(pl + 5);
            tx_window = get_be32(pl + 9);
            remote_max_packet = get_be32(pl + 13);
            serial_printf("[ssh] channel open remote=%d tx_window=%d max_packet=%d\n",
                          remote_chan, tx_window, remote_max_packet);
            return 0;
        }
        if (pl[0] == SSH_MSG_CHANNEL_OPEN_FAILURE) return -1;
        if (pl[0] == SSH_MSG_DISCONNECT) return -1;
    }
}

int wait_for_request_reply() {
    char *pl; int pllen;
    while (1) {
        if (bpp_recv(&pl, &pllen) != 0) return -1;
        if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_DEBUG) continue;
        if (pl[0] == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
            tx_window = tx_window + get_be32(pl + 5);
            continue;
        }
        if (pl[0] == SSH_MSG_CHANNEL_SUCCESS) return 0;
        if (pl[0] == SSH_MSG_CHANNEL_FAILURE) return -1;
        if (pl[0] == SSH_MSG_DISCONNECT) return -1;
        /* Unexpected — bail. */
        return -1;
    }
}

/* ----- VT100/ANSI passthrough renderer -----
 *
 * The CupidOS GUI renderer (shell_gui_putchar_ext) already understands
 * VT100/ANSI escape sequences (CSI move/erase, SGR colors, etc) — see
 * kernel/lang/ansi.* and kernel/core/kernel.c. So the cupidc client
 * acts as a thin passthrough: every byte the server sends goes
 * directly to print_n, which feeds shell_gui_putchar_ext.
 *
 * The cupidc-side state machine therefore does only one thing: drops
 * the C0 codes that the local terminal can't handle gracefully (BEL
 * → ignore). Everything else passes through verbatim. */
void render_bytes(char *buf, int len) {
    /* Pass through directly; print_n loops shell_gui_putchar_ext which
     * is connected to the same ANSI handler the local shell uses. */
    if (ssh_debug_render) debug_dump_bytes("rx-render", buf, len);
    print_n(buf, len);
}

/* ----- main I/O loop ----- */

int io_loop() {
    char *pl; int pllen;
    char keybuf[16];
    int last_cols;
    int last_rows;

    get_screen_size(&last_cols, &last_rows);

    while (channel_open && !channel_eof) {
        int cur_cols;
        int cur_rows;
        get_screen_size(&cur_cols, &cur_rows);
        if (cur_cols != last_cols || cur_rows != last_rows) {
            send_window_change(cur_cols, cur_rows);
            last_cols = cur_cols;
            last_rows = cur_rows;
        }

        /* Receive whatever is available. */
        while (sock_avail(fd) > 0) {
            if (bpp_recv(&pl, &pllen) != 0) {
                channel_open = 0;
                break;
            }
            int t = pl[0] & 0xFF;
            if (t == SSH_MSG_IGNORE || t == SSH_MSG_DEBUG) continue;
            if (t == SSH_MSG_CHANNEL_DATA) {
                int dl = get_be32(pl + 5);
                serial_printf("[ssh-debug] rx channel-data pllen=%d data=%d rx_window_before=%d\n",
                              pllen, dl, rx_window);
                render_bytes(pl + 9, dl);
                rx_window = rx_window - dl;
                serial_printf("[ssh-debug] rx channel-data consumed=%d rx_window_after=%d\n",
                              dl, rx_window);
                if (rx_window < 65536) send_window_adjust(1048576);
                continue;
            }
            if (t == SSH_MSG_CHANNEL_EXTENDED_DATA) {
                int dl = get_be32(pl + 9);
                serial_printf("[ssh-debug] rx extended-data pllen=%d data=%d rx_window_before=%d\n",
                              pllen, dl, rx_window);
                render_bytes(pl + 13, dl);
                rx_window = rx_window - dl;
                serial_printf("[ssh-debug] rx extended-data consumed=%d rx_window_after=%d\n",
                              dl, rx_window);
                if (rx_window < 65536) send_window_adjust(1048576);
                continue;
            }
            if (t == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
                tx_window = tx_window + get_be32(pl + 5);
                serial_printf("[ssh] channel window adjust tx_window=%d\n", tx_window);
                continue;
            }
            if (t == SSH_MSG_CHANNEL_EOF) { channel_eof = 1; continue; }
            if (t == SSH_MSG_CHANNEL_CLOSE) {
                send_channel_close();
                channel_open = 0;
                break;
            }
            if (t == SSH_MSG_CHANNEL_REQUEST) {
                /* Server-initiated channel request — exit-status etc.
                 * Ignore; reply FAILURE if want_reply. */
                int off2 = 1 + 4;
                int nl = get_be32(pl + off2); off2 = off2 + 4 + nl;
                int want = pl[off2] & 0xFF;
                if (want) {
                    char r[5]; r[0] = SSH_MSG_CHANNEL_FAILURE;
                    put_be32(r + 1, remote_chan);
                    bpp_send(r, 5);
                }
                continue;
            }
            if (t == SSH_MSG_DISCONNECT) { channel_open = 0; break; }
            if (t == SSH_MSG_GLOBAL_REQUEST) {
                /* might want_reply */
                int off2 = 1;
                int nl = get_be32(pl + off2); off2 = off2 + 4 + nl;
                int want = pl[off2] & 0xFF;
                if (want) {
                    char r[1]; r[0] = SSH_MSG_REQUEST_FAILURE;
                    bpp_send(r, 1);
                }
                continue;
            }
        }

        /* Drain available keyboard events. */
        while (1) {
            int n = poll_key_vt(keybuf);
            if (n <= 0) break;
            debug_dump_bytes("tx-key", keybuf, n);
            if (tx_window > 0) send_channel_data(keybuf, n);
            else serial_printf("[ssh-debug] tx key waiting for window len=%d\n", n);
        }

        yield();
    }
    return 0;
}

/* ----- argument + IP parsing ----- */

int parse_ip(char *s, U32 *out) {
    int parts[4];
    int p = 0;
    int i = 0;
    while (1) {
        int n = 0;
        int has = 0;
        while (s[i] >= '0' && s[i] <= '9') { n = n * 10 + (s[i] - '0'); has = 1; i = i + 1; }
        if (!has) return -1;
        if (n < 0 || n > 255) return -1;
        parts[p] = n; p = p + 1;
        if (s[i] == 0) break;
        if (s[i] != '.') return -1;
        if (p >= 4) return -1;
        i = i + 1;
    }
    if (p != 4) return -1;
    U32 ip = ((parts[0] & 0xFF) << 24)
           | ((parts[1] & 0xFF) << 16)
           | ((parts[2] & 0xFF) << 8)
           |  (parts[3] & 0xFF);
    *out = htonl(ip);
    return 0;
}

int parse_target(char *arg, char *user_out, int user_cap,
                 char *host_out, int host_cap, int *port_out) {
    /* user defaults to "root". */
    user_out[0] = 'r'; user_out[1] = 'o'; user_out[2] = 'o'; user_out[3] = 't'; user_out[4] = 0;
    *port_out = SSH_PORT;
    int i = 0;
    /* Find '@'. */
    int at = -1;
    while (arg[i]) {
        if (arg[i] == '@') { at = i; break; }
        i = i + 1;
    }
    int hstart = 0;
    if (at >= 0) {
        if (at >= user_cap) return -1;
        int j = 0; while (j < at) { user_out[j] = arg[j]; j = j + 1; } user_out[j] = 0;
        hstart = at + 1;
    }
    /* Copy host until ':' or end. */
    i = hstart;
    int h = 0;
    while (arg[i] && arg[i] != ':' && h < host_cap - 1) {
        host_out[h] = arg[i]; h = h + 1; i = i + 1;
    }
    host_out[h] = 0;
    if (arg[i] == ':') {
        i = i + 1;
        int p = 0;
        while (arg[i] >= '0' && arg[i] <= '9') {
            p = p * 10 + (arg[i] - '0'); i = i + 1;
        }
        if (p <= 0 || p > 65535) return -1;
        *port_out = p;
    }
    return 0;
}

/* ----- main ----- */

void main() {
    serial_printf("[ssh] main entered\n");
    const char *args = get_args();
    serial_printf("[ssh] args=%p '%s'\n", args, args ? args : "<null>");
    if (!args || !args[0]) {
        println("Usage: ssh [user@]host[:port]");
        return;
    }
    serial_printf("[ssh] args ok\n");

    /* args is the rest of the command line — strip leading space. */
    int ai = 0;
    while (args[ai] == ' ' || args[ai] == 9) ai = ai + 1;
    char target[256];
    int ti = 0;
    while (args[ai] && args[ai] != ' ' && ti < 255) {
        target[ti] = args[ai]; ti = ti + 1; ai = ai + 1;
    }
    target[ti] = 0;
    serial_printf("[ssh] target='%s' ti=%d\n", target, ti);
    if (ti == 0) {
        println("Usage: ssh [user@]host[:port]");
        return;
    }

    char user[64]; char host[128]; int port = 22;
    if (parse_target(target, user, 64, host, 128, &port) != 0) {
        println("ssh: bad target");
        return;
    }
    serial_printf("[ssh] user='%s' host='%s' port=%d\n", user, host, port);

    /* Resolve to IP. parse_ip and dns_resolve both return network byte
     * order suitable for socket_connect. */
    U32 ip = 0;
    if (parse_ip(host, &ip) != 0) {
        if (dns_resolve(host, &ip) != 0) {
            print("ssh: cannot resolve ");
            println(host);
            return;
        }
    }

    print("ssh: connecting to ");
    print(host);
    print(":");
    print_int(port);
    println("");

    /* Allocate big buffers. */
    rxbuf  = kmalloc(BUF_CAP);
    txbuf  = kmalloc(BUF_CAP);
    pkt_pl = kmalloc(BUF_CAP);
    mac_scratch = kmalloc(32772);
    if (!rxbuf || !txbuf || !pkt_pl || !mac_scratch) { println("ssh: oom"); return; }

    /* Initialize sequence numbers. */
    zero_bytes(seq_c2s, 8);
    zero_bytes(seq_s2c, 8);

    serial_printf("[ssh] socket+connect\n");
    fd = socket(SOCK_TCP);
    if (fd < 0) { print_err("socket"); return; }
    if (connect(fd, ip, htons(port)) != 0) {
        print_err("connect failed");
        close(fd);
        return;
    }

    serial_printf("[ssh] tcp connected, fd=%d\n", fd);
    if (send_banner() != 0) { print_err("banner send"); close(fd); return; }
    serial_printf("[ssh] banner sent\n");
    if (recv_banner() != 0) { print_err("banner recv"); close(fd); return; }
    serial_printf("[ssh] banner recv, V_S_len=%d\n", V_S_len);
    print("ssh: remote banner: ");
    print_n(V_S, V_S_len);
    println("");

    serial_printf("[ssh] kexinit start\n");
    if (do_kexinit() != 0) { close(fd); return; }
    serial_printf("[ssh] ecdh start\n");
    if (do_ecdh() != 0)    { close(fd); return; }
    serial_printf("[ssh] newkeys start\n");
    if (do_newkeys() != 0) { print_err("newkeys"); close(fd); return; }
    serial_printf("[ssh] handshake done\n");

    println("ssh: handshake complete, authenticating...");
    if (do_userauth(user) != 0) { print_err("authentication failed"); close(fd); return; }
    println("ssh: authenticated.");

    /* Open channel, allocate pty, request shell. */
    local_chan = 0;
    send_channel_open();
    if (wait_for_channel_open() != 0) { print_err("channel open failed"); close(fd); return; }

    int cols = 80, rows = 25;
    get_screen_size(&cols, &rows);
    serial_printf("[ssh] pty: cols=%d rows=%d\n", cols, rows);
    send_pty_req(cols, rows);
    if (wait_for_request_reply() != 0) { print_err("pty-req refused"); send_disconnect(11, "by user"); close(fd); return; }

    send_shell_req();
    if (wait_for_request_reply() != 0) { print_err("shell request refused"); send_disconnect(11, "by user"); close(fd); return; }

    channel_open = 1;
    io_loop();

    send_disconnect(11, "client exiting");
    close(fd);
    println("");
    println("Connection closed.");
}

main();
