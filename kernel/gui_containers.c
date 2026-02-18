/**
 * gui_containers.c - Advanced Layout & Containers for cupid-os
 *
 * Tab controls, split panes, scroll areas, tree views,
 * group boxes, and collapsible panels.
 */

#include "gui_containers.h"
#include "gfx2d.h"
#include "graphics.h"
#include "font_8x8.h"
#include "string.h"
#include "../drivers/vga.h"

#define SPLITTER_W   4
#define TAB_PAD_H    4
#define TAB_PAD_W   12
#define TREE_INDENT 16
#define TREE_ITEM_H (FONT_H + 4)

static const uint32_t COL_TAB_ACTIVE   = 0x00FFFFFF;
static const uint32_t COL_TAB_INACTIVE = 0x00D8D8E0;
static const uint32_t COL_SPLITTER     = 0x00C0C0C8;
static const uint32_t COL_SPLITTER_ACT = 0x00A0B0D0;
static const uint32_t COL_TREE_SEL     = 0x00B8DDFF;

void gui_containers_init(void) {
    /* Nothing to initialize */
}

/* Tab Control */

int ui_draw_tabbar(ui_rect_t r, const char **tab_labels, int count,
                   ui_tabbar_state_t *state, int16_t mx, int16_t my,
                   bool clicked) {
    int i;
    int tab_x = (int)r.x;
    int new_active = -1;

    state->tab_count = count;
    state->hover_tab = -1;

    /* Draw tabs */
    for (i = 0; i < count; i++) {
        int tw = (int)strlen(tab_labels[i]) * FONT_W + TAB_PAD_W * 2;
        int th = (int)r.h;
        ui_rect_t tab_r = ui_rect((int16_t)tab_x, r.y,
                                  (uint16_t)tw, (uint16_t)th);
        bool active = (i == state->active_tab);
        bool hover = ui_contains(tab_r, mx, my);

        if (hover) state->hover_tab = i;

        /* Tab background */
        if (active) {
            gfx2d_rect_fill(tab_r.x, tab_r.y, tw, th, COL_TAB_ACTIVE);
            /* Remove bottom border for active tab */
            gfx2d_hline(tab_r.x + 1, (int)(tab_r.y + (int16_t)th - 1),
                        tw - 2, COL_TAB_ACTIVE);
        } else {
            gfx2d_rect_fill(tab_r.x, tab_r.y + 2, tw, th - 2,
                            hover ? 0x00E8E8F0 : COL_TAB_INACTIVE);
        }

        /* Tab border (top, left, right) */
        gfx2d_hline(tab_r.x, tab_r.y, tw, COLOR_BORDER);
        gfx2d_vline(tab_r.x, tab_r.y, th, COLOR_BORDER);
        gfx2d_vline(tab_r.x + tw - 1, tab_r.y, th, COLOR_BORDER);

        /* Label */
        {
            int16_t tx = (int16_t)(tab_r.x + TAB_PAD_W);
            int16_t ty = (int16_t)(tab_r.y + (th - FONT_H) / 2);
            gfx2d_text(tx, ty, tab_labels[i], COLOR_TEXT, GFX2D_FONT_NORMAL);
        }

        /* Handle click */
        if (clicked && hover && !active) {
            new_active = i;
            state->active_tab = i;
        }

        tab_x += tw;
    }

    /* Bottom border line for the bar */
    gfx2d_hline(r.x, (int)(r.y + (int16_t)r.h - 1),
                (int)r.w, COLOR_BORDER);

    return new_active;
}

ui_rect_t ui_tab_content_rect(ui_rect_t tabs_rect, int tab_height) {
    return ui_rect(tabs_rect.x,
                   (int16_t)(tabs_rect.y + (int16_t)tab_height),
                   tabs_rect.w,
                   (uint16_t)(tabs_rect.h - (uint16_t)tab_height));
}

void ui_tabs_init(ui_tabs_t *tabs, ui_rect_t r, int tab_height) {
    tabs->active_tab = 0;
    tabs->hover_tab = -1;
    tabs->tabs_rect = ui_rect(r.x, r.y, r.w, (uint16_t)tab_height);
    tabs->content_rect = ui_tab_content_rect(r, tab_height);
}

