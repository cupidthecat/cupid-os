/**
 * @file ssh_channel.h
 * @brief Run the SSH session channel and forward terminal input and output.
 */

#ifndef CUPID_SSH_CHANNEL_H
#define CUPID_SSH_CHANNEL_H

/* Channel + pty-req + shell */

void send_channel_open() {
    char msg[256];
    int off = 0;
    msg[off] = SSH_MSG_CHANNEL_OPEN;
    off = off + 1;
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
     * output, and Ctrl-C/Backspace defaults.*/
    modes[moff] = 1;
    moff = moff + 1;
    put_be32(modes + moff, 3);
    moff = moff + 4; /* VINTR  ^C */
    modes[moff] = 3;
    moff = moff + 1;
    put_be32(modes + moff, 127);
    moff = moff + 4; /* VERASE DEL */
    modes[moff] = 4;
    moff = moff + 1;
    put_be32(modes + moff, 21);
    moff = moff + 4; /* VKILL  ^U */
    modes[moff] = 5;
    moff = moff + 1;
    put_be32(modes + moff, 4);
    moff = moff + 4; /* VEOF   ^D */
    modes[moff] = 36;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* ICRNL */
    modes[moff] = 38;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* IXON */
    modes[moff] = 50;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* ISIG */
    modes[moff] = 51;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* ICANON */
    modes[moff] = 53;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* ECHO */
    modes[moff] = 54;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* ECHOE */
    modes[moff] = 55;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* ECHOK */
    modes[moff] = 59;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* IEXTEN */
    modes[moff] = 70;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* OPOST */
    modes[moff] = 72;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* ONLCR */
    modes[moff] = 91;
    moff = moff + 1;
    put_be32(modes + moff, 1);
    moff = moff + 4; /* CS8 */
    modes[moff] = 128;
    moff = moff + 1;
    put_be32(modes + moff, 38400);
    moff = moff + 4; /* ISPEED */
    modes[moff] = 129;
    moff = moff + 1;
    put_be32(modes + moff, 38400);
    moff = moff + 4; /* OSPEED */
    modes[moff] = 0;
    moff = moff + 1; /* TTY_OP_END */

    msg[off] = SSH_MSG_CHANNEL_REQUEST;
    off = off + 1;
    off = wb_be32(msg, off, remote_chan);
    off = wb_cstr(msg, off, "pty-req");
    msg[off] = 1;
    off = off + 1; /* want_reply */
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
    serial_printf("[ssh-debug] pty window-change cols=%d rows=%d remote=%d\n", cols, rows,
                  remote_chan);
    msg[off] = SSH_MSG_CHANNEL_REQUEST;
    off = off + 1;
    off = wb_be32(msg, off, remote_chan);
    off = wb_cstr(msg, off, "window-change");
    msg[off] = 0;
    off = off + 1; /* want_reply */
    off = wb_be32(msg, off, cols);
    off = wb_be32(msg, off, rows);
    off = wb_be32(msg, off, 0);
    off = wb_be32(msg, off, 0);
    bpp_send(msg, off);
}

void send_shell_req() {
    char msg[64];
    int off = 0;
    msg[off] = SSH_MSG_CHANNEL_REQUEST;
    off = off + 1;
    off = wb_be32(msg, off, remote_chan);
    off = wb_cstr(msg, off, "shell");
    msg[off] = 1;
    off = off + 1; /* want_reply */
    bpp_send(msg, off);
}

void send_channel_data(char *data, int len) {
    int requested = len;
    int before_window = tx_window;
    if (len > tx_window)
        len = tx_window;
    if (len > remote_max_packet - 9)
        len = remote_max_packet - 9;
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
    int i = 0;
    while (i < 9) {
        buf[i] = hdr[i];
        i = i + 1;
    }
    i = 0;
    while (i < len) {
        buf[9 + i] = data[i];
        i = i + 1;
    }
    bpp_send(buf, 9 + len);
    kfree(buf);
    tx_window = tx_window - len;
    serial_printf("[ssh-debug] tx channel sent=%d requested=%d window %d->%d\n", len, requested,
                  before_window, tx_window);
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
    msg[off] = SSH_MSG_DISCONNECT;
    off = off + 1;
    off = wb_be32(msg, off, reason);
    off = wb_cstr(msg, off, desc);
    off = wb_cstr(msg, off, ""); /* language */
    bpp_send(msg, off);
}

int wait_for_channel_open() {
    char *pl;
    int pllen;
    while (1) {
        if (bpp_recv(&pl, &pllen) != 0)
            return -1;
        if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_DEBUG)
            continue;
        if (pl[0] == SSH_MSG_GLOBAL_REQUEST) {
            /* Ignore; reply with REQUEST_FAILURE if want_reply. */
            int off2 = 1;
            int nl = get_be32(pl + off2);
            off2 = off2 + 4 + nl;
            int want = pl[off2] & 0xFF;
            if (want) {
                char rep[1];
                rep[0] = SSH_MSG_REQUEST_FAILURE;
                bpp_send(rep, 1);
            }
            continue;
        }
        if (pl[0] == SSH_MSG_CHANNEL_OPEN_CONFIRMATION) {
            remote_chan = get_be32(pl + 5);
            tx_window = get_be32(pl + 9);
            remote_max_packet = get_be32(pl + 13);
            serial_printf("[ssh] channel open remote=%d tx_window=%d max_packet=%d\n", remote_chan,
                          tx_window, remote_max_packet);
            return 0;
        }
        if (pl[0] == SSH_MSG_CHANNEL_OPEN_FAILURE)
            return -1;
        if (pl[0] == SSH_MSG_DISCONNECT)
            return -1;
    }
}

