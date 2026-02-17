/**
 * gui_themes.c - Theming System for cupid-os
 *
 * Active theme management, 6 built-in theme presets,
 * style properties, and .theme file I/O.
 */

#include "gui_themes.h"
#include "string.h"
#include "memory.h"
#include "vfs.h"

/* Built-in Theme Presets */

const ui_theme_t UI_THEME_WINDOWS95 = {
    /* window */
    .window_bg               = 0x00C0C0C0,
    .window_border           = 0x00000000,
    .titlebar_active_start   = 0x00000080,
    .titlebar_active_end     = 0x001084D0,
    .titlebar_inactive_start = 0x00808080,
    .titlebar_inactive_end   = 0x00C0C0C0,
    .titlebar_text           = 0x00FFFFFF,
    /* controls */
    .button_face             = 0x00C0C0C0,
    .button_highlight        = 0x00FFFFFF,
    .button_shadow           = 0x00808080,
    .button_text             = 0x00000000,
    .button_disabled_text    = 0x00808080,
    /* input */
    .input_bg                = 0x00FFFFFF,
    .input_border            = 0x00808080,
    .input_text              = 0x00000000,
    .input_selection         = 0x000000C0,
    /* menu */
    .menu_bg                 = 0x00C0C0C0,
    .menu_hover              = 0x000000C0,
    .menu_selected           = 0x000000C0,
    .menu_text               = 0x00000000,
    .menu_disabled_text      = 0x00808080,
    .menu_separator          = 0x00808080,
    /* accent */
    .accent_primary          = 0x000000C0,
    .accent_secondary        = 0x00008080,
    .link_color              = 0x000000FF,
    /* status */
    .success                 = 0x00008000,
    .warning                 = 0x00808000,
    .error                   = 0x00C00000,
    .info                    = 0x000000C0,
    /* desktop */
    .desktop_bg              = 0x00008080,
    .taskbar_bg              = 0x00C0C0C0,
    .taskbar_text            = 0x00000000
};

const ui_theme_t UI_THEME_PASTEL_DREAM = {
    /* window */
    .window_bg               = 0x00FFF0F5,
    .window_border           = 0x009898A0,
    .titlebar_active_start   = 0x00B8DDFF,
    .titlebar_active_end     = 0x00E0F0FF,
    .titlebar_inactive_start = 0x00D0D0D8,
    .titlebar_inactive_end   = 0x00E8E8F0,
    .titlebar_text           = 0x00282830,
    /* controls */
    .button_face             = 0x00E8E0F0,
    .button_highlight        = 0x00FFFFFF,
    .button_shadow           = 0x00A0A0B0,
    .button_text             = 0x00282830,
    .button_disabled_text    = 0x00A0A0B0,
    /* input */
    .input_bg                = 0x00FFFFFF,
    .input_border            = 0x00B0B0C0,
    .input_text              = 0x00282830,
    .input_selection         = 0x00B8DDFF,
    /* menu */
    .menu_bg                 = 0x00F0F0F5,
    .menu_hover              = 0x00D0E4F8,
    .menu_selected           = 0x00B8DDFF,
    .menu_text               = 0x00282830,
    .menu_disabled_text      = 0x00A0A0B0,
    .menu_separator          = 0x00D0D0D8,
    /* accent */
    .accent_primary          = 0x00B8DDFF,
    .accent_secondary        = 0x00FFB8D0,
    .link_color              = 0x006080C0,
    /* status */
    .success                 = 0x0060C060,
    .warning                 = 0x00E0B040,
    .error                   = 0x00E06060,
    .info                    = 0x0060A0E0,
    /* desktop */
    .desktop_bg              = 0x00E8F0FF,
    .taskbar_bg              = 0x00D8D8E8,
    .taskbar_text            = 0x00282830
};

