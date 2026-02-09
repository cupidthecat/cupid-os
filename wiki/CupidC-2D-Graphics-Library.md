# CupidC 2D Graphics Library (gfx2d)

A software-rendered 2D graphics library for cupid-os, exposed as CupidC kernel bindings. Enables desktop development, app development, and retro-aesthetic visual effects.

---

## Overview

The `gfx2d` library lives in `kernel/gfx2d.c` and is registered as CupidC bindings in `cupidc.c`. CupidC programs call `gfx2d_*` functions directly — no imports needed.

**Target display:** 640×480, 32-bit XRGB/ARGB framebuffer (VBE/Bochs)

**Color format:** `0xAARRGGBB` (unsigned int, alpha byte enables transparency)

---

## Compiler Improvements (Required)

These additions to the CupidC compiler enable cleaner graphics app code:

### `unsigned` types
```c
unsigned int color = 0xFF112233;   // proper full-range color literals
unsigned char alpha = 255;
```

### `typedef`
```c
typedef int Color;
typedef int Sprite;
Color bg = 0x00FFE8F0;
```

### `const`
```c
const int SCREEN_W = 640;
const int SCREEN_H = 480;
```

---

## API Reference

### Screen

```c
void gfx2d_init();                         // initialize library (called by runtime)
void gfx2d_clear(unsigned int color);      // clear framebuffer
void gfx2d_flip();                         // present back buffer to screen
void gfx2d_set_framebuffer(unsigned int *fb); // update backing buffer (advanced)
int  gfx2d_width();                        // returns 640
int  gfx2d_height();                       // returns 480
```

### Pixels & Lines

```c
void     gfx2d_pixel(int x, int y, unsigned int color);
unsigned int gfx2d_getpixel(int x, int y);
void     gfx2d_line(int x1, int y1, int x2, int y2, unsigned int color);
void     gfx2d_hline(int x, int y, int w, unsigned int color);
void     gfx2d_vline(int x, int y, int h, unsigned int color);
```

### Rectangles

```c
void gfx2d_rect(int x, int y, int w, int h, unsigned int color);
void gfx2d_rect_fill(int x, int y, int w, int h, unsigned int color);
void gfx2d_rect_round(int x, int y, int w, int h, int r, unsigned int color);
void gfx2d_rect_round_fill(int x, int y, int w, int h, int r, unsigned int color);
```

### Circles & Ellipses

```c
void gfx2d_circle(int x, int y, int r, unsigned int color);
void gfx2d_circle_fill(int x, int y, int r, unsigned int color);
void gfx2d_ellipse(int x, int y, int rx, int ry, unsigned int color);
void gfx2d_ellipse_fill(int x, int y, int rx, int ry, unsigned int color);
```

### Alpha Blending

Alpha byte: `0x00` = fully transparent, `0xFF` = fully opaque.

```c
void gfx2d_rect_fill_alpha(int x, int y, int w, int h, unsigned int argb);
void gfx2d_pixel_alpha(int x, int y, unsigned int argb);
```

### Gradients

```c
void gfx2d_gradient_h(int x, int y, int w, int h,
                       unsigned int c1, unsigned int c2);  // left→right
void gfx2d_gradient_v(int x, int y, int w, int h,
                       unsigned int c1, unsigned int c2);  // top→bottom
```

### Drop Shadow

```c
// blur: 1-4 (softness level, integer box blur)
void gfx2d_shadow(int x, int y, int w, int h, int blur, unsigned int color);
```

### Dithering

Win95-style pattern fills. Pattern: `0`=checker, `1`=horizontal, `2`=vertical, `3`=diagonal.

```c
void gfx2d_dither_rect(int x, int y, int w, int h,
                        unsigned int c1, unsigned int c2, int pattern);
```

### Scanlines (CRT Effect)

```c
// alpha: 0-255 intensity of scanline overlay
void gfx2d_scanlines(int x, int y, int w, int h, int alpha);
```

### Clipping

All subsequent draw calls are clipped to the set region.

```c
void gfx2d_clip_set(int x, int y, int w, int h);
void gfx2d_clip_clear();
```

