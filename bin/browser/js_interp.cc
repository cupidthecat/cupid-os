/* §7 JavaScript tree-walking interpreter. F1b scope:
 *   - primitives: number (double), string, bool, null, undefined
 *   - operators: + - * / %, == !=  !==, < <= > >=, && || !
 *               assignment + compound, prefix/postfix ++/--
 *   - control flow: if/else, while, for(init;cond;step), break/continue
 *   - var declarations (let/const treated as var; const-write check
 *     deferred until F1c gives us the binding kind tag at lookup)
 *   - console.log builtin: stringify args, route to serial_printf and
 *     status_msg
 * Functions, objects, arrays land in F1c / F1d.*/

/* JavaScript treats both zero encodings and NaN as false. */
int js_number_truth(double v) {
    return v != 0.0 && v == v;
}

/* value stack helpers */
int js_push_slot() {
    if (jvs_top >= MAX_JS_VS) {
        js_set_err("js: value stack overflow");
        return -1;
    }
    int t = jvs_top;
    jvs_tag[t] = JS_VAL_UNDEF; jvs_num[t] = 0.0;
    jvs_str_off[t] = -1; jvs_str_len[t] = 0;
    jvs_obj_idx[t] = -1; jvs_dom_idx[t] = -1; jvs_native_id[t] = 0;
    jvs_top = t + 1;
    return t;
}
void js_push_undef() {
    js_push_slot();
}
void js_push_null() {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t] = JS_VAL_NULL;
}
void js_push_num(double v) {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t] = JS_VAL_NUM; jvs_num[t] = v;
}
void js_push_bool(int b) {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t] = JS_VAL_BOOL; jvs_num[t] = b ? 1.0 : 0.0;
}
void js_push_str(int off, int len) {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t] = JS_VAL_STR; jvs_str_off[t] = off; jvs_str_len[t] = len;
}

void js_pop() { if (jvs_top > 0) jvs_top = jvs_top - 1; }

int js_copy_top_from(int src) {
    if (src < 0 || src >= jvs_top) {
        js_set_err("js: invalid value stack copy");
        return -1;
    }
    int t = js_push_slot();
    if (t < 0) return -1;
    jvs_tag[t]      = jvs_tag[src];
    jvs_num[t]      = jvs_num[src];
    jvs_str_off[t]  = jvs_str_off[src];
    jvs_str_len[t]  = jvs_str_len[src];
    jvs_obj_idx[t]  = jvs_obj_idx[src];
    jvs_dom_idx[t]  = jvs_dom_idx[src];
    jvs_native_id[t]= jvs_native_id[src];
    return 0;
}

/* coercion */
double js_nan_value() {
    return 0.0 / 0.0;
}

int js_utf8_next(char *s, int end, int *index) {
    int i = *index;
    if (i >= end) return -1;
    int b0 = (unsigned char)s[i];
    if (b0 < 0x80) {
        *index = i + 1;
        return b0;
    }

    if (b0 >= 0xC2 && b0 <= 0xDF && i + 1 < end) {
        int b1 = (unsigned char)s[i + 1];
        if (b1 >= 0x80 && b1 <= 0xBF) {
            *index = i + 2;
            return ((b0 & 0x1F) << 6) | (b1 & 0x3F);
        }
    }

    if (b0 >= 0xE0 && b0 <= 0xEF && i + 2 < end) {
        int b1 = (unsigned char)s[i + 1];
        int b2 = (unsigned char)s[i + 2];
        int valid_second = b1 >= 0x80 && b1 <= 0xBF;
        if (b0 == 0xE0 && b1 < 0xA0) valid_second = 0;
        if (b0 == 0xED && b1 >= 0xA0) valid_second = 0;
        if (valid_second && b2 >= 0x80 && b2 <= 0xBF) {
            *index = i + 3;
            return ((b0 & 0x0F) << 12) |
                   ((b1 & 0x3F) << 6) | (b2 & 0x3F);
        }
    }

    if (b0 >= 0xF0 && b0 <= 0xF4 && i + 3 < end) {
        int b1 = (unsigned char)s[i + 1];
        int b2 = (unsigned char)s[i + 2];
        int b3 = (unsigned char)s[i + 3];
        int valid_second = b1 >= 0x80 && b1 <= 0xBF;
        if (b0 == 0xF0 && b1 < 0x90) valid_second = 0;
        if (b0 == 0xF4 && b1 > 0x8F) valid_second = 0;
        if (valid_second && b2 >= 0x80 && b2 <= 0xBF &&
            b3 >= 0x80 && b3 <= 0xBF) {
            *index = i + 4;
            return ((b0 & 0x07) << 18) |
                   ((b1 & 0x3F) << 12) |
                   ((b2 & 0x3F) << 6) | (b3 & 0x3F);
        }
    }

    /* Keep malformed input deterministic without reading past its byte. */
    *index = i + 1;
    return b0;
}

int js_number_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == 11 || c == 12 || c == 0x00A0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 ||
           c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000 || c == 0xFEFF;
}

void js_trim_number_string(char *s, int length,
                           int *start_out, int *end_out) {
    int i = 0;
    int start = length;
    int end = 0;
    int found = 0;
    while (i < length) {
        int byte_start = i;
        int codepoint = js_utf8_next(s, length, &i);
        if (!js_number_space(codepoint)) {
            if (!found) start = byte_start;
            end = i;
            found = 1;
        }
    }
    if (!found) start = end;
    *start_out = start;
    *end_out = end;
}

int js_number_string_matches(char *s, int start, int end, char *word) {
    int i = start;
    int w = 0;
    while (i < end && word[w] && s[i] == word[w]) {
        i = i + 1;
        w = w + 1;
    }
    return i == end && word[w] == 0;
}

double js_to_number_at(int idx) {
    int t = jvs_tag[idx];
    if (t == JS_VAL_NUM)  return jvs_num[idx];
    if (t == JS_VAL_BOOL) return jvs_num[idx];
    if (t == JS_VAL_NULL) return 0.0;
    if (t == JS_VAL_UNDEF) return js_nan_value();
    if (t == JS_VAL_STR) {
        char *s = js_str_pool + jvs_str_off[idx];
        int i = 0;
        int end = jvs_str_len[idx];
        js_trim_number_string(s, end, &i, &end);
        if (i == end) return 0.0;

        int sign_negative = 0;
        int sign_present = 0;
        if (s[i] == '-' || s[i] == '+') {
            sign_negative = s[i] == '-';
            sign_present = 1;
            i = i + 1;
        }
        if (i == end) return js_nan_value();

        if (js_number_string_matches(s, i, end, "Infinity")) {
            double infinity = 1.0 / 0.0;
            if (sign_negative) return -infinity;
            return infinity;
        }

        if (!sign_present && i + 2 <= end && s[i] == '0' &&
            (s[i + 1] == 'x' || s[i + 1] == 'X' ||
             s[i + 1] == 'b' || s[i + 1] == 'B' ||
             s[i + 1] == 'o' || s[i + 1] == 'O')) {
            int prefix = s[i + 1];
            int base = 16;
            if (prefix == 'b' || prefix == 'B') base = 2;
            else if (prefix == 'o' || prefix == 'O') base = 8;
            i = i + 2;
            int digits = 0;
            double radix_value = 0.0;
            while (i < end) {
                int digit = js_digit_value((unsigned char)s[i]);
                if (digit < 0 || digit >= base) return js_nan_value();
                radix_value = radix_value * (double)base + (double)digit;
                digits = digits + 1;
                i = i + 1;
            }
            if (digits == 0) return js_nan_value();
            return radix_value;
        }

        double v = 0.0;
        int digits = 0;
        while (i < end && s[i] >= '0' && s[i] <= '9') {
            v = v * 10.0 + (double)(s[i] - '0');
            i = i + 1;
            digits = digits + 1;
        }
        if (i < end && s[i] == '.') {
            i = i + 1;
            double frac = 0.1;
            while (i < end && s[i] >= '0' && s[i] <= '9') {
                v = v + frac * (double)(s[i] - '0');
                frac = frac * 0.1;
                i = i + 1;
                digits = digits + 1;
            }
        }
        if (digits == 0) return js_nan_value();

        if (i < end && (s[i] == 'e' || s[i] == 'E')) {
            i = i + 1;
            int exponent_negative = 0;
            if (i < end && (s[i] == '+' || s[i] == '-')) {
                exponent_negative = s[i] == '-';
                i = i + 1;
            }
            int exponent = 0;
            int exponent_digits = 0;
            while (i < end && s[i] >= '0' && s[i] <= '9') {
                if (exponent < 400) {
                    exponent = exponent * 10 + (s[i] - '0');
                    if (exponent > 400) exponent = 400;
                }
                exponent_digits = exponent_digits + 1;
                i = i + 1;
            }
            if (exponent_digits == 0) return js_nan_value();
            while (exponent > 0) {
                v = exponent_negative ? v / 10.0 : v * 10.0;
                exponent = exponent - 1;
            }
        }
        if (i != end) return js_nan_value();
        if (sign_negative) return -v;
        return v;
    }
    return js_nan_value();
}

int js_to_bool_at(int idx) {
    int t = jvs_tag[idx];
    if (t == JS_VAL_UNDEF || t == JS_VAL_NULL) return 0;
    if (t == JS_VAL_BOOL) return js_number_truth(jvs_num[idx]);
    if (t == JS_VAL_NUM)  return js_number_truth(jvs_num[idx]);
    if (t == JS_VAL_STR) return jvs_str_len[idx] > 0;
    return 1;       /* objects/funcs always truthy */
}

/* Format an int into buf. Returns count. Caller's buf must be >=12 bytes. */
int js_format_int(int v, char *buf) {
    int b = 0;
    if (v == (-2147483647 - 1)) {
        char *minimum = "-2147483648";
        while (minimum[b]) { buf[b] = minimum[b]; b = b + 1; }
        buf[b] = 0;
        return b;
    }
    if (v < 0) { buf[b] = '-'; b = b + 1; v = 0 - v; }
    char tmp[16];
    int tn = 0;
    if (v == 0) { tmp[tn] = '0'; tn = tn + 1; }
    while (v > 0 && tn < 15) {
        tmp[tn] = '0' + (v % 10);
        v = v / 10;
        tn = tn + 1;
    }
    while (tn > 0) {
        tn = tn - 1;
        buf[b] = tmp[tn];
        b = b + 1;
    }
    buf[b] = 0;
    return b;
}

int js_format_positive_integer(double value, char *buf) {
    char reversed[32];
    int count = 0;
    if (value == 0.0) {
        buf[0] = '0';
        buf[1] = 0;
        return 1;
    }
    while (value >= 1.0 && count < 31) {
        int digit = (int)fmod(value, 10.0);
        reversed[count] = '0' + digit;
        count = count + 1;
        value = (value - (double)digit) / 10.0;
    }
    int length = count;
    for (int i = 0; i < length; i++) {
        count = count - 1;
        buf[i] = reversed[count];
    }
    buf[length] = 0;
    return length;
}

int js_format_large_uint_key(double value, char *buf) {
    return js_format_positive_integer(value, buf);
}

int js_append_fraction_six(double fraction, char *buf, int b) {
    int micros = (int)(fraction * 1000000.0);
    if (micros <= 0) return b;
    buf[b] = '.';
    b = b + 1;
    int trimmed = micros;
    while (trimmed > 0 && (trimmed % 10) == 0) trimmed = trimmed / 10;
    int leading = 6;
    int width = micros;
    while (width > 0) { width = width / 10; leading = leading - 1; }
    while (leading > 0) {
        buf[b] = '0';
        b = b + 1;
        leading = leading - 1;
    }
    char digits[16];
    int length = js_format_int(trimmed, digits);
    for (int i = 0; i < length; i++) {
        buf[b] = digits[i];
        b = b + 1;
    }
    return b;
}

/* Format a double without %f. The buffer must hold at least 64 bytes. */
int js_format_num(double v, char *buf) {
    int b = 0;
    char *special;
    double cancelled;
    if (v != v) {
        special = "NaN";
        while (special[b]) { buf[b] = special[b]; b = b + 1; }
        buf[b] = 0;
        return b;
    }
    cancelled = v + -v;
    if (cancelled != cancelled) {
        if (v < 0.0) { buf[b] = '-'; b = b + 1; }
        special = "Infinity";
        int i = 0;
        while (special[i]) { buf[b] = special[i]; b = b + 1; i = i + 1; }
        buf[b] = 0;
        return b;
    }
    if (v < 0.0) {
        buf[b] = '-'; b = b + 1;
        v = -v;
    }
    if (v >= 1e21 || (v > 0.0 && v < 0.000001)) {
        int exponent = 0;
        while (v >= 10.0 && exponent < 400) {
            v = v / 10.0;
            exponent = exponent + 1;
        }
        while (v > 0.0 && v < 1.0 && exponent > -400) {
            v = v * 10.0;
            exponent = exponent - 1;
        }
        int leading_digit = (int)v;
        buf[b] = '0' + leading_digit;
        b = b + 1;
        b = js_append_fraction_six(v - (double)leading_digit, buf, b);
        buf[b] = 'e';
        b = b + 1;
        if (exponent >= 0) {
            buf[b] = '+';
            b = b + 1;
        }
        char exponent_text[16];
        int exponent_length = js_format_int(exponent, exponent_text);
        for (int i = 0; i < exponent_length; i++) {
            buf[b] = exponent_text[i];
            b = b + 1;
        }
    } else {
        double fraction = fmod(v, 1.0);
        double integer_value = v - fraction;
        char integer_text[32];
        int integer_length =
            js_format_positive_integer(integer_value, integer_text);
        for (int i = 0; i < integer_length; i++) {
            buf[b] = integer_text[i];
            b = b + 1;
        }
        b = js_append_fraction_six(fraction, buf, b);
    }
    buf[b] = 0;
    return b;
}

