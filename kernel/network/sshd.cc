/* Minimal OpenSSH-compatible SSH-2 server for CupidOS.
 *
 * Supported v1 surface:
 *   kex:      curve25519-sha256
 *   hostkey:  ecdsa-sha2-nistp256
 *   cipher:   chacha20-poly1305@openssh.com
 *   auth:     root/password
 *   channels: session shell and exec
*/

#include "sshd.h"
#include "socket.h"
#include "process.h"
#include "memory.h"
#include "string.h"
#include "serial.h"
#include "shell.h"
#include "vfs.h"
#include "timer.h"
#include "csprng.h"
#include "sha256.h"
#include "hmac.h"
#include "hkdf.h"
#include "x25519.h"
#include "chacha20.h"
#include "poly1305.h"
#include "p256.h"
#include "ecdsa.h"

enum {
    SSH_MSG_DISCONNECT                = 1,
    SSH_MSG_IGNORE                    = 2,
    SSH_MSG_UNIMPLEMENTED             = 3,
    SSH_MSG_SERVICE_REQUEST           = 5,
    SSH_MSG_SERVICE_ACCEPT            = 6,
    SSH_MSG_KEXINIT                   = 20,
    SSH_MSG_NEWKEYS                   = 21,
    SSH_MSG_KEX_ECDH_INIT             = 30,
    SSH_MSG_KEX_ECDH_REPLY            = 31,
    SSH_MSG_USERAUTH_REQUEST          = 50,
    SSH_MSG_USERAUTH_FAILURE          = 51,
    SSH_MSG_USERAUTH_SUCCESS          = 52,
    SSH_MSG_GLOBAL_REQUEST            = 80,
    SSH_MSG_REQUEST_SUCCESS           = 81,
    SSH_MSG_REQUEST_FAILURE           = 82,
    SSH_MSG_CHANNEL_OPEN              = 90,
    SSH_MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    SSH_MSG_CHANNEL_OPEN_FAILURE      = 92,
    SSH_MSG_CHANNEL_WINDOW_ADJUST     = 93,
    SSH_MSG_CHANNEL_DATA              = 94,
    SSH_MSG_CHANNEL_EOF               = 96,
    SSH_MSG_CHANNEL_CLOSE             = 97,
    SSH_MSG_CHANNEL_REQUEST           = 98,
    SSH_MSG_CHANNEL_SUCCESS           = 99,
    SSH_MSG_CHANNEL_FAILURE           = 100,

    SSH_BUF_CAP       = 65536,
    SSH_PAYLOAD_MAX   = 32768,
    SSH_WINDOW        = 1048576,
    SSH_MAX_PACKET    = 32768,
    SSH_LINE_MAX      = 512
};

typedef struct ssh_session {
    int fd;
    uint32_t peer_ip;
    uint16_t peer_port;

    uint8_t seq_in[8];
    uint8_t seq_out[8];
    uint8_t key_c2s_main[32];
    uint8_t key_c2s_hdr[32];
    uint8_t key_s2c_main[32];
    uint8_t key_s2c_hdr[32];
    int newkeys_in;
    int newkeys_out;

    uint8_t shared_K[32];
    uint8_t session_id[32];
    uint8_t H[32];

    char V_C[256];
    int V_C_len;
    char V_S[64];
    int V_S_len;
    uint8_t *I_C;
    int I_C_len;
    uint8_t *I_S;
    int I_S_len;

    uint8_t *rx;
    uint8_t *tx;
    uint8_t *pkt;
    uint8_t *mac;

    uint32_t client_chan;
    uint32_t server_chan;
    uint32_t client_window;
    uint32_t client_max_packet;
    int channel_open;
} ssh_session_t;