### Sprites

Handle-based. Raw binary format: first 8 bytes are `int width, int height`, then `w*h` pixels as ARGB `unsigned int` each.

```c
int  gfx2d_sprite_load(char *path);
void gfx2d_sprite_free(int handle);
void gfx2d_sprite_draw(int handle, int x, int y);
void gfx2d_sprite_draw_alpha(int handle, int x, int y);
void gfx2d_sprite_draw_scaled(int handle, int x, int y, int w, int h);
int  gfx2d_sprite_width(int handle);
int  gfx2d_sprite_height(int handle);
```

### Text & Fonts

Three built-in bitmap fonts: `FONT_SMALL`=0 (6×8), `FONT_NORMAL`=1 (8×8), `FONT_LARGE`=2 (8×16 doubled).

```c
void gfx2d_text(int x, int y, char *str, unsigned int color, int font);
void gfx2d_text_shadow(int x, int y, char *str,
                        unsigned int color, unsigned int shadow, int font);
void gfx2d_text_outline(int x, int y, char *str,
                         unsigned int color, unsigned int outline, int font);
int  gfx2d_text_width(char *str, int font);
int  gfx2d_text_height(int font);
```

### Retro Atmosphere Effects

```c
void gfx2d_vignette(int strength);                         // darkens corners
void gfx2d_pixelate(int x, int y, int w, int h, int block_size);
void gfx2d_invert(int x, int y, int w, int h);
void gfx2d_tint(int x, int y, int w, int h, unsigned int color, int alpha);
```

### Win95-Style UI Helpers

```c
void gfx2d_bevel(int x, int y, int w, int h, int raised);  // 1=raised, 0=sunken
void gfx2d_panel(int x, int y, int w, int h);              // sunken inset panel
void gfx2d_titlebar(int x, int y, int w, int h,
                     unsigned int c1, unsigned int c2);     // gradient title bar
```

### Demo Scene Effects

Tick-based — pass `uptime_ms()` for animation. No floating point; plasma uses integer sine LUT.

```c
void gfx2d_copper_bars(int y, int count, int spacing, int *colors);
void gfx2d_plasma(int x, int y, int w, int h, int tick);
void gfx2d_checkerboard(int x, int y, int w, int h,
                         int size, unsigned int c1, unsigned int c2);
```

### Blend Modes

Set the global blend mode for all subsequent drawing operations.

```c
void gfx2d_blend_mode(int mode);

// Mode constants:
// GFX2D_BLEND_NORMAL   = 0  (default: source overwrites dest)
// GFX2D_BLEND_ADD      = 1  (additive: src + dst, clamped)
// GFX2D_BLEND_MULTIPLY = 2  (multiply: src * dst / 255)
// GFX2D_BLEND_SCREEN   = 3  (screen: 1 - (1-src)(1-dst))
// GFX2D_BLEND_OVERLAY  = 4  (overlay: combines multiply + screen)
```

### Offscreen Surfaces

Create offscreen render targets for compositing and effects.

```c
int  gfx2d_surface_alloc(int w, int h);      // returns handle 0..7, or -1
void gfx2d_surface_free(int handle);
void gfx2d_surface_fill(int handle, unsigned int color);
void gfx2d_surface_set_active(int handle);   // redirect drawing here
void gfx2d_surface_unset_active();           // back to screen
void gfx2d_surface_blit(int handle, int x, int y);
void gfx2d_surface_blit_alpha(int handle, int x, int y, int alpha);
```

### Tweening / Easing

Integer-based easing functions for smooth animations. All map `t=0..dur` to `start..end`.

```c
int gfx2d_tween_linear(int t, int start, int end, int dur);
int gfx2d_tween_ease_in_out(int t, int start, int end, int dur);  // smoothstep
int gfx2d_tween_bounce(int t, int start, int end, int dur);       // bounce out
int gfx2d_tween_elastic(int t, int start, int end, int dur);      // elastic wobble
```

**Example:**
```c
int x = gfx2d_tween_bounce(tick % 120, 50, 400, 120);
```