const ui_theme_t UI_THEME_DARK_MODE = {
    /* window */
    .window_bg               = 0x00282830,
    .window_border           = 0x00484858,
    .titlebar_active_start   = 0x00384870,
    .titlebar_active_end     = 0x00506090,
    .titlebar_inactive_start = 0x00383840,
    .titlebar_inactive_end   = 0x00484858,
    .titlebar_text           = 0x00E0E0E8,
    /* controls */
    .button_face             = 0x00404050,
    .button_highlight        = 0x00585868,
    .button_shadow           = 0x00202028,
    .button_text             = 0x00E0E0E8,
    .button_disabled_text    = 0x00686878,
    /* input */
    .input_bg                = 0x00202028,
    .input_border            = 0x00585868,
    .input_text              = 0x00E0E0E8,
    .input_selection         = 0x00506090,
    /* menu */
    .menu_bg                 = 0x00303038,
    .menu_hover              = 0x00506090,
    .menu_selected           = 0x00506090,
    .menu_text               = 0x00E0E0E8,
    .menu_disabled_text      = 0x00686878,
    .menu_separator          = 0x00484858,
    /* accent */
    .accent_primary          = 0x006090D0,
    .accent_secondary        = 0x00D07090,
    .link_color              = 0x0080B0F0,
    /* status */
    .success                 = 0x0050C070,
    .warning                 = 0x00D0A040,
    .error                   = 0x00D05050,
    .info                    = 0x005090D0,
    /* desktop */
    .desktop_bg              = 0x00181820,
    .taskbar_bg              = 0x00202028,
    .taskbar_text            = 0x00E0E0E8
};

const ui_theme_t UI_THEME_HIGH_CONTRAST = {
    /* window */
    .window_bg               = 0x00000000,
    .window_border           = 0x00FFFFFF,
    .titlebar_active_start   = 0x000000FF,
    .titlebar_active_end     = 0x000000FF,
    .titlebar_inactive_start = 0x00008000,
    .titlebar_inactive_end   = 0x00008000,
    .titlebar_text           = 0x00FFFFFF,
    /* controls */
    .button_face             = 0x00000000,
    .button_highlight        = 0x00FFFFFF,
    .button_shadow           = 0x00FFFFFF,
    .button_text             = 0x00FFFFFF,
    .button_disabled_text    = 0x00808080,
    /* input */
    .input_bg                = 0x00000000,
    .input_border            = 0x00FFFFFF,
    .input_text              = 0x00FFFFFF,
    .input_selection         = 0x00FFFF00,
    /* menu */
    .menu_bg                 = 0x00000000,
    .menu_hover              = 0x00FFFF00,
    .menu_selected           = 0x00FFFF00,
    .menu_text               = 0x00FFFFFF,
    .menu_disabled_text      = 0x00808080,
    .menu_separator          = 0x00FFFFFF,
    /* accent */
    .accent_primary          = 0x00FFFF00,
    .accent_secondary        = 0x0000FFFF,
    .link_color              = 0x0000FF00,
    /* status */
    .success                 = 0x0000FF00,
    .warning                 = 0x00FFFF00,
    .error                   = 0x00FF0000,
    .info                    = 0x0000FFFF,
    /* desktop */
    .desktop_bg              = 0x00000000,
    .taskbar_bg              = 0x00000000,
    .taskbar_text            = 0x00FFFFFF
};

const ui_theme_t UI_THEME_RETRO_AMBER = {
    /* window */
    .window_bg               = 0x00201000,
    .window_border           = 0x00C88020,
    .titlebar_active_start   = 0x00C88020,
    .titlebar_active_end     = 0x00E8A040,
    .titlebar_inactive_start = 0x00604010,
    .titlebar_inactive_end   = 0x00805020,
    .titlebar_text           = 0x00201000,
    /* controls */
    .button_face             = 0x00402008,
    .button_highlight        = 0x00C88020,
    .button_shadow           = 0x00100800,
    .button_text             = 0x00FFB840,
    .button_disabled_text    = 0x00604010,
    /* input */
    .input_bg                = 0x00100800,
    .input_border            = 0x00C88020,
    .input_text              = 0x00FFB840,
    .input_selection         = 0x00805020,
    /* menu */
    .menu_bg                 = 0x00201000,
    .menu_hover              = 0x00C88020,
    .menu_selected           = 0x00C88020,
    .menu_text               = 0x00FFB840,
    .menu_disabled_text      = 0x00604010,
    .menu_separator          = 0x00805020,
    /* accent */
    .accent_primary          = 0x00FFB840,
    .accent_secondary        = 0x00C88020,
    .link_color              = 0x00FFD870,
    /* status */
    .success                 = 0x00C88020,
    .warning                 = 0x00FFB840,
    .error                   = 0x00FF4020,
    .info                    = 0x00C88020,
    /* desktop */
    .desktop_bg              = 0x00100800,
    .taskbar_bg              = 0x00201000,
    .taskbar_text            = 0x00FFB840
};