int js_to_string_at(int idx, char *buf, int max) {
    int t = jvs_tag[idx];
    if (t == JS_VAL_UNDEF) {
        char *s = "undefined"; int i = 0;
        while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
        buf[i] = 0; return i;
    }
    if (t == JS_VAL_NULL) {
        char *s = "null"; int i = 0;
        while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
        buf[i] = 0; return i;
    }
    if (t == JS_VAL_BOOL) {
        char *s = js_number_truth(jvs_num[idx]) ? "true" : "false";
        int i = 0;
        while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
        buf[i] = 0; return i;
    }
    if (t == JS_VAL_NUM) {
        char fbuf[64];
        int fl = js_format_num(jvs_num[idx], fbuf);
        if (fl > max - 1) fl = max - 1;
        int fk = 0;
        while (fk < fl) { buf[fk] = fbuf[fk]; fk = fk + 1; }
        buf[fl] = 0;
        return fl;
    }
    if (t == JS_VAL_STR) {
        int i = 0;
        int n = jvs_str_len[idx];
        char *s = js_str_pool + jvs_str_off[idx];
        while (i < n && i < max - 1) { buf[i] = s[i]; i = i + 1; }
        buf[i] = 0; return i;
    }
    char *s = "[object]"; int i = 0;
    while (s[i] && i < max - 1) { buf[i] = s[i]; i = i + 1; }
    buf[i] = 0; return i;
}

void js_concat_at(int a, int b) {
    char left_scratch[64];
    char right_scratch[64];
    char *left = left_scratch;
    char *right = right_scratch;
    int left_len;
    int right_len;
    if (jvs_tag[a] == JS_VAL_STR) {
        left = js_str_pool + jvs_str_off[a];
        left_len = jvs_str_len[a];
    } else {
        left_len = js_to_string_at(a, left_scratch, 64);
    }
    if (jvs_tag[b] == JS_VAL_STR) {
        right = js_str_pool + jvs_str_off[b];
        right_len = jvs_str_len[b];
    } else {
        right_len = js_to_string_at(b, right_scratch, 64);
    }

    int available = JS_STR_POOL - js_str_pool_pos;
    if (left_len > available - 1 ||
        right_len > available - 1 - left_len) {
        js_set_err("js: string pool full");
        jvs_top = a;
        return;
    }

    int off = js_str_pool_pos;
    int p = 0;
    int i = 0;
    while (i < left_len) {
        js_str_pool[off + p] = left[i];
        p = p + 1;
        i = i + 1;
    }
    i = 0;
    while (i < right_len) {
        js_str_pool[off + p] = right[i];
        p = p + 1;
        i = i + 1;
    }
    js_str_pool[off + p] = 0;
    js_str_pool_pos = off + p + 1;
    jvs_top = a;
    js_push_str(off, p);
}

int js_utf16_next(char *s, int length,
                  int *index, int *pending) {
    if (*pending >= 0) {
        int unit = *pending;
        *pending = -1;
        return unit;
    }
    int codepoint = js_utf8_next(s, length, index);
    if (codepoint < 0) return -1;
    if (codepoint <= 0xFFFF) return codepoint;
    codepoint = codepoint - 0x10000;
    *pending = 0xDC00 | (codepoint & 0x3FF);
    return 0xD800 | (codepoint >> 10);
}

int js_string_compare_at(int a, int b) {
    char *left = js_str_pool + jvs_str_off[a];
    char *right = js_str_pool + jvs_str_off[b];
    int left_len = jvs_str_len[a];
    int right_len = jvs_str_len[b];
    int left_index = 0;
    int right_index = 0;
    int left_pending = -1;
    int right_pending = -1;
    while (1) {
        int lc = js_utf16_next(left, left_len,
                               &left_index, &left_pending);
        int rc = js_utf16_next(right, right_len,
                               &right_index, &right_pending);
        if (lc < 0 && rc < 0) return 0;
        if (lc < 0) return -1;
        if (rc < 0) return 1;
        if (lc < rc) return -1;
        if (lc > rc) return 1;
    }
}

int js_same_type_eq_at(int a, int b) {
    int tag = jvs_tag[a];
    if (tag != jvs_tag[b]) return 0;
    if (tag == JS_VAL_UNDEF || tag == JS_VAL_NULL) return 1;
    if (tag == JS_VAL_NUM || tag == JS_VAL_BOOL) {
        return jvs_num[a] == jvs_num[b];
    }
    if (tag == JS_VAL_STR) return js_string_compare_at(a, b) == 0;
    if (tag == JS_VAL_OBJ || tag == JS_VAL_ARR || tag == JS_VAL_FUNC) {
        return jvs_obj_idx[a] == jvs_obj_idx[b];
    }
    if (tag == JS_VAL_NATIVE) {
        return jvs_native_id[a] == jvs_native_id[b];
    }
    if (tag == JS_VAL_DOMNODE || tag == JS_VAL_STYLE) {
        return jvs_dom_idx[a] == jvs_dom_idx[b];
    }
    return 0;
}

int js_eq_at(int a, int b) {
    int ta = jvs_tag[a];
    int tb = jvs_tag[b];
    if (ta == tb) return js_same_type_eq_at(a, b);
    if ((ta == JS_VAL_NULL && tb == JS_VAL_UNDEF) ||
        (ta == JS_VAL_UNDEF && tb == JS_VAL_NULL)) return 1;
    if ((ta == JS_VAL_NUM && tb == JS_VAL_STR) ||
        (ta == JS_VAL_STR && tb == JS_VAL_NUM)) {
        return js_to_number_at(a) == js_to_number_at(b);
    }
    if (ta == JS_VAL_BOOL) {
        if (tb == JS_VAL_NUM) return jvs_num[a] == jvs_num[b];
        if (tb == JS_VAL_STR) return jvs_num[a] == js_to_number_at(b);
        return 0;
    }
    if (tb == JS_VAL_BOOL) {
        if (ta == JS_VAL_NUM) return jvs_num[a] == jvs_num[b];
        if (ta == JS_VAL_STR) return js_to_number_at(a) == jvs_num[b];
        return 0;
    }
    return 0;
}

int js_strict_eq_at(int a, int b) {
    if (jvs_tag[a] != jvs_tag[b]) return 0;
    return js_same_type_eq_at(a, b);
}

/* scope / bindings */
int js_scope_enter(int parent) {
    if (jsc_top >= MAX_JS_SCOPES) { js_set_err("js: scope overflow"); return -1; }
    int s = jsc_top;
    jsc_parent[s] = parent;
    jsc_top = s + 1;
    return s;
}

int js_binding_alloc(int scope, int name_off, int name_len) {
    if (jb_count >= MAX_JS_BINDINGS) { js_set_err("js: bindings overflow"); return -1; }
    int b = jb_count;
    jb_name_off[b] = name_off;
    jb_name_len[b] = name_len;
    jb_scope[b]    = scope;
    jb_tag[b]       = JS_VAL_UNDEF;
    jb_num[b]       = 0.0;
    jb_str_off[b]   = -1;
    jb_str_len[b]   = 0;
    jb_obj_idx[b]   = -1;
    jb_dom_idx[b]   = -1;
    jb_native_id[b] = 0;
    jb_count = b + 1;
    return b;
}

int js_str_eq(int off1, int len1, int off2, int len2) {
    if (len1 != len2) return 0;
    if (off1 < 0 || off2 < 0 || len1 < 0 ||
        off1 > js_str_pool_pos || off2 > js_str_pool_pos ||
        len1 > js_str_pool_pos - off1 ||
        len2 > js_str_pool_pos - off2) {
        return 0;
    }
    char *s1 = js_str_pool + off1;
    char *s2 = js_str_pool + off2;
    for (int i = 0; i < len1; i++) if (s1[i] != s2[i]) return 0;
    return 1;
}

int js_str_eq_text(int off, int len, char *text) {
    if (off < 0 || len < 0 || off > js_str_pool_pos ||
        len > js_str_pool_pos - off) {
        return 0;
    }
    for (int i = 0; i < len; i++) {
        if (js_str_pool[off + i] != text[i]) return 0;
    }
    return text[len] == 0;
}

int js_array_index_from_key(int off, int len, int *out_index) {
    if (off < 0 || len <= 0 || off > js_str_pool_pos ||
        len > js_str_pool_pos - off) {
        return 0;
    }
    if (len > 1 && js_str_pool[off] == '0') return 0;
    for (int i = 0; i < len; i++) {
        int c = (unsigned char)js_str_pool[off + i];
        if (c < '0' || c > '9') return 0;
    }
    /* This runtime stores array lengths in signed ints. Return -1 for a
     * canonical ECMAScript index that does not fit that lane, while keys
     * above the ECMAScript index ceiling remain ordinary properties. */
    if (len > 10) return 0;
    if (len == 10) {
        char *ecmascript_max = "4294967294";
        int ecmascript_compare = 0;
        for (int i = 0; i < 10 && ecmascript_compare == 0; i++) {
            if (js_str_pool[off + i] < ecmascript_max[i]) {
                ecmascript_compare = -1;
            } else if (js_str_pool[off + i] > ecmascript_max[i]) {
                ecmascript_compare = 1;
            }
        }
        if (ecmascript_compare > 0) return 0;

        char *runtime_max = "2147483646";
        int runtime_compare = 0;
        for (int i = 0; i < 10 && runtime_compare == 0; i++) {
            if (js_str_pool[off + i] < runtime_max[i]) {
                runtime_compare = -1;
            } else if (js_str_pool[off + i] > runtime_max[i]) {
                runtime_compare = 1;
            }
        }
        if (runtime_compare > 0) return -1;
    }
    int value = 0;
    for (int i = 0; i < len; i++) {
        int digit = (unsigned char)js_str_pool[off + i] - '0';
        value = value * 10 + digit;
    }
    *out_index = value;
    return 1;
}

int js_array_numeric_index_is_unsupported(int value_top) {
    if (value_top < 0 || jvs_tag[value_top] != JS_VAL_NUM) return 0;
    double value = jvs_num[value_top];
    return value >= 2147483647.0 && value <= 4294967294.0 &&
           fmod(value, 1.0) == 0.0;
}

int js_lookup_binding_in_scope(int scope,
                               int name_off, int name_len) {
    for (int b = jb_count - 1; b >= 0; b--) {
        if (jb_scope[b] == scope &&
            js_str_eq(jb_name_off[b], jb_name_len[b],
                      name_off, name_len)) {
            return b;
        }
    }
    return -1;
}

int js_lookup_binding(int scope, int name_off, int name_len) {
    int s = scope;
    while (s >= 0) {
        int binding = js_lookup_binding_in_scope(s, name_off, name_len);
        if (binding >= 0) return binding;
        s = jsc_parent[s];
    }
    return -1;
}

int js_selftest_binding_is_true(char *name) {
    int name_len = b_strlen(name);
    int name_off = js_str_intern(name, name_len);
    if (name_off < 0) return 0;
    int binding = js_lookup_binding(jsc_cur, name_off, name_len);
    return binding >= 0 && jb_tag[binding] == JS_VAL_BOOL &&
           js_number_truth(jb_num[binding]);
}

