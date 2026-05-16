//help: Interactive Telnet client.
//help: Usage: telnet <host> [port]
//help:   Default port is 23. Press Ctrl+] for the local telnet prompt.

enum {
    SOCK_TCP = 2,
    TCPS_ESTABLISHED = 4,

    TEL_SE   = 240,
    TEL_NOP  = 241,
    TEL_DM   = 242,
    TEL_BRK  = 243,
    TEL_IP   = 244,
    TEL_AO   = 245,
    TEL_AYT  = 246,
    TEL_EC   = 247,
    TEL_EL   = 248,
    TEL_GA   = 249,
    TEL_SB   = 250,
    TEL_WILL = 251,
    TEL_WONT = 252,
    TEL_DO   = 253,
    TEL_DONT = 254,
    TEL_IAC  = 255,

    OPT_BINARY = 0,
    OPT_ECHO   = 1,
    OPT_SGA    = 3,
    OPT_TTYPE  = 24,
    OPT_NAWS   = 31,

    TTYPE_IS   = 0,
    TTYPE_SEND = 1,

    RX_CAP = 1024,
    SB_CAP = 512
};

int fd = -1;
char local_opt[256];
char remote_opt[256];
char local_req[256];
char remote_req[256];

int tel_state = 0;
int tel_cmd = 0;
int tel_sb_opt = 0;
char sb_buf[SB_CAP];
int sb_len = 0;
int pending_cr = 0;

int cstrlen(char *s) {
    int n = 0;
    while (s[n]) n = n + 1;
    return n;
}

int streq(char *a, char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i = i + 1;
    return a[i] == b[i];
}

void cstrcpy_n(char *d, char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) {
        d[i] = s[i];
        i = i + 1;
    }
    d[i] = 0;
}

int next_token(char *s, int pos, char *out, int max) {
    int i = pos;
    while (s[i] == ' ' || s[i] == '\t') i = i + 1;
    if (s[i] == 0) return -1;
    int j = 0;
    while (s[i] && s[i] != ' ' && s[i] != '\t' && j < max - 1) {
        out[j] = s[i];
        i = i + 1;
        j = j + 1;
    }
    out[j] = 0;
    return i;
}

int parse_port(char *s) {
    int i = 0;
    int v = 0;
    if (!s || s[0] == 0) return -1;
    while (s[i]) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
        if (v > 65535) return -1;
        i = i + 1;
    }
    if (v <= 0) return -1;
    return v;
}

void zero_opts() {
    int i = 0;
    while (i < 256) {
        local_opt[i] = 0;
        remote_opt[i] = 0;
        local_req[i] = 0;
        remote_req[i] = 0;
        i = i + 1;
    }
}

int send_all(char *buf, int len) {
    int sent = 0;
    while (sent < len) {
        int r = send(fd, buf + sent, len - sent);
        if (r < 0) return -1;
        if (r == 0) yield();
        else sent = sent + r;
    }
    return 0;
}

void send_iac_cmd(int cmd) {
    char b[2];
    b[0] = TEL_IAC;
    b[1] = cmd;
    send_all(b, 2);
}

void send_iac_opt(int cmd, int opt) {
    char b[3];
    b[0] = TEL_IAC;
    b[1] = cmd;
    b[2] = opt;
    send_all(b, 3);
}

void request_local(int opt) {
    if (local_opt[opt] || local_req[opt]) return;
    local_req[opt] = 1;
    send_iac_opt(TEL_WILL, opt);
}

void request_remote(int opt) {
    if (remote_opt[opt] || remote_req[opt]) return;
    remote_req[opt] = 1;
    send_iac_opt(TEL_DO, opt);
}

int support_local(int opt) {
    if (opt == OPT_BINARY) return 1;
    if (opt == OPT_SGA) return 1;
    if (opt == OPT_TTYPE) return 1;
    if (opt == OPT_NAWS) return 1;
    return 0;
}

int support_remote(int opt) {
    if (opt == OPT_BINARY) return 1;
    if (opt == OPT_ECHO) return 1;
    if (opt == OPT_SGA) return 1;
    return 0;
}

int sb_put(char *out, int p, int v) {
    out[p] = v & 0xFF;
    p = p + 1;
    if ((v & 0xFF) == TEL_IAC) {
        out[p] = TEL_IAC;
        p = p + 1;
    }
    return p;
}

void send_naws() {
    int cols;
    int rows;
    char b[16];
    int p = 0;
    if (!local_opt[OPT_NAWS]) return;
    get_screen_size(&cols, &rows);
    if (cols <= 0) cols = 80;
    if (rows <= 0) rows = 24;

    b[p] = TEL_IAC; p = p + 1;
    b[p] = TEL_SB;  p = p + 1;
    b[p] = OPT_NAWS; p = p + 1;
    p = sb_put(b, p, (cols >> 8) & 0xFF);
    p = sb_put(b, p, cols & 0xFF);
    p = sb_put(b, p, (rows >> 8) & 0xFF);
    p = sb_put(b, p, rows & 0xFF);
    b[p] = TEL_IAC; p = p + 1;
    b[p] = TEL_SE;  p = p + 1;
    send_all(b, p);
}