const ui_theme_t UI_THEME_VAPORWAVE = {
    /* window */
    .window_bg               = 0x001A0028,
    .window_border           = 0x00FF71CE,
    .titlebar_active_start   = 0x00FF71CE,
    .titlebar_active_end     = 0x0001CDFE,
    .titlebar_inactive_start = 0x00602080,
    .titlebar_inactive_end   = 0x00404080,
    .titlebar_text           = 0x00FFFFFF,
    /* controls */
    .button_face             = 0x002D1050,
    .button_highlight        = 0x00FF71CE,
    .button_shadow           = 0x00100020,
    .button_text             = 0x0001CDFE,
    .button_disabled_text    = 0x00604080,
    /* input */
    .input_bg                = 0x00100020,
    .input_border            = 0x00B967FF,
    .input_text              = 0x0001CDFE,
    .input_selection         = 0x00FF71CE,
    /* menu */
    .menu_bg                 = 0x001A0028,
    .menu_hover              = 0x00FF71CE,
    .menu_selected           = 0x00B967FF,
    .menu_text               = 0x0001CDFE,
    .menu_disabled_text      = 0x00604080,
    .menu_separator          = 0x00B967FF,
    /* accent */
    .accent_primary          = 0x00FF71CE,
    .accent_secondary        = 0x0001CDFE,
    .link_color              = 0x00B967FF,
    /* status */
    .success                 = 0x0005FFA1,
    .warning                 = 0x00FFFB96,
    .error                   = 0x00FF6B6B,
    .info                    = 0x0001CDFE,
    /* desktop */
    .desktop_bg              = 0x000D0018,
    .taskbar_bg              = 0x001A0028,
    .taskbar_text            = 0x0001CDFE
};

/* Active State */

static ui_theme_t g_active_theme;
static ui_style_t g_active_style;

void gui_themes_init(void) {
    g_active_theme = UI_THEME_PASTEL_DREAM;

    g_active_style.window_shadow_offset = 2;
    g_active_style.window_shadow_blur   = 0;
    g_active_style.window_border_width  = 1;
    g_active_style.button_border_width  = 1;
    g_active_style.corner_radius        = 0;
    g_active_style.use_gradients        = false;
    g_active_style.use_shadows          = true;
    g_active_style.use_animations       = false;
    g_active_style.animation_duration_ms = 200;
}

void ui_theme_set(const ui_theme_t *theme) {
    if (theme) g_active_theme = *theme;
}

ui_theme_t *ui_theme_get(void) {
    return &g_active_theme;
}

void ui_theme_reset_default(void) {
    g_active_theme = UI_THEME_PASTEL_DREAM;
}

void ui_style_set(const ui_style_t *style) {
    if (style) g_active_style = *style;
}

ui_style_t *ui_style_get(void) {
    return &g_active_style;
}

/* Theme File I/O */

/* parse hex value from string like "0x00FF80" or "FF80" */
static uint32_t parse_hex(const char *s) {
    uint32_t val = 0;
    /* Skip "0x" or "0X" prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    while (*s) {
        char c = *s;
        uint32_t digit;
        if (c >= '0' && c <= '9')      digit = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = (uint32_t)(c - 'A' + 10);
        else break;
        val = (val << 4) | digit;
        s++;
    }
    return val;
}

/* write hex to buffer */
static int hex_to_str(uint32_t v, char *buf) {
    static const char hex[] = "0123456789ABCDEF";
    int i;
    buf[0] = '0'; buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> (28 - i * 4)) & 0xF];
    }
    buf[10] = '\0';
    return 10;
}

/* Simple line-by-line parser for INI-style .theme files.
   Format:
     [Colors]
     window_bg = 0x00FFF0F5
     ...
*/

/* Map of field name -> offset into ui_theme_t */
typedef struct {
    const char *name;
    int         offset;
} theme_field_t;

#define FIELD(n) { #n, (int)__builtin_offsetof(ui_theme_t, n) }