int ui_tabs_handle_input(ui_tabs_t *tabs, const char **labels, int count,
                         int16_t mx, int16_t my, bool clicked) {
    ui_tabbar_state_t state;
    int result;

    state.active_tab = tabs->active_tab;
    state.hover_tab = tabs->hover_tab;
    state.tab_count = count;

    result = ui_draw_tabbar(tabs->tabs_rect, labels, count,
                            &state, mx, my, clicked);

    tabs->active_tab = state.active_tab;
    tabs->hover_tab = state.hover_tab;

    /* Draw content border */
    gfx2d_rect(tabs->content_rect.x, tabs->content_rect.y,
               (int)tabs->content_rect.w, (int)tabs->content_rect.h,
               COLOR_BORDER);
    gfx2d_rect_fill(tabs->content_rect.x + 1, tabs->content_rect.y,
                    (int)tabs->content_rect.w - 2,
                    (int)tabs->content_rect.h - 1,
                    COL_TAB_ACTIVE);

    return result;
}

/* Split Panes */

void ui_split_h(ui_rect_t r, ui_split_state_t *state,
                ui_rect_t *left, ui_rect_t *right,
                int16_t mx, int16_t my, bool pressed) {
    int min_size = 30;
    int max_pos = (int)r.w - min_size - SPLITTER_W;
    ui_rect_t splitter_r;

    /* Clamp split position */
    if (state->split_pos < min_size) state->split_pos = min_size;
    if (state->split_pos > max_pos) state->split_pos = max_pos;

    /* Splitter hit rect */
    splitter_r = ui_rect(
        (int16_t)(r.x + (int16_t)state->split_pos),
        r.y, SPLITTER_W, r.h);

    /* Drag logic */
    if (pressed) {
        if (state->dragging) {
            state->split_pos = (int)(mx - r.x) - state->drag_offset;
            if (state->split_pos < min_size) state->split_pos = min_size;
            if (state->split_pos > max_pos) state->split_pos = max_pos;
        } else if (ui_contains(splitter_r, mx, my)) {
            state->dragging = true;
            state->drag_offset = (int)(mx - r.x) - state->split_pos;
        }
    } else {
        state->dragging = false;
    }

    /* Draw splitter */
    {
        bool hover = ui_contains(splitter_r, mx, my);
        ui_draw_splitter_h(r,
                           r.x + state->split_pos,
                           hover, state->dragging);
    }

    /* Output rects */
    *left = ui_rect(r.x, r.y,
                    (uint16_t)state->split_pos, r.h);
    *right = ui_rect(
        (int16_t)(r.x + (int16_t)state->split_pos + SPLITTER_W),
        r.y,
        (uint16_t)((int)r.w - state->split_pos - SPLITTER_W),
        r.h);
}

void ui_split_v(ui_rect_t r, ui_split_state_t *state,
                ui_rect_t *top, ui_rect_t *bottom,
                int16_t mx, int16_t my, bool pressed) {
    int min_size = 30;
    int max_pos = (int)r.h - min_size - SPLITTER_W;
    ui_rect_t splitter_r;

    if (state->split_pos < min_size) state->split_pos = min_size;
    if (state->split_pos > max_pos) state->split_pos = max_pos;

    splitter_r = ui_rect(r.x,
        (int16_t)(r.y + (int16_t)state->split_pos),
        r.w, SPLITTER_W);

    if (pressed) {
        if (state->dragging) {
            state->split_pos = (int)(my - r.y) - state->drag_offset;
            if (state->split_pos < min_size) state->split_pos = min_size;
            if (state->split_pos > max_pos) state->split_pos = max_pos;
        } else if (ui_contains(splitter_r, mx, my)) {
            state->dragging = true;
            state->drag_offset = (int)(my - r.y) - state->split_pos;
        }
    } else {
        state->dragging = false;
    }

    {
        bool hover = ui_contains(splitter_r, mx, my);
        ui_draw_splitter_v(r,
                           r.y + state->split_pos,
                           hover, state->dragging);
    }

    *top = ui_rect(r.x, r.y, r.w, (uint16_t)state->split_pos);
    *bottom = ui_rect(r.x,
        (int16_t)(r.y + (int16_t)state->split_pos + SPLITTER_W),
        r.w,
        (uint16_t)((int)r.h - state->split_pos - SPLITTER_W));
}

void ui_draw_splitter_h(ui_rect_t r, int x, bool hover, bool dragging) {
    uint32_t col = dragging ? COL_SPLITTER_ACT :
                   (hover ? 0x00B0B0C0 : COL_SPLITTER);
    (void)r;
    gfx2d_rect_fill(x, r.y, SPLITTER_W, (int)r.h, col);
    /* Grip dots */
    {
        int cy = r.y + (int)r.h / 2;
        int cx = x + SPLITTER_W / 2;
        gfx2d_pixel(cx, cy - 4, COLOR_BORDER);
        gfx2d_pixel(cx, cy,     COLOR_BORDER);
        gfx2d_pixel(cx, cy + 4, COLOR_BORDER);
    }
}