void send_ttype() {
    char b[32];
    char *name = "CUPIDOS";
    int p = 0;
    int i = 0;
    if (!local_opt[OPT_TTYPE]) return;
    b[p] = TEL_IAC; p = p + 1;
    b[p] = TEL_SB; p = p + 1;
    b[p] = OPT_TTYPE; p = p + 1;
    b[p] = TTYPE_IS; p = p + 1;
    while (name[i]) {
        p = sb_put(b, p, name[i] & 0xFF);
        i = i + 1;
    }
    b[p] = TEL_IAC; p = p + 1;
    b[p] = TEL_SE; p = p + 1;
    send_all(b, p);
}

void initial_negotiate() {
    request_local(OPT_TTYPE);
    request_local(OPT_NAWS);
    request_local(OPT_SGA);
    request_remote(OPT_SGA);
    request_remote(OPT_ECHO);
}

void handle_do(int opt) {
    if (support_local(opt)) {
        if (local_req[opt]) {
            local_req[opt] = 0;
            local_opt[opt] = 1;
        } else if (!local_opt[opt]) {
            local_opt[opt] = 1;
            send_iac_opt(TEL_WILL, opt);
        }
        if (opt == OPT_NAWS) send_naws();
    } else {
        local_req[opt] = 0;
        send_iac_opt(TEL_WONT, opt);
    }
}

void handle_dont(int opt) {
    local_req[opt] = 0;
    if (local_opt[opt]) {
        local_opt[opt] = 0;
        send_iac_opt(TEL_WONT, opt);
    }
}

void handle_will(int opt) {
    if (support_remote(opt)) {
        if (remote_req[opt]) {
            remote_req[opt] = 0;
            remote_opt[opt] = 1;
        } else if (!remote_opt[opt]) {
            remote_opt[opt] = 1;
            send_iac_opt(TEL_DO, opt);
        }
    } else {
        remote_req[opt] = 0;
        send_iac_opt(TEL_DONT, opt);
    }
}

void handle_wont(int opt) {
    remote_req[opt] = 0;
    remote_opt[opt] = 0;
}

void render_byte(int c) {
    char b[1];
    b[0] = c & 0xFF;
    print_n(b, 1);
}

void render_data(int c) {
    c = c & 0xFF;
    if (remote_opt[OPT_BINARY]) {
        render_byte(c);
        return;
    }

    if (pending_cr) {
        if (c == 0) {
            render_byte('\r');
            pending_cr = 0;
            return;
        }
        if (c == '\n') {
            render_byte('\r');
            render_byte('\n');
            pending_cr = 0;
            return;
        }
        render_byte('\r');
        pending_cr = 0;
    }

    if (c == '\r') {
        pending_cr = 1;
        return;
    }
    render_byte(c);
}

void handle_sb() {
    if (tel_sb_opt == OPT_TTYPE && sb_len >= 1) {
        int q = sb_buf[0] & 0xFF;
        if (q == TTYPE_SEND) send_ttype();
    }
}

void parse_telnet_byte(int c) {
    c = c & 0xFF;
    if (tel_state == 0) {
        if (c == TEL_IAC) tel_state = 1;
        else render_data(c);
        return;
    }
    if (tel_state == 1) {
        if (c == TEL_IAC) {
            render_data(TEL_IAC);
            tel_state = 0;
        } else if (c == TEL_WILL || c == TEL_WONT ||
                   c == TEL_DO || c == TEL_DONT) {
            tel_cmd = c;
            tel_state = 2;
        } else if (c == TEL_SB) {
            tel_state = 3;
        } else {
            tel_state = 0;
        }
        return;
    }
    if (tel_state == 2) {
        if (tel_cmd == TEL_DO) handle_do(c);
        else if (tel_cmd == TEL_DONT) handle_dont(c);
        else if (tel_cmd == TEL_WILL) handle_will(c);
        else if (tel_cmd == TEL_WONT) handle_wont(c);
        tel_state = 0;
        return;
    }
    if (tel_state == 3) {
        tel_sb_opt = c;
        sb_len = 0;
        tel_state = 4;
        return;
    }
    if (tel_state == 4) {
        if (c == TEL_IAC) {
            tel_state = 5;
        } else if (sb_len < SB_CAP) {
            sb_buf[sb_len] = c;
            sb_len = sb_len + 1;
        }
        return;
    }
    if (tel_state == 5) {
        if (c == TEL_IAC) {
            if (sb_len < SB_CAP) {
                sb_buf[sb_len] = TEL_IAC;
                sb_len = sb_len + 1;
            }
            tel_state = 4;
        } else if (c == TEL_SE) {
            handle_sb();
            tel_state = 0;
        } else {
            tel_state = 0;
        }
    }
}