void js_number_selftest() {
    char *script =
        "var cupidClose=1.0000005;"
        "var cupidTiny=.0000001;"
        "var cupidLarge=5e3;"
        "var cupidNan=0/0;"
        "var cupidInfinity=1/0;"
        "var cupidNegativeInfinity=-1/0;"
        "var cupidNegativeZero=-0;"
        "var cupidRemainder=1%0;"
        "var cupidLiteral=cupidClose>1&&cupidTiny&&cupidLarge===5000;"
        "var cupidSignedExp=5e+3===5000&&5e-3>0.004&&5e-3<0.006;"
        "var cupidUpperExp=2E2===200&&2E-2>0.019&&2E-2<0.021;"
        "var cupidOrder=1<2&&2<=2&&3>=2;"
        "var cupidDivide=cupidNan!==cupidNan&&!cupidNan&&"
        "cupidInfinity>cupidLarge;"
        "var cupidDivideAssign=1;cupidDivideAssign/=0;"
        "var cupidDivideAssignOk=cupidDivideAssign===cupidInfinity;"
        "var cupidNegativeZeroOk=1/cupidNegativeZero==="
        "cupidNegativeInfinity;"
        "var cupidRemainderOk=cupidRemainder!==cupidRemainder;"
        "var cupidCap=1e999999999999999999999999999999999999999;"
        "var cupidCapOk=cupidCap===cupidInfinity;"
        "var cupidRadix=0xff===255&&0X10===16&&"
        "0b1010===10&&0B11===3&&0o17===15&&0O10===8;"
        "var cupidSeparators=1_000===1000&&0xff_ff===65535&&"
        "0b1010_0101===165&&0o7_7===63&&1_2.5===12.5&&"
        "1e1_0===1e10;"
        "var cupidStringInvalid=+'12x';"
        "var cupidStringSeparator=+'1_0';"
        "var cupidSignedRadix=+'+0x10';"
        "var cupidBadRadixString=+'0b2';"
        "var cupidUndefinedNumber=+undefined;"
        "var cupidStringNegativeZero=+'  -0  ';"
        "var cupidToNumber=+''===0&&+'  12.5e1  '===125&&"
        "+'\\t42\\n'===42&&"
        "+'0x10'===16&&+'0b11'===3&&+'0o10'===8&&"
        "+'Infinity'===cupidInfinity&&"
        "+'-Infinity'===cupidNegativeInfinity&&"
        "cupidStringInvalid!==cupidStringInvalid&&"
        "cupidStringSeparator!==cupidStringSeparator&&"
        "cupidSignedRadix!==cupidSignedRadix&&"
        "cupidBadRadixString!==cupidBadRadixString&&"
        "cupidUndefinedNumber!==cupidUndefinedNumber&&"
        "1/cupidStringNegativeZero===cupidNegativeInfinity;"
        "var cupidLooseEq=0==false&&'0'==false&&'1'==true&&false==''&&"
        "null==undefined&&!(0==undefined)&&!(0==null)&&"
        "!(''==undefined)&&!(0==='0')&&!(cupidNan==cupidNan);"
        "var cupidStringOrder='10'<'2'&&'2'>'10'&&"
        "'same'<='same'&&'same'>='same'&&!('10'<2);"
        "var cupidLargeRemainder=1e20%3;"
        "var cupidNegativeRemainder=-5%2;"
        "var cupidFiniteInfinityRemainder=5%cupidInfinity;"
        "var cupidInfiniteRemainder=cupidInfinity%2;"
        "var cupidLargeFmod=cupidLargeRemainder===1&&"
        "cupidNegativeRemainder===-1&&"
        "cupidFiniteInfinityRemainder===5&&"
        "cupidInfiniteRemainder!==cupidInfiniteRemainder&&"
        "1/(-4%2)===cupidNegativeInfinity;"
        "var cupidModAssign=1e20;cupidModAssign%=3;"
        "var cupidModZero=-4;cupidModZero%=2;"
        "var cupidModMemberCalls=0;var cupidModMemberOriginal={value:10};"
        "var cupidModMemberOther={value:9};"
        "var cupidModMemberCurrent=cupidModMemberOriginal;"
        "function cupidModMemberTarget(){cupidModMemberCalls+=1;"
        "return cupidModMemberCurrent;}"
        "function cupidModMemberRhs(){cupidModMemberCurrent=cupidModMemberOther;"
        "return 4;}cupidModMemberTarget().value%=cupidModMemberRhs();"
        "var cupidModIndexCalls=0;var cupidModIndexPosition=0;"
        "var cupidModIndex=[10,9];"
        "function cupidModIndexKey(){cupidModIndexCalls+=1;"
        "return cupidModIndexPosition++;}"
        "cupidModIndex[cupidModIndexKey()]%=4;"
        "var cupidAssignmentCount=0;while(cupidAssignmentCount<1100){"
        "cupidAssignmentCount=cupidAssignmentCount+1;}"
        "function cupidBindingRhs(cupidBindingParam){"
        "var cupidBindingLocal=7;return cupidBindingParam;}"
        "cupidOuterAfterCall=cupidBindingRhs(3);"
        "var cupidShadow=1;function cupidMakeShadow(){"
        "var cupidShadow=2;return function(){return cupidShadow;};}"
        "var cupidShadowClosure=cupidMakeShadow();cupidShadow=3;"
        "var cupidModAssignOk=cupidModAssign===1&&"
        "1/cupidModZero===cupidNegativeInfinity&&"
        "cupidModMemberCalls===1&&cupidModMemberOriginal.value===2&&"
        "cupidModMemberOther.value===9&&"
        "cupidModMemberCurrent===cupidModMemberOther&&"
        "cupidModIndexCalls===1&&cupidModIndexPosition===1&&"
        "cupidModIndex[0]===2&&cupidModIndex[1]===9&&"
        "cupidAssignmentCount===1100&&cupidOuterAfterCall===3&&"
        "cupidBindingLocal===undefined&&cupidBindingParam===undefined&&"
        "cupidShadowClosure()===2&&cupidShadow===3;"
        "var cupidConcat='Cupid';cupidConcat+=42;"
        "var cupidRightConcat=7;cupidRightConcat+=' cats';"
        "var cupidNullConcat='Cupid';cupidNullConcat+=null;"
        "var cupidPlusMemberCalls=0;"
        "var cupidPlusMemberOriginal={value:'A'};"
        "var cupidPlusMemberOther={value:'Z'};"
        "var cupidPlusMemberCurrent=cupidPlusMemberOriginal;"
        "function cupidPlusMemberTarget(){cupidPlusMemberCalls+=1;"
        "return cupidPlusMemberCurrent;}"
        "function cupidPlusMemberRhs(){"
        "cupidPlusMemberCurrent=cupidPlusMemberOther;return 'B';}"
        "cupidPlusMemberTarget().value+=cupidPlusMemberRhs();"
        "var cupidPlusIndexCalls=0;var cupidPlusIndexPosition=0;"
        "var cupidPlusIndex=['A','Z'];"
        "function cupidPlusIndexKey(){cupidPlusIndexCalls+=1;"
        "return cupidPlusIndexPosition++;}"
        "cupidPlusIndex[cupidPlusIndexKey()]+='B';"
        "var cupidLongA='"
        "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
        "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
        "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
        "';var cupidLongB='"
        "klmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrst"
        "klmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrst"
        "klmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrst"
        "';var cupidLongExpected='"
        "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
        "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
        "abcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghijabcdefghij"
        "klmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrst"
        "klmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrst"
        "klmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrstklmnopqrst"
        "';var cupidLongPlus=cupidLongA+cupidLongB;cupidLongA+=cupidLongB;"
        "var cupidConcatOk=cupidConcat==='Cupid42'&&"
        "cupidRightConcat==='7 cats'&&cupidNullConcat==='Cupidnull'&&"
        "cupidLongPlus===cupidLongExpected&&cupidLongA===cupidLongExpected&&"
        "cupidPlusMemberCalls===1&&cupidPlusMemberOriginal.value==='AB'&&"
        "cupidPlusMemberOther.value==='Z'&&"
        "cupidPlusMemberCurrent===cupidPlusMemberOther&&"
        "cupidPlusIndexCalls===1&&cupidPlusIndexPosition===1&&"
        "cupidPlusIndex[0]==='AB'&&cupidPlusIndex[1]==='Z';"
        "var cupidFiniteFormat=''+4294967295==='4294967295'&&"
        "''+1e20==='100000000000000000000'&&"
        "''+1e-7==='1e-7';"
        "function cupidNativeIdentity(cupidNativeValue){"
        "return cupidNativeValue;}"
        "var cupidNativeReturned=cupidNativeIdentity(cupidNativeProbe);"
        "var cupidNativeCallResult=cupidNativeReturned();"
        "var cupidNativeRoundTrip=cupidNativeCallResult===null;";
    char *bad_script = "var cupidBad=1e;";
    char *bad_hex_script = "var cupidBad=0x;";
    char *bad_binary_script = "var cupidBad=0b2;";
    char *bad_octal_script = "var cupidBad=0o8;";
    char *bad_separator_script = "var cupidBad=1__0;";
    char *bad_radix_separator_script = "var cupidBad=0x_1;";
    char *bad_fraction_separator_script = "var cupidBad=1._0;";
    char *bad_exponent_separator_script = "var cupidBad=1e_2;";
    char *bad_leading_zero_separator_script = "var cupidBad=00_1;";
    char *bad_suffix_script = "var cupidBad=12foo;";
    char *recovery_script = "var cupidRecovery=0x10+0b10+0o7+1_0===35;";
    char *pool_setup_script =
        "var cupidPoolObject={stable:'A'};cupidPoolObject[123]=7;"
        "var cupidPoolArray=[];cupidPoolArray.stable=7;"
        "var cupidPoolRhsCalls=0;function cupidPoolRhs(){"
        "cupidPoolRhsCalls+=1;return 9;}var cupidStackTarget=1;"
        "var cupidStackCallCount=0;function cupidStackCall(a,b){"
        "cupidStackCallCount+=1;return a+b;}";
    char *pool_assignment_script =
        "cupidPoolObject[456]=cupidPoolRhs();";
    char *pool_concat_script = "cupidPoolObject.stable+='B';";
    char *pool_array_script = "cupidPoolArray.stable+=1;";
    char *pool_typeof_script = "typeof cupidStackTarget;";
    char *array_length_script = "cupidPoolArray.length=4;";
    char *array_index_limit_script =
        "cupidPoolArray['2147483647']=cupidPoolRhs();";
    char *array_numeric_limit_script =
        "cupidPoolArray[2147483648]=cupidPoolRhs();";
    char *pool_recovery_script =
        "var cupidPoolUnchanged=cupidPoolObject[456]===undefined&&"
        "cupidPoolObject[123]===7&&cupidPoolObject.stable==='A'&&"
        "cupidPoolArray.stable===8&&cupidPoolArray.length===0&&"
        "cupidPoolRhsCalls===0;"
        "cupidPoolObject[456]=cupidPoolRhs();"
        "cupidPoolObject.stable+='B';"
        "cupidPoolArray[4294967295]=12;"
        "cupidPoolArray[2]=11;"
        "var cupidPoolRecovery=cupidPoolObject[456]===9&&"
        "cupidPoolObject.stable==='AB'&&cupidPoolRhsCalls===1&&"
        "cupidPoolArray.length===3&&cupidPoolArray[2]===11&&"
        "cupidPoolArray[4294967295]===12;";
    char *stack_assignment_script = "cupidStackTarget=9;";
    char *stack_full_assignment_script = "cupidStackTarget=10;";
    char *stack_binary_script = "1+2;";
    char *stack_call_script = "cupidStackCall(1,2);";
    char *stack_var_script = "var cupidStackNew=11;";
    char *stack_recovery_script =
        "var cupidStackCallUnchanged=cupidStackCallCount===0;"
        "cupidStackTarget=9;"
        "var cupidStackNew=11;"
        "var cupidStackCallValue=cupidStackCall(5,7);"
        "var cupidStackRecovery=cupidStackCallUnchanged&&"
        "cupidStackTarget===9&&"
        "cupidStackNew===11&&cupidStackCallValue===12&&"
        "cupidStackCallCount===1;";
    double close_low = 1.0;
    double close_high = 1.0000005;
    double large_positive = 5000.0;
    double large_negative = -5000.0;
    double negative_zero = -0.0;
    double negative_zero_reciprocal = 1.0 / negative_zero;
    double number_nan = 0.0 / 0.0;
    double positive_infinity = 1.0 / 0.0;
    double negative_infinity = -1.0 / 0.0;
    double tiny_nonzero = 0.0000001;
    char nan_text[64];
    char positive_infinity_text[64];
    char negative_infinity_text[64];

    int close_ok = close_low != close_high && close_low < close_high;
    int large_ok = large_positive > 0.0 && large_negative < 0.0 &&
                   large_positive > large_negative;
    int negative_zero_ok = negative_zero == 0.0 &&
                           !js_number_truth(negative_zero) &&
                           negative_zero_reciprocal < 0.0;
    int nan_ok = number_nan != number_nan &&
                 !(number_nan == number_nan) &&
                 !(number_nan < 0.0) && !(number_nan > 0.0) &&
                 !(number_nan <= 0.0) && !(number_nan >= 0.0);
    int truth_ok = js_number_truth(tiny_nonzero) &&
                   !js_number_truth(0.0) &&
                   !js_number_truth(number_nan);
    int nan_format_ok = js_format_num(number_nan, nan_text) == 3 &&
                        b_streq(nan_text, "NaN");
    int positive_infinity_format_ok =
        js_format_num(positive_infinity, positive_infinity_text) == 8 &&
        b_streq(positive_infinity_text, "Infinity");
    int negative_infinity_format_ok =
        js_format_num(negative_infinity, negative_infinity_text) == 9 &&
        b_streq(negative_infinity_text, "-Infinity");
    int native_probe_setup_ok = 1;
    if (jsc_top == 0) {
        jsc_cur = js_scope_enter(-1);
        if (jsc_cur < 0) native_probe_setup_ok = 0;
    }
    int native_probe_name = js_str_intern("cupidNativeProbe", 16);
    if (native_probe_name < 0) native_probe_setup_ok = 0;
    int native_probe_binding = -1;
    if (native_probe_setup_ok) {
        native_probe_binding =
            js_lookup_binding_in_scope(0, native_probe_name, 16);
        if (native_probe_binding < 0) {
            native_probe_binding =
                js_binding_alloc(0, native_probe_name, 16);
        }
        if (native_probe_binding < 0) native_probe_setup_ok = 0;
    }
    if (native_probe_setup_ok) {
        jb_tag[native_probe_binding] = JS_VAL_NATIVE;
        jb_native_id[native_probe_binding] =
            JS_NATIVE_DOC_GET_ELEMENT_BY_ID;
    }
    int script_ok = js_run(script, b_strlen(script)) == 0;
    int literal_ok = script_ok &&
                     js_selftest_binding_is_true("cupidLiteral");
    int signed_exponent_ok = script_ok &&
        js_selftest_binding_is_true("cupidSignedExp");
    int uppercase_exponent_ok = script_ok &&
        js_selftest_binding_is_true("cupidUpperExp");
    int order_ok = script_ok &&
                   js_selftest_binding_is_true("cupidOrder");
    int divide_ok = script_ok &&
                    js_selftest_binding_is_true("cupidDivide");
    int divide_assign_ok = script_ok &&
        js_selftest_binding_is_true("cupidDivideAssignOk");
    int script_negative_zero_ok = script_ok &&
        js_selftest_binding_is_true("cupidNegativeZeroOk");
    int remainder_ok = script_ok &&
                       js_selftest_binding_is_true("cupidRemainderOk");
    int exponent_cap_ok = script_ok &&
                          js_selftest_binding_is_true("cupidCapOk");
    int radix_ok = script_ok &&
                   js_selftest_binding_is_true("cupidRadix");
    int separators_ok = script_ok &&
                        js_selftest_binding_is_true("cupidSeparators");
    int to_number_ok = script_ok &&
                       js_selftest_binding_is_true("cupidToNumber");
    int loose_equality_ok = script_ok &&
                            js_selftest_binding_is_true("cupidLooseEq");
    int string_order_ok = script_ok &&
                          js_selftest_binding_is_true("cupidStringOrder");
    int large_fmod_ok = script_ok &&
                        js_selftest_binding_is_true("cupidLargeFmod");
    int modulo_assign_ok = script_ok &&
        js_selftest_binding_is_true("cupidModAssignOk");
    int string_plus_assign_ok = script_ok &&
        js_selftest_binding_is_true("cupidConcatOk");
    int finite_format_ok = script_ok &&
        js_selftest_binding_is_true("cupidFiniteFormat");
    int native_round_trip_ok = native_probe_setup_ok && script_ok &&
        js_selftest_binding_is_true("cupidNativeRoundTrip");
    int exponent_reject_ok = js_run(bad_script, b_strlen(bad_script)) != 0;
    int hex_reject_ok =
        js_run(bad_hex_script, b_strlen(bad_hex_script)) != 0;
    int binary_reject_ok =
        js_run(bad_binary_script, b_strlen(bad_binary_script)) != 0;
    int octal_reject_ok =
        js_run(bad_octal_script, b_strlen(bad_octal_script)) != 0;
    int separator_reject_ok =
        js_run(bad_separator_script, b_strlen(bad_separator_script)) != 0;
    int radix_separator_reject_ok =
        js_run(bad_radix_separator_script,
               b_strlen(bad_radix_separator_script)) != 0;
    int fraction_separator_reject_ok =
        js_run(bad_fraction_separator_script,
               b_strlen(bad_fraction_separator_script)) != 0;
    int exponent_separator_reject_ok =
        js_run(bad_exponent_separator_script,
               b_strlen(bad_exponent_separator_script)) != 0;
    int leading_zero_separator_reject_ok =
        js_run(bad_leading_zero_separator_script,
               b_strlen(bad_leading_zero_separator_script)) != 0;
    int suffix_reject_ok =
        js_run(bad_suffix_script, b_strlen(bad_suffix_script)) != 0;
    int reject_ok = exponent_reject_ok && hex_reject_ok &&
                    binary_reject_ok && octal_reject_ok &&
                    separator_reject_ok && radix_separator_reject_ok &&
                    fraction_separator_reject_ok &&
                    exponent_separator_reject_ok &&
                    leading_zero_separator_reject_ok && suffix_reject_ok;
    int recovery_run_ok =
        js_run(recovery_script, b_strlen(recovery_script)) == 0;
    int recovery_ok = recovery_run_ok &&
                      js_selftest_binding_is_true("cupidRecovery");

    char unicode_number_text[8];
    unicode_number_text[0] = (char)0xC2;
    unicode_number_text[1] = (char)0xA0;
    unicode_number_text[2] = '4';
    unicode_number_text[3] = '2';
    unicode_number_text[4] = (char)0xEF;
    unicode_number_text[5] = (char)0xBB;
    unicode_number_text[6] = (char)0xBF;
    unicode_number_text[7] = 0;
    int unicode_number_off = js_str_intern(unicode_number_text, 7);
    jvs_top = 0;
    int unicode_whitespace_ok = 0;
    if (unicode_number_off >= 0) {
        js_push_str(unicode_number_off, 7);
        unicode_whitespace_ok = js_last_error[0] == 0 && jvs_top == 1 &&
                                js_to_number_at(0) == 42.0;
    }

    char supplementary_text[5];
    supplementary_text[0] = (char)0xF0;
    supplementary_text[1] = (char)0x90;
    supplementary_text[2] = (char)0x80;
    supplementary_text[3] = (char)0x80;
    supplementary_text[4] = 0;
    char bmp_text[4];
    bmp_text[0] = (char)0xEE;
    bmp_text[1] = (char)0x80;
    bmp_text[2] = (char)0x80;
    bmp_text[3] = 0;
    int supplementary_off = js_str_intern(supplementary_text, 4);
    int bmp_off = js_str_intern(bmp_text, 3);
    jvs_top = 0;
    int utf16_order_ok = 0;
    if (supplementary_off >= 0 && bmp_off >= 0) {
        js_push_str(supplementary_off, 4);
        js_push_str(bmp_off, 3);
        utf16_order_ok = js_last_error[0] == 0 && jvs_top == 2 &&
                         js_string_compare_at(0, 1) < 0;
    }

    int pool_setup_ok =
        js_run(pool_setup_script, b_strlen(pool_setup_script)) == 0;
    jtk_count = 0;
    js_last_error[0] = 0;
    int pool_assignment_root = -1;
    int pool_assignment_parse_ok =
        js_tokenize(pool_assignment_script,
                    b_strlen(pool_assignment_script)) == 0;
    if (pool_assignment_parse_ok) {
        pool_assignment_root = js_parse();
        pool_assignment_parse_ok = pool_assignment_root >= 0 &&
                                   js_last_error[0] == 0;
    }
    jtk_count = 0;
    js_last_error[0] = 0;
    int pool_concat_root = -1;
    int pool_concat_parse_ok =
        js_tokenize(pool_concat_script, b_strlen(pool_concat_script)) == 0;
    if (pool_concat_parse_ok) {
        pool_concat_root = js_parse();
        pool_concat_parse_ok = pool_concat_root >= 0 &&
                               js_last_error[0] == 0;
    }
    jtk_count = 0;
    js_last_error[0] = 0;
    int pool_array_root = -1;
    int pool_array_parse_ok =
        js_tokenize(pool_array_script, b_strlen(pool_array_script)) == 0;
    if (pool_array_parse_ok) {
        pool_array_root = js_parse();
        pool_array_parse_ok = pool_array_root >= 0 &&
                              js_last_error[0] == 0;
    }
    jtk_count = 0;
    js_last_error[0] = 0;
    int pool_typeof_root = -1;
    int pool_typeof_parse_ok =
        js_tokenize(pool_typeof_script, b_strlen(pool_typeof_script)) == 0;
    if (pool_typeof_parse_ok) {
        pool_typeof_root = js_parse();
        pool_typeof_parse_ok = pool_typeof_root >= 0 &&
                               js_last_error[0] == 0;
    }
    jtk_count = 0;
    js_last_error[0] = 0;
    int stack_assignment_root = -1;
    int stack_assignment_parse_ok =
        js_tokenize(stack_assignment_script,
                    b_strlen(stack_assignment_script)) == 0;
    if (stack_assignment_parse_ok) {
        stack_assignment_root = js_parse();
        stack_assignment_parse_ok = stack_assignment_root >= 0 &&
                                    js_last_error[0] == 0;
    }
    jtk_count = 0;
    js_last_error[0] = 0;
    int stack_full_assignment_root = -1;
    int stack_full_assignment_parse_ok =
        js_tokenize(stack_full_assignment_script,
                    b_strlen(stack_full_assignment_script)) == 0;
    if (stack_full_assignment_parse_ok) {
        stack_full_assignment_root = js_parse();
        stack_full_assignment_parse_ok =
            stack_full_assignment_root >= 0 && js_last_error[0] == 0;
    }
    jtk_count = 0;
    js_last_error[0] = 0;
    int stack_binary_root = -1;
    int stack_binary_parse_ok =
        js_tokenize(stack_binary_script,
                    b_strlen(stack_binary_script)) == 0;
    if (stack_binary_parse_ok) {
        stack_binary_root = js_parse();
        stack_binary_parse_ok = stack_binary_root >= 0 &&
                                js_last_error[0] == 0;
    }
    jtk_count = 0;
    js_last_error[0] = 0;
    int stack_call_root = -1;
    int stack_call_parse_ok =
        js_tokenize(stack_call_script,
                    b_strlen(stack_call_script)) == 0;
    if (stack_call_parse_ok) {
        stack_call_root = js_parse();
        stack_call_parse_ok = stack_call_root >= 0 &&
                              js_last_error[0] == 0;
    }
    jtk_count = 0;
    js_last_error[0] = 0;
    int stack_var_root = -1;
    int stack_var_parse_ok =
        js_tokenize(stack_var_script, b_strlen(stack_var_script)) == 0;
    if (stack_var_parse_ok) {
        stack_var_root = js_parse();
        stack_var_parse_ok = stack_var_root >= 0 &&
                             js_last_error[0] == 0;
    }

    int pool_object_name = js_str_intern("cupidPoolObject", 15);
    int pool_object_binding = -1;
    if (pool_object_name >= 0) {
        pool_object_binding =
            js_lookup_binding(jsc_cur, pool_object_name, 15);
    }
    int pool_object_idx = -1;
    if (pool_object_binding >= 0 &&
        jb_tag[pool_object_binding] == JS_VAL_OBJ) {
        pool_object_idx = jb_obj_idx[pool_object_binding];
    }
    int pool_array_name = js_str_intern("cupidPoolArray", 14);
    int pool_array_binding = -1;
    if (pool_array_name >= 0) {
        pool_array_binding =
            js_lookup_binding(jsc_cur, pool_array_name, 14);
    }
    int pool_array_idx = -1;
    if (pool_array_binding >= 0 &&
        jb_tag[pool_array_binding] == JS_VAL_ARR) {
        pool_array_idx = jb_obj_idx[pool_array_binding];
    }
    int pool_stable_key = js_str_intern("stable", 6);
    int pool_dom_key = js_str_intern("tagName", 7);
    int pool_saved_pos = js_str_pool_pos;
    int pool_saved_stack = jvs_top;
    int pool_saved_prop_count = jp_count;
    int pool_saved_first_prop = -1;
    int pool_saved_array_first_prop = -1;
    int pool_saved_array_length = -1;
    if (pool_object_idx >= 0) {
        pool_saved_first_prop = jobj_first_prop[pool_object_idx];
    }
    if (pool_array_idx >= 0) {
        pool_saved_array_first_prop = jobj_first_prop[pool_array_idx];
        pool_saved_array_length = jobj_arr_len[pool_array_idx];
    }
    int pool_fill = pool_saved_pos;
    while (pool_fill < JS_STR_POOL - 1) {
        js_str_pool[pool_fill] = 'x';
        pool_fill = pool_fill + 1;
    }
    js_str_pool[JS_STR_POOL - 1] = 0;
    js_str_pool_pos = JS_STR_POOL;
    js_last_error[0] = 0;
    int pool_embedded_boundary_result =
        js_str_intern(js_str_pool + pool_saved_pos,
                      JS_STR_POOL - pool_saved_pos);
    int pool_embedded_boundary_ok = pool_embedded_boundary_result < 0 &&
        b_streq(js_last_error, "js: string pool full") &&
        js_str_pool_pos == JS_STR_POOL;
    js_last_error[0] = 0;
    if (pool_concat_parse_ok) {
        js_exec_program(pool_concat_root);
    }
    int pool_concat_reject_ok =
        b_streq(js_last_error, "js: string pool full");
    int pool_concat_stack_ok = jvs_top == pool_saved_stack;
    int pool_concat_property_ok = pool_object_idx >= 0 &&
        pool_saved_prop_count == jp_count &&
        pool_saved_first_prop == jobj_first_prop[pool_object_idx];
    jvs_top = pool_saved_stack;
    js_last_error[0] = 0;
    if (pool_assignment_parse_ok) {
        js_exec_program(pool_assignment_root);
    }
    int pool_assignment_reject_ok =
        b_streq(js_last_error, "js: string pool full");
    int pool_assignment_stack_ok = jvs_top == pool_saved_stack;
    int pool_assignment_property_ok = pool_object_idx >= 0 &&
        pool_saved_prop_count == jp_count &&
        pool_saved_first_prop == jobj_first_prop[pool_object_idx];

    jvs_top = pool_saved_stack;
    js_last_error[0] = 0;
    if (pool_array_parse_ok) {
        js_exec_program(pool_array_root);
    }
    int pool_array_existing_key_ok = js_last_error[0] == 0 &&
        jvs_top == pool_saved_stack && pool_array_idx >= 0 &&
        pool_saved_prop_count == jp_count &&
        pool_saved_array_first_prop == jobj_first_prop[pool_array_idx] &&
        pool_saved_array_length == jobj_arr_len[pool_array_idx];
    int pool_array_stable_property = -1;
    if (pool_array_idx >= 0 && pool_stable_key >= 0) {
        pool_array_stable_property =
            js_obj_find_prop(pool_array_idx, pool_stable_key, 6);
    }
    pool_array_existing_key_ok = pool_array_existing_key_ok &&
        pool_array_stable_property >= 0 &&
        jp_tag[pool_array_stable_property] == JS_VAL_NUM &&
        jp_num[pool_array_stable_property] == 8.0;

    jtk_count = 0;
    js_last_error[0] = 0;
    int pool_lexer_reject_ok =
        js_tokenize("cupidPoolFreshIdentifier", 24) != 0 &&
        b_streq(js_last_error, "js: string pool full") &&
        jtk_count == 0;

    jvs_top = pool_saved_stack;
    js_last_error[0] = 0;
    if (pool_typeof_parse_ok) {
        js_exec_program(pool_typeof_root);
    }
    int pool_typeof_reject_ok =
        b_streq(js_last_error, "js: string pool full") &&
        jvs_top == pool_saved_stack;

    jvs_top = pool_saved_stack;
    js_last_error[0] = 0;
    int pool_saved_dom_tag = n_tag[0];
    n_tag[0] = T_DIV;
    if (pool_dom_key >= 0) jsd_dom_member_get(0, pool_dom_key, 7);
    n_tag[0] = pool_saved_dom_tag;
    int pool_dom_reject_ok =
        b_streq(js_last_error, "js: string pool full") &&
        jvs_top == pool_saved_stack;

    js_str_pool_pos = pool_saved_pos;
    jvs_top = pool_saved_stack;
    js_last_error[0] = 0;
    jtk_count = 0;
    int array_length_root = -1;
    int array_length_parse_ok =
        js_tokenize(array_length_script,
                    b_strlen(array_length_script)) == 0;
    if (array_length_parse_ok) {
        array_length_root = js_parse();
        array_length_parse_ok = array_length_root >= 0 &&
                                js_last_error[0] == 0;
    }
    js_last_error[0] = 0;
    if (array_length_parse_ok) {
        js_exec_program(array_length_root);
    }
    int array_length_reject_ok =
        b_streq(js_last_error, "js: array length assignment unsupported") &&
        jvs_top == pool_saved_stack && pool_array_idx >= 0 &&
        jobj_arr_len[pool_array_idx] == pool_saved_array_length &&
        jp_count == pool_saved_prop_count &&
        jobj_first_prop[pool_array_idx] == pool_saved_array_first_prop;

    jvs_top = pool_saved_stack;
    js_last_error[0] = 0;
    jtk_count = 0;
    int array_index_limit_root = -1;
    int array_index_limit_parse_ok =
        js_tokenize(array_index_limit_script,
                    b_strlen(array_index_limit_script)) == 0;
    if (array_index_limit_parse_ok) {
        array_index_limit_root = js_parse();
        array_index_limit_parse_ok = array_index_limit_root >= 0 &&
                                     js_last_error[0] == 0;
    }
    js_last_error[0] = 0;
    if (array_index_limit_parse_ok) {
        js_exec_program(array_index_limit_root);
    }
    int array_index_limit_reject_ok =
        b_streq(js_last_error, "js: array index exceeds runtime limit") &&
        jvs_top == pool_saved_stack && pool_array_idx >= 0 &&
        jobj_arr_len[pool_array_idx] == pool_saved_array_length &&
        jp_count == pool_saved_prop_count &&
        jobj_first_prop[pool_array_idx] == pool_saved_array_first_prop;

    jvs_top = pool_saved_stack;
    js_last_error[0] = 0;
    jtk_count = 0;
    int array_numeric_limit_root = -1;
    int array_numeric_limit_parse_ok =
        js_tokenize(array_numeric_limit_script,
                    b_strlen(array_numeric_limit_script)) == 0;
    if (array_numeric_limit_parse_ok) {
        array_numeric_limit_root = js_parse();
        array_numeric_limit_parse_ok = array_numeric_limit_root >= 0 &&
                                       js_last_error[0] == 0;
    }
    js_last_error[0] = 0;
    if (array_numeric_limit_parse_ok) {
        js_exec_program(array_numeric_limit_root);
    }
    int array_numeric_limit_reject_ok =
        b_streq(js_last_error, "js: array index exceeds runtime limit") &&
        jvs_top == pool_saved_stack && pool_array_idx >= 0 &&
        jobj_arr_len[pool_array_idx] == pool_saved_array_length &&
        jp_count == pool_saved_prop_count &&
        jobj_first_prop[pool_array_idx] == pool_saved_array_first_prop;

    jvs_top = pool_saved_stack;
    js_last_error[0] = 0;
    int pool_recovery_run_ok =
        js_run(pool_recovery_script, b_strlen(pool_recovery_script)) == 0;
    int pool_assignment_unchanged_ok = pool_recovery_run_ok &&
        js_selftest_binding_is_true("cupidPoolUnchanged");
    int pool_assignment_recovery_ok = pool_recovery_run_ok &&
        js_selftest_binding_is_true("cupidPoolRecovery");
    int concat_pool_reject_ok =
        pool_setup_ok && pool_concat_parse_ok && pool_concat_reject_ok &&
        pool_concat_stack_ok && pool_concat_property_ok &&
        pool_assignment_unchanged_ok && pool_assignment_recovery_ok;
    int index_key_pool_reject_ok =
        pool_setup_ok && pool_assignment_parse_ok &&
        pool_assignment_reject_ok && pool_assignment_stack_ok &&
        pool_assignment_property_ok &&
        pool_assignment_unchanged_ok && pool_assignment_recovery_ok;
    int pool_intern_boundary_ok =
        pool_setup_ok && pool_array_parse_ok && pool_typeof_parse_ok &&
        pool_array_existing_key_ok && pool_lexer_reject_ok &&
        pool_typeof_reject_ok && pool_dom_reject_ok &&
        pool_embedded_boundary_ok && array_length_parse_ok &&
        array_length_reject_ok && array_index_limit_parse_ok &&
        array_index_limit_reject_ok && array_numeric_limit_parse_ok &&
        array_numeric_limit_reject_ok;

    int stack_target_name = js_str_intern("cupidStackTarget", 16);
    int stack_target_binding = -1;
    if (stack_target_name >= 0) {
        stack_target_binding =
            js_lookup_binding(jsc_cur, stack_target_name, 16);
    }
    int stack_call_count_name = js_str_intern("cupidStackCallCount", 19);
    int stack_call_count_binding = -1;
    if (stack_call_count_name >= 0) {
        stack_call_count_binding =
            js_lookup_binding(jsc_cur, stack_call_count_name, 19);
    }
    int stack_new_name = js_str_intern("cupidStackNew", 13);
    int stack_saved_top = jvs_top;
    jvs_top = MAX_JS_VS - 1;
    js_last_error[0] = 0;
    if (stack_assignment_parse_ok) {
        js_exec_program(stack_assignment_root);
    }
    int stack_assignment_reject_ok =
        b_streq(js_last_error, "js: value stack overflow");
    int stack_assignment_balanced_ok = jvs_top == MAX_JS_VS - 1;
    int stack_assignment_unchanged_ok = stack_target_binding >= 0 &&
        jb_tag[stack_target_binding] == JS_VAL_NUM &&
        jb_num[stack_target_binding] == 1.0;

    jvs_top = MAX_JS_VS;
    js_last_error[0] = 0;
    if (stack_full_assignment_parse_ok) {
        js_exec_program(stack_full_assignment_root);
    }
    int stack_full_assignment_reject_ok =
        b_streq(js_last_error, "js: value stack overflow");
    int stack_full_assignment_balanced_ok = jvs_top == MAX_JS_VS;
    int stack_full_assignment_unchanged_ok = stack_target_binding >= 0 &&
        jb_tag[stack_target_binding] == JS_VAL_NUM &&
        jb_num[stack_target_binding] == 1.0;

    jvs_top = MAX_JS_VS - 1;
    js_last_error[0] = 0;
    if (stack_binary_parse_ok) {
        js_exec_program(stack_binary_root);
    }
    int stack_binary_reject_ok =
        b_streq(js_last_error, "js: value stack overflow");
    int stack_binary_balanced_ok = jvs_top == MAX_JS_VS - 1;

    jvs_top = MAX_JS_VS - 2;
    js_last_error[0] = 0;
    if (stack_call_parse_ok) {
        js_exec_program(stack_call_root);
    }
    int stack_call_reject_ok =
        b_streq(js_last_error, "js: value stack overflow");
    int stack_call_balanced_ok = jvs_top == MAX_JS_VS - 2;
    int stack_call_unchanged_ok = stack_call_count_binding >= 0 &&
        jb_tag[stack_call_count_binding] == JS_VAL_NUM &&
        jb_num[stack_call_count_binding] == 0.0;

    jvs_top = MAX_JS_VS;
    js_last_error[0] = 0;
    if (stack_var_parse_ok) {
        js_exec_program(stack_var_root);
    }
    int stack_var_reject_ok =
        b_streq(js_last_error, "js: value stack overflow");
    int stack_var_balanced_ok = jvs_top == MAX_JS_VS;
    int stack_new_binding = -1;
    if (stack_new_name >= 0) {
        stack_new_binding =
            js_lookup_binding(jsc_cur, stack_new_name, 13);
    }
    int stack_var_unchanged_ok = stack_new_binding < 0 ||
        jb_tag[stack_new_binding] == JS_VAL_UNDEF;

    jvs_top = stack_saved_top;
    js_last_error[0] = 0;
    int stack_recovery_run_ok =
        js_run(stack_recovery_script, b_strlen(stack_recovery_script)) == 0;
    int stack_assignment_recovery_ok = stack_recovery_run_ok &&
        js_selftest_binding_is_true("cupidStackRecovery");

    negative_zero_ok = negative_zero_ok && script_negative_zero_ok;
    to_number_ok = to_number_ok && unicode_whitespace_ok;
    string_order_ok = string_order_ok && utf16_order_ok;
    string_plus_assign_ok = string_plus_assign_ok && finite_format_ok &&
                            concat_pool_reject_ok;
    reject_ok = reject_ok && index_key_pool_reject_ok &&
                pool_intern_boundary_ok &&
                stack_assignment_parse_ok && stack_assignment_reject_ok &&
                stack_assignment_balanced_ok &&
                stack_assignment_unchanged_ok &&
                stack_full_assignment_parse_ok &&
                stack_full_assignment_reject_ok &&
                stack_full_assignment_balanced_ok &&
                stack_full_assignment_unchanged_ok &&
                stack_binary_parse_ok && stack_binary_reject_ok &&
                stack_binary_balanced_ok && stack_call_parse_ok &&
                stack_call_reject_ok && stack_call_balanced_ok &&
                stack_call_unchanged_ok && stack_var_parse_ok &&
                stack_var_reject_ok && stack_var_balanced_ok &&
                stack_var_unchanged_ok;
    recovery_ok = recovery_ok && pool_assignment_recovery_ok &&
                  stack_assignment_recovery_ok && native_round_trip_ok;
    if (close_ok && large_ok && negative_zero_ok && nan_ok && truth_ok &&
        nan_format_ok && positive_infinity_format_ok &&
        negative_infinity_format_ok && literal_ok && signed_exponent_ok &&
        uppercase_exponent_ok && order_ok && divide_ok && divide_assign_ok &&
        remainder_ok && exponent_cap_ok && radix_ok && separators_ok &&
        to_number_ok && loose_equality_ok && string_order_ok &&
        large_fmod_ok && modulo_assign_ok && string_plus_assign_ok &&
        reject_ok && recovery_ok) {
        serial_printf(
            "[browser-js-number] PASS close=%d large=%d negzero=%d nan=%d "
            "truth=%d nanformat=%d posinfformat=%d neginfformat=%d literal=%d "
            "signedexp=%d upperexp=%d order=%d divide=%d divideassign=%d "
            "remainder=%d expcap=%d radix=%d separators=%d tonumber=%d "
            "looseeq=%d stringrel=%d largefmod=%d modassign=%d "
            "strplusassign=%d reject=%d recovery=%d\n",
            close_ok, large_ok, negative_zero_ok, nan_ok, truth_ok,
            nan_format_ok, positive_infinity_format_ok,
            negative_infinity_format_ok, literal_ok, signed_exponent_ok,
            uppercase_exponent_ok, order_ok, divide_ok, divide_assign_ok,
            remainder_ok, exponent_cap_ok, radix_ok, separators_ok,
            to_number_ok, loose_equality_ok, string_order_ok, large_fmod_ok,
            modulo_assign_ok, string_plus_assign_ok, reject_ok, recovery_ok);
    } else {
        serial_printf(
            "[browser-js-number] FAIL close=%d large=%d negzero=%d nan=%d "
            "truth=%d nanformat=%d posinfformat=%d neginfformat=%d literal=%d "
            "signedexp=%d upperexp=%d order=%d divide=%d divideassign=%d "
            "remainder=%d expcap=%d radix=%d separators=%d tonumber=%d "
            "looseeq=%d stringrel=%d largefmod=%d modassign=%d "
            "strplusassign=%d reject=%d recovery=%d\n",
            close_ok, large_ok, negative_zero_ok, nan_ok, truth_ok,
            nan_format_ok, positive_infinity_format_ok,
            negative_infinity_format_ok, literal_ok, signed_exponent_ok,
            uppercase_exponent_ok, order_ok, divide_ok, divide_assign_ok,
            remainder_ok, exponent_cap_ok, radix_ok, separators_ok,
            to_number_ok, loose_equality_ok, string_order_ok, large_fmod_ok,
            modulo_assign_ok, string_plus_assign_ok, reject_ok, recovery_ok);
    }
}