int wait_for_request_reply() {
    char *pl;
    int pllen;
    while (1) {
        if (bpp_recv(&pl, &pllen) != 0)
            return -1;
        if (pl[0] == SSH_MSG_IGNORE || pl[0] == SSH_MSG_DEBUG)
            continue;
        if (pl[0] == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
            tx_window = tx_window + get_be32(pl + 5);
            continue;
        }
        if (pl[0] == SSH_MSG_CHANNEL_SUCCESS)
            return 0;
        if (pl[0] == SSH_MSG_CHANNEL_FAILURE)
            return -1;
        if (pl[0] == SSH_MSG_DISCONNECT)
            return -1;
        /* Unexpected - bail. */
        return -1;
    }
}

/*  VT100/ANSI passthrough renderer
 *
 * The CupidOS GUI renderer (shell_gui_putchar_ext) already understands
 * VT100/ANSI escape sequences (CSI move/erase, SGR colors, etc) - see
 * kernel/lang/ansi.* and kernel/core/kernel.cc. So the cupidc client
 * acts as a thin passthrough: every byte the server sends goes
 * directly to print_n, which feeds shell_gui_putchar_ext.
 *
 * The cupidc-side state machine therefore does only one thing: drops
 * the C0 codes that the local terminal can't handle gracefully (BEL
 * → ignore). Everything else passes through verbatim.*/
void render_bytes(char *buf, int len) {
    /* Pass through directly; print_n loops shell_gui_putchar_ext which
     * is connected to the same ANSI handler the local shell uses.*/
    if (ssh_debug_render)
        debug_dump_bytes("rx-render", buf, len);
    print_n(buf, len);
}

/* main I/O loop */

int io_loop() {
    char *pl;
    int pllen;
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
            if (t == SSH_MSG_IGNORE || t == SSH_MSG_DEBUG)
                continue;
            if (t == SSH_MSG_CHANNEL_DATA) {
                int dl = get_be32(pl + 5);
                serial_printf("[ssh-debug] rx channel-data pllen=%d data=%d rx_window_before=%d\n",
                              pllen, dl, rx_window);
                render_bytes(pl + 9, dl);
                rx_window = rx_window - dl;
                serial_printf("[ssh-debug] rx channel-data consumed=%d rx_window_after=%d\n", dl,
                              rx_window);
                if (rx_window < 65536)
                    send_window_adjust(1048576);
                continue;
            }
            if (t == SSH_MSG_CHANNEL_EXTENDED_DATA) {
                int dl = get_be32(pl + 9);
                serial_printf("[ssh-debug] rx extended-data pllen=%d data=%d rx_window_before=%d\n",
                              pllen, dl, rx_window);
                render_bytes(pl + 13, dl);
                rx_window = rx_window - dl;
                serial_printf("[ssh-debug] rx extended-data consumed=%d rx_window_after=%d\n", dl,
                              rx_window);
                if (rx_window < 65536)
                    send_window_adjust(1048576);
                continue;
            }
            if (t == SSH_MSG_CHANNEL_WINDOW_ADJUST) {
                tx_window = tx_window + get_be32(pl + 5);
                serial_printf("[ssh] channel window adjust tx_window=%d\n", tx_window);
                continue;
            }
            if (t == SSH_MSG_CHANNEL_EOF) {
                channel_eof = 1;
                continue;
            }
            if (t == SSH_MSG_CHANNEL_CLOSE) {
                send_channel_close();
                channel_open = 0;
                break;
            }
            if (t == SSH_MSG_CHANNEL_REQUEST) {
                /* Server-initiated channel request - exit-status etc.
                 * Ignore; reply FAILURE if want_reply.*/
                int off2 = 1 + 4;
                int nl = get_be32(pl + off2);
                off2 = off2 + 4 + nl;
                int want = pl[off2] & 0xFF;
                if (want) {
                    char r[5];
                    r[0] = SSH_MSG_CHANNEL_FAILURE;
                    put_be32(r + 1, remote_chan);
                    bpp_send(r, 5);
                }
                continue;
            }
            if (t == SSH_MSG_DISCONNECT) {
                channel_open = 0;
                break;
            }
            if (t == SSH_MSG_GLOBAL_REQUEST) {
                /* might want_reply */
                int off2 = 1;
                int nl = get_be32(pl + off2);
                off2 = off2 + 4 + nl;
                int want = pl[off2] & 0xFF;
                if (want) {
                    char r[1];
                    r[0] = SSH_MSG_REQUEST_FAILURE;
                    bpp_send(r, 1);
                }
                continue;
            }
        }

        /* Drain available keyboard events. */
        while (1) {
            int n = poll_key_vt(keybuf);
            if (n <= 0)
                break;
            debug_dump_bytes("tx-key", keybuf, n);
            if (tx_window > 0)
                send_channel_data(keybuf, n);
            else
                serial_printf("[ssh-debug] tx key waiting for window len=%d\n", n);
        }

        yield();
    }
    return 0;
}

#endif
