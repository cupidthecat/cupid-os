//help: Ed line editor
//help: Usage: ed [filename]
//help: A POSIX-like ed(1) line editor for CupidOS.
//help: Commands: a i c d p n l = q Q w r e f s m t j k u g v H h
//help: Enter '.' on a line by itself to end input mode.

// ── Constants ──────────────────────────────────────────────────────
enum {
    MAX_LINES    = 1024,
    MAX_LINE_LEN = 256,
    CMD_BUF      = 512,
    FNAME_LEN    = 64,
    VFS_RDONLY   = 0,
    VFS_WRONLY   = 1,
    VFS_CREAT    = 256,
    VFS_TRUNC    = 512
};

// ── Global state ───────────────────────────────────────────────────
int ed_lines[1024];
int ed_nlines;
int ed_cur;
int ed_dirty;
int ed_quit;
int ed_show_errs;
char ed_fname[64];
char ed_lerr[128];
char ed_lpat[256];
char ed_lrep[256];
int ed_undo_lines[1024];
int ed_undo_nlines;
int ed_undo_cur;
int ed_undo_valid;
int ed_marks[26];

// ── Utility ────────────────────────────────────────────────────────
int ed_isdigit(int c) {
    if (c >= '0' && c <= '9') return 1;
    return 0;
}

int ed_parse_int_at(char *s, int *pos) {
    int val = 0;
    while (ed_isdigit(s[*pos])) {
        val = val * 10 + (s[*pos] - '0');
        *pos = *pos + 1;
    }
    return val;
}

void ed_int_to_str(int val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = 0;
        return;
    }
    int neg = 0;
    if (val < 0) { neg = 1; val = 0 - val; }
    char tmp[16];
    int ti = 0;
    while (val > 0) {
        tmp[ti] = (char)('0' + val % 10);
        ti = ti + 1;
        val = val / 10;
    }
    int bi = 0;
    if (neg) { buf[bi] = '-'; bi = bi + 1; }
    while (ti > 0) {
        ti = ti - 1;
        buf[bi] = tmp[ti];
        bi = bi + 1;
    }
    buf[bi] = 0;
}

int ed_strdup(char *s) {
    int len = strlen(s);
    char *d = (char*)kmalloc(len + 1);
    if (d == 0) return 0;
    strcpy(d, s);
    return (int)d;
}