void js_binding_set_from_top(int b) {
    int t = jvs_top - 1;
    jb_tag[b]       = jvs_tag[t];
    jb_num[b]       = jvs_num[t];
    jb_str_off[b]   = jvs_str_off[t];
    jb_str_len[b]   = jvs_str_len[t];
    jb_obj_idx[b]   = jvs_obj_idx[t];
    jb_dom_idx[b]   = jvs_dom_idx[t];
    jb_native_id[b] = jvs_native_id[t];
}

void js_push_from_binding(int b) {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t]       = jb_tag[b];
    jvs_num[t]       = jb_num[b];
    jvs_str_off[t]   = jb_str_off[b];
    jvs_str_len[t]   = jb_str_len[b];
    jvs_obj_idx[t]   = jb_obj_idx[b];
    jvs_dom_idx[t]   = jb_dom_idx[b];
    jvs_native_id[t] = jb_native_id[b];
}

/* console builtin */
void js_console_log_top_n(int argc) {
    /* args sit on the value stack at [top-argc .. top-1]. */
    char line[512];
    int p = 0;
    for (int i = 0; i < argc; i++) {
        int idx = jvs_top - argc + i;
        if (i > 0 && p < 511) { line[p] = ' '; p = p + 1; }
        char tmp[256];
        int tl = js_to_string_at(idx, tmp, 256);
        for (int k = 0; k < tl && p < 511; k++) { line[p] = tmp[k]; p = p + 1; }
    }
    line[p] = 0;
    serial_printf("[js] %s\n", line);
    /* status bar mirror so the user sees it without serial */
    int s = 0;
    char *prefix = "[js] ";
    while (prefix[s] && s < 5) { status_msg[s] = prefix[s]; s = s + 1; }
    int k = 0;
    while (k < p && s < 255) { status_msg[s] = line[k]; s = s + 1; k = k + 1; }
    status_msg[s] = 0;
}