void ui_draw_splitter_v(ui_rect_t r, int y, bool hover, bool dragging) {
    uint32_t col = dragging ? COL_SPLITTER_ACT :
                   (hover ? 0x00B0B0C0 : COL_SPLITTER);
    (void)r;
    gfx2d_rect_fill(r.x, y, (int)r.w, SPLITTER_W, col);
    /* Grip dots */
    {
        int cx = r.x + (int)r.w / 2;
        int cy = y + SPLITTER_W / 2;
        gfx2d_pixel(cx - 4, cy, COLOR_BORDER);
        gfx2d_pixel(cx,     cy, COLOR_BORDER);
        gfx2d_pixel(cx + 4, cy, COLOR_BORDER);
    }
}

/* Scroll Area */

void ui_scroll_init(ui_scroll_state_t *state, int content_w, int content_h,
                    int viewport_w, int viewport_h) {
    state->scroll_x = 0;
    state->scroll_y = 0;
    state->content_w = content_w;
    state->content_h = content_h;
    state->viewport_w = viewport_w;
    state->viewport_h = viewport_h;
    state->dragging = false;
    state->drag_start_x = 0;
    state->drag_start_y = 0;
}

void ui_scroll_handle_input(ui_scroll_state_t *state, ui_rect_t r,
                            int16_t mx, int16_t my, bool pressed,
                            int wheel_delta) {
    int max_x, max_y;

    /* Mouse wheel scrolling */
    if (wheel_delta != 0 && ui_contains(r, mx, my)) {
        state->scroll_y += wheel_delta * 20;
    }

    /* Drag scrolling not implemented for simplicity in bare-metal.
       Could be expanded if needed. */
    (void)pressed;
    (void)mx;
    (void)my;

    /* Clamp scroll position */
    max_x = state->content_w - state->viewport_w;
    max_y = state->content_h - state->viewport_h;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (state->scroll_x < 0) state->scroll_x = 0;
    if (state->scroll_x > max_x) state->scroll_x = max_x;
    if (state->scroll_y < 0) state->scroll_y = 0;
    if (state->scroll_y > max_y) state->scroll_y = max_y;
}

ui_rect_t ui_scroll_draw(ui_rect_t r, ui_scroll_state_t *state,
                         int16_t mx, int16_t my) {
    int sb_w = 12;
    bool need_vscroll = (state->content_h > state->viewport_h);
    bool need_hscroll = (state->content_w > state->viewport_w);
    ui_rect_t viewport;

    (void)mx;
    (void)my;

    /* Compute viewport */
    viewport = r;
    if (need_vscroll) viewport.w = (uint16_t)(viewport.w - (uint16_t)sb_w);
    if (need_hscroll) viewport.h = (uint16_t)(viewport.h - (uint16_t)sb_w);

    /* Draw vertical scrollbar */
    if (need_vscroll) {
        ui_rect_t sb = ui_rect(
            (int16_t)(r.x + (int16_t)r.w - (int16_t)sb_w),
            r.y, (uint16_t)sb_w,
            need_hscroll ? (uint16_t)(r.h - (uint16_t)sb_w) : r.h);
        int total_lines = state->content_h / (FONT_H + 2);
        int vis_lines = state->viewport_h / (FONT_H + 2);
        int off_lines = state->scroll_y / (FONT_H + 2);
        if (total_lines < 1) total_lines = 1;
        if (vis_lines < 1) vis_lines = 1;
        ui_draw_vscrollbar(sb, total_lines, vis_lines, off_lines);
    }

    /* Draw horizontal scrollbar (simplified: just a track) */
    if (need_hscroll) {
        ui_rect_t sb = ui_rect(
            r.x,
            (int16_t)(r.y + (int16_t)r.h - (int16_t)sb_w),
            need_vscroll ? (uint16_t)(r.w - (uint16_t)sb_w) : r.w,
            (uint16_t)sb_w);
        gfx2d_rect_fill(sb.x, sb.y, (int)sb.w, sb_w, COLOR_BORDER);
        /* Thumb */
        if (state->content_w > 0) {
            int thumb_w = ((int)sb.w * state->viewport_w) / state->content_w;
            if (thumb_w < 16) thumb_w = 16;
            if (thumb_w > (int)sb.w) thumb_w = (int)sb.w;
            int thumb_x = 0;
            int max_scroll = state->content_w - state->viewport_w;
            if (max_scroll > 0)
                thumb_x = (state->scroll_x * ((int)sb.w - thumb_w)) / max_scroll;
            ui_rect_t thumb = ui_rect(
                (int16_t)(sb.x + (int16_t)thumb_x),
                (int16_t)(sb.y + 1),
                (uint16_t)thumb_w,
                (uint16_t)(sb_w - 2));
            ui_draw_panel(thumb, COLOR_WINDOW_BG, true, true);
        }
    }

    return viewport;
}