void ed_strcopy(char *dst, char *src) {
    int i = 0;
    while (src[i] != 0) {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = 0;
}

// ── Error handling ─────────────────────────────────────────────────
void ed_error(char *msg) {
    ed_strcopy(ed_lerr, msg);
    print("?\n");
    if (ed_show_errs) {
        print(msg);
        print("\n");
    }
}

// ── Buffer manipulation ───────────────────────────────────────────
int ed_insert_line(int after, char *text) {
    if (ed_nlines >= MAX_LINES) {
        ed_error("buffer full");
        return 0;
    }
    int dup = ed_strdup(text);
    if (dup == 0) {
        ed_error("out of memory");
        return 0;
    }
    int i = ed_nlines;
    while (i > after) {
        ed_lines[i] = ed_lines[i - 1];
        i = i - 1;
    }
    ed_lines[after] = dup;
    ed_nlines = ed_nlines + 1;
    int m = 0;
    while (m < 26) {
        if (ed_marks[m] > after) ed_marks[m] = ed_marks[m] + 1;
        m = m + 1;
    }
    return 1;
}

void ed_delete_line(int pos) {
    if (pos < 1 || pos > ed_nlines) return;
    int idx = pos - 1;
    if (ed_lines[idx] != 0) kfree((void*)ed_lines[idx]);
    int i = idx;
    while (i < ed_nlines - 1) {
        ed_lines[i] = ed_lines[i + 1];
        i = i + 1;
    }
    ed_lines[ed_nlines - 1] = 0;
    ed_nlines = ed_nlines - 1;
    int m = 0;
    while (m < 26) {
        if (ed_marks[m] == pos) ed_marks[m] = 0;
        else if (ed_marks[m] > pos) ed_marks[m] = ed_marks[m] - 1;
        m = m + 1;
    }
}

int ed_replace_line(int pos, char *text) {
    if (pos < 1 || pos > ed_nlines) return 0;
    int idx = pos - 1;
    int dup = ed_strdup(text);
    if (dup == 0) {
        ed_error("out of memory");
        return 0;
    }
    if (ed_lines[idx] != 0) kfree((void*)ed_lines[idx]);
    ed_lines[idx] = dup;
    return 1;
}

// ── Undo ───────────────────────────────────────────────────────────
void ed_save_undo() {
    int i = 0;
    while (i < ed_undo_nlines) {
        if (ed_undo_lines[i] != 0) kfree((void*)ed_undo_lines[i]);
        ed_undo_lines[i] = 0;
        i = i + 1;
    }
    i = 0;
    while (i < ed_nlines) {
        char *src = (char*)ed_lines[i];
        if (src != 0) {
            ed_undo_lines[i] = ed_strdup(src);
        } else {
            ed_undo_lines[i] = 0;
        }
        i = i + 1;
    }
    ed_undo_nlines = ed_nlines;
    ed_undo_cur = ed_cur;
    ed_undo_valid = 1;
}

void ed_restore_undo() {
    if (ed_undo_valid == 0) {
        ed_error("nothing to undo");
        return;
    }
    // Swap current and undo buffers
    int tmp_n = ed_nlines;
    int tmp_c = ed_cur;
    int i = 0;
    int max = ed_nlines;
    if (ed_undo_nlines > max) max = ed_undo_nlines;
    if (MAX_LINES > max) max = MAX_LINES;
    // We need temp storage for swap
    int tmp_lines[1024];
    i = 0;
    while (i < MAX_LINES) {
        tmp_lines[i] = ed_lines[i];
        ed_lines[i] = ed_undo_lines[i];
        ed_undo_lines[i] = tmp_lines[i];
        i = i + 1;
    }
    ed_nlines = ed_undo_nlines;
    ed_cur = ed_undo_cur;
    ed_undo_nlines = tmp_n;
    ed_undo_cur = tmp_c;
}

// ── Regex engine ───────────────────────────────────────────────────
// Merged match_here and match_star to avoid mutual recursion
int ed_match_here(char *pat, char *text) {
    while (1) {
        if (pat[0] == 0) return 1;
        if (pat[1] == '*') {
            int c = pat[0];
            char *rest = pat + 2;
            char *t = text;
            while (1) {
                if (ed_match_here(rest, t)) return 1;
                if (*t == 0) break;
                if (c != '.' && *t != (char)c) break;
                t = t + 1;
            }
            return 0;
        }
        if (pat[0] == '$' && pat[1] == 0) {
            if (*text == 0) return 1;
            return 0;
        }
        if (*text != 0 && (pat[0] == '.' || pat[0] == *text)) {
            pat = pat + 1;
            text = text + 1;
            continue;
        }
        return 0;
    }
}

// Search for pattern in text. Returns index of match start, or -1.
int ed_regex_search(char *pat, char *text) {
    if (pat[0] == '^') {
        if (ed_match_here(pat + 1, text)) return 0;
        return -1;
    }
    int i = 0;
    while (1) {
        if (ed_match_here(pat, text + i)) return i;
        if (text[i] == 0) break;
        i = i + 1;
    }
    return -1;
}

// Find end of match for pattern at text position
int ed_match_end(char *pat, char *text) {
    if (pat[0] == '^') pat = pat + 1;
    char *t = text;
    while (*pat != 0) {
        if (pat[1] == '*') {
            int c = pat[0];
            char *start = t;
            while (*t != 0 && (c == '.' || *t == (char)c)) t = t + 1;
            // Greedy: try from rightmost
            while (t >= start) {
                if (ed_match_here(pat + 2, t)) {
                    return (int)(t - text);
                }
                t = t - 1;
            }
            return -1;
        }
        if (pat[0] == '$' && pat[1] == 0) return (int)(t - text);
        if (*t == 0) return -1;
        if (pat[0] != '.' && pat[0] != *t) return -1;
        pat = pat + 1;
        t = t + 1;
    }
    return (int)(t - text);
}

// ── Console I/O ────────────────────────────────────────────────────
void ed_readline(char *buf, int maxlen) {
    putchar(':');
    int i = 0;
    while (1) {
        char c = getchar();
        if (c == '\n' || c == '\r') {
            putchar('\n');
            break;
        }
        if (c == 8 || c == 127) {
            if (i > 0) {
                i = i - 1;
                print("\b \b");
            }
            continue;
        }
        if (i < maxlen - 1) {
            buf[i] = c;
            i = i + 1;
            putchar(c);
        }
    }
    buf[i] = 0;
}

int ed_read_input_line(char *buf, int maxlen) {
    int i = 0;
    while (1) {
        char c = getchar();
        if (c == '\n' || c == '\r') {
            putchar('\n');
            break;
        }
        if (c == 8 || c == 127) {
            if (i > 0) {
                i = i - 1;
                print("\b \b");
            }
            continue;
        }
        if (i < maxlen - 1) {
            buf[i] = c;
            i = i + 1;
            putchar(c);
        }
    }
    buf[i] = 0;
    return i;
}

// ── Input mode (for a, i, c commands) ──────────────────────────────
int ed_input_mode(int after) {
    char buf[256];
    int count = 0;
    while (1) {
        ed_read_input_line(buf, MAX_LINE_LEN);
        if (buf[0] == '.' && buf[1] == 0) break;
        if (ed_insert_line(after + count, buf) == 0) break;
        count = count + 1;
        ed_cur = after + count;
    }
    if (count > 0) ed_dirty = 1;
    return after + count;
}

// ── Text loading ───────────────────────────────────────────────────
int ed_load_text(char *data, int size, int after) {
    int bytes = 0;
    char line[256];
    int lp = 0;
    int count = 0;
    int i = 0;
    while (i < size) {
        if (data[i] == '\n' || data[i] == '\r') {
            line[lp] = 0;
            if (ed_insert_line(after + count, line) == 0) break;
            count = count + 1;
            bytes = bytes + lp + 1;
            lp = 0;
            if (data[i] == '\r' && i + 1 < size && data[i + 1] == '\n') {
                i = i + 1;
            }
        } else {
            if (lp < MAX_LINE_LEN - 1) {
                line[lp] = data[i];
                lp = lp + 1;
            }
        }
        i = i + 1;
    }
    if (lp > 0) {
        line[lp] = 0;
        if (ed_insert_line(after + count, line) != 0) {
            count = count + 1;
            bytes = bytes + lp;
        }
    }
    if (count > 0) ed_cur = after + count;
    return bytes;
}

// ── File I/O ───────────────────────────────────────────────────────
int ed_read_file(char *fname) {
    char path[256];
    resolve_path(fname, path);
    int fd = vfs_open(path, VFS_RDONLY);
    if (fd < 0) return -1;
    char bigbuf[4096];
    int total = 0;
    char rbuf[256];
    int r = vfs_read(fd, rbuf, 255);
    while (r > 0) {
        int j = 0;
        while (j < r && total < 4095) {
            bigbuf[total] = rbuf[j];
            total = total + 1;
            j = j + 1;
        }
        r = vfs_read(fd, rbuf, 255);
    }
    vfs_close(fd);
    bigbuf[total] = 0;
    int bytes = ed_load_text(bigbuf, total, ed_nlines);
    return bytes;
}

int ed_write_file(char *fname, int from, int to) {
    char path[256];
    resolve_path(fname, path);
    // Calculate total size
    int total = 0;
    int i = from;
    while (i <= to) {
        char *line = (char*)ed_lines[i - 1];
        if (line != 0) total = total + strlen(line);
        total = total + 1; // newline
        i = i + 1;
    }
    char *buf = (char*)kmalloc(total + 1);
    if (buf == 0) {
        ed_error("out of memory");
        return -1;
    }
    int pos = 0;
    i = from;
    while (i <= to) {
        char *line = (char*)ed_lines[i - 1];
        if (line != 0) {
            int len = strlen(line);
            memcpy(buf + pos, line, len);
            pos = pos + len;
        }
        buf[pos] = '\n';
        pos = pos + 1;
        i = i + 1;
    }
    int fd = vfs_open(path, VFS_WRONLY + VFS_CREAT + VFS_TRUNC);
    if (fd < 0) {
        kfree(buf);
        ed_error("cannot open file for writing");
        return -1;
    }
    vfs_write(fd, buf, total);
    vfs_close(fd);
    kfree(buf);
    return total;
}

// ── Address parsing ────────────────────────────────────────────────
// Returns address (1-based), -1 if no address, -2 on error.
int ed_parse_addr(char *cmd, int *pos) {
    int addr = -1;
    while (cmd[*pos] == ' ') *pos = *pos + 1;

    if (ed_isdigit(cmd[*pos])) {
        addr = ed_parse_int_at(cmd, pos);
    } else if (cmd[*pos] == '.') {
        addr = ed_cur;
        *pos = *pos + 1;
    } else if (cmd[*pos] == '$') {
        addr = ed_nlines;
        *pos = *pos + 1;
    } else if (cmd[*pos] == '\'') {
        *pos = *pos + 1;
        int ch = cmd[*pos];
        if (ch >= 'a' && ch <= 'z') {
            int m = ch - 'a';
            if (ed_marks[m] == 0) {
                ed_error("undefined mark");
                return -2;
            }
            addr = ed_marks[m];
            *pos = *pos + 1;
        } else {
            ed_error("invalid mark");
            return -2;
        }
    } else if (cmd[*pos] == '/' || cmd[*pos] == '?') {
        int delim = cmd[*pos];
        *pos = *pos + 1;
        char pattern[256];
        int pi = 0;
        while (cmd[*pos] != 0 && cmd[*pos] != (char)delim && pi < 255) {
            pattern[pi] = cmd[*pos];
            pi = pi + 1;
            *pos = *pos + 1;
        }
        pattern[pi] = 0;
        if (cmd[*pos] == (char)delim) *pos = *pos + 1;

        // Use last pattern if empty
        if (pi == 0) {
            if (ed_lpat[0] == 0) {
                ed_error("no previous pattern");
                return -2;
            }
            ed_strcopy(pattern, ed_lpat);
        } else {
            ed_strcopy(ed_lpat, pattern);
        }

        // Search
        int found = 0;
        if (delim == '/') {
            // Forward from cur+1, wrapping
            int si = 0;
            while (si < ed_nlines) {
                int ln = ((ed_cur + si) % ed_nlines) + 1;
                char *ltext = (char*)ed_lines[ln - 1];
                if (ltext != 0 && ed_regex_search(pattern, ltext) >= 0) {
                    addr = ln;
                    found = 1;
                    break;
                }
                si = si + 1;
            }
        } else {
            // Backward from cur-1, wrapping
            int si = 0;
            while (si < ed_nlines) {
                int ln = ed_cur - 1 - si;
                while (ln < 1) ln = ln + ed_nlines;
                char *ltext = (char*)ed_lines[ln - 1];
                if (ltext != 0 && ed_regex_search(pattern, ltext) >= 0) {
                    addr = ln;
                    found = 1;
                    break;
                }
                si = si + 1;
            }
        }
        if (found == 0) {
            ed_error("pattern not found");
            return -2;
        }
    }

    if (addr == -1) return -1;

    // Handle +/- offsets
    while (cmd[*pos] == '+' || cmd[*pos] == '-') {
        int op = cmd[*pos];
        *pos = *pos + 1;
        int offset = 1;
        if (ed_isdigit(cmd[*pos])) {
            offset = ed_parse_int_at(cmd, pos);
        }
        if (op == '+') addr = addr + offset;
        else addr = addr - offset;
    }

    return addr;
}

// ── Substitution on a single line ──────────────────────────────────
int ed_sub_line(int linenum, char *pattern, char *repl, int gflag, int count_tgt) {
    char *line = (char*)ed_lines[linenum - 1];
    if (line == 0) return 0;
    char result[256];
    int rp = 0;
    int subs = 0;
    int match_num = 0;
    int p = 0; // index into line

    while (line[p] != 0 && rp < MAX_LINE_LEN - 1) {
        int mpos = ed_regex_search(pattern, line + p);
        if (mpos < 0) {
            // No match, copy rest
            while (line[p] != 0 && rp < MAX_LINE_LEN - 1) {
                result[rp] = line[p];
                rp = rp + 1;
                p = p + 1;
            }
            break;
        }
        if (mpos > 0) {
            // Copy chars before match
            int ci = 0;
            while (ci < mpos && rp < MAX_LINE_LEN - 1) {
                result[rp] = line[p];
                rp = rp + 1;
                p = p + 1;
                ci = ci + 1;
            }
        }
        match_num = match_num + 1;

        // Find match end
        int mend = ed_match_end(pattern, line + p);
        if (mend < 0 || mend == 0) {
            // Zero-length match, copy one char
            if (line[p] != 0) {
                result[rp] = line[p];
                rp = rp + 1;
                p = p + 1;
            }
            continue;
        }

        // Should we replace this occurrence?
        int do_rep = 0;
        if (gflag) {
            do_rep = 1;
        } else if (count_tgt > 0) {
            if (match_num == count_tgt) do_rep = 1;
        } else {
            if (match_num == 1) do_rep = 1;
        }

        if (do_rep) {
            // Copy replacement, handle & and backslash escapes
            int ri = 0;
            while (repl[ri] != 0 && rp < MAX_LINE_LEN - 1) {
                if (repl[ri] == '&') {
                    // Copy matched text
                    int mi = 0;
                    while (mi < mend && rp < MAX_LINE_LEN - 1) {
                        result[rp] = line[p + mi];
                        rp = rp + 1;
                        mi = mi + 1;
                    }
                } else if (repl[ri] == '\\' && repl[ri + 1] != 0) {
                    ri = ri + 1;
                    if (repl[ri] == 'n') { result[rp] = '\n'; rp = rp + 1; }
                    else if (repl[ri] == 't') { result[rp] = '\t'; rp = rp + 1; }
                    else { result[rp] = repl[ri]; rp = rp + 1; }
                } else {
                    result[rp] = repl[ri];
                    rp = rp + 1;
                }
                ri = ri + 1;
            }
            p = p + mend;
            subs = subs + 1;
            if (gflag == 0 && count_tgt <= 0) {
                // Only first match for non-global
                while (line[p] != 0 && rp < MAX_LINE_LEN - 1) {
                    result[rp] = line[p];
                    rp = rp + 1;
                    p = p + 1;
                }
                break;
            }
        } else {
            // Copy matched text literally
            int mi = 0;
            while (mi < mend && rp < MAX_LINE_LEN - 1) {
                result[rp] = line[p + mi];
                rp = rp + 1;
                mi = mi + 1;
            }
            p = p + mend;
        }
    }
    result[rp] = 0;

    if (subs > 0) {
        ed_replace_line(linenum, result);
    }
    return subs;
}

// ── Command execution ──────────────────────────────────────────────
void ed_exec_cmd(char *cmdline) {
    int pos = 0;
    int addr1;
    int addr2;
    int has_range = 0;
    int i;
    int j;
    int n;

    while (cmdline[pos] == ' ' || cmdline[pos] == '\t') pos = pos + 1;

    // Skip optional ':' prefix (vi/vim compatibility)
    if (cmdline[pos] == ':') pos = pos + 1;

    // Empty line: advance and print next line
    if (cmdline[pos] == 0) {
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (ed_cur < ed_nlines) {
            ed_cur = ed_cur + 1;
            print((char*)ed_lines[ed_cur - 1]);
            print("\n");
        } else {
            ed_error("invalid address");
        }
        return;
    }

    // Peek ahead to find the command character for default addresses
    int peek_pos = pos;
    int pa = ed_parse_addr(cmdline, &peek_pos);
    while (cmdline[peek_pos] == ' ') peek_pos = peek_pos + 1;
    if (cmdline[peek_pos] == ',' || cmdline[peek_pos] == ';') {
        peek_pos = peek_pos + 1;
        int pa2 = ed_parse_addr(cmdline, &peek_pos);
    }
    while (cmdline[peek_pos] == ' ') peek_pos = peek_pos + 1;
    int upcoming = cmdline[peek_pos];

    // Set defaults based on command
    int def1 = ed_cur;
    int def2 = ed_cur;
    if (upcoming == 'w' || upcoming == 'W' || upcoming == 'g' ||
        upcoming == 'v') {
        def1 = 1;
        def2 = ed_nlines;
    }

    addr1 = def1;
    addr2 = def2;

    // Parse address range
    if (cmdline[pos] == '%') {
        addr1 = 1;
        addr2 = ed_nlines;
        pos = pos + 1;
        has_range = 1;
    } else if (cmdline[pos] == ',' && cmdline[pos + 1] != 0) {
        addr1 = 1;
        addr2 = ed_nlines;
        pos = pos + 1;
        has_range = 1;
    } else {
        int a1 = ed_parse_addr(cmdline, &pos);
        if (a1 == -2) return; // error reported
        if (a1 >= 0) {
            addr1 = a1;
            addr2 = a1;
            has_range = 1;
        }
        while (cmdline[pos] == ' ') pos = pos + 1;
        if (cmdline[pos] == ',' || cmdline[pos] == ';') {
            int sep = cmdline[pos];
            pos = pos + 1;
            if (sep == ';') ed_cur = addr1;
            int a2 = ed_parse_addr(cmdline, &pos);
            if (a2 == -2) return;
            if (a2 >= 0) {
                addr2 = a2;
            } else {
                addr2 = ed_nlines;
            }
        }
    }

    while (cmdline[pos] == ' ') pos = pos + 1;
    int cmd = cmdline[pos];

    // Handle line-number-only input (bare address)
    if (cmd == 0 && has_range) {
        if (addr2 >= 1 && addr2 <= ed_nlines) {
            ed_cur = addr2;
            print((char*)ed_lines[ed_cur - 1]);
            print("\n");
        } else {
            ed_error("invalid address");
        }
        return;
    }

    // Command dispatch
    if (cmd == 'a') {
        // ── Append ──
        if (has_range == 0) addr1 = ed_cur;
        if (ed_nlines == 0) addr1 = 0;
        else if (addr1 < 0 || addr1 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        pos = pos + 1;
        ed_save_undo();
        ed_input_mode(addr1);
    } else if (cmd == 'i') {
        // ── Insert ──
        if (has_range == 0) addr1 = ed_cur;
        if (ed_nlines == 0) addr1 = 0;
        else if (addr1 < 1) addr1 = 1;
        if (addr1 > ed_nlines && ed_nlines > 0) {
            ed_error("invalid address");
            return;
        }
        pos = pos + 1;
        ed_save_undo();
        if (addr1 > 0) {
            ed_input_mode(addr1 - 1);
        } else {
            ed_input_mode(0);
        }
    } else if (cmd == 'c') {
        // ── Change ──
        if (ed_nlines == 0) {
            pos = pos + 1;
            ed_save_undo();
            ed_input_mode(0);
            return;
        }
        if (addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        pos = pos + 1;
        ed_save_undo();
        int insert_at = addr1 - 1;
        i = addr2;
        while (i >= addr1) {
            ed_delete_line(i);
            i = i - 1;
        }
        ed_input_mode(insert_at);
    } else if (cmd == 'd') {
        // ── Delete ──
        if (ed_nlines == 0 || addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        ed_save_undo();
        i = addr2;
        while (i >= addr1) {
            ed_delete_line(i);
            i = i - 1;
        }
        if (ed_nlines == 0) {
            ed_cur = 0;
        } else if (addr1 <= ed_nlines) {
            ed_cur = addr1;
        } else {
            ed_cur = ed_nlines;
        }
        ed_dirty = 1;
    } else if (cmd == 'p') {
        // ── Print ──
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        i = addr1;
        while (i <= addr2) {
            char *line = (char*)ed_lines[i - 1];
            if (line != 0) print(line);
            print("\n");
            i = i + 1;
        }
        ed_cur = addr2;
    } else if (cmd == 'n') {
        // ── Number ──
        if (ed_nlines == 0 || addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        i = addr1;
        while (i <= addr2) {
            print_int(i);
            putchar('\t');
            char *line = (char*)ed_lines[i - 1];
            if (line != 0) print(line);
            print("\n");
            i = i + 1;
        }
        ed_cur = addr2;
    } else if (cmd == 'l') {
        // ── List (show escaped) ──
        if (ed_nlines == 0 || addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        i = addr1;
        while (i <= addr2) {
            char *line = (char*)ed_lines[i - 1];
            if (line != 0) {
                j = 0;
                while (line[j] != 0) {
                    int ch = line[j];
                    if (ch == '\\') print("\\\\");
                    else if (ch == '\t') print("\\t");
                    else if (ch == '\b') print("\\b");
                    else if (ch == '\r') print("\\r");
                    else if (ch < 32 || ch == 127) {
                        print("\\x");
                        print_hex_byte(ch);
                    } else {
                        putchar(ch);
                    }
                    j = j + 1;
                }
            }
            print("$\n");
            i = i + 1;
        }
        ed_cur = addr2;
    } else if (cmd == '=') {
        // ── Print line number ──
        if (has_range == 0) addr2 = ed_nlines;
        print_int(addr2);
        print("\n");
    } else if (cmd == 'q') {
        // ── Quit ──
        if (ed_dirty) {
            ed_error("warning: buffer modified");
            ed_dirty = 0; // allow second q to quit
            return;
        }
        ed_quit = 1;
    } else if (cmd == 'Q') {
        // ── Quit unconditionally ──
        ed_quit = 1;
    } else if (cmd == 'w') {
        // ── Write ──
        pos = pos + 1;
        // Check for wq
        int do_quit = 0;
        if (cmdline[pos] == 'q') {
            do_quit = 1;
            pos = pos + 1;
        }
        while (cmdline[pos] == ' ') pos = pos + 1;
        if (cmdline[pos] != 0) {
            // New filename
            int fi = 0;
            while (cmdline[pos] != 0 && cmdline[pos] != ' ' && fi < FNAME_LEN - 1) {
                ed_fname[fi] = cmdline[pos];
                fi = fi + 1;
                pos = pos + 1;
            }
            ed_fname[fi] = 0;
        }
        if (ed_fname[0] == 0) {
            ed_error("no filename");
            return;
        }
        if (has_range == 0) {
            addr1 = 1;
            addr2 = ed_nlines;
        }
        if (ed_nlines == 0) {
            // Write empty file
            char path[256];
            resolve_path(ed_fname, path);
            int fd = vfs_open(path, VFS_WRONLY + VFS_CREAT + VFS_TRUNC);
            if (fd >= 0) {
                vfs_close(fd);
                print_int(0);
                print("\n");
                ed_dirty = 0;
            } else {
                ed_error("write failed");
            }
        } else {
            int bytes = ed_write_file(ed_fname, addr1, addr2);
            if (bytes >= 0) {
                print_int(bytes);
                print("\n");
                ed_dirty = 0;
            }
        }
        if (do_quit) ed_quit = 1;
    } else if (cmd == 'W') {
        // ── Write append ──
        pos = pos + 1;
        while (cmdline[pos] == ' ') pos = pos + 1;
        if (cmdline[pos] != 0) {
            int fi = 0;
            while (cmdline[pos] != 0 && cmdline[pos] != ' ' && fi < FNAME_LEN - 1) {
                ed_fname[fi] = cmdline[pos];
                fi = fi + 1;
                pos = pos + 1;
            }
            ed_fname[fi] = 0;
        }
        if (ed_fname[0] == 0) {
            ed_error("no filename");
            return;
        }
        if (has_range == 0) {
            addr1 = 1;
            addr2 = ed_nlines;
        }
        // Read existing file, append new lines, rewrite
        char path[256];
        resolve_path(ed_fname, path);
        int existing_size = 0;
        int existing_ptr = 0;
        int efd = vfs_open(path, VFS_RDONLY);
        if (efd >= 0) {
            // Read entire existing file
            char *ebuf = (char*)kmalloc(8192);
            if (ebuf != 0) {
                char rtmp[256];
                int er = vfs_read(efd, rtmp, 255);
                while (er > 0 && existing_size < 8000) {
                    int ei = 0;
                    while (ei < er && existing_size < 8000) {
                        ebuf[existing_size] = rtmp[ei];
                        existing_size = existing_size + 1;
                        ei = ei + 1;
                    }
                    er = vfs_read(efd, rtmp, 255);
                }
                existing_ptr = (int)ebuf;
            }
            vfs_close(efd);
        }
        // Calculate new data
        int new_bytes = 0;
        i = addr1;
        while (i <= addr2) {
            char *line = (char*)ed_lines[i - 1];
            if (line != 0) new_bytes = new_bytes + strlen(line);
            new_bytes = new_bytes + 1;
            i = i + 1;
        }
        int total = existing_size + new_bytes;
        char *combined = (char*)kmalloc(total + 1);
        if (combined == 0) {
            if (existing_ptr != 0) kfree((void*)existing_ptr);
            ed_error("out of memory");
            return;
        }
        if (existing_ptr != 0 && existing_size > 0) {
            memcpy(combined, (void*)existing_ptr, existing_size);
            kfree((void*)existing_ptr);
        }
        int wpos = existing_size;
        i = addr1;
        while (i <= addr2) {
            char *line = (char*)ed_lines[i - 1];
            if (line != 0) {
                int len = strlen(line);
                memcpy(combined + wpos, line, len);
                wpos = wpos + len;
            }
            combined[wpos] = '\n';
            wpos = wpos + 1;
            i = i + 1;
        }
        int wfd = vfs_open(path, VFS_WRONLY + VFS_CREAT + VFS_TRUNC);
        if (wfd >= 0) {
            vfs_write(wfd, combined, total);
            vfs_close(wfd);
            print_int(new_bytes);
            print("\n");
            ed_dirty = 0;
        } else {
            ed_error("write failed");
        }
        kfree(combined);
    } else if (cmd == 'r') {
        // ── Read file into buffer ──
        pos = pos + 1;
        while (cmdline[pos] == ' ') pos = pos + 1;
        char rfname[64];
        int fi = 0;
        if (cmdline[pos] != 0) {
            while (cmdline[pos] != 0 && cmdline[pos] != ' ' && fi < FNAME_LEN - 1) {
                rfname[fi] = cmdline[pos];
                fi = fi + 1;
                pos = pos + 1;
            }
            rfname[fi] = 0;
        } else {
            ed_strcopy(rfname, ed_fname);
        }
        if (rfname[0] == 0) {
            ed_error("no filename");
            return;
        }
        char rpath[256];
        resolve_path(rfname, rpath);
        int rfd = vfs_open(rpath, VFS_RDONLY);
        if (rfd < 0) {
            ed_error("cannot open file");
            return;
        }
        char bigbuf[4096];
        int rtotal = 0;
        char rtmp[256];
        int rr = vfs_read(rfd, rtmp, 255);
        while (rr > 0 && rtotal < 4095) {
            int ri = 0;
            while (ri < rr && rtotal < 4095) {
                bigbuf[rtotal] = rtmp[ri];
                rtotal = rtotal + 1;
                ri = ri + 1;
            }
            rr = vfs_read(rfd, rtmp, 255);
        }
        vfs_close(rfd);
        bigbuf[rtotal] = 0;
        int insert_after = addr2;
        if (has_range == 0) insert_after = ed_nlines;
        int bytes = ed_load_text(bigbuf, rtotal, insert_after);
        print_int(bytes);
        print("\n");
        ed_dirty = 1;
    } else if (cmd == 'e' || cmd == 'E') {
        // ── Edit (load new file) ──
        if (cmd == 'e' && ed_dirty) {
            ed_error("warning: buffer modified");
            ed_dirty = 0;
            return;
        }
        // Clear buffer
        i = 0;
        while (i < ed_nlines) {
            if (ed_lines[i] != 0) kfree((void*)ed_lines[i]);
            ed_lines[i] = 0;
            i = i + 1;
        }
        ed_nlines = 0;
        ed_cur = 0;
        ed_dirty = 0;
        pos = pos + 1;
        while (cmdline[pos] == ' ') pos = pos + 1;
        if (cmdline[pos] != 0) {
            int fi = 0;
            while (cmdline[pos] != 0 && cmdline[pos] != ' ' && fi < FNAME_LEN - 1) {
                ed_fname[fi] = cmdline[pos];
                fi = fi + 1;
                pos = pos + 1;
            }
            ed_fname[fi] = 0;
        }
        if (ed_fname[0] == 0) {
            ed_error("no filename");
            return;
        }
        int bytes = ed_read_file(ed_fname);
        if (bytes >= 0) {
            print_int(bytes);
            print("\n");
        } else {
            print(ed_fname);
            print(": No such file\n");
        }
    } else if (cmd == 'f') {
        // ── Filename ──
        pos = pos + 1;
        while (cmdline[pos] == ' ') pos = pos + 1;
        if (cmdline[pos] != 0) {
            int fi = 0;
            while (cmdline[pos] != 0 && cmdline[pos] != ' ' && fi < FNAME_LEN - 1) {
                ed_fname[fi] = cmdline[pos];
                fi = fi + 1;
                pos = pos + 1;
            }
            ed_fname[fi] = 0;
        }
        if (ed_fname[0] != 0) {
            print(ed_fname);
            print("\n");
        } else {
            ed_error("no filename");
        }
    } else if (cmd == 's') {
        // ── Substitute ──
        pos = pos + 1;
        int delim = cmdline[pos];
        if (delim == 0 || delim == ' ' || delim == '\n') {
            // Repeat last substitution
            if (ed_lpat[0] == 0) {
                ed_error("no previous substitution");
                return;
            }
            ed_save_undo();
            int total = 0;
            i = addr1;
            while (i <= addr2) {
                total = total + ed_sub_line(i, ed_lpat, ed_lrep, 0, 0);
                i = i + 1;
            }
            if (total == 0) ed_error("no match");
            else ed_dirty = 1;
            return;
        }
        pos = pos + 1;
        // Parse pattern
        char pattern[256];
        int pi = 0;
        while (cmdline[pos] != 0 && cmdline[pos] != (char)delim && pi < 255) {
            if (cmdline[pos] == '\\' && cmdline[pos + 1] != 0) {
                pattern[pi] = cmdline[pos]; pi = pi + 1; pos = pos + 1;
                pattern[pi] = cmdline[pos]; pi = pi + 1; pos = pos + 1;
            } else {
                pattern[pi] = cmdline[pos]; pi = pi + 1; pos = pos + 1;
            }
        }
        pattern[pi] = 0;
        if (cmdline[pos] == (char)delim) pos = pos + 1;
        // Parse replacement
        char repl[256];
        int ri = 0;
        while (cmdline[pos] != 0 && cmdline[pos] != (char)delim && ri < 255) {
            if (cmdline[pos] == '\\' && cmdline[pos + 1] != 0) {
                repl[ri] = cmdline[pos]; ri = ri + 1; pos = pos + 1;
                repl[ri] = cmdline[pos]; ri = ri + 1; pos = pos + 1;
            } else {
                repl[ri] = cmdline[pos]; ri = ri + 1; pos = pos + 1;
            }
        }
        repl[ri] = 0;
        if (cmdline[pos] == (char)delim) pos = pos + 1;
        // Parse flags
        int gflag = 0;
        int pflag = 0;
        int nflag = 0;
        int count_tgt = 0;
        while (cmdline[pos] != 0) {
            if (cmdline[pos] == 'g') gflag = 1;
            else if (cmdline[pos] == 'p') pflag = 1;
            else if (cmdline[pos] == 'n') nflag = 1;
            else if (ed_isdigit(cmdline[pos])) {
                count_tgt = count_tgt * 10 + cmdline[pos] - '0';
            }
            pos = pos + 1;
        }
        // Use last pattern if empty
        if (pi == 0) {
            if (ed_lpat[0] == 0) {
                ed_error("no previous pattern");
                return;
            }
            ed_strcopy(pattern, ed_lpat);
        } else {
            ed_strcopy(ed_lpat, pattern);
        }
        ed_strcopy(ed_lrep, repl);

        ed_save_undo();
        int total_subs = 0;
        i = addr1;
        while (i <= addr2) {
            n = ed_sub_line(i, pattern, repl, gflag, count_tgt);
            if (n > 0) {
                total_subs = total_subs + n;
                ed_cur = i;
            }
            i = i + 1;
        }
        if (total_subs == 0) {
            ed_error("no match");
            return;
        }
        ed_dirty = 1;
        if (pflag || nflag) {
            if (nflag) {
                print_int(ed_cur);
                putchar('\t');
            }
            char *line = (char*)ed_lines[ed_cur - 1];
            if (line != 0) print(line);
            print("\n");
        }
    } else if (cmd == 'm') {
        // ── Move lines ──
        if (ed_nlines == 0 || addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        pos = pos + 1;
        int dest = ed_parse_addr(cmdline, &pos);
        if (dest < 0) dest = ed_cur;
        if (dest >= addr1 && dest <= addr2) {
            ed_error("invalid destination");
            return;
        }
        ed_save_undo();
        // Collect lines to move
        int count = addr2 - addr1 + 1;
        int *tmp = (int*)kmalloc(count * 4);
        if (tmp == 0) {
            ed_error("out of memory");
            return;
        }
        i = 0;
        while (i < count) {
            tmp[i] = ed_lines[addr1 - 1 + i];
            i = i + 1;
        }
        // Remove from source (shift up)
        i = addr1 - 1;
        while (i < ed_nlines - count) {
            ed_lines[i] = ed_lines[i + count];
            i = i + 1;
        }
        ed_nlines = ed_nlines - count;
        // Adjust destination
        if (dest > addr2) dest = dest - count;
        // Insert at destination
        i = ed_nlines - 1;
        while (i >= dest) {
            ed_lines[i + count] = ed_lines[i];
            i = i - 1;
        }
        i = 0;
        while (i < count) {
            ed_lines[dest + i] = tmp[i];
            i = i + 1;
        }
        ed_nlines = ed_nlines + count;
        ed_cur = dest + count;
        ed_dirty = 1;
        kfree(tmp);
    } else if (cmd == 't') {
        // ── Transfer (copy) lines ──
        if (ed_nlines == 0 || addr1 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        pos = pos + 1;
        int dest = ed_parse_addr(cmdline, &pos);
        if (dest < 0) dest = ed_cur;
        if (dest > ed_nlines) dest = ed_nlines;
        ed_save_undo();
        int count = addr2 - addr1 + 1;
        i = 0;
        while (i < count) {
            char *src = (char*)ed_lines[addr1 - 1 + i];
            if (src != 0) {
                ed_insert_line(dest + i, src);
            }
            i = i + 1;
        }
        ed_cur = dest + count;
        ed_dirty = 1;
    } else if (cmd == 'j') {
        // ── Join lines ──
        if (has_range == 0) {
            addr1 = ed_cur;
            addr2 = ed_cur + 1;
        }
        if (addr1 < 1 || addr2 > ed_nlines || addr1 >= addr2) {
            if (addr1 == addr2) return; // joining single line is no-op
            ed_error("invalid address");
            return;
        }
        ed_save_undo();
        // Calculate total length
        int total_len = 0;
        i = addr1;
        while (i <= addr2) {
            char *line = (char*)ed_lines[i - 1];
            if (line != 0) total_len = total_len + strlen(line);
            i = i + 1;
        }
        if (total_len >= MAX_LINE_LEN - 1) total_len = MAX_LINE_LEN - 2;
        char *joined = (char*)kmalloc(total_len + 1);
        if (joined == 0) {
            ed_error("out of memory");
            return;
        }
        int jp = 0;
        i = addr1;
        while (i <= addr2) {
            char *line = (char*)ed_lines[i - 1];
            if (line != 0) {
                j = 0;
                while (line[j] != 0 && jp < total_len) {
                    joined[jp] = line[j];
                    jp = jp + 1;
                    j = j + 1;
                }
            }
            i = i + 1;
        }
        joined[jp] = 0;
        // Replace first line, delete rest
        ed_replace_line(addr1, joined);
        i = addr2;
        while (i > addr1) {
            ed_delete_line(i);
            i = i - 1;
        }
        ed_cur = addr1;
        ed_dirty = 1;
        kfree(joined);
    } else if (cmd == 'k') {
        // ── Mark ──
        pos = pos + 1;
        int mark_ch = cmdline[pos];
        if (mark_ch < 'a' || mark_ch > 'z') {
            ed_error("invalid mark");
            return;
        }
        if (has_range == 0) addr2 = ed_cur;
        if (addr2 < 1 || addr2 > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        ed_marks[mark_ch - 'a'] = addr2;
    } else if (cmd == 'u') {
        // ── Undo ──
        ed_restore_undo();
    } else if (cmd == 'g') {
        // ── Global ──
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (has_range == 0) {
            addr1 = 1;
            addr2 = ed_nlines;
        }
        pos = pos + 1;
        if (cmdline[pos] == 0) {
            ed_error("invalid command suffix");
            return;
        }
        int gdelim = cmdline[pos];
        pos = pos + 1;
        char gpat[256];
        int gpi = 0;
        while (cmdline[pos] != 0 && cmdline[pos] != (char)gdelim && gpi < 255) {
            gpat[gpi] = cmdline[pos];
            gpi = gpi + 1;
            pos = pos + 1;
        }
        gpat[gpi] = 0;
        if (cmdline[pos] == (char)gdelim) pos = pos + 1;
        if (gpi == 0) {
            if (ed_lpat[0] == 0) {
                ed_error("no previous pattern");
                return;
            }
            ed_strcopy(gpat, ed_lpat);
        } else {
            ed_strcopy(ed_lpat, gpat);
        }
        // Get command to execute
        char *gcmd = cmdline + pos;
        if (*gcmd == 0) gcmd = "p";
        // Mark matching lines
        char *marked = (char*)kmalloc(MAX_LINES);
        if (marked == 0) {
            ed_error("out of memory");
            return;
        }
        memset(marked, 0, MAX_LINES);
        i = addr1;
        while (i <= addr2) {
            char *line = (char*)ed_lines[i - 1];
            if (line != 0 && ed_regex_search(gpat, line) >= 0) {
                marked[i - 1] = 1;
            }
            i = i + 1;
        }
        // Execute command on marked lines
        i = 0;
        while (i < ed_nlines) {
            if (marked[i] == 1) {
                ed_cur = i + 1;
                char gcmd_buf[512];
                char numstr[16];
                ed_int_to_str(ed_cur, numstr);
                int gi = 0;
                j = 0;
                while (numstr[j] != 0 && gi < 500) {
                    gcmd_buf[gi] = numstr[j];
                    gi = gi + 1;
                    j = j + 1;
                }
                j = 0;
                while (gcmd[j] != 0 && gi < 500) {
                    gcmd_buf[gi] = gcmd[j];
                    gi = gi + 1;
                    j = j + 1;
                }
                gcmd_buf[gi] = 0;
                ed_exec_cmd(gcmd_buf);
            }
            i = i + 1;
        }
        kfree(marked);
    } else if (cmd == 'v') {
        // ── Inverse global ──
        if (ed_nlines == 0) {
            ed_error("invalid address");
            return;
        }
        if (has_range == 0) {
            addr1 = 1;
            addr2 = ed_nlines;
        }
        pos = pos + 1;
        if (cmdline[pos] == 0) {
            ed_error("invalid command suffix");
            return;
        }
        int vdelim = cmdline[pos];
        pos = pos + 1;
        char vpat[256];
        int vpi = 0;
        while (cmdline[pos] != 0 && cmdline[pos] != (char)vdelim && vpi < 255) {
            vpat[vpi] = cmdline[pos];
            vpi = vpi + 1;
            pos = pos + 1;
        }
        vpat[vpi] = 0;
        if (cmdline[pos] == (char)vdelim) pos = pos + 1;
        if (vpi == 0) {
            if (ed_lpat[0] == 0) {
                ed_error("no previous pattern");
                return;
            }
            ed_strcopy(vpat, ed_lpat);
        } else {
            ed_strcopy(ed_lpat, vpat);
        }
        char *vcmd = cmdline + pos;
        if (*vcmd == 0) vcmd = "p";
        char *vmarked = (char*)kmalloc(MAX_LINES);
        if (vmarked == 0) {
            ed_error("out of memory");
            return;
        }
        memset(vmarked, 0, MAX_LINES);
        i = addr1;
        while (i <= addr2) {
            char *line = (char*)ed_lines[i - 1];
            if (line == 0 || ed_regex_search(vpat, line) < 0) {
                vmarked[i - 1] = 1;
            }
            i = i + 1;
        }
        i = 0;
        while (i < ed_nlines) {
            if (vmarked[i] == 1) {
                ed_cur = i + 1;
                char vcmd_buf[512];
                char numstr[16];
                ed_int_to_str(ed_cur, numstr);
                int vi = 0;
                j = 0;
                while (numstr[j] != 0 && vi < 500) {
                    vcmd_buf[vi] = numstr[j];
                    vi = vi + 1;
                    j = j + 1;
                }
                j = 0;
                while (vcmd[j] != 0 && vi < 500) {
                    vcmd_buf[vi] = vcmd[j];
                    vi = vi + 1;
                    j = j + 1;
                }
                vcmd_buf[vi] = 0;
                ed_exec_cmd(vcmd_buf);
            }
            i = i + 1;
        }
        kfree(vmarked);
    } else if (cmd == 'H') {
        // ── Toggle error messages ──
        if (ed_show_errs == 0) ed_show_errs = 1;
        else ed_show_errs = 0;
        if (ed_show_errs && ed_lerr[0] != 0) {
            print(ed_lerr);
            print("\n");
        }
    } else if (cmd == 'h') {
        // ── Show last error ──
        if (ed_lerr[0] != 0) {
            print(ed_lerr);
            print("\n");
        }
    } else if (cmd == 'P') {
        // ── Prompt toggle (accepted, no-op) ──
    } else if (cmd == '+') {
        // ── Forward navigation ──
        pos = pos + 1;
        int offset = 1;
        if (ed_isdigit(cmdline[pos])) offset = ed_parse_int_at(cmdline, &pos);
        int target = ed_cur + offset;
        if (target < 1 || target > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        ed_cur = target;
        print((char*)ed_lines[ed_cur - 1]);
        print("\n");
    } else if (cmd == '-') {
        // ── Backward navigation ──
        pos = pos + 1;
        int offset = 1;
        if (ed_isdigit(cmdline[pos])) offset = ed_parse_int_at(cmdline, &pos);
        int target = ed_cur - offset;
        if (target < 1 || target > ed_nlines) {
            ed_error("invalid address");
            return;
        }
        ed_cur = target;
        print((char*)ed_lines[ed_cur - 1]);
        print("\n");
    } else {
        ed_error("unknown command");
    }
}

// ── Main entry point ───────────────────────────────────────────────
void main() {
    // Initialize state
    ed_nlines = 0;
    ed_cur = 0;
    ed_dirty = 0;
    ed_quit = 0;
    ed_show_errs = 0;
    ed_lerr[0] = 0;
    ed_lpat[0] = 0;
    ed_lrep[0] = 0;
    ed_undo_nlines = 0;
    ed_undo_valid = 0;
    ed_fname[0] = 0;
    memset(ed_marks, 0, 104);
    memset(ed_lines, 0, 4096);
    memset(ed_undo_lines, 0, 4096);

    // Get filename from args
    char *args = (char*)get_args();
    if (args != 0 && strlen(args) > 0) {
        int fi = 0;
        while (args[fi] != 0 && args[fi] != ' ' && fi < FNAME_LEN - 1) {
            ed_fname[fi] = args[fi];
            fi = fi + 1;
        }
        ed_fname[fi] = 0;

        int bytes = ed_read_file(ed_fname);
        if (bytes >= 0) {
            print_int(bytes);
            print("\n");
        } else {
            print(ed_fname);
            print(": No such file\n");
        }
    }

    // Main command loop
    char cmd[512];
    while (ed_quit == 0) {
        ed_readline(cmd, CMD_BUF);
        ed_exec_cmd(cmd);
    }

    // Clean up
    int i = 0;
    while (i < ed_nlines) {
        if (ed_lines[i] != 0) kfree((void*)ed_lines[i]);
        i = i + 1;
    }
    i = 0;
    while (i < ed_undo_nlines) {
        if (ed_undo_lines[i] != 0) kfree((void*)ed_undo_lines[i]);
        i = i + 1;
    }
}