static volatile int g_sshd_running;
static int g_listen_fd = -1;
static uint16_t g_port = 22;
static uint32_t g_listener_pid;
static uint8_t g_host_priv[32];
static uint8_t g_host_pub[65];
static int g_host_ready;

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFFu);
    p[1] = (uint8_t)((v >> 16) & 0xFFu);
    p[2] = (uint8_t)((v >> 8) & 0xFFu);
    p[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void seq_inc(uint8_t seq[8]) {
    int i;
    for (i = 7; i >= 0; i--) {
        seq[i] = (uint8_t)(seq[i] + 1u);
        if (seq[i] != 0u) return;
    }
}

static int cstreqn(const uint8_t *p, uint32_t n, const char *s) {
    return strlen(s) == n && memcmp(p, s, n) == 0;
}

static int wb_byte(uint8_t *out, int off, uint8_t v) {
    out[off] = v;
    return off + 1;
}

static int wb_bool(uint8_t *out, int off, int v) {
    out[off] = v ? 1u : 0u;
    return off + 1;
}

static int wb_be32(uint8_t *out, int off, uint32_t v) {
    put_be32(out + off, v);
    return off + 4;
}

static int wb_bytes(uint8_t *out, int off, const void *src, uint32_t n) {
    memcpy(out + off, src, n);
    return off + (int)n;
}

static int wb_string(uint8_t *out, int off, const void *src, uint32_t n) {
    off = wb_be32(out, off, n);
    return wb_bytes(out, off, src, n);
}

static int wb_cstr(uint8_t *out, int off, const char *s) {
    return wb_string(out, off, s, (uint32_t)strlen(s));
}

static int wb_mpint32(uint8_t *out, int off, const uint8_t be32[32]) {
    uint32_t start = 0;
    while (start < 32u && be32[start] == 0u) start++;
    if (start == 32u) return wb_be32(out, off, 0u);
    if (be32[start] & 0x80u) {
        off = wb_be32(out, off, 33u - start);
        out[off++] = 0u;
    } else {
        off = wb_be32(out, off, 32u - start);
    }
    return wb_bytes(out, off, be32 + start, 32u - start);
}

static int read_string(const uint8_t *p, int len, int *off,
                       const uint8_t **out, uint32_t *out_len) {
    uint32_t n;
    if (*off + 4 > len) return -1;
    n = get_be32(p + *off);
    *off += 4;
    if (n > (uint32_t)(len - *off)) return -1;
    *out = p + *off;
    *out_len = n;
    *off += (int)n;
    return 0;
}

static int tcp_send_all(int fd, const uint8_t *buf, uint32_t len) {
    uint32_t sent = 0;
    while (sent < len) {
        int r = socket_send(fd, buf + sent, len - sent);
        if (r < 0) return -1;
        if (r == 0) process_yield();
        else sent += (uint32_t)r;
    }
    return 0;
}

static int tcp_recv_exact(int fd, uint8_t *buf, uint32_t len) {
    uint32_t got = 0;
    while (got < len) {
        int r;
        if (socket_state(fd) != TCPS_ESTABLISHED && socket_avail(fd) <= 0)
            return -1;
        if (socket_avail(fd) <= 0) {
            process_yield();
            continue;
        }
        r = socket_recv(fd, buf + got, len - got);
        if (r <= 0) return -1;
        got += (uint32_t)r;
    }
    return 0;
}

static void build_nonce(uint8_t nonce[12], const uint8_t seq[8]) {
    memset(nonce, 0, 4u);
    memcpy(nonce + 4, seq, 8u);
}

static int bpp_send(ssh_session_t *s, const uint8_t *payload, uint32_t plen) {
    uint32_t block = 8;
    uint32_t padlen;
    uint32_t inner;
    uint32_t wire_len;

    if (s->newkeys_out) {
        uint8_t nonce[12];
        uint8_t poly_key[64];
        uint8_t tag[16];

        padlen = block - ((1u + plen) % block);
        if (padlen < 4u) padlen += block;
        inner = 1u + plen + padlen;
        wire_len = 4u + inner + 16u;
        if (wire_len > SSH_BUF_CAP) return -1;

        put_be32(s->tx, inner);
        s->tx[4] = (uint8_t)padlen;
        memcpy(s->tx + 5, payload, plen);
        crypto_random_bytes(s->tx + 5 + plen, padlen);

        build_nonce(nonce, s->seq_out);
        chacha20_xor(s->key_s2c_hdr, 0u, nonce, s->tx, s->tx, 4u);
        memset(poly_key, 0, sizeof(poly_key));
        chacha20_xor(s->key_s2c_main, 0u, nonce, poly_key, poly_key, 64u);
        chacha20_xor(s->key_s2c_main, 1u, nonce, s->tx + 4, s->tx + 4, inner);
        poly1305_auth(tag, s->tx, 4u + inner, poly_key);
        memcpy(s->tx + 4u + inner, tag, 16u);
    } else {
        padlen = block - ((5u + plen) % block);
        if (padlen < 4u) padlen += block;
        inner = 1u + plen + padlen;
        wire_len = 4u + inner;
        if (wire_len > SSH_BUF_CAP) return -1;
        put_be32(s->tx, inner);
        s->tx[4] = (uint8_t)padlen;
        memcpy(s->tx + 5, payload, plen);
        crypto_random_bytes(s->tx + 5 + plen, padlen);
    }
    if (tcp_send_all(s->fd, s->tx, wire_len) != 0) return -1;
    seq_inc(s->seq_out);
    return 0;
}

static int bpp_recv(ssh_session_t *s, uint8_t **payload, int *plen) {
    uint8_t nonce[12];
    uint32_t inner;
    uint32_t padlen;
    uint32_t out_len;

    build_nonce(nonce, s->seq_in);
    if (s->newkeys_in) {
        uint8_t len_enc[4];
        uint8_t len_clear[4];
        uint8_t poly_key[64];
        uint8_t tag[16];

        if (tcp_recv_exact(s->fd, len_enc, 4u) != 0) return -1;
        chacha20_xor(s->key_c2s_hdr, 0u, nonce, len_enc, len_clear, 4u);
        inner = get_be32(len_clear);
        if (inner < 5u || inner > SSH_PAYLOAD_MAX) return -2;
        if (tcp_recv_exact(s->fd, s->rx, inner + 16u) != 0) return -1;

        memset(poly_key, 0, sizeof(poly_key));
        chacha20_xor(s->key_c2s_main, 0u, nonce, poly_key, poly_key, 64u);
        memcpy(s->mac, len_enc, 4u);
        memcpy(s->mac + 4, s->rx, inner);
        poly1305_auth(tag, s->mac, 4u + inner, poly_key);
        if (!poly1305_verify(tag, s->rx + inner)) return -2;

        chacha20_xor(s->key_c2s_main, 1u, nonce, s->rx, s->rx, inner);
        padlen = s->rx[0];
        if (padlen + 1u >= inner) return -2;
        out_len = inner - 1u - padlen;
        memcpy(s->pkt, s->rx + 1, out_len);
    } else {
        uint8_t hdr[5];
        if (tcp_recv_exact(s->fd, hdr, 5u) != 0) return -1;
        inner = get_be32(hdr);
        padlen = hdr[4];
        if (inner < 1u + padlen + 1u || inner > SSH_PAYLOAD_MAX) return -2;
        out_len = inner - 1u - padlen;
        if (tcp_recv_exact(s->fd, s->rx, inner - 1u) != 0) return -1;
        memcpy(s->pkt, s->rx, out_len);
    }
    seq_inc(s->seq_in);
    *payload = s->pkt;
    *plen = (int)out_len;
    return 0;
}

static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void hexenc(const uint8_t *in, uint32_t len, char *out) {
    static const char h[] = "0123456789abcdef";
    uint32_t i;
    for (i = 0; i < len; i++) {
        out[i * 2u] = h[in[i] >> 4];
        out[i * 2u + 1u] = h[in[i] & 15u];
    }
    out[len * 2u] = 0;
}

static int hexdec(const char *in, uint8_t *out, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) {
        int a = hexval(in[i * 2u]);
        int b = hexval(in[i * 2u + 1u]);
        if (a < 0 || b < 0) return -1;
        out[i] = (uint8_t)((a << 4) | b);
    }
    return 0;
}