/* expression eval - CupidC defers cross-resolve for recursive calls. */

int js_alloc_object(int kind) {
    if (jobj_count >= MAX_JS_OBJS) { js_set_err("js: object pool full"); return -1; }
    int o = jobj_count;
    jobj_kind[o] = kind;
    jobj_first_prop[o] = -1;
    jobj_arr_len[o] = 0;
    jobj_count = o + 1;
    return o;
}

void js_push_obj(int obj_idx) {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t] = JS_VAL_OBJ;
    jvs_obj_idx[t] = obj_idx;
}
void js_push_arr(int obj_idx) {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t] = JS_VAL_ARR;
    jvs_obj_idx[t] = obj_idx;
}

/* Find property `key` on object `obj`. Returns property index or -1. */
int js_obj_find_prop(int obj, int key_off, int key_len) {
    int p = jobj_first_prop[obj];
    while (p >= 0) {
        if (js_str_eq(jp_key_off[p], jp_key_len[p], key_off, key_len)) return p;
        p = jp_next[p];
    }
    return -1;
}

int js_obj_set_prop_from_top(int obj, int key_off, int key_len) {
    int p = js_obj_find_prop(obj, key_off, key_len);
    if (p < 0) {
        if (jp_count >= MAX_JS_PROPS) { js_set_err("js: prop pool full"); return -1; }
        p = jp_count;
        jp_key_off[p] = key_off;
        jp_key_len[p] = key_len;
        jp_next[p] = jobj_first_prop[obj];
        jobj_first_prop[obj] = p;
        jp_count = p + 1;
    }
    int t = jvs_top - 1;
    jp_tag    [p] = jvs_tag[t];
    jp_num    [p] = jvs_num[t];
    jp_str_off[p] = jvs_str_off[t];
    jp_str_len[p] = jvs_str_len[t];
    jp_obj_idx[p] = jvs_obj_idx[t];
    jp_dom_idx[p] = jvs_dom_idx[t];
    jp_native_id[p] = jvs_native_id[t];
    return p;
}

