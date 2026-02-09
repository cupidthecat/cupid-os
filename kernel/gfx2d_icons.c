/**
 * gfx2d_icons.c - Desktop icon system for CupidOS
 *
 * Manages desktop icons: registration, drawing, hit-testing,
 * selection, drag & drop, persistence, and auto-discovery from
 * //icon: directives in CupidC source files.
 */

#include "gfx2d_icons.h"
#include "gfx2d.h"
#include "string.h"
#include "memory.h"
#include "vfs.h"
#include "../drivers/serial.h"

/* ── Icon storage ─────────────────────────────────────────────────── */
static gfx2d_icon_t icons[GFX2D_MAX_ICONS];
static int icon_count = 0;

/* ── Config file path ─────────────────────────────────────────────── */
static const char *ICON_CONFIG_PATH = "/home/.desktop_icons.conf";
static const int ICON_LEFT_MARGIN = 20;

static int clamp_icon_x(int x) {
    if (x < ICON_LEFT_MARGIN) {
        return ICON_LEFT_MARGIN;
    }
    return x;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Initialization
 * ══════════════════════════════════════════════════════════════════════ */

void gfx2d_icons_init(void) {
    icon_count = 0;
    memset(icons, 0, sizeof(icons));
    serial_printf("[icons] Icon system initialized\n");
}

/* ══════════════════════════════════════════════════════════════════════
 *  Icon Registration
 * ══════════════════════════════════════════════════════════════════════ */

int gfx2d_icon_register(const char *label, const char *program_path,
                        int x, int y) {
    if (icon_count >= GFX2D_MAX_ICONS) {
        serial_printf("[icons] Max icons reached (%d)\n", GFX2D_MAX_ICONS);
        return -1;
    }

    /* Check for duplicate path */
    for (int i = 0; i < icon_count; i++) {
        if (icons[i].enabled && strcmp(icons[i].program_path, program_path) == 0) {
            serial_printf("[icons] Duplicate icon for %s, skipping\n", program_path);
            return i; /* Return existing handle */
        }
    }

    gfx2d_icon_t *ic = &icons[icon_count];
    memset(ic, 0, sizeof(gfx2d_icon_t));

    /* Copy label */
    int i = 0;
    while (label[i] && i < GFX2D_ICON_LABEL_MAX - 1) {
        ic->label[i] = label[i];
        i++;
    }
    ic->label[i] = '\0';

    /* Copy program path */
    i = 0;
    while (program_path[i] && i < GFX2D_ICON_PATH_MAX - 1) {
        ic->program_path[i] = program_path[i];
        i++;
    }
    ic->program_path[i] = '\0';

    /* Auto-position if not specified */
    if (x < 0 || y < 0) {
        /* Place in a column along the left edge */
        ic->x = ICON_LEFT_MARGIN;
        ic->y = 10 + icon_count * GFX2D_ICON_GRID_SIZE;
    } else {
        ic->x = clamp_icon_x(x);
        ic->y = y;
    }

    ic->type = ICON_TYPE_APP;
    ic->color = 0x0080FF; /* Default blue */
    ic->custom_draw = 0;
    ic->launch = 0;
    ic->selected = 0;
    ic->enabled = 1;

    serial_printf("[icons] Registered icon '%s' at (%d,%d) for %s\n",
                  ic->label, ic->x, ic->y, ic->program_path);

    return icon_count++;
}

void gfx2d_icon_set_desc(int handle, const char *desc) {
    if (handle < 0 || handle >= icon_count) return;
    int i = 0;
    while (desc[i] && i < GFX2D_ICON_DESC_MAX - 1) {
        icons[handle].description[i] = desc[i];
        i++;
    }
    icons[handle].description[i] = '\0';
}

void gfx2d_icon_set_type(int handle, int type) {
    if (handle < 0 || handle >= icon_count) return;
    icons[handle].type = type;
}

void gfx2d_icon_set_color(int handle, uint32_t color) {
    if (handle < 0 || handle >= icon_count) return;
    icons[handle].color = color;
}

void gfx2d_icon_set_custom_drawer(int handle, void (*drawer)(int, int)) {
    if (handle < 0 || handle >= icon_count) return;
    icons[handle].custom_draw = drawer;
    icons[handle].type = ICON_TYPE_CUSTOM;
}

void gfx2d_icon_set_launch(int handle, void (*launch_fn)(void)) {
    if (handle < 0 || handle >= icon_count) return;
    icons[handle].launch = launch_fn;
}

void (*gfx2d_icon_get_launch(int handle))(void) {
    if (handle < 0 || handle >= icon_count) return 0;
    return icons[handle].launch;
}

void gfx2d_icon_set_pos(int handle, int x, int y) {
    if (handle < 0 || handle >= icon_count) return;
    icons[handle].x = clamp_icon_x(x);
    icons[handle].y = y;
}

void gfx2d_icon_snap_to_grid(int handle) {
    if (handle < 0 || handle >= icon_count) return;
    gfx2d_icon_t *ic = &icons[handle];
    int rel_x = ic->x - ICON_LEFT_MARGIN;
    if (rel_x < 0) rel_x = 0;
    ic->x = (rel_x / GFX2D_ICON_GRID_SIZE) * GFX2D_ICON_GRID_SIZE +
            ICON_LEFT_MARGIN;
    ic->y = (ic->y / GFX2D_ICON_GRID_SIZE) * GFX2D_ICON_GRID_SIZE + 10;
}

const char *gfx2d_icon_get_label(int handle) {
    if (handle < 0 || handle >= icon_count) return "";
    return icons[handle].label;
}

const char *gfx2d_icon_get_path(int handle) {
    if (handle < 0 || handle >= icon_count) return "";
    return icons[handle].program_path;
}

void gfx2d_icon_select(int handle) {
    /* Deselect all first */
    for (int i = 0; i < icon_count; i++)
        icons[i].selected = 0;
    if (handle >= 0 && handle < icon_count)
        icons[handle].selected = 1;
}

void gfx2d_icon_deselect_all(void) {
    for (int i = 0; i < icon_count; i++)
        icons[i].selected = 0;
}

int gfx2d_icon_find_by_path(const char *path) {
    for (int i = 0; i < icon_count; i++) {
        if (icons[i].enabled && strcmp(icons[i].program_path, path) == 0)
            return i;
    }
    return -1;
}

void gfx2d_icon_unregister(int handle) {
    if (handle < 0 || handle >= icon_count) return;
    icons[handle].enabled = 0;
}

int gfx2d_icon_count(void) {
    return icon_count;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Hit Testing
 * ══════════════════════════════════════════════════════════════════════ */

int gfx2d_icon_at_pos(int x, int y) {
    for (int i = 0; i < icon_count; i++) {
        if (!icons[i].enabled) continue;
        gfx2d_icon_t *ic = &icons[i];
        /* Hit area covers icon (32x32) + label area below (32x12) */
        if (x >= ic->x && x < ic->x + GFX2D_ICON_SIZE &&
            y >= ic->y && y < ic->y + GFX2D_ICON_SIZE + 14) {
            return i;
        }
    }
    return -1;
}

int gfx2d_icons_handle_click(int x, int y) {
    int handle = gfx2d_icon_at_pos(x, y);
    if (handle >= 0) {
        gfx2d_icon_select(handle);
        return 1;
    }
    gfx2d_icon_deselect_all();
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Default Icon Drawing
 * ══════════════════════════════════════════════════════════════════════ */

void gfx2d_draw_icon_app(int x, int y, uint32_t color) {
    /* Application window icon (32x32) */
    /* Outer window frame */
    gfx2d_rect_fill(x + 2, y + 2, 28, 28, color);
    gfx2d_bevel(x + 2, y + 2, 28, 28, 1);

    /* Title bar */
    gfx2d_rect_fill(x + 4, y + 4, 24, 6, 0x000080);

    /* Window contents (white area) */
    gfx2d_rect_fill(x + 4, y + 11, 24, 17, 0xFFFFFF);

    /* Shadow effect */
    gfx2d_rect_fill_alpha(x + 4, y + 30, 28, 2, 0x40000000);
}

void gfx2d_draw_icon_folder(int x, int y, uint32_t color) {
    /* Folder tab */
    gfx2d_rect_fill(x + 4, y + 8, 10, 4, color);
    gfx2d_bevel(x + 4, y + 8, 10, 4, 1);

    /* Folder body */
    gfx2d_rect_fill(x + 2, y + 12, 28, 16, color);
    gfx2d_bevel(x + 2, y + 12, 28, 16, 1);

    /* Shadow */
    gfx2d_rect_fill_alpha(x + 4, y + 30, 28, 2, 0x40000000);
}

void gfx2d_draw_icon_file(int x, int y, uint32_t color) {
    (void)color;

    /* Paper sheet */
    gfx2d_rect_fill(x + 8, y + 4, 16, 24, 0xFFFFFF);
    gfx2d_rect(x + 8, y + 4, 16, 24, 0x000000);

    /* Dog-ear corner */
    gfx2d_line(x + 20, y + 4, x + 24, y + 8, 0x000000);
    gfx2d_line(x + 20, y + 4, x + 20, y + 8, 0x000000);
    gfx2d_line(x + 20, y + 8, x + 24, y + 8, 0x000000);

    /* Lines on paper */
    gfx2d_hline(x + 10, y + 12, 12, 0x000080);
    gfx2d_hline(x + 10, y + 16, 12, 0x000080);
    gfx2d_hline(x + 10, y + 20, 8, 0x000080);
}

void gfx2d_draw_icon_terminal(int x, int y, uint32_t color) {
    (void)color;

    /* Terminal/console icon (32x32) */
    /* Monitor body */
    gfx2d_rect_fill(x + 2, y + 4, 28, 20, 0x202020);
    gfx2d_bevel(x + 2, y + 4, 28, 20, 1);

    /* Screen area (dark) */
    gfx2d_rect_fill(x + 4, y + 6, 24, 14, 0x000000);

    /* Prompt text ">_" in green */
    gfx2d_text(x + 6, y + 9, ">_", 0x00FF00, 0);

    /* Monitor stand */
    gfx2d_rect_fill(x + 12, y + 24, 8, 3, 0x808080);
    gfx2d_rect_fill(x + 8, y + 27, 16, 2, 0x808080);
    gfx2d_bevel(x + 8, y + 27, 16, 2, 1);
}

void gfx2d_draw_icon_notepad(int x, int y, uint32_t color) {
    /* Spiral-bound notebook icon (32x32) */
    (void)color;

    /* Page background */
    gfx2d_rect_fill(x + 4, y + 2, 24, 28, 0xFFFFF0);
    gfx2d_rect(x + 4, y + 2, 24, 28, 0x000000);

    /* Spiral binding strip on left */
    gfx2d_rect_fill(x + 4, y + 2, 6, 28, 0xC0C0C0);

    /* Spiral coils */
    for (int cy = 4; cy < 26; cy += 5) {
        gfx2d_rect_fill(x + 5, y + cy, 4, 3, 0xFFFFF0);
        gfx2d_rect(x + 5, y + cy, 4, 3, 0x808080);
    }

    /* Ruled lines */
    gfx2d_hline(x + 12, y + 10, 14, 0x8080C0);
    gfx2d_hline(x + 12, y + 15, 14, 0x8080C0);
    gfx2d_hline(x + 12, y + 20, 14, 0x8080C0);
    gfx2d_hline(x + 12, y + 25, 10, 0x8080C0);
}

void gfx2d_draw_icon_default(int x, int y, int type, uint32_t color) {
    switch (type) {
        case ICON_TYPE_APP:
            gfx2d_draw_icon_app(x, y, color);
            break;
        case ICON_TYPE_FOLDER:
            gfx2d_draw_icon_folder(x, y, color);
            break;
        case ICON_TYPE_FILE:
            gfx2d_draw_icon_file(x, y, color);
            break;
        default:
            /* Generic colored square */
            gfx2d_rect_fill(x + 8, y + 8, 16, 16, color);
            gfx2d_bevel(x + 8, y + 8, 16, 16, 1);
            break;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Icon Rendering
 * ══════════════════════════════════════════════════════════════════════ */

static void draw_single_icon(gfx2d_icon_t *icon) {
    int x = clamp_icon_x(icon->x);
    int y = icon->y;

    /* Draw selection highlight */
    if (icon->selected) {
        gfx2d_rect_fill_alpha(x - 2, y - 2, 36, 36, 0x400080FF);
        gfx2d_rect(x - 2, y - 2, 36, 36, 0x0080FF);
    }

    /* Draw icon graphic */
    if (icon->custom_draw) {
        icon->custom_draw(x, y);
    } else {
        gfx2d_draw_icon_default(x, y, icon->type, icon->color);
    }

    /* Draw label centered below icon */
    int screen_w = gfx2d_width();
    int label_w = gfx2d_text_width(icon->label, 0); /* font 0 = small */
    int label_x = x + (GFX2D_ICON_SIZE / 2) - (label_w / 2);
    if (label_x < 0) {
        label_x = 0;
    }
    if (screen_w > 0) {
        int max_x = screen_w - label_w - 2;
        if (max_x < 0) {
            max_x = 0;
        }
        if (label_x > max_x) {
            label_x = max_x;
        }
    }

    /* Label background (semi-transparent for readability) */
    gfx2d_rect_fill_alpha(label_x - 2, y + GFX2D_ICON_SIZE + 1,
                          label_w + 4, 10, 0x80000000);

    /* Label text */
    gfx2d_text(label_x, y + GFX2D_ICON_SIZE + 2, icon->label,
               0xFFFFFF, 0);
}

void gfx2d_icons_draw_all(void) {
    for (int i = 0; i < icon_count; i++) {
        if (!icons[i].enabled) continue;
        draw_single_icon(&icons[i]);
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Directive Parsing
 * ══════════════════════════════════════════════════════════════════════ */

/* Helper: skip leading whitespace and optional quotes */
static void parse_directive_value(const char *src, char *dst, int max) {
    /* Skip leading whitespace */
    while (*src == ' ' || *src == '\t') src++;
    /* Skip optional opening quote */
    int in_quote = 0;
    if (*src == '"') { src++; in_quote = 1; }

    int i = 0;
    while (*src && i < max - 1) {
        if (in_quote && *src == '"') break;
        if (!in_quote && (*src == '\n' || *src == '\r')) break;
        dst[i++] = *src++;
    }
    /* Trim trailing whitespace */
    while (i > 0 && (dst[i - 1] == ' ' || dst[i - 1] == '\t' ||
                     dst[i - 1] == '\r' || dst[i - 1] == '\n'))
        i--;
    dst[i] = '\0';
}

/* Helper: parse integer from directive value */
static int parse_directive_int(const char *src) {
    while (*src == ' ' || *src == '\t') src++;
    int val = 0;
    int neg = 0;
    if (*src == '-') { neg = 1; src++; }
    while (*src >= '0' && *src <= '9') {
        val = val * 10 + (*src - '0');
        src++;
    }
    return neg ? -val : val;
}

/* Helper: parse hex from directive value (0xRRGGBB) */
static uint32_t parse_directive_hex(const char *src) {
    while (*src == ' ' || *src == '\t') src++;
    if (*src == '0' && (*(src + 1) == 'x' || *(src + 1) == 'X'))
        src += 2;

    uint32_t val = 0;
    for (int i = 0; i < 8; i++) {
        char c = *src;
        if (c >= '0' && c <= '9')
            val = (val << 4) | (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f')
            val = (val << 4) | (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            val = (val << 4) | (uint32_t)(c - 'A' + 10);
        else
            break;
        src++;
    }
    return val;
}

int gfx2d_icons_parse_directives(const char *path, icon_info_t *info) {
    /* Default values */
    strcpy(info->label, "Program");
    info->description[0] = '\0';
    info->x = -1;  /* Auto-position */
    info->y = -1;
    info->type = ICON_TYPE_APP;
    info->color = 0x0080FF;  /* Default blue */

    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return 0;

    char buf[512];
    int total_read = 0;
    int found_icon = 0;

    /* Read up to 2KB to find directives (they should be at the top) */
    while (total_read < 2048) {
        int n = vfs_read(fd, buf, 511);
        if (n <= 0) break;
        buf[n] = '\0';
        total_read += n;

        /* Scan buffer line by line */
        char *line = buf;
        while (line && *line) {
            /* Find end of line */
            char *next = strchr(line, '\n');
            if (next) *next = '\0';

            /* Parse //icon: directives */
            if (strncmp(line, "//icon:", 7) == 0) {
                found_icon = 1;
                parse_directive_value(line + 7, info->label,
                                     GFX2D_ICON_LABEL_MAX);
            }
            else if (strncmp(line, "//icon_desc:", 12) == 0) {
                parse_directive_value(line + 12, info->description,
                                     GFX2D_ICON_DESC_MAX);
            }
            else if (strncmp(line, "//icon_x:", 9) == 0) {
                info->x = parse_directive_int(line + 9);
            }
            else if (strncmp(line, "//icon_y:", 9) == 0) {
                info->y = parse_directive_int(line + 9);
            }
            else if (strncmp(line, "//icon_type:", 12) == 0) {
                char type_str[32];
                parse_directive_value(line + 12, type_str, 32);
                if (strcmp(type_str, "folder") == 0)
                    info->type = ICON_TYPE_FOLDER;
                else if (strcmp(type_str, "file") == 0)
                    info->type = ICON_TYPE_FILE;
                else if (strcmp(type_str, "custom") == 0)
                    info->type = ICON_TYPE_CUSTOM;
                else
                    info->type = ICON_TYPE_APP;
            }
            else if (strncmp(line, "//icon_color:", 13) == 0) {
                info->color = parse_directive_hex(line + 13);
            }

            /* Stop scanning after first non-comment, non-blank line
             * once we've passed the header area */
            if (found_icon && line[0] != '/' && line[0] != ' ' &&
                line[0] != '\t' && line[0] != '\n' &&
                line[0] != '\r' && line[0] != '\0' &&
                line[0] != '*') {
                goto done;
            }

            if (next) line = next + 1;
            else break;
        }

        if (total_read >= 512 && !found_icon)
            break; /* No icon directive in first 512 bytes, give up */
    }

done:
    vfs_close(fd);
    return found_icon;
}

/* ══════════════════════════════════════════════════════════════════════
 *  Auto-Discovery: Scan /bin for icons
 * ══════════════════════════════════════════════════════════════════════ */

void gfx2d_icons_scan_bin(void) {
    int fd = vfs_open("/bin", O_RDONLY);
    if (fd < 0) {
        serial_printf("[icons] Cannot open /bin for scanning\n");
        return;
    }

    vfs_dirent_t ent;
    int scanned = 0;
    int registered = 0;

    while (vfs_readdir(fd, &ent) > 0) {
        /* Check for .cc extension */
        int len = (int)strlen(ent.name);
        if (len < 4) continue;
        if (ent.name[len - 3] != '.' ||
            ent.name[len - 2] != 'c' ||
            ent.name[len - 1] != 'c')
            continue;

        /* Build full path */
        char path[GFX2D_ICON_PATH_MAX];
        strcpy(path, "/bin/");
        strcat(path, ent.name);

        scanned++;

        /* Parse icon directives */
        icon_info_t info;
        if (gfx2d_icons_parse_directives(path, &info)) {
            int handle = gfx2d_icon_register(info.label, path,
                                             info.x, info.y);
            if (handle >= 0) {
                gfx2d_icon_set_desc(handle, info.description);
                gfx2d_icon_set_type(handle, info.type);
                gfx2d_icon_set_color(handle, info.color);
                registered++;
            }
        }
    }

    vfs_close(fd);

    serial_printf("[icons] Scanned %d .cc files, registered %d icons\n",
                  scanned, registered);

    /* Load saved positions (overrides defaults) */
    gfx2d_icons_load();
}

/* ══════════════════════════════════════════════════════════════════════
 *  Persistence: Save/Load icon positions
 * ══════════════════════════════════════════════════════════════════════ */

void gfx2d_icons_save(void) {
    int fd = vfs_open(ICON_CONFIG_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) {
        serial_printf("[icons] Cannot save icon config\n");
        return;
    }

    /* Write header comment */
    const char *header = "# Desktop icon positions\n";
    vfs_write(fd, header, (uint32_t)strlen(header));

    /* Write each icon: path,x,y,enabled */
    for (int i = 0; i < icon_count; i++) {
        gfx2d_icon_t *ic = &icons[i];
        char line[256];
        int pos = 0;

        /* path */
        int j = 0;
        while (ic->program_path[j] && pos < 200)
            line[pos++] = ic->program_path[j++];
        line[pos++] = ',';

        /* x (int to string) */
        {
            char num[16];
            int n = ic->x;
            int neg = 0;
            int ni = 0;
            if (n < 0) { neg = 1; n = -n; }
            if (n == 0) { num[ni++] = '0'; }
            else {
                char tmp[16];
                int ti = 0;
                while (n > 0) { tmp[ti++] = (char)('0' + n % 10); n /= 10; }
                while (ti > 0) num[ni++] = tmp[--ti];
            }
            if (neg) { line[pos++] = '-'; }
            for (int k = 0; k < ni; k++) line[pos++] = num[k];
        }
        line[pos++] = ',';

        /* y */
        {
            char num[16];
            int n = ic->y;
            int neg = 0;
            int ni = 0;
            if (n < 0) { neg = 1; n = -n; }
            if (n == 0) { num[ni++] = '0'; }
            else {
                char tmp[16];
                int ti = 0;
                while (n > 0) { tmp[ti++] = (char)('0' + n % 10); n /= 10; }
                while (ti > 0) num[ni++] = tmp[--ti];
            }
            if (neg) { line[pos++] = '-'; }
            for (int k = 0; k < ni; k++) line[pos++] = num[k];
        }
        line[pos++] = ',';

        /* enabled */
        line[pos++] = ic->enabled ? '1' : '0';
        line[pos++] = '\n';
        line[pos] = '\0';

        vfs_write(fd, line, (uint32_t)pos);
    }

    vfs_close(fd);
    serial_printf("[icons] Saved %d icon positions\n", icon_count);
}

void gfx2d_icons_load(void) {
    int fd = vfs_open(ICON_CONFIG_PATH, O_RDONLY);
    if (fd < 0) {
        /* Config file doesn't exist yet — that's fine */
        return;
    }

    char buf[1024];
    int n = vfs_read(fd, buf, 1023);
    vfs_close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    /* Parse line by line: path,x,y,enabled */
    char *line = buf;
    while (line && *line) {
        char *next = strchr(line, '\n');
        if (next) *next = '\0';

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0' || line[0] == '\r') {
            if (next) { line = next + 1; continue; }
            else break;
        }

        /* Parse: path,x,y,enabled */
        char path[GFX2D_ICON_PATH_MAX];
        int x = 0, y = 0, enabled = 1;

        /* Extract path */
        int pi = 0;
        while (*line && *line != ',' && pi < GFX2D_ICON_PATH_MAX - 1)
            path[pi++] = *line++;
        path[pi] = '\0';
        if (*line == ',') line++;

        /* Extract x */
        int neg = 0;
        if (*line == '-') { neg = 1; line++; }
        while (*line >= '0' && *line <= '9') {
            x = x * 10 + (*line - '0');
            line++;
        }
        if (neg) x = -x;
        if (*line == ',') line++;

        /* Extract y */
        neg = 0;
        if (*line == '-') { neg = 1; line++; }
        while (*line >= '0' && *line <= '9') {
            y = y * 10 + (*line - '0');
            line++;
        }
        if (neg) y = -y;
        if (*line == ',') line++;

        /* Extract enabled */
        if (*line == '0') enabled = 0;

        /* Find existing icon by path and update position */
        int handle = gfx2d_icon_find_by_path(path);
        if (handle >= 0) {
            icons[handle].x = clamp_icon_x(x);
            icons[handle].y = y;
            icons[handle].enabled = enabled;
            serial_printf("[icons] Loaded position for %s: (%d,%d)\n",
                          path, icons[handle].x, y);
        }

        if (next) line = next + 1;
        else break;
    }
}