### Particle System

Lightweight particle effects with gravity and alpha fadeout.

```c
int  gfx2d_particles_create();    // allocates system, returns handle
void gfx2d_particles_free(int handle);
void gfx2d_particle_emit(int handle, int x, int y,
                          int vx, int vy, unsigned int color, int life);
void gfx2d_particles_update(int handle, int gravity);  // call each frame
void gfx2d_particles_draw(int handle);
int  gfx2d_particles_alive(int handle);  // count of live particles
```

**Example:**
```c
int psys = gfx2d_particles_create();
gfx2d_particle_emit(psys, 320, 240, -2, -4, 0xFF88FF, 60);
// in game loop:
gfx2d_particles_update(psys, 1);  // gravity = 1
gfx2d_particles_draw(psys);
```

### Advanced Drawing Tools

```c
// Quadratic Bezier curve (recursive subdivision)
void gfx2d_bezier(int x0, int y0, int x1, int y1,
                   int x2, int y2, unsigned int color);

// Filled triangle (scanline rasterization)
void gfx2d_tri_fill(int x0, int y0, int x1, int y1,
                     int x2, int y2, unsigned int color);

// Anti-aliased line (Wu's algorithm)
void gfx2d_line_aa(int x0, int y0, int x1, int y1, unsigned int color);

// Flood fill (iterative BFS, ~2KB stack)
void gfx2d_flood_fill(int x, int y, unsigned int color);
```

### Fullscreen Mode

Taking over the screen disables the desktop compositor/WM, giving you raw access.

```c
void gfx2d_fullscreen_enter();
void gfx2d_fullscreen_exit();
int  gfx2d_fullscreen_active();  // returns 1 if active
```

### Mouse Cursor

For fullscreen apps that need to render their own cursor.

```c
void gfx2d_draw_cursor();   // draw cursor at current mouse position
void gfx2d_cursor_hide();   // restore pixels under cursor (call before drawing frame)
```

### File Dialogs

Modal file open/save dialogs with directory navigation. The dialog blocks the caller with its own event loop (keyboard + mouse) until the user confirms or cancels. Visual style matches the Win95-inspired notepad dialog.

```c
int gfx2d_file_dialog_open(char *start_path, char *result_path, char *filter_ext);
int gfx2d_file_dialog_save(char *start_path, char *default_name,
                           char *result_path, char *filter_ext);
```

**Parameters:**

| Parameter | Description |
|-----------|-------------|
| `start_path` | Initial directory to browse (e.g., `"/"`, `"/home"`) |
| `default_name` | Pre-filled filename for save dialog (e.g., `"untitled.bmp"`) |
| `result_path` | Output buffer for selected path (must be 128 bytes / `VFS_MAX_PATH`) |
| `filter_ext` | Extension filter (e.g., `".bmp"`, `".txt"`) or `0` for all files |

**Returns:** `1` if user confirmed, `0` if cancelled, negative on error.

**Dialog features:**
- Directory listing with `[D]` icons for folders, `|=` for files
- Click or Enter to navigate into directories, `..` goes up
- Double-click file to select (open) or type a filename (save)
- Scrollable file list (up to 64 entries)
- OK and Cancel buttons
- Extension filter shown in title bar: `"Open (*.bmp)"`

**Example:**
```c
void main() {
    gfx2d_init();
    gfx2d_fullscreen_enter();

    char path[128];

    // Open dialog filtered to .bmp files
    if (file_dialog_open("/home", path, ".bmp") == 1) {
        int spr = gfx2d_sprite_load(path);
        if (spr >= 0) {
            gfx2d_sprite_draw(spr, 0, 0);
            gfx2d_flip();
            gfx2d_sprite_free(spr);
        }
    }

    gfx2d_fullscreen_exit();
}
```

### BMP Image Support

Load and save standard 24-bit BMP (bitmap) image files. Decodes to/from 32bpp XRGB pixel buffers (`0x00RRGGBB`), compatible with the framebuffer and sprite system.