static int compute_host_pub(void) {
    p256_scalar_t d;
    p256_aff_t G, P;
    p256_jac_t J;
    if (p256_scalar_from_be(d, g_host_priv) != 0 || p256_scalar_iszero(d))
        return -1;
    p256_fe_copy(G.x, P256_GX);
    p256_fe_copy(G.y, P256_GY);
    G.infinity = 0;
    p256_scalar_mul_point(&J, d, &G);
    if (p256_jac_is_infinity(&J)) return -1;
    p256_jac_to_affine(&P, &J);
    g_host_pub[0] = 0x04u;
    p256_fe_to_be(g_host_pub + 1, P.x);
    p256_fe_to_be(g_host_pub + 33, P.y);
    return 0;
}

static int save_host_key(void) {
    char hex[65];
    int fd;
    vfs_mkdir("/home/etc");
    vfs_mkdir("/home/etc/ssh");
    hexenc(g_host_priv, 32u, hex);
    fd = vfs_open("/home/etc/ssh/ssh_host_ecdsa_key", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    vfs_write(fd, hex, 64u);
    vfs_write(fd, "\n", 1u);
    vfs_close(fd);
    return 0;
}

static int load_or_create_host_key(void) {
    char buf[96];
    int fd;
    if (g_host_ready) return 0;
    fd = vfs_open("/home/etc/ssh/ssh_host_ecdsa_key", O_RDONLY);
    if (fd >= 0) {
        int n = vfs_read(fd, buf, sizeof(buf) - 1u);
        vfs_close(fd);
        if (n >= 64) {
            buf[n] = 0;
            if (hexdec(buf, g_host_priv, 32u) == 0 && compute_host_pub() == 0) {
                g_host_ready = 1;
                return 0;
            }
        }
    }

    do {
        crypto_random_bytes(g_host_priv, 32u);
    } while (compute_host_pub() != 0);
    if (save_host_key() != 0) {
        serial_printf("[sshd] warning: host key is ephemeral; cannot write /home/etc/ssh\n");
    }
    g_host_ready = 1;
    return 0;
}

static int hostkey_blob(uint8_t *out) {
    int off = 0;
    off = wb_cstr(out, off, "ecdsa-sha2-nistp256");
    off = wb_cstr(out, off, "nistp256");
    off = wb_string(out, off, g_host_pub, 65u);
    return off;
}

static int build_kexinit(uint8_t *out) {
    int off = 0;
    out[off++] = SSH_MSG_KEXINIT;
    crypto_random_bytes(out + off, 16u); off += 16;
    off = wb_cstr(out, off, "curve25519-sha256");
    off = wb_cstr(out, off, "ecdsa-sha2-nistp256");
    off = wb_cstr(out, off, "chacha20-poly1305@openssh.com");
    off = wb_cstr(out, off, "chacha20-poly1305@openssh.com");
    off = wb_cstr(out, off, "hmac-sha2-256");
    off = wb_cstr(out, off, "hmac-sha2-256");
    off = wb_cstr(out, off, "none");
    off = wb_cstr(out, off, "none");
    off = wb_cstr(out, off, "");
    off = wb_cstr(out, off, "");
    off = wb_bool(out, off, 0);
    off = wb_be32(out, off, 0);
    return off;
}

static void derive_key(ssh_session_t *s, char letter, uint8_t *out, int out_len) {
    uint8_t Kbuf[48];
    uint8_t stage[256];
    uint8_t block[32];
    int koff = wb_mpint32(Kbuf, 0, s->shared_K);
    int produced = 0;
    int off, take;

    off = 0;
    off = wb_bytes(stage, off, Kbuf, (uint32_t)koff);
    off = wb_bytes(stage, off, s->H, 32u);
    stage[off++] = (uint8_t)letter;
    off = wb_bytes(stage, off, s->session_id, 32u);
    sha256(stage, (uint32_t)off, block);
    take = out_len < 32 ? out_len : 32;
    memcpy(out, block, (uint32_t)take);
    produced = take;

    while (produced < out_len) {
        off = 0;
        off = wb_bytes(stage, off, Kbuf, (uint32_t)koff);
        off = wb_bytes(stage, off, s->H, 32u);
        off = wb_bytes(stage, off, out, (uint32_t)produced);
        sha256(stage, (uint32_t)off, block);
        take = out_len - produced;
        if (take > 32) take = 32;
        memcpy(out + produced, block, (uint32_t)take);
        produced += take;
    }
}

static void derive_keys(ssh_session_t *s) {
    uint8_t c[64], d[64];
    derive_key(s, 'C', c, 64);
    derive_key(s, 'D', d, 64);
    memcpy(s->key_c2s_main, c, 32u);
    memcpy(s->key_c2s_hdr, c + 32, 32u);
    memcpy(s->key_s2c_main, d, 32u);
    memcpy(s->key_s2c_hdr, d + 32, 32u);
}

static int send_disconnect(ssh_session_t *s, const char *msg) {
    uint8_t p[256];
    int off = 0;
    off = wb_byte(p, off, SSH_MSG_DISCONNECT);
    off = wb_be32(p, off, 11u);
    off = wb_cstr(p, off, msg);
    off = wb_cstr(p, off, "");
    return bpp_send(s, p, (uint32_t)off);
}

static int read_client_banner(ssh_session_t *s) {
    int n = 0;
    while (n < (int)sizeof(s->V_C) - 1) {
        uint8_t c;
        if (tcp_recv_exact(s->fd, &c, 1u) != 0) return -1;
        if (c == '\n') break;
        if (c != '\r') s->V_C[n++] = (char)c;
    }
    s->V_C[n] = 0;
    s->V_C_len = n;
    return (n >= 4 && s->V_C[0] == 'S' && s->V_C[1] == 'S' &&
            s->V_C[2] == 'H' && s->V_C[3] == '-') ? 0 : -1;
}

static int do_kex(ssh_session_t *s) {
    uint8_t server_kex_priv[32];
    uint8_t server_kex_pub[32];
    uint8_t client_kex_pub[32];
    uint8_t base[32];
    uint8_t *pl;
    int pllen;
    int off;
    const uint8_t *q_c;
    uint32_t q_c_len;
    uint8_t *ks;
    int ks_len;
    uint8_t sig_r[32], sig_s[32];
    uint8_t sig_inner[96], sig_blob[160];
    int sig_inner_len, sig_blob_len;
    uint8_t *Hin;
    int hoff;
    uint8_t msg[512];

    s->V_S_len = (int)strlen("SSH-2.0-CupidOS_ssh_0.1");
    memcpy(s->V_S, "SSH-2.0-CupidOS_ssh_0.1", (uint32_t)s->V_S_len + 1u);
    if (tcp_send_all(s->fd, (const uint8_t *)"SSH-2.0-CupidOS_ssh_0.1\r\n",
                     (uint32_t)strlen("SSH-2.0-CupidOS_ssh_0.1\r\n")) != 0)
        return -1;
    if (read_client_banner(s) != 0) return -1;

    s->I_S_len = build_kexinit(s->tx);
    s->I_S = kmalloc((uint32_t)s->I_S_len);
    if (!s->I_S) return -1;
    memcpy(s->I_S, s->tx, (uint32_t)s->I_S_len);
    if (bpp_send(s, s->I_S, (uint32_t)s->I_S_len) != 0) return -1;

    if (bpp_recv(s, &pl, &pllen) != 0 || pllen < 1 || pl[0] != SSH_MSG_KEXINIT)
        return -1;
    s->I_C = kmalloc((uint32_t)pllen);
    if (!s->I_C) return -1;
    s->I_C_len = pllen;
    memcpy(s->I_C, pl, (uint32_t)pllen);

    if (bpp_recv(s, &pl, &pllen) != 0 || pllen < 1 || pl[0] != SSH_MSG_KEX_ECDH_INIT)
        return -1;
    off = 1;
    if (read_string(pl, pllen, &off, &q_c, &q_c_len) != 0 || q_c_len != 32u)
        return -1;
    memcpy(client_kex_pub, q_c, 32u);

    crypto_random_bytes(server_kex_priv, 32u);
    server_kex_priv[0] &= 248u;
    server_kex_priv[31] = (uint8_t)((server_kex_priv[31] & 127u) | 64u);
    memset(base, 0, sizeof(base));
    base[0] = 9u;
    x25519(server_kex_pub, server_kex_priv, base);
    x25519(s->shared_K, server_kex_priv, client_kex_pub);

    ks = kmalloc(256u);
    if (!ks) return -1;
    ks_len = hostkey_blob(ks);

    Hin = kmalloc(8192u);
    if (!Hin) { kfree(ks); return -1; }
    hoff = 0;
    hoff = wb_string(Hin, hoff, s->V_C, (uint32_t)s->V_C_len);
    hoff = wb_string(Hin, hoff, s->V_S, (uint32_t)s->V_S_len);
    hoff = wb_string(Hin, hoff, s->I_C, (uint32_t)s->I_C_len);
    hoff = wb_string(Hin, hoff, s->I_S, (uint32_t)s->I_S_len);
    hoff = wb_string(Hin, hoff, ks, (uint32_t)ks_len);
    hoff = wb_string(Hin, hoff, client_kex_pub, 32u);
    hoff = wb_string(Hin, hoff, server_kex_pub, 32u);
    hoff = wb_mpint32(Hin, hoff, s->shared_K);
    sha256(Hin, (uint32_t)hoff, s->H);
    memcpy(s->session_id, s->H, 32u);
    kfree(Hin);

    {
        uint8_t hhash[32];
        sha256(s->H, 32u, hhash);
        if (ecdsa_p256_sign(g_host_priv, hhash, 32u, sig_r, sig_s) != 0) {
            kfree(ks);
            return -1;
        }
    }
    sig_inner_len = 0;
    sig_inner_len = wb_mpint32(sig_inner, sig_inner_len, sig_r);
    sig_inner_len = wb_mpint32(sig_inner, sig_inner_len, sig_s);
    sig_blob_len = 0;
    sig_blob_len = wb_cstr(sig_blob, sig_blob_len, "ecdsa-sha2-nistp256");
    sig_blob_len = wb_string(sig_blob, sig_blob_len, sig_inner, (uint32_t)sig_inner_len);

    off = 0;
    off = wb_byte(msg, off, SSH_MSG_KEX_ECDH_REPLY);
    off = wb_string(msg, off, ks, (uint32_t)ks_len);
    off = wb_string(msg, off, server_kex_pub, 32u);
    off = wb_string(msg, off, sig_blob, (uint32_t)sig_blob_len);
    kfree(ks);
    if (bpp_send(s, msg, (uint32_t)off) != 0) return -1;

    msg[0] = SSH_MSG_NEWKEYS;
    if (bpp_send(s, msg, 1u) != 0) return -1;
    derive_keys(s);
    s->newkeys_out = 1;

    if (bpp_recv(s, &pl, &pllen) != 0 || pllen < 1 || pl[0] != SSH_MSG_NEWKEYS)
        return -1;
    s->newkeys_in = 1;
    return 0;
}

static void hash_password(const uint8_t salt[16], const char *pass, uint8_t out[32]) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, salt, 16u);
    sha256_update(&ctx, (const uint8_t *)pass, (uint32_t)strlen(pass));
    sha256_final(&ctx, out);
}