void js_push_from_prop(int p) {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t]      = jp_tag[p];
    jvs_num[t]      = jp_num[p];
    jvs_str_off[t]  = jp_str_off[p];
    jvs_str_len[t]  = jp_str_len[p];
    jvs_obj_idx[t]  = jp_obj_idx[p];
    jvs_dom_idx[t]  = jp_dom_idx[p];
    jvs_native_id[t]= jp_native_id[p];
}

/* For [obj][index]: convert TOS to a string key; returns interned offset. */
int js_index_top_to_key(int *out_off, int *out_len) {
    int t = jvs_top - 1;
    if (jvs_tag[t] == JS_VAL_STR) {
        *out_off = jvs_str_off[t];
        *out_len = jvs_str_len[t];
        return 0;
    }
    char buf[64];
    int n;
    if (jvs_tag[t] == JS_VAL_NUM &&
        jvs_num[t] >= 2147483648.0 && jvs_num[t] <= 4294967295.0 &&
        fmod(jvs_num[t], 1.0) == 0.0) {
        n = js_format_large_uint_key(jvs_num[t], buf);
    } else if (jvs_tag[t] == JS_VAL_NUM &&
               jvs_num[t] <= -2147483648.0 &&
               jvs_num[t] >= -4294967295.0 &&
               fmod(jvs_num[t], 1.0) == 0.0) {
        buf[0] = '-';
        n = 1 + js_format_large_uint_key(-jvs_num[t], buf + 1);
    } else {
        n = js_to_string_at(t, buf, 64);
    }
    int off = js_str_intern(buf, n);
    if (off < 0) {
        js_set_err("js: string pool full");
        *out_off = -1;
        *out_len = 0;
        return -1;
    }
    *out_off = off;
    *out_len = n;
    return 0;
}

int js_alloc_function(int param_first, int body, int captured_scope) {
    if (jfn_count >= MAX_JS_FUNCS) { js_set_err("js: function pool full"); return -1; }
    int f = jfn_count;
    jfn_param_first[f]    = param_first;
    jfn_body[f]           = body;
    jfn_captured_scope[f] = captured_scope;
    jfn_native_id[f]      = -1;
    jfn_count = f + 1;
    return f;
}

void js_push_func(int fn_idx) {
    int t = js_push_slot();
    if (t < 0) return;
    jvs_tag[t] = JS_VAL_FUNC;
    jvs_obj_idx[t] = fn_idx;
}

void js_call_user_function(int fn_idx, int argc) {
    /* args sit at [jvs_top-argc .. jvs_top-1]. Build a fresh scope
     * frame parented to the function's captured scope (closure), bind
     * each param, run the body, restore caller scope.*/
    int saved_scope = jsc_cur;
    int new_scope = js_scope_enter(jfn_captured_scope[fn_idx]);
    if (new_scope < 0) { jvs_top = jvs_top - argc; return; }
    jsc_cur = new_scope;
    /* bind params */
    int p = jfn_param_first[fn_idx];
    int i = 0;
    while (p >= 0) {
        int o = jn_a[p]; int l = jn_b[p];
        int b = js_binding_alloc(new_scope, o, l);
        if (i < argc && b >= 0) {
            int src = jvs_top - argc + i;
            jb_tag[b]      = jvs_tag[src];
            jb_num[b]      = jvs_num[src];
            jb_str_off[b]  = jvs_str_off[src];
            jb_str_len[b]  = jvs_str_len[src];
            jb_obj_idx[b]  = jvs_obj_idx[src];
            jb_dom_idx[b]  = jvs_dom_idx[src];
            jb_native_id[b]= jvs_native_id[src];
        }
        p = jn_next[p];
        i = i + 1;
    }
    /* drop args */
    jvs_top = jvs_top - argc;
    if (js_last_error[0] != 0) {
        jsc_cur = saved_scope;
        return;
    }
    /* execute body */
    int saved_signal = js_ctrl_signal;
    js_ctrl_signal = 0;
    int saved_vs_top = jvs_top;
    js_eval_stmt(jfn_body[fn_idx]);
    if (js_last_error[0] != 0) {
        jvs_top = saved_vs_top;
        js_ctrl_signal = saved_signal;
        jsc_cur = saved_scope;
        return;
    }
    /* if RETURN signaled, top of stack already holds the return value. */
    if (js_ctrl_signal != 3) {
        /* fell off end - push undefined */
        if (jvs_top == saved_vs_top) js_push_undef();
        else {
            /* stray values on stack from expr stmts - drop them */
            while (jvs_top > saved_vs_top + 1) js_pop();
        }
    }
    if (js_last_error[0] != 0 || jvs_top != saved_vs_top + 1) {
        if (js_last_error[0] == 0) {
            js_set_err("js: function result stack imbalance");
        }
        jvs_top = saved_vs_top;
    }
    js_ctrl_signal = saved_signal;
    jsc_cur = saved_scope;
}

void js_eval_call(int node) {
    int expr_stack_base = jvs_top;
    int callee = jn_a[node];
    /* Special-case console.log syntactically: there is no console
     * global at runtime, so the regular MEMBER path can't find it.*/
    int handled_console_log = 0;
    if (callee >= 0 && jn_kind[callee] == JS_NODE_MEMBER) {
        int obj = jn_a[callee];
        int koff = jn_b[callee];
        int klen = jn_c[callee];
        if (obj >= 0 && jn_kind[obj] == JS_NODE_IDENT) {
            int ioff = jn_a[obj]; int ilen = jn_b[obj];
            if (ilen == 7 && klen == 3 &&
                js_str_eq_text(ioff, ilen, "console") &&
                js_str_eq_text(koff, klen, "log")) {
                handled_console_log = 1;
            }
        }
    }
    /* If the callee is a MEMBER expression on a value, capture the
     * receiver before evaluating - the property lookup eats the
     * object off the stack.*/
    int has_this = 0;
    int this_tag = JS_VAL_UNDEF;
    int this_dom_idx = -1;
    int this_obj_idx = -1;
    if (!handled_console_log && callee >= 0 && jn_kind[callee] == JS_NODE_MEMBER) {
        js_eval_expr(jn_a[callee]);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        int rt = jvs_top - 1;
        this_tag     = jvs_tag[rt];
        this_dom_idx = jvs_dom_idx[rt];
        this_obj_idx = jvs_obj_idx[rt];
        has_this = 1;
        int koff = jn_b[callee]; int klen = jn_c[callee];
        if (this_tag == JS_VAL_DOMNODE) {
            jvs_top = rt;
            jsd_dom_member_get(this_dom_idx, koff, klen);
        } else if (this_tag == JS_VAL_OBJ || this_tag == JS_VAL_ARR) {
            int p = js_obj_find_prop(this_obj_idx, koff, klen);
            jvs_top = rt;
            if (p >= 0) js_push_from_prop(p); else js_push_undef();
        } else {
            jvs_top = rt;
            js_push_undef();
        }
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
    } else if (!handled_console_log) {
        js_eval_expr(callee);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
    }
    int callee_top = jvs_top - 1;
    int saved_before_args = jvs_top;
    int argc = 0;
    int arg = jn_b[node];
    while (arg >= 0) {
        js_eval_expr(arg);
        if (js_last_error[0] != 0 ||
            jvs_top != saved_before_args + argc + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        arg = jn_next[arg];
        argc = argc + 1;
    }
    if (handled_console_log) {
        js_console_log_top_n(argc);
        jvs_top = expr_stack_base;
        js_push_undef();
        if (js_last_error[0] != 0) jvs_top = expr_stack_base;
        return;
    }
    int ctag = jvs_tag[callee_top];
    if (ctag == JS_VAL_FUNC) {
        int fn_idx = jvs_obj_idx[callee_top];
        for (int k = 0; k < argc; k++) {
            int dst = callee_top + k;
            int src = callee_top + 1 + k;
            jvs_tag[dst]     = jvs_tag[src];
            jvs_num[dst]     = jvs_num[src];
            jvs_str_off[dst] = jvs_str_off[src];
            jvs_str_len[dst] = jvs_str_len[src];
            jvs_obj_idx[dst] = jvs_obj_idx[src];
            jvs_dom_idx[dst] = jvs_dom_idx[src];
            jvs_native_id[dst] = jvs_native_id[src];
        }
        jvs_top = callee_top + argc;
        js_call_user_function(fn_idx, argc);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
        }
        return;
    }
    if (ctag == JS_VAL_NATIVE) {
        int native_id = jvs_native_id[callee_top];
        /* Make args contiguous at top. */
        for (int k = 0; k < argc; k++) {
            int dst = callee_top + k;
            int src = callee_top + 1 + k;
            jvs_tag[dst]     = jvs_tag[src];
            jvs_num[dst]     = jvs_num[src];
            jvs_str_off[dst] = jvs_str_off[src];
            jvs_str_len[dst] = jvs_str_len[src];
            jvs_obj_idx[dst] = jvs_obj_idx[src];
            jvs_dom_idx[dst] = jvs_dom_idx[src];
            jvs_native_id[dst] = jvs_native_id[src];
        }
        jvs_top = callee_top + argc;
        if (has_this) {
            jsd_this_tag = this_tag;
            jsd_this_dom_idx = this_dom_idx;
            jsd_this_obj_idx = this_obj_idx;
        } else {
            jsd_this_tag = JS_VAL_UNDEF;
            jsd_this_dom_idx = -1;
            jsd_this_obj_idx = -1;
        }
        js_native_call(native_id, argc);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
        }
        return;
    }
    jvs_top = expr_stack_base;
    js_set_err("js: callee is not a function");
}

typedef struct js_target_ref {
    int kind;
    int binding;
    int name_off;
    int name_len;
    int value_tag;
    int obj_idx;
    int dom_idx;
    int key_off;
    int key_len;
} js_target_ref_t;