void parse_telnet(char *buf, int len) {
    int i = 0;
    while (i < len) {
        parse_telnet_byte(buf[i]);
        i = i + 1;
    }
}

void send_user_byte(int c) {
    char b[2];
    c = c & 0xFF;
    if (c == TEL_IAC) {
        b[0] = TEL_IAC;
        b[1] = TEL_IAC;
        send_all(b, 2);
        return;
    }
    b[0] = c;
    send_all(b, 1);
}

void send_user_key(char *buf, int len) {
    int i = 0;
    if (len == 1 && (buf[0] == '\n' || buf[0] == '\r')) {
        char nl[2];
        nl[0] = '\r';
        nl[1] = '\n';
        send_all(nl, 2);
        return;
    }
    while (i < len) {
        send_user_byte(buf[i]);
        i = i + 1;
    }
}

void print_status() {
    print("connected, options: ");
    if (remote_opt[OPT_ECHO]) print("remote-echo ");
    if (remote_opt[OPT_SGA]) print("remote-sga ");
    if (remote_opt[OPT_BINARY]) print("remote-binary ");
    if (local_opt[OPT_SGA]) print("local-sga ");
    if (local_opt[OPT_TTYPE]) print("ttype ");
    if (local_opt[OPT_NAWS]) print("naws ");
    if (local_opt[OPT_BINARY]) print("local-binary ");
    println("");
}

int local_prompt() {
    char line[64];
    print("\ntelnet> ");
    int n = read_line(line, 64);
    if (n < 0) return -1;
    if (streq(line, "quit") || streq(line, "close")) return -1;
    if (streq(line, "status")) {
        print_status();
        return 0;
    }
    if (streq(line, "send ayt")) { send_iac_cmd(TEL_AYT); return 0; }
    if (streq(line, "send brk")) { send_iac_cmd(TEL_BRK); return 0; }
    if (streq(line, "send ip"))  { send_iac_cmd(TEL_IP);  return 0; }
    if (streq(line, "send ao"))  { send_iac_cmd(TEL_AO);  return 0; }
    if (streq(line, "send nop")) { send_iac_cmd(TEL_NOP); return 0; }
    println("commands: quit, close, status, send ayt, send brk, send ip, send ao, send nop");
    return 0;
}

int resolve_host(char *host, U32 *ip) {
    if (ip_parse(host, ip) == 0) return 0;
    return dns_resolve(host, ip);
}

int io_loop() {
    char rx[RX_CAP];
    char key[8];
    int last_cols;
    int last_rows;
    get_screen_size(&last_cols, &last_rows);

    while (1) {
        int avail = sock_avail(fd);
        if (sock_state(fd) != TCPS_ESTABLISHED && avail <= 0) break;

        while (avail > 0) {
            int want = avail;
            if (want > RX_CAP) want = RX_CAP;
            int n = recv(fd, rx, want);
            if (n <= 0) return -1;
            parse_telnet(rx, n);
            avail = sock_avail(fd);
        }

        while (1) {
            int n = poll_key_vt(key);
            if (n <= 0) break;
            if (n == 1 && (key[0] & 0xFF) == 29) {
                if (local_prompt() < 0) return 0;
            } else {
                send_user_key(key, n);
            }
        }

        if (local_opt[OPT_NAWS]) {
            int cols;
            int rows;
            get_screen_size(&cols, &rows);
            if (cols != last_cols || rows != last_rows) {
                last_cols = cols;
                last_rows = rows;
                send_naws();
            }
        }

        yield();
    }
    return 0;
}

void main() {
    char *raw = (char*)get_args();
    char host[256];
    char tok[256];
    int port = 23;
    int pos = 0;

    host[0] = 0;
    if (!raw || raw[0] == 0) {
        println("usage: telnet <host> [port]");
        return;
    }

    pos = next_token(raw, pos, host, 256);
    if (pos < 0 || host[0] == 0) {
        println("usage: telnet <host> [port]");
        return;
    }
    pos = next_token(raw, pos, tok, 256);
    if (pos >= 0) {
        port = parse_port(tok);
        if (port < 0) {
            print("telnet: bad port ");
            println(tok);
            return;
        }
    }

    U32 ip = 0;
    if (resolve_host(host, &ip) != 0) {
        print("telnet: could not resolve ");
        println(host);
        return;
    }

    zero_opts();
    fd = socket(SOCK_TCP);
    if (fd < 0) {
        println("telnet: socket failed");
        return;
    }
    if (connect(fd, ip, htons(port)) != 0) {
        print("telnet: connect failed: ");
        println(host);
        close(fd);
        return;
    }

    initial_negotiate();
    io_loop();
    close(fd);
    println("\ntelnet: connection closed");
}