int sshd_set_password(const char *password) {
    uint8_t salt[16], digest[32];
    char salt_hex[33], digest_hex[65];
    char line[128];
    int fd;
    if (!password || !password[0]) return -1;
    vfs_mkdir("/home/etc");
    vfs_mkdir("/home/etc/ssh");
    crypto_random_bytes(salt, 16u);
    hash_password(salt, password, digest);
    hexenc(salt, 16u, salt_hex);
    hexenc(digest, 32u, digest_hex);
    strcpy(line, "root:");
    strcat(line, salt_hex);
    strcat(line, ":");
    strcat(line, digest_hex);
    strcat(line, "\n");
    fd = vfs_open("/home/etc/ssh/passwd", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;
    vfs_write(fd, line, (uint32_t)strlen(line));
    vfs_close(fd);
    return 0;
}

static int check_password(const char *user, const uint8_t *pass, uint32_t pass_len) {
    char pass_buf[128];
    char file[160];
    int fd, n;
    if (!cstreqn((const uint8_t *)user, (uint32_t)strlen(user), "root"))
        return 0;
    if (pass_len >= sizeof(pass_buf)) return 0;
    memcpy(pass_buf, pass, pass_len);
    pass_buf[pass_len] = 0;

    fd = vfs_open("/home/etc/ssh/passwd", O_RDONLY);
    if (fd < 0) {
        return strcmp(pass_buf, "cupid") == 0;
    }
    n = vfs_read(fd, file, sizeof(file) - 1u);
    vfs_close(fd);
    if (n < 104) return 0;
    file[n] = 0;
    if (strncmp(file, "root:", 5u) == 0) {
        uint8_t salt[16], expected[32], actual[32];
        if (hexdec(file + 5, salt, 16u) != 0) return 0;
        if (file[37] != ':') return 0;
        if (hexdec(file + 38, expected, 32u) != 0) return 0;
        hash_password(salt, pass_buf, actual);
        return memcmp(expected, actual, 32u) == 0;
    }
    return 0;
}

static int do_auth(ssh_session_t *s) {
    uint8_t *pl;
    int pllen;
    int tries = 0;

    for (;;) {
        if (bpp_recv(s, &pl, &pllen) != 0 || pllen < 1) return -1;
        if (pl[0] == SSH_MSG_IGNORE) continue;
        if (pl[0] == SSH_MSG_SERVICE_REQUEST) {
            int off = 1;
            const uint8_t *svc;
            uint32_t svc_len;
            uint8_t resp[64];
            int roff = 0;
            if (read_string(pl, pllen, &off, &svc, &svc_len) != 0) return -1;
            if (!cstreqn(svc, svc_len, "ssh-userauth")) return -1;
            roff = wb_byte(resp, roff, SSH_MSG_SERVICE_ACCEPT);
            roff = wb_cstr(resp, roff, "ssh-userauth");
            if (bpp_send(s, resp, (uint32_t)roff) != 0) return -1;
            break;
        }
    }

    while (tries++ < 3) {
        int off;
        const uint8_t *user, *svc, *method, *pass;
        uint32_t user_len, svc_len, method_len, pass_len;
        uint8_t resp[128];
        int roff;
        if (bpp_recv(s, &pl, &pllen) != 0 || pllen < 1) return -1;
        if (pl[0] != SSH_MSG_USERAUTH_REQUEST) continue;
        off = 1;
        if (read_string(pl, pllen, &off, &user, &user_len) != 0) return -1;
        if (read_string(pl, pllen, &off, &svc, &svc_len) != 0) return -1;
        if (read_string(pl, pllen, &off, &method, &method_len) != 0) return -1;
        if (!cstreqn(svc, svc_len, "ssh-connection")) return -1;
        if (cstreqn(method, method_len, "password")) {
            char user_buf[32];
            if (off >= pllen) return -1;
            off++; /* FALSE: password change request */
            if (read_string(pl, pllen, &off, &pass, &pass_len) != 0) return -1;
            if (user_len < sizeof(user_buf)) {
                memcpy(user_buf, user, user_len);
                user_buf[user_len] = 0;
                if (check_password(user_buf, pass, pass_len)) {
                    resp[0] = SSH_MSG_USERAUTH_SUCCESS;
                    return bpp_send(s, resp, 1u);
                }
            }
        }

        roff = 0;
        roff = wb_byte(resp, roff, SSH_MSG_USERAUTH_FAILURE);
        roff = wb_cstr(resp, roff, "password");
        roff = wb_bool(resp, roff, 0);
        if (bpp_send(s, resp, (uint32_t)roff) != 0) return -1;
    }
    return -1;
}

static int send_channel_data(ssh_session_t *s, const uint8_t *buf, uint32_t len) {
    uint32_t off = 0;
    while (off < len) {
        uint32_t n = len - off;
        uint8_t frame[9 + 4096];
        uint32_t cap = s->client_max_packet ? s->client_max_packet : 1024u;
        if (cap > 4096u) cap = 4096u;
        if (n > cap) n = cap;
        frame[0] = SSH_MSG_CHANNEL_DATA;
        put_be32(frame + 1, s->client_chan);
        put_be32(frame + 5, n);
        memcpy(frame + 9, buf + off, n);
        if (bpp_send(s, frame, n + 9u) != 0) return -1;
        off += n;
    }
    return 0;
}

static void ssh_output_sink(const char *buf, uint32_t len, void *ctx) {
    ssh_session_t *s = (ssh_session_t *)ctx;
    uint32_t i;
    uint8_t out[1024];
    uint32_t out_len = 0;
    if (!s || len == 0u) return;
    for (i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            if (out_len + 2u > sizeof(out)) {
                send_channel_data(s, out, out_len);
                out_len = 0;
            }
            out[out_len++] = '\r';
            out[out_len++] = '\n';
        } else {
            if (out_len + 1u > sizeof(out)) {
                send_channel_data(s, out, out_len);
                out_len = 0;
            }
            out[out_len++] = (uint8_t)buf[i];
        }
    }
    if (out_len > 0u)
        send_channel_data(s, out, out_len);
}