static const theme_field_t theme_fields[] = {
    FIELD(window_bg),
    FIELD(window_border),
    FIELD(titlebar_active_start),
    FIELD(titlebar_active_end),
    FIELD(titlebar_inactive_start),
    FIELD(titlebar_inactive_end),
    FIELD(titlebar_text),
    FIELD(button_face),
    FIELD(button_highlight),
    FIELD(button_shadow),
    FIELD(button_text),
    FIELD(button_disabled_text),
    FIELD(input_bg),
    FIELD(input_border),
    FIELD(input_text),
    FIELD(input_selection),
    FIELD(menu_bg),
    FIELD(menu_hover),
    FIELD(menu_selected),
    FIELD(menu_text),
    FIELD(menu_disabled_text),
    FIELD(menu_separator),
    FIELD(accent_primary),
    FIELD(accent_secondary),
    FIELD(link_color),
    FIELD(success),
    FIELD(warning),
    FIELD(error),
    FIELD(info),
    FIELD(desktop_bg),
    FIELD(taskbar_bg),
    FIELD(taskbar_text),
    { NULL, 0 }
};

#undef FIELD

int ui_theme_load(const char *path) {
    int fd;
    char buf[512];
    int bytes_read;
    int pos, line_start;

    fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return -1;

    bytes_read = vfs_read(fd, buf, (int)sizeof(buf) - 1);
    vfs_close(fd);

    if (bytes_read <= 0) return -1;
    buf[bytes_read] = '\0';

    /* Parse line by line */
    pos = 0;
    while (pos < bytes_read) {
        char line[128];
        int len = 0;

        line_start = pos;

        /* Extract line */
        while (pos < bytes_read && buf[pos] != '\n' && len < 127) {
            line[len++] = buf[pos++];
        }
        line[len] = '\0';
        if (pos < bytes_read && buf[pos] == '\n') pos++;

        (void)line_start;

        /* Skip comments and section headers */
        if (line[0] == '#' || line[0] == ';' || line[0] == '[' || len == 0)
            continue;

        /* Find '=' */
        {
            int eq = -1;
            int k;
            for (k = 0; k < len; k++) {
                if (line[k] == '=') { eq = k; break; }
            }
            if (eq < 0) continue;

            /* Extract key (trim trailing spaces) */
            {
                char key[64];
                char val[64];
                int klen = eq;
                int vstart = eq + 1;

                while (klen > 0 && line[klen - 1] == ' ') klen--;
                if (klen <= 0 || klen >= 64) continue;
                memcpy(key, line, (uint32_t)klen);
                key[klen] = '\0';

                /* Extract value (skip leading spaces) */
                while (vstart < len && line[vstart] == ' ') vstart++;
                {
                    int vlen = len - vstart;
                    if (vlen <= 0 || vlen >= 64) continue;
                    memcpy(val, line + vstart, (uint32_t)vlen);
                    val[vlen] = '\0';
                }

                /* Look up field and set */
                {
                    const theme_field_t *f = theme_fields;
                    while (f->name) {
                        if (strcmp(f->name, key) == 0) {
                            uint32_t *dst = (uint32_t *)((char *)&g_active_theme + f->offset);
                            *dst = parse_hex(val);
                            break;
                        }
                        f++;
                    }
                }
            }
        }
    }

    return 0;
}

int ui_theme_save(const char *path) {
    int fd;
    char buf[1024];
    int pos = 0;
    const theme_field_t *f;

    fd = vfs_open(path, O_WRONLY | O_CREAT);
    if (fd < 0) return -1;

    /* Write header */
    {
        const char *hdr = "[Colors]\n";
        int hlen = (int)strlen(hdr);
        memcpy(buf + pos, hdr, (uint32_t)hlen);
        pos += hlen;
    }

    /* Write each field */
    f = theme_fields;
    while (f->name && pos < 900) {
        uint32_t *src = (uint32_t *)((char *)&g_active_theme + f->offset);
        int nlen = (int)strlen(f->name);

        memcpy(buf + pos, f->name, (uint32_t)nlen);
        pos += nlen;

        buf[pos++] = ' ';
        buf[pos++] = '=';
        buf[pos++] = ' ';

        pos += hex_to_str(*src, buf + pos);

        buf[pos++] = '\n';
        f++;
    }

    buf[pos] = '\0';
    vfs_write(fd, buf, (uint32_t)pos);
    vfs_close(fd);

    return 0;
}