```c
int bmp_get_info(char *path, void *info);        // read dimensions without loading pixels
int bmp_decode(char *path, int *buffer, int buffer_size);  // decode BMP to XRGB buffer
int bmp_encode(char *path, int *buffer, int width, int height);  // encode XRGB buffer to BMP
int bmp_decode_to_fb(char *path, int x, int y);  // decode BMP directly to framebuffer
```

**`bmp_info_t` struct** (passed as `void*` to `bmp_get_info`):

| Field | Type | Description |
|-------|------|-------------|
| `width` | `int` | Image width in pixels |
| `height` | `int` | Image height in pixels |
| `bpp` | `int` | Bits per pixel (24 for supported files) |
| `data_size` | `int` | Bytes needed for XRGB buffer (`width * height * 4`) |

**Error codes:**

| Code | Name | Meaning |
|------|------|--------|
| `0` | `BMP_OK` | Success |
| `-1` | `BMP_EINVAL` | Invalid file or parameters |
| `-2` | `BMP_EFORMAT` | Unsupported BMP format (not 24-bit uncompressed) |
| `-3` | `BMP_EIO` | File I/O error |
| `-4` | `BMP_ENOMEM` | Buffer too small |

**Supported format:** BITMAPINFOHEADER (Windows 3.x+), 24-bit uncompressed only. Max dimension: 8192×8192.

**Example — Screenshot:**
```c
void main() {
    // Capture the framebuffer as a BMP
    int *fb = 0;  // use gfx2d framebuffer
    // ... draw something first ...
    bmp_encode("/home/screen.bmp", fb, 640, 480);
    println("Screenshot saved!");
}
```

**Example — Load and display:**
```c
void main() {
    // Decode directly to screen
    int ret = bmp_decode_to_fb("/home/logo.bmp", 100, 100);
    if (ret < 0) {
        println("Failed to load BMP");
    }
}
```

---

## Example: Retro Window

```c
// Draw a Win95-style window with gradient title bar
void draw_window(int x, int y, int w, int h, char *title) {
    // Drop shadow
    gfx2d_shadow(x + 4, y + 4, w, h, 2, 0x80000000);

    // Window background
    gfx2d_rect_fill(x, y, w, h, 0x00D4D0C8);

    // Gradient title bar
    gfx2d_titlebar(x, y, w, 20, 0x00000080, 0x001084D0);

    // Title text
    gfx2d_text(x + 4, y + 4, title, 0x00FFFFFF, FONT_NORMAL);

    // Bevel border
    gfx2d_bevel(x, y, w, h, 1);
}
```

---

## Implementation Files

| File | Purpose |
|------|---------|
| `kernel/gfx2d.h` | Public API header (including file dialogs) |
| `kernel/gfx2d.c` | Library implementation (including file dialog UI + event loop) |
| `kernel/bmp.h` | BMP encoding/decoding API header |
| `kernel/bmp.c` | BMP format implementation |
| `kernel/vfs_helpers.h` | High-level VFS convenience functions |
| `kernel/vfs_helpers.c` | VFS helpers implementation |
| `cupidc.c` | Binding registration (~100 entries) |
| `cupidc_lex.c` | `unsigned`, `typedef`, `const` keywords |
| `cupidc_parse.c` | Type parsing for new keywords |

---

## Internal Design Notes

- **Alpha blend formula:** `out = (src * a + dst * (255 - a)) / 255` per channel, integer only
- **Gradient interpolation:** linear lerp per scanline, integer step
- **Shadow:** 1-4 pass box blur of a solid rect at offset
- **Plasma:** precomputed 256-entry sine LUT, two-frequency interference pattern
- **Sprite pool:** max 32 handles, stored in kernel heap
- **Surface pool:** max 8 handles, each consumes w*h*4 bytes heap
- **Particle systems:** max 4 systems × 64 particles each, 8.8 fixed-point positions
- **Clipping:** global clip rect checked in every pixel-write path
- **Font LARGE:** same 8×8 glyphs as FONT_NORMAL, scaled 2× at draw time
- **Blend modes:** applied in g2d_put() helper for all drawing operations
- **Tweening:** pure integer math, no floating point