static void send_prompt(ssh_session_t *s) {
    const char *cwd = shell_get_cwd();
    send_channel_data(s, (const uint8_t *)cwd, (uint32_t)strlen(cwd));
    send_channel_data(s, (const uint8_t *)"> ", 2u);
}

static void exec_line_to_channel(ssh_session_t *s, const char *line) {
    uint32_t pid = process_get_current_pid();
    shell_set_process_output_sink(0, ssh_output_sink, s);
    shell_set_process_output_sink(pid, ssh_output_sink, s);
    shell_execute_line(line);
    shell_clear_process_output_sink(pid);
    shell_clear_process_output_sink(0);
}

static int send_channel_success(ssh_session_t *s) {
    uint8_t p[5];
    p[0] = SSH_MSG_CHANNEL_SUCCESS;
    put_be32(p + 1, s->client_chan);
    return bpp_send(s, p, 5u);
}

static int send_channel_failure(ssh_session_t *s) {
    uint8_t p[5];
    p[0] = SSH_MSG_CHANNEL_FAILURE;
    put_be32(p + 1, s->client_chan);
    return bpp_send(s, p, 5u);
}

static int send_exit_and_close(ssh_session_t *s, uint32_t status) {
    uint8_t p[64];
    int off = 0;
    off = wb_byte(p, off, SSH_MSG_CHANNEL_REQUEST);
    off = wb_be32(p, off, s->client_chan);
    off = wb_cstr(p, off, "exit-status");
    off = wb_bool(p, off, 0);
    off = wb_be32(p, off, status);
    bpp_send(s, p, (uint32_t)off);
    p[0] = SSH_MSG_CHANNEL_EOF;
    put_be32(p + 1, s->client_chan);
    bpp_send(s, p, 5u);
    p[0] = SSH_MSG_CHANNEL_CLOSE;
    put_be32(p + 1, s->client_chan);
    return bpp_send(s, p, 5u);
}