void ui_scroll_begin_content(ui_scroll_state_t *state, ui_rect_t viewport) {
    gfx2d_clip_set(viewport.x, viewport.y,
                   (int)viewport.w, (int)viewport.h);
    /* Caller applies -scroll_x, -scroll_y offset to content drawing */
    (void)state;
}

void ui_scroll_end_content(void) {
    gfx2d_clip_clear();
}

/* Tree View */

static int tree_flatten_recursive(ui_tree_node_t *node,
                                  ui_tree_node_t **out, int max,
                                  int idx) {
    int i;
    if (idx >= max) return idx;
    out[idx++] = node;
    if (node->expanded && node->children) {
        for (i = 0; i < node->child_count && idx < max; i++) {
            idx = tree_flatten_recursive(node->children[i], out, max, idx);
        }
    }
    return idx;
}

int ui_tree_flatten(ui_tree_node_t *root, ui_tree_node_t **out, int max) {
    if (!root) return 0;
    return tree_flatten_recursive(root, out, max, 0);
}

ui_tree_node_t *ui_draw_treeview(ui_rect_t r, ui_tree_node_t *root,
                                  ui_tree_state_t *state, int16_t mx,
                                  int16_t my, bool clicked) {
    ui_tree_node_t *flat[128];
    int count, i;
    int visible;
    ui_tree_node_t *clicked_node = NULL;

    if (!root) return NULL;

    count = ui_tree_flatten(root, flat, 128);
    visible = (int)r.h / TREE_ITEM_H;

    /* Background */
    ui_draw_panel(r, 0x00FFFFFF, true, false);

    /* Clamp scroll */
    if (state->scroll_offset < 0) state->scroll_offset = 0;
    if (state->scroll_offset > count - visible)
        state->scroll_offset = count - visible;
    if (state->scroll_offset < 0) state->scroll_offset = 0;

    state->hover_node = -1;

    for (i = 0; i < visible && (i + state->scroll_offset) < count; i++) {
        int idx = i + state->scroll_offset;
        ui_tree_node_t *node = flat[idx];
        int16_t iy = (int16_t)(r.y + (int16_t)(i * TREE_ITEM_H));
        int indent = node->depth * TREE_INDENT;
        ui_rect_t ir = ui_rect((int16_t)(r.x + (int16_t)indent + 16),
                               iy,
                               (uint16_t)((int)r.w - indent - 16),
                               TREE_ITEM_H);

        /* Hover detection */
        ui_rect_t full_ir = ui_rect(r.x, iy, r.w, TREE_ITEM_H);
        bool hover = ui_contains(full_ir, mx, my);
        if (hover) state->hover_node = idx;

        /* Selection highlight */
        if (node->selected || node == state->selected_node) {
            gfx2d_rect_fill(r.x + 1, iy, (int)r.w - 2, TREE_ITEM_H,
                            COL_TREE_SEL);
        } else if (hover) {
            gfx2d_rect_fill(r.x + 1, iy, (int)r.w - 2, TREE_ITEM_H,
                            COLOR_HIGHLIGHT);
        }

        /* Expand/collapse arrow */
        if (node->child_count > 0) {
            int16_t ax = (int16_t)(r.x + (int16_t)indent + 4);
            int16_t ay = (int16_t)(iy + TREE_ITEM_H / 2);
            if (node->expanded) {
                /* Down arrow */
                gfx2d_line(ax, ay - 2, ax + 4, ay + 2, COLOR_TEXT);
                gfx2d_line(ax + 4, ay + 2, ax + 8, ay - 2, COLOR_TEXT);
            } else {
                /* Right arrow */
                gfx2d_line(ax, ay - 4, ax + 4, ay, COLOR_TEXT);
                gfx2d_line(ax + 4, ay, ax, ay + 4, COLOR_TEXT);
            }
        }

        /* Label */
        ui_draw_label(ir, node->label, COLOR_TEXT, UI_ALIGN_LEFT);

        /* Handle click */
        if (clicked && hover) {
            /* Check if click is on the expand arrow */
            if (node->child_count > 0 &&
                mx < (int16_t)(r.x + (int16_t)indent + 16)) {
                node->expanded = !node->expanded;
            } else {
                state->selected_node = node;
                clicked_node = node;
            }
        }
    }

    return clicked_node;
}

