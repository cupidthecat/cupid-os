/**
 * gfx2d_icons.h - Desktop icon system for CupidOS
 *
 * Manages desktop icons: registration, drawing, hit-testing,
 * selection, drag & drop, and persistence.
 */

#ifndef GFX2D_ICONS_H
#define GFX2D_ICONS_H

#include "types.h"

#define GFX2D_MAX_ICONS       32
#define GFX2D_ICON_SIZE       32   /* 32x32 pixel icons */
#define GFX2D_ICON_GRID_SIZE  60   /* Grid snap spacing */
#define GFX2D_ICON_LABEL_MAX  32
#define GFX2D_ICON_DESC_MAX   64
#define GFX2D_ICON_PATH_MAX   128

#define ICON_TYPE_APP         0
#define ICON_TYPE_FOLDER      1
#define ICON_TYPE_FILE        2
#define ICON_TYPE_CUSTOM      3

typedef struct {
    char     label[GFX2D_ICON_LABEL_MAX];
    char     description[GFX2D_ICON_DESC_MAX];
    int      x;
    int      y;
    int      type;
    uint32_t color;
} icon_info_t;

typedef struct {
    char     label[GFX2D_ICON_LABEL_MAX];
    char     description[GFX2D_ICON_DESC_MAX];
    char     program_path[GFX2D_ICON_PATH_MAX];
    int      x;
    int      y;
    int      type;
    uint32_t color;
    void   (*custom_draw)(int x, int y);
    void   (*launch)(void);     /* Direct launch callback (kernel icons) */
    int      selected;
    int      enabled;
} gfx2d_icon_t;

/** Initialize the icon system */
void gfx2d_icons_init(void);

/** Register a desktop icon. Returns handle (>=0) or -1 on error. */
int gfx2d_icon_register(const char *label, const char *program_path,
                        int x, int y);

/** Set icon description (tooltip) */
void gfx2d_icon_set_desc(int handle, const char *desc);

/** Set icon type (ICON_TYPE_APP, ICON_TYPE_FOLDER, etc.) */
void gfx2d_icon_set_type(int handle, int type);

/** Set icon color */
void gfx2d_icon_set_color(int handle, uint32_t color);

/** Register custom icon drawing function */
void gfx2d_icon_set_custom_drawer(int handle, void (*drawer)(int, int));

/** Set direct launch callback (for kernel-level icons) */
void gfx2d_icon_set_launch(int handle, void (*launch_fn)(void));

/** Get launch callback (NULL if none) */
void (*gfx2d_icon_get_launch(int handle))(void);

/** Get icon at desktop position. Returns handle or -1. */
int gfx2d_icon_at_pos(int x, int y);

/** Set icon position (for drag & drop) */
void gfx2d_icon_set_pos(int handle, int x, int y);

/** Snap icon to grid */
void gfx2d_icon_snap_to_grid(int handle);

/** Get icon label */
const char *gfx2d_icon_get_label(int handle);

/** Get icon program path */
const char *gfx2d_icon_get_path(int handle);

/** Get icon description */
const char *gfx2d_icon_get_desc(int handle);

/** Get icon X position */
int gfx2d_icon_get_x(int handle);

/** Get icon Y position */
int gfx2d_icon_get_y(int handle);

/** Select an icon (deselects others) */
void gfx2d_icon_select(int handle);

/** Deselect all icons */
void gfx2d_icon_deselect_all(void);

/** Find icon by program path. Returns handle or -1. */
int gfx2d_icon_find_by_path(const char *path);

/** Draw all registered icons */
void gfx2d_icons_draw_all(void);

/** Handle icon click. Returns 1 if an icon was clicked. */
int gfx2d_icons_handle_click(int x, int y);

/** Save icon positions to disk */
void gfx2d_icons_save(void);

/** Load icon positions from disk */
void gfx2d_icons_load(void);

/** Unregister an icon */
void gfx2d_icon_unregister(int handle);

/** Get total number of registered icons */
int gfx2d_icon_count(void);

/** Scan /bin for .cc files with //icon: directives and register them */
void gfx2d_icons_scan_bin(void);

/** Parse //icon: directives from a .cc source file.
 *  Returns 1 if //icon: was found, 0 otherwise. */
int gfx2d_icons_parse_directives(const char *path, icon_info_t *info);

void gfx2d_draw_icon_app(int x, int y, uint32_t color);
void gfx2d_draw_icon_folder(int x, int y, uint32_t color);
void gfx2d_draw_icon_file(int x, int y, uint32_t color);
void gfx2d_draw_icon_default(int x, int y, int type, uint32_t color);
void gfx2d_draw_icon_terminal(int x, int y, uint32_t color);
void gfx2d_draw_icon_notepad(int x, int y, uint32_t color);
void gfx2d_icon_draw_named(const char *label, int x, int y, uint32_t color);

#endif /* GFX2D_ICONS_H */