static int do_exec_request(ssh_session_t *s, const uint8_t *cmd, uint32_t cmd_len) {
    char line[SSH_LINE_MAX];
    uint32_t n = cmd_len;
    if (n >= sizeof(line)) n = sizeof(line) - 1u;
    memcpy(line, cmd, n);
    line[n] = 0;
    exec_line_to_channel(s, line);
    return send_exit_and_close(s, 0u);
}

static int shell_loop(ssh_session_t *s) {
    char line[SSH_LINE_MAX];
    int line_len = 0;
    int skip_lf_after_cr = 0;
    uint8_t *pl;
    int pllen;

    send_channel_data(s, (const uint8_t *)"cupid-os ssh shell\r\n", 20u);
    send_prompt(s);

    while (s->channel_open) {
        int rc = bpp_recv(s, &pl, &pllen);
        if (rc != 0 || pllen < 1) return -1;
        if (pl[0] == SSH_MSG_CHANNEL_DATA) {
            int off = 1;
            uint32_t recp, n, i;
            if (off + 8 > pllen) return -1;
            recp = get_be32(pl + off); off += 4;
            n = get_be32(pl + off); off += 4;
            if (recp != s->server_chan || n > (uint32_t)(pllen - off)) return -1;
            for (i = 0; i < n; i++) {
                char c = (char)pl[off + (int)i];
                if (c == '\r' || c == '\n') {
                    if (c == '\n' && skip_lf_after_cr) {
                        skip_lf_after_cr = 0;
                        continue;
                    }
                    skip_lf_after_cr = (c == '\r');
                    line[line_len] = 0;
                    send_channel_data(s, (const uint8_t *)"\r\n", 2u);
                    if (strcmp(line, "exit") == 0) return send_exit_and_close(s, 0u);
                    if (line_len > 0) exec_line_to_channel(s, line);
                    send_channel_data(s, (const uint8_t *)"\r\n", 2u);
                    send_prompt(s);
                    line_len = 0;
                } else if (c == 0x7f || c == '\b') {
                    skip_lf_after_cr = 0;
                    if (line_len > 0) {
                        line_len--;
                        send_channel_data(s, (const uint8_t *)"\b \b", 3u);
                    }
                } else if ((uint8_t)c == 4u) {
                    return send_exit_and_close(s, 0u);
                } else if ((uint8_t)c >= 32u && line_len < SSH_LINE_MAX - 1) {
                    skip_lf_after_cr = 0;
                    line[line_len++] = c;
                    send_channel_data(s, (const uint8_t *)&c, 1u);
                }
            }
        } else if (pl[0] == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
            continue;
        } else if (pl[0] == SSH_MSG_CHANNEL_EOF || pl[0] == SSH_MSG_CHANNEL_CLOSE ||
                   pl[0] == SSH_MSG_DISCONNECT) {
            return send_exit_and_close(s, 0u);
        } else if (pl[0] == SSH_MSG_GLOBAL_REQUEST) {
            int off = 1;
            const uint8_t *name;
            uint32_t name_len;
            if (read_string(pl, pllen, &off, &name, &name_len) == 0 &&
                off < pllen && pl[off]) {
                uint8_t r = SSH_MSG_REQUEST_FAILURE;
                bpp_send(s, &r, 1u);
            }
        }
    }
    return 0;
}