int js_resolve_target(int target_node, js_target_ref_t *ref) {
    ref->kind = -1;
    ref->binding = -1;
    ref->name_off = -1;
    ref->name_len = 0;
    ref->value_tag = JS_VAL_UNDEF;
    ref->obj_idx = -1;
    ref->dom_idx = -1;
    ref->key_off = -1;
    ref->key_len = 0;
    if (target_node < 0) {
        js_set_err("js: assignment target unsupported");
        return 0;
    }

    int kind = jn_kind[target_node];
    ref->kind = kind;
    if (kind == JS_NODE_IDENT) {
        ref->name_off = jn_a[target_node];
        ref->name_len = jn_b[target_node];
        ref->binding = js_lookup_binding(jsc_cur,
                                         ref->name_off, ref->name_len);
        return 1;
    }

    int stack_base = jvs_top;
    if (kind == JS_NODE_MEMBER) {
        js_eval_expr(jn_a[target_node]);
        if (js_last_error[0] != 0) {
            jvs_top = stack_base;
            return 0;
        }
        if (jvs_top <= stack_base) {
            js_set_err("js: assignment target evaluation failed");
            return 0;
        }
        int value = jvs_top - 1;
        ref->value_tag = jvs_tag[value];
        ref->obj_idx = jvs_obj_idx[value];
        ref->dom_idx = jvs_dom_idx[value];
        ref->key_off = jn_b[target_node];
        ref->key_len = jn_c[target_node];
        jvs_top = stack_base;
        return 1;
    }

    if (kind == JS_NODE_INDEX) {
        js_eval_expr(jn_a[target_node]);
        if (js_last_error[0] != 0) {
            jvs_top = stack_base;
            return 0;
        }
        if (jvs_top <= stack_base) {
            js_set_err("js: assignment target evaluation failed");
            return 0;
        }
        int object_value = jvs_top - 1;
        ref->value_tag = jvs_tag[object_value];
        ref->obj_idx = jvs_obj_idx[object_value];
        ref->dom_idx = jvs_dom_idx[object_value];
        js_eval_expr(jn_b[target_node]);
        if (js_last_error[0] != 0) {
            jvs_top = stack_base;
            return 0;
        }
        if (jvs_top <= object_value + 1) {
            jvs_top = stack_base;
            js_set_err("js: assignment key evaluation failed");
            return 0;
        }
        if (ref->value_tag == JS_VAL_ARR &&
            js_array_numeric_index_is_unsupported(jvs_top - 1)) {
            js_set_err("js: array index exceeds runtime limit");
            jvs_top = stack_base;
            return 0;
        }
        if (js_index_top_to_key(&ref->key_off, &ref->key_len) != 0) {
            jvs_top = stack_base;
            return 0;
        }
        int resolved_array_index;
        if (ref->value_tag == JS_VAL_ARR &&
            js_array_index_from_key(ref->key_off, ref->key_len,
                                    &resolved_array_index) < 0) {
            js_set_err("js: array index exceeds runtime limit");
            jvs_top = stack_base;
            return 0;
        }
        jvs_top = stack_base;
        return 1;
    }

    js_set_err("js: assignment target unsupported");
    return 0;
}

void js_load_target(js_target_ref_t *ref) {
    if (ref->kind == JS_NODE_IDENT) {
        if (ref->binding >= 0) js_push_from_binding(ref->binding);
        else js_push_undef();
        return;
    }

    if (ref->value_tag == JS_VAL_ARR && ref->key_len == 6 &&
        js_str_eq_text(ref->key_off, ref->key_len, "length")) {
        js_push_num((double)jobj_arr_len[ref->obj_idx]);
        return;
    }
    if (ref->kind == JS_NODE_MEMBER &&
        ref->value_tag == JS_VAL_DOMNODE) {
        jsd_dom_member_get(ref->dom_idx, ref->key_off, ref->key_len);
        return;
    }
    if (ref->kind == JS_NODE_MEMBER && ref->value_tag == JS_VAL_STYLE) {
        jsd_style_get(ref->dom_idx, ref->key_off, ref->key_len);
        return;
    }
    if (ref->value_tag == JS_VAL_OBJ || ref->value_tag == JS_VAL_ARR) {
        int property = js_obj_find_prop(ref->obj_idx,
                                        ref->key_off, ref->key_len);
        if (property >= 0) js_push_from_prop(property);
        else js_push_undef();
        return;
    }
    js_push_undef();
}

void js_store_target(js_target_ref_t *ref) {
    int value = jvs_top - 1;
    if (value < 0) return;

    if (ref->kind == JS_NODE_IDENT) {
        int binding = ref->binding;
        if (binding < 0) {
            /* The right side may have created the unresolved name first. */
            binding = js_lookup_binding(jsc_cur,
                                        ref->name_off, ref->name_len);
            if (binding < 0) {
                /* Sloppy-mode assignment creates a root binding. */
                binding = js_binding_alloc(0, ref->name_off, ref->name_len);
            }
            ref->binding = binding;
        }
        if (binding >= 0) js_binding_set_from_top(binding);
        js_pop();
        return;
    }

    if (ref->kind == JS_NODE_MEMBER) {
        serial_printf("[c2] member-set: tag=%d dom=%d klen=%d\n",
                      ref->value_tag, ref->dom_idx, ref->key_len);
    }
    if (ref->value_tag == JS_VAL_ARR && ref->key_len == 6 &&
        js_str_eq_text(ref->key_off, ref->key_len, "length")) {
        js_set_err("js: array length assignment unsupported");
        js_pop();
        return;
    }
    if (ref->kind == JS_NODE_MEMBER && ref->value_tag == JS_VAL_STYLE) {
        char value_text[256];
        int value_len = js_to_string_at(value, value_text, 256);
        jsd_style_set(ref->dom_idx, ref->key_off, ref->key_len,
                      value_text, value_len);
    } else if (ref->kind == JS_NODE_MEMBER &&
               ref->value_tag == JS_VAL_DOMNODE) {
        jsd_dom_member_set(ref->dom_idx, ref->key_off, ref->key_len);
    } else if (ref->value_tag == JS_VAL_OBJ ||
               ref->value_tag == JS_VAL_ARR) {
        int array_index = 0;
        int array_index_status = 0;
        if (ref->value_tag == JS_VAL_ARR) {
            array_index_status =
                js_array_index_from_key(ref->key_off, ref->key_len,
                                        &array_index);
            if (array_index_status < 0) {
                js_set_err("js: array index exceeds runtime limit");
                js_pop();
                return;
            }
        }
        int property = js_obj_set_prop_from_top(ref->obj_idx,
                                                ref->key_off, ref->key_len);
        if (property >= 0 && array_index_status > 0) {
            if (array_index >= jobj_arr_len[ref->obj_idx]) {
                jobj_arr_len[ref->obj_idx] = array_index + 1;
            }
        }
    }
    js_pop();
}

void js_eval_assign(int node) {
    int op = jn_a[node];
    int lhs = jn_b[node];
    int rhs = jn_c[node];
    int stack_base = jvs_top;
    js_target_ref_t target;
    if (!js_resolve_target(lhs, &target)) {
        jvs_top = stack_base;
        return;
    }
    if (op == JS_TOK_ASSIGN) {
        js_eval_expr(rhs);
        if (js_last_error[0] != 0 || jvs_top != stack_base + 1) {
            if (js_last_error[0] == 0) {
                js_set_err("js: assignment value stack imbalance");
            }
            jvs_top = stack_base;
            return;
        }
        /* duplicate top so assignment leaves the value on the stack */
        if (js_copy_top_from(jvs_top - 1) != 0) {
            jvs_top = stack_base;
            return;
        }
        js_store_target(&target);
        return;
    }
    /* Resolve the reference once, then load and store through that record. */
    js_load_target(&target);
    if (js_last_error[0] != 0 || jvs_top != stack_base + 1) {
        if (js_last_error[0] == 0) {
            js_set_err("js: assignment load stack imbalance");
        }
        jvs_top = stack_base;
        return;
    }
    js_eval_expr(rhs);
    if (js_last_error[0] != 0 || jvs_top != stack_base + 2) {
        if (js_last_error[0] == 0) {
            js_set_err("js: assignment value stack imbalance");
        }
        jvs_top = stack_base;
        return;
    }
    int a = jvs_top - 2;
    int b = jvs_top - 1;
    if (op == JS_TOK_PLUS_EQ &&
        (jvs_tag[a] == JS_VAL_STR || jvs_tag[b] == JS_VAL_STR)) {
        js_concat_at(a, b);
        if (js_last_error[0] != 0) {
            jvs_top = stack_base;
            return;
        }
        if (js_copy_top_from(jvs_top - 1) != 0) {
            jvs_top = stack_base;
            return;
        }
        js_store_target(&target);
        return;
    }
    double na = js_to_number_at(a);
    double nb = js_to_number_at(b);
    double r = na;
    if (op == JS_TOK_PLUS_EQ) r = na + nb;
    else if (op == JS_TOK_MINUS_EQ) r = na - nb;
    else if (op == JS_TOK_STAR_EQ)  r = na * nb;
    else if (op == JS_TOK_SLASH_EQ) r = na / nb;
    else if (op == JS_TOK_PERCENT_EQ) r = fmod(na, nb);
    jvs_top = a;
    js_push_num(r);
    if (js_copy_top_from(jvs_top - 1) != 0) {
        jvs_top = stack_base;
        return;
    }
    js_store_target(&target);
}

void js_eval_bin(int node) {
    int op = jn_a[node];
    int l = jn_b[node];
    int r = jn_c[node];
    int expr_stack_base = jvs_top;
    /* Short-circuit operators evaluate one side first. */
    if (op == JS_TOK_AND_AND) {
        js_eval_expr(l);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        if (!js_to_bool_at(jvs_top - 1)) return;
        jvs_top = expr_stack_base;
        js_eval_expr(r);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
        }
        return;
    }
    if (op == JS_TOK_OR_OR) {
        js_eval_expr(l);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        if (js_to_bool_at(jvs_top - 1)) return;
        jvs_top = expr_stack_base;
        js_eval_expr(r);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
        }
        return;
    }
    js_eval_expr(l);
    if (js_last_error[0] != 0 || jvs_top != expr_stack_base + 1) {
        jvs_top = expr_stack_base;
        return;
    }
    js_eval_expr(r);
    if (js_last_error[0] != 0 || jvs_top != expr_stack_base + 2) {
        jvs_top = expr_stack_base;
        return;
    }
    int a = jvs_top - 2;
    int b = jvs_top - 1;
    /* + with any string -> string concat */
    if (op == JS_TOK_PLUS && (jvs_tag[a] == JS_VAL_STR || jvs_tag[b] == JS_VAL_STR)) {
        js_concat_at(a, b);
        if (js_last_error[0] != 0) jvs_top = expr_stack_base;
        return;
    }
    if (op == JS_TOK_EQ_EQ || op == JS_TOK_EQ) {
        int eq = (op == JS_TOK_EQ_EQ) ? js_strict_eq_at(a, b) : js_eq_at(a, b);
        jvs_top = a; js_push_bool(eq); return;
    }
    if (op == JS_TOK_NEQ_EQ || op == JS_TOK_NEQ) {
        int eq = (op == JS_TOK_NEQ_EQ) ? js_strict_eq_at(a, b) : js_eq_at(a, b);
        jvs_top = a; js_push_bool(!eq); return;
    }
    if ((op == JS_TOK_LT || op == JS_TOK_GT ||
         op == JS_TOK_LE || op == JS_TOK_GE) &&
        jvs_tag[a] == JS_VAL_STR && jvs_tag[b] == JS_VAL_STR) {
        int string_order = js_string_compare_at(a, b);
        int string_result = 0;
        if (op == JS_TOK_LT) string_result = string_order < 0;
        else if (op == JS_TOK_GT) string_result = string_order > 0;
        else if (op == JS_TOK_LE) string_result = string_order <= 0;
        else if (op == JS_TOK_GE) string_result = string_order >= 0;
        jvs_top = a;
        js_push_bool(string_result);
        return;
    }
    double na = js_to_number_at(a);
    double nb = js_to_number_at(b);
    double v = 0.0;
    int as_bool = 0;
    int bv = 0;
    if (op == JS_TOK_PLUS)        v = na + nb;
    else if (op == JS_TOK_MINUS)  v = na - nb;
    else if (op == JS_TOK_STAR)   v = na * nb;
    else if (op == JS_TOK_SLASH)  v = na / nb;
    else if (op == JS_TOK_PERCENT) v = fmod(na, nb);
    else if (op == JS_TOK_LT) { as_bool = 1; bv = na < nb; }
    else if (op == JS_TOK_GT) { as_bool = 1; bv = na > nb; }
    else if (op == JS_TOK_LE) { as_bool = 1; bv = na <= nb; }
    else if (op == JS_TOK_GE) { as_bool = 1; bv = na >= nb; }
    jvs_top = a;
    if (as_bool) js_push_bool(bv); else js_push_num(v);
}

void js_eval_unary(int node) {
    int op = jn_a[node];
    int operand = jn_b[node];
    int expr_stack_base = jvs_top;
    js_eval_expr(operand);
    if (js_last_error[0] != 0 || jvs_top != expr_stack_base + 1) {
        jvs_top = expr_stack_base;
        return;
    }
    int t = jvs_top - 1;
    if (op == JS_TOK_NOT) {
        int b = !js_to_bool_at(t);
        jvs_top = t; js_push_bool(b); return;
    }
    if (op == JS_TOK_MINUS) {
        double v = -js_to_number_at(t);
        jvs_top = t; js_push_num(v); return;
    }
    if (op == JS_TOK_PLUS) {
        double v = js_to_number_at(t);
        jvs_top = t; js_push_num(v); return;
    }
    if (op == JS_TOK_KW_TYPEOF) {
        char *s = "undefined";
        int tag = jvs_tag[t];
        if (tag == JS_VAL_NUM)  s = "number";
        else if (tag == JS_VAL_STR)  s = "string";
        else if (tag == JS_VAL_BOOL) s = "boolean";
        else if (tag == JS_VAL_NULL) s = "object";
        else if (tag == JS_VAL_FUNC || tag == JS_VAL_NATIVE) s = "function";
        else if (tag == JS_VAL_OBJ || tag == JS_VAL_ARR || tag == JS_VAL_DOMNODE) s = "object";
        int sl = 0; while (s[sl]) sl = sl + 1;
        int off = js_str_intern(s, sl);
        if (off < 0) {
            jvs_top = expr_stack_base;
            return;
        }
        jvs_top = t; js_push_str(off, sl); return;
    }
}