/* Group Box & Containers */

ui_rect_t ui_draw_groupbox(ui_rect_t r, const char *title) {
    int title_w = (int)strlen(title) * FONT_W + 8;
    int16_t frame_y = (int16_t)(r.y + FONT_H / 2);
    ui_rect_t content;

    /* Frame lines (skip where title is) */
    /* Top left */
    gfx2d_hline(r.x, frame_y, 6, COLOR_BORDER);
    /* Top right (after title) */
    gfx2d_hline(r.x + 6 + title_w, frame_y,
                (int)r.w - 6 - title_w, COLOR_BORDER);
    /* Left side */
    gfx2d_vline(r.x, frame_y, (int)r.h - FONT_H / 2, COLOR_BORDER);
    /* Right side */
    gfx2d_vline(r.x + (int16_t)r.w - 1, frame_y,
                (int)r.h - FONT_H / 2, COLOR_BORDER);
    /* Bottom */
    gfx2d_hline(r.x, r.y + (int16_t)r.h - 1, (int)r.w, COLOR_BORDER);

    /* Title text */
    gfx2d_text(r.x + 8, r.y, title, COLOR_TEXT, GFX2D_FONT_NORMAL);

    /* Content rect */
    content = ui_rect((int16_t)(r.x + 4),
                      (int16_t)(frame_y + 4),
                      (uint16_t)(r.w - 8),
                      (uint16_t)((int)r.h - FONT_H / 2 - 8));
    return content;
}

ui_rect_t ui_groupbox_content(ui_rect_t r, const char *title) {
    (void)title;
    return ui_rect((int16_t)(r.x + 4),
                   (int16_t)(r.y + FONT_H / 2 + 4),
                   (uint16_t)(r.w - 8),
                   (uint16_t)((int)r.h - FONT_H / 2 - 8));
}

ui_rect_t ui_draw_container(ui_rect_t r, bool border) {
    gfx2d_rect_fill(r.x, r.y, (int)r.w, (int)r.h, COLOR_WINDOW_BG);
    if (border) {
        gfx2d_rect(r.x, r.y, (int)r.w, (int)r.h, COLOR_BORDER);
    }
    return ui_pad(r, 2);
}

bool ui_draw_collapsible(ui_rect_t r, const char *title,
                         ui_collapsible_state_t *state, int16_t mx,
                         int16_t my, bool clicked) {
    int header_h = FONT_H + 8;
    ui_rect_t header_r = ui_rect(r.x, r.y, r.w, (uint16_t)header_h);
    bool hit = false;

    state->hover = ui_contains(header_r, mx, my);

    /* Header background */
    gfx2d_rect_fill(header_r.x, header_r.y, (int)header_r.w, header_h,
                    state->hover ? 0x00E0E8F0 : 0x00D8D8E0);
    gfx2d_rect(header_r.x, header_r.y, (int)header_r.w, header_h,
               COLOR_BORDER);

    /* Arrow */
    {
        int16_t ax = (int16_t)(header_r.x + 6);
        int16_t ay = (int16_t)(header_r.y + header_h / 2);
        if (state->collapsed) {
            /* Right arrow */
            gfx2d_line(ax, ay - 3, ax + 4, ay, COLOR_TEXT);
            gfx2d_line(ax + 4, ay, ax, ay + 3, COLOR_TEXT);
        } else {
            /* Down arrow */
            gfx2d_line(ax - 1, ay - 2, ax + 3, ay + 2, COLOR_TEXT);
            gfx2d_line(ax + 3, ay + 2, ax + 7, ay - 2, COLOR_TEXT);
        }
    }

    /* Title */
    gfx2d_text((int)(header_r.x + 16),
               (int)(header_r.y + (header_h - FONT_H) / 2),
               title, COLOR_TEXT, GFX2D_FONT_NORMAL);

    /* Handle click */
    if (clicked && state->hover) {
        state->collapsed = !state->collapsed;
        hit = true;
    }

    /* Draw content area if not collapsed */
    if (!state->collapsed) {
        ui_rect_t content = ui_rect(r.x,
            (int16_t)(r.y + (int16_t)header_h),
            r.w,
            (uint16_t)((int)r.h - header_h));
        gfx2d_rect_fill(content.x, content.y,
                         (int)content.w, (int)content.h, COLOR_WINDOW_BG);
        gfx2d_rect(content.x, content.y,
                   (int)content.w, (int)content.h, COLOR_BORDER);
    }

    return hit;
}