static int do_connection(ssh_session_t *s) {
    uint8_t *pl;
    int pllen;

    while (1) {
        int rc = bpp_recv(s, &pl, &pllen);
        if (rc != 0 || pllen < 1) return -1;
        if (pl[0] == SSH_MSG_CHANNEL_OPEN) {
            int off = 1;
            const uint8_t *typ;
            uint32_t typ_len;
            uint8_t resp[64];
            int roff = 0;
            if (read_string(pl, pllen, &off, &typ, &typ_len) != 0) return -1;
            if (!cstreqn(typ, typ_len, "session")) return -1;
            if (off + 12 > pllen) return -1;
            s->client_chan = get_be32(pl + off); off += 4;
            s->client_window = get_be32(pl + off); off += 4;
            s->client_max_packet = get_be32(pl + off); off += 4;
            s->server_chan = 0;
            s->channel_open = 1;
            roff = wb_byte(resp, roff, SSH_MSG_CHANNEL_OPEN_CONFIRMATION);
            roff = wb_be32(resp, roff, s->client_chan);
            roff = wb_be32(resp, roff, s->server_chan);
            roff = wb_be32(resp, roff, SSH_WINDOW);
            roff = wb_be32(resp, roff, SSH_MAX_PACKET);
            if (bpp_send(s, resp, (uint32_t)roff) != 0) return -1;
            break;
        } else if (pl[0] == SSH_MSG_GLOBAL_REQUEST) {
            int off = 1;
            const uint8_t *name;
            uint32_t name_len;
            if (read_string(pl, pllen, &off, &name, &name_len) == 0 &&
                off < pllen && pl[off]) {
                uint8_t r = SSH_MSG_REQUEST_FAILURE;
                bpp_send(s, &r, 1u);
            }
        }
    }

    while (s->channel_open) {
        int rc = bpp_recv(s, &pl, &pllen);
        if (rc != 0 || pllen < 1) return -1;
        if (pl[0] == SSH_MSG_CHANNEL_REQUEST) {
            int off = 1;
            uint32_t recp;
            const uint8_t *req;
            uint32_t req_len;
            int want_reply;
            if (off + 4 > pllen) return -1;
            recp = get_be32(pl + off); off += 4;
            if (recp != s->server_chan) return -1;
            if (read_string(pl, pllen, &off, &req, &req_len) != 0) return -1;
            if (off >= pllen) return -1;
            want_reply = pl[off++];
            if (cstreqn(req, req_len, "pty-req")) {
                if (want_reply) send_channel_success(s);
            } else if (cstreqn(req, req_len, "shell")) {
                if (want_reply) send_channel_success(s);
                return shell_loop(s);
            } else if (cstreqn(req, req_len, "exec")) {
                const uint8_t *cmd;
                uint32_t cmd_len;
                if (read_string(pl, pllen, &off, &cmd, &cmd_len) != 0) return -1;
                if (want_reply) send_channel_success(s);
                return do_exec_request(s, cmd, cmd_len);
            } else {
                if (want_reply) send_channel_failure(s);
            }
        } else if (pl[0] == SSH_MSG_CHANNEL_CLOSE || pl[0] == SSH_MSG_DISCONNECT) {
            return send_exit_and_close(s, 0u);
        }
    }
    return 0;
}