void js_eval_inc(int node, int post) {
    int op = jn_a[node];
    int operand = jn_b[node];
    int expr_stack_base = jvs_top;
    if (jn_kind[operand] != JS_NODE_IDENT) {
        js_set_err("js: ++/-- target must be identifier");
        return;
    }
    int o = jn_a[operand]; int l = jn_b[operand];
    int b = js_lookup_binding(jsc_cur, o, l);
    if (b < 0) { js_set_err("js: undefined variable in ++/--"); return; }
    double cur = (jb_tag[b] == JS_VAL_NUM) ? jb_num[b]
               : (jb_tag[b] == JS_VAL_BOOL ? jb_num[b] : 0.0);
    double next = (op == JS_TOK_PLUS_PLUS) ? cur + 1.0 : cur - 1.0;
    if (post) js_push_num(cur); else js_push_num(next);
    if (js_last_error[0] != 0 ||
        jvs_top != expr_stack_base + 1) {
        jvs_top = expr_stack_base;
        return;
    }
    jb_tag[b] = JS_VAL_NUM;
    jb_num[b] = next;
}

void js_eval_expr(int node) {
    if (node < 0) { js_push_undef(); return; }
    if (js_last_error[0] != 0) return;
    int expr_stack_base = jvs_top;
    /* Every expression produces one value. Refuse before evaluating any
     * side effects when the caller cannot accept that result. */
    if (expr_stack_base >= MAX_JS_VS) {
        js_set_err("js: value stack overflow");
        return;
    }
    int k = jn_kind[node];
    if (k == JS_NODE_NUM)   { js_push_num(jn_num[node]); return; }
    if (k == JS_NODE_STR)   { js_push_str(jn_a[node], jn_b[node]); return; }
    if (k == JS_NODE_BOOL)  { js_push_bool(jn_a[node]); return; }
    if (k == JS_NODE_NULL)  { js_push_null(); return; }
    if (k == JS_NODE_UNDEF) { js_push_undef(); return; }
    if (k == JS_NODE_IDENT) {
        int o = jn_a[node]; int l = jn_b[node];
        int b = js_lookup_binding(jsc_cur, o, l);
        if (b < 0) { js_push_undef(); return; }
        js_push_from_binding(b); return;
    }
    if (k == JS_NODE_BIN)        { js_eval_bin(node); return; }
    if (k == JS_NODE_UNARY)      { js_eval_unary(node); return; }
    if (k == JS_NODE_ASSIGN)     { js_eval_assign(node); return; }
    if (k == JS_NODE_PRE_INC)    { js_eval_inc(node, 0); return; }
    if (k == JS_NODE_POST_INC)   { js_eval_inc(node, 1); return; }
    if (k == JS_NODE_COND) {
        js_eval_expr(jn_a[node]);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        int b = js_to_bool_at(jvs_top - 1);
        jvs_top = expr_stack_base;
        if (b) js_eval_expr(jn_b[node]); else js_eval_expr(jn_c[node]);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
        }
        return;
    }
    if (k == JS_NODE_CALL) { js_eval_call(node); return; }
    if (k == JS_NODE_FUNC_EXPR) {
        int fn = js_alloc_function(jn_c[node], jn_d[node], jsc_cur);
        if (fn < 0) return;
        js_push_func(fn);
        return;
    }
    if (k == JS_NODE_ARR_LIT) {
        int o = js_alloc_object(1);
        if (o < 0) return;
        int e = jn_a[node];
        int i = 0;
        char keybuf[16];
        while (e >= 0) {
            js_eval_expr(e);
            if (js_last_error[0] != 0 ||
                jvs_top != expr_stack_base + 1) {
                jvs_top = expr_stack_base;
                return;
            }
            int kn = js_format_int(i, keybuf);
            int koff = js_str_intern(keybuf, kn);
            if (koff < 0) {
                js_set_err("js: string pool full");
                jvs_top = expr_stack_base;
                return;
            }
            js_obj_set_prop_from_top(o, koff, kn);
            if (js_last_error[0] != 0) {
                jvs_top = expr_stack_base;
                return;
            }
            jvs_top = expr_stack_base;
            i = i + 1;
            e = jn_next[e];
        }
        jobj_arr_len[o] = i;
        js_push_arr(o);
        return;
    }
    if (k == JS_NODE_OBJ_LIT) {
        int o = js_alloc_object(0);
        if (o < 0) return;
        int prop = jn_a[node];
        while (prop >= 0) {
            int koff = jn_a[prop];
            int klen = jn_b[prop];
            int val = jn_c[prop];
            js_eval_expr(val);
            if (js_last_error[0] != 0 ||
                jvs_top != expr_stack_base + 1) {
                jvs_top = expr_stack_base;
                return;
            }
            js_obj_set_prop_from_top(o, koff, klen);
            if (js_last_error[0] != 0) {
                jvs_top = expr_stack_base;
                return;
            }
            jvs_top = expr_stack_base;
            prop = jn_next[prop];
        }
        js_push_obj(o);
        return;
    }
    if (k == JS_NODE_MEMBER) {
        js_eval_expr(jn_a[node]);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        int t = jvs_top - 1;
        int koff = jn_b[node]; int klen = jn_c[node];
        int tag = jvs_tag[t];
        /* arrays expose .length */
        if (tag == JS_VAL_ARR && klen == 6 &&
            js_str_eq_text(koff, klen, "length")) {
            double n = (double)jobj_arr_len[jvs_obj_idx[t]];
            jvs_top = t;
            js_push_num(n);
            return;
        }
        if (tag == JS_VAL_DOMNODE) {
            int dom_idx = jvs_dom_idx[t];
            jvs_top = t;
            jsd_dom_member_get(dom_idx, koff, klen);
            return;
        }
        if (tag == JS_VAL_STYLE) {
            int dom_idx = jvs_dom_idx[t];
            jvs_top = t;
            jsd_style_get(dom_idx, koff, klen);
            return;
        }
        if (tag == JS_VAL_OBJ || tag == JS_VAL_ARR) {
            int p = js_obj_find_prop(jvs_obj_idx[t], koff, klen);
            jvs_top = t;
            if (p >= 0) js_push_from_prop(p); else js_push_undef();
            return;
        }
        jvs_top = t;
        js_push_undef();
        return;
    }
    if (k == JS_NODE_INDEX) {
        js_eval_expr(jn_a[node]);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        int obj_top = jvs_top - 1;
        int object_tag = jvs_tag[obj_top];
        js_eval_expr(jn_b[node]);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 2) {
            jvs_top = expr_stack_base;
            return;
        }
        if (object_tag == JS_VAL_ARR &&
            js_array_numeric_index_is_unsupported(jvs_top - 1)) {
            js_set_err("js: array index exceeds runtime limit");
            jvs_top = expr_stack_base;
            return;
        }
        int koff; int klen;
        if (js_index_top_to_key(&koff, &klen) != 0) {
            jvs_top = expr_stack_base;
            return;
        }
        int tag = object_tag;
        int oi = jvs_obj_idx[obj_top];
        jvs_top = obj_top;
        if (tag == JS_VAL_OBJ || tag == JS_VAL_ARR) {
            if (tag == JS_VAL_ARR && klen == 6 &&
                js_str_eq_text(koff, klen, "length")) {
                js_push_num((double)jobj_arr_len[oi]);
                return;
            }
            int p = js_obj_find_prop(oi, koff, klen);
            if (p >= 0) js_push_from_prop(p); else js_push_undef();
            return;
        }
        js_push_undef();
        return;
    }
    js_set_err("js: unsupported expression");
    jvs_top = expr_stack_base;
}

/* statements */
void js_exec_block(int block_node) {
    if (block_node < 0) return;
    int s = jn_a[block_node];
    while (s >= 0 && js_ctrl_signal == 0 && js_last_error[0] == 0) {
        js_eval_stmt(s);
        s = jn_next[s];
    }
}

void js_eval_stmt(int node) {
    if (node < 0) return;
    if (js_last_error[0] != 0) return;
    int k = jn_kind[node];
    if (k == JS_NODE_BLOCK) { js_exec_block(node); return; }
    if (k == JS_NODE_EXPR_STMT) {
        int expr_stack_base = jvs_top;
        js_eval_expr(jn_a[node]);
        jvs_top = expr_stack_base;
        return;
    }
    if (k == JS_NODE_VAR_DECL) {
        int d = jn_a[node];
        while (d >= 0) {
            int o = jn_a[d]; int l = jn_b[d]; int init = jn_c[d];
            int b = js_lookup_binding_in_scope(jsc_cur, o, l);
            if (b < 0) b = js_binding_alloc(jsc_cur, o, l);
            if (b < 0) return;
            if (init >= 0 && b >= 0) {
                int expr_stack_base = jvs_top;
                js_eval_expr(init);
                if (js_last_error[0] != 0 ||
                    jvs_top != expr_stack_base + 1) {
                    jvs_top = expr_stack_base;
                    return;
                }
                js_binding_set_from_top(b);
                jvs_top = expr_stack_base;
            }
            d = jn_next[d];
        }
        return;
    }
    if (k == JS_NODE_IF) {
        int expr_stack_base = jvs_top;
        js_eval_expr(jn_a[node]);
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        int b = js_to_bool_at(jvs_top - 1);
        jvs_top = expr_stack_base;
        if (b) js_eval_stmt(jn_b[node]);
        else if (jn_c[node] >= 0) js_eval_stmt(jn_c[node]);
        return;
    }
    if (k == JS_NODE_WHILE) {
        int guard = 0;
        while (guard < 100000) {
            int expr_stack_base = jvs_top;
            js_eval_expr(jn_a[node]);
            if (js_last_error[0] != 0 ||
                jvs_top != expr_stack_base + 1) {
                jvs_top = expr_stack_base;
                return;
            }
            int b = js_to_bool_at(jvs_top - 1);
            jvs_top = expr_stack_base;
            if (!b) break;
            js_eval_stmt(jn_b[node]);
            if (js_ctrl_signal == 1) { js_ctrl_signal = 0; break; }
            if (js_ctrl_signal == 2) { js_ctrl_signal = 0; }
            if (js_ctrl_signal == 3) break;
            if (js_last_error[0] != 0) break;
            guard = guard + 1;
        }
        if (guard >= 100000) js_set_err("js: while loop iteration cap reached");
        return;
    }
    if (k == JS_NODE_FOR) {
        if (jn_a[node] >= 0) js_eval_stmt(jn_a[node]);
        if (js_last_error[0] != 0) return;
        int guard = 0;
        while (guard < 100000) {
            int b = 1;
            if (jn_b[node] >= 0) {
                int expr_stack_base = jvs_top;
                js_eval_expr(jn_b[node]);
                if (js_last_error[0] != 0 ||
                    jvs_top != expr_stack_base + 1) {
                    jvs_top = expr_stack_base;
                    return;
                }
                b = js_to_bool_at(jvs_top - 1);
                jvs_top = expr_stack_base;
            }
            if (!b) break;
            js_eval_stmt(jn_d[node]);
            if (js_ctrl_signal == 1) { js_ctrl_signal = 0; break; }
            if (js_ctrl_signal == 2) { js_ctrl_signal = 0; }
            if (js_ctrl_signal == 3) break;
            if (js_last_error[0] != 0) break;
            if (jn_c[node] >= 0) {
                int expr_stack_base = jvs_top;
                js_eval_expr(jn_c[node]);
                jvs_top = expr_stack_base;
                if (js_last_error[0] != 0) return;
            }
            guard = guard + 1;
        }
        if (guard >= 100000) js_set_err("js: for loop iteration cap reached");
        return;
    }
    if (k == JS_NODE_BREAK)    { js_ctrl_signal = 1; return; }
    if (k == JS_NODE_CONTINUE) { js_ctrl_signal = 2; return; }
    if (k == JS_NODE_RETURN) {
        int expr_stack_base = jvs_top;
        if (jn_a[node] >= 0) js_eval_expr(jn_a[node]);
        else js_push_undef();
        if (js_last_error[0] != 0 ||
            jvs_top != expr_stack_base + 1) {
            jvs_top = expr_stack_base;
            return;
        }
        js_ctrl_signal = 3;
        return;
    }
    if (k == JS_NODE_FUNC_DECL) {
        int fn = js_alloc_function(jn_c[node], jn_d[node], jsc_cur);
        if (fn < 0) return;
        int o = jn_a[node]; int l = jn_b[node];
        if (o >= 0 && l > 0) {
            int b = js_lookup_binding_in_scope(jsc_cur, o, l);
            if (b < 0) b = js_binding_alloc(jsc_cur, o, l);
            if (b >= 0) {
                jb_tag[b]      = JS_VAL_FUNC;
                jb_num[b]      = 0.0;
                jb_obj_idx[b]  = fn;
            }
        }
        return;
    }
}

void js_exec_program(int root) {
    if (root < 0) return;
    /* Allocate (or re-use) the root scope. */
    if (jsc_top == 0) jsc_cur = js_scope_enter(-1);
    int s = jn_a[root];
    while (s >= 0 && js_last_error[0] == 0) {
        js_eval_stmt(s);
        if (js_ctrl_signal == 3) break;        /* top-level return is rare but safe */
        s = jn_next[s];
    }
}