static void free_session(ssh_session_t *s) {
    if (!s) return;
    if (s->fd >= 0) socket_close(s->fd);
    if (s->I_C) kfree(s->I_C);
    if (s->I_S) kfree(s->I_S);
    if (s->rx) kfree(s->rx);
    if (s->tx) kfree(s->tx);
    if (s->pkt) kfree(s->pkt);
    if (s->mac) kfree(s->mac);
    kfree(s);
}

static void session_entry(uint32_t arg) {
    ssh_session_t *s = (ssh_session_t *)arg;
    if (!s) process_exit();
    if (do_kex(s) != 0) goto done;
    if (do_auth(s) != 0) goto done;
    do_connection(s);
done:
    if (s && s->fd >= 0) {
        send_disconnect(s, "connection closed");
    }
    free_session(s);
    process_exit();
}

static ssh_session_t *alloc_session(int fd, uint32_t ip, uint16_t port) {
    ssh_session_t *s = kmalloc(sizeof(*s));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    s->fd = fd;
    s->peer_ip = ip;
    s->peer_port = port;
    s->rx = kmalloc(SSH_BUF_CAP);
    s->tx = kmalloc(SSH_BUF_CAP);
    s->pkt = kmalloc(SSH_PAYLOAD_MAX);
    s->mac = kmalloc(SSH_PAYLOAD_MAX + 4u);
    if (!s->rx || !s->tx || !s->pkt || !s->mac) {
        free_session(s);
        return NULL;
    }
    return s;
}

static void listener_entry(void) {
    while (g_sshd_running) {
        uint32_t peer_ip = 0;
        uint16_t peer_port = 0;
        int cfd = socket_accept(g_listen_fd, &peer_ip, &peer_port);
        if (cfd < 0) {
            process_yield();
            continue;
        }
        ssh_session_t *s = alloc_session(cfd, peer_ip, peer_port);
        if (!s) {
            socket_close(cfd);
            continue;
        }
        if (process_create_with_arg_ex((void (*)(void))session_entry,
                                       "sshd-session", 1048576u,
                                       (uint32_t)s,
                                       PROCESS_DOMAIN_KERNEL) == 0) {
            free_session(s);
        }
    }
    if (g_listen_fd >= 0) {
        socket_close(g_listen_fd);
        g_listen_fd = -1;
    }
    g_listener_pid = 0;
    process_exit();
}

int sshd_start(uint16_t port) {
    int fd;
    if (g_sshd_running) return 0;
    if (load_or_create_host_key() != 0) return -1;
    fd = socket_create(SOCK_TYPE_TCP);
    if (fd < 0) return -1;
    if (socket_bind(fd, 0u, htons(port ? port : 22u)) != 0) {
        socket_close(fd);
        return -1;
    }
    if (socket_listen(fd, 8) != 0) {
        socket_close(fd);
        return -1;
    }
    g_listen_fd = fd;
    g_port = port ? port : 22u;
    g_sshd_running = 1;
    g_listener_pid = process_create_ex(listener_entry, "sshd", 65536u,
                                       PROCESS_DOMAIN_KERNEL);
    if (g_listener_pid == 0) {
        g_sshd_running = 0;
        socket_close(g_listen_fd);
        g_listen_fd = -1;
        return -1;
    }
    serial_printf("[sshd] listening on port %u\n", (uint32_t)g_port);
    return 0;
}

void sshd_stop(void) {
    g_sshd_running = 0;
    if (g_listen_fd >= 0) {
        socket_close(g_listen_fd);
        g_listen_fd = -1;
    }
}

int sshd_is_running(void) {
    return g_sshd_running ? 1 : 0;
}

uint16_t sshd_port(void) {
    return g_port;
}
