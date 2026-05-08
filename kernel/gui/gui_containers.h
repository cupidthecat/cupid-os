/**
 * gui_containers.h - Advanced Layout & Containers for cupid-os
 *
 * Tab controls, split panes, scroll areas, tree views,
 * group boxes, and collapsible panels.
 */

#ifndef GUI_CONTAINERS_H
#define GUI_CONTAINERS_H

#include "types.h"
#include "ui.h"

typedef struct {
    int active_tab;
    int hover_tab;
    int tab_count;
} ui_tabbar_state_t;

typedef struct {
    int       active_tab;
    int       hover_tab;
    ui_rect_t tabs_rect;
    ui_rect_t content_rect;
} ui_tabs_t;

typedef struct {
    int  split_pos;     /* Divider position (pixels from left/top)  */
    bool dragging;
    int  drag_offset;
} ui_split_state_t;

typedef struct {
    int    scroll_x, scroll_y;       /* Current scroll position    */
    int    content_w, content_h;     /* Total content size          */
    int    viewport_w, viewport_h;   /* Visible area size           */
    bool   dragging;
    int16_t drag_start_x, drag_start_y;
} ui_scroll_state_t;

typedef struct ui_tree_node {
    const char          *label;
    bool                 expanded;
    bool                 selected;
    int                  depth;
    int                  child_count;
    struct ui_tree_node **children;
    void                *user_data;
} ui_tree_node_t;

typedef struct {
    int             scroll_offset;
    int             hover_node;
    ui_tree_node_t *selected_node;
} ui_tree_state_t;

typedef struct {
    bool collapsed;
    bool hover;
} ui_collapsible_state_t;

/* Tab Control */

/** Draw tab bar. Returns new active tab index, or -1 if unchanged. */
int ui_draw_tabbar(ui_rect_t r, const char **tab_labels, int count,
                   ui_tabbar_state_t *state, int16_t mx, int16_t my,
                   bool clicked);

/** Get content rect below a tab bar. */
ui_rect_t ui_tab_content_rect(ui_rect_t tabs_rect, int tab_height);

/** Initialize a complete tab control. */
void ui_tabs_init(ui_tabs_t *tabs, ui_rect_t r, int tab_height);

/** Handle input for the tab control. Returns new active tab or -1. */
int ui_tabs_handle_input(ui_tabs_t *tabs, const char **labels, int count,
                         int16_t mx, int16_t my, bool clicked);

/* Split Panes */

/** Horizontal split (left | right). Outputs two rects. */
void ui_split_h(ui_rect_t r, ui_split_state_t *state,
                ui_rect_t *left, ui_rect_t *right,
                int16_t mx, int16_t my, bool pressed);

/** Vertical split (top / bottom). Outputs two rects. */
void ui_split_v(ui_rect_t r, ui_split_state_t *state,
                ui_rect_t *top, ui_rect_t *bottom,
                int16_t mx, int16_t my, bool pressed);

/** Draw horizontal splitter handle. */
void ui_draw_splitter_h(ui_rect_t r, int x, bool hover, bool dragging);

/** Draw vertical splitter handle. */
void ui_draw_splitter_v(ui_rect_t r, int y, bool hover, bool dragging);

/* Scroll Area */

/** Initialize scroll area state. */
void ui_scroll_init(ui_scroll_state_t *state, int content_w, int content_h,
                    int viewport_w, int viewport_h);

/** Handle scroll input (wheel, click-drag). */
void ui_scroll_handle_input(ui_scroll_state_t *state, ui_rect_t r,
                            int16_t mx, int16_t my, bool pressed,
                            int wheel_delta);

/** Draw scrollbars and return visible content rect. */
ui_rect_t ui_scroll_draw(ui_rect_t r, ui_scroll_state_t *state,
                         int16_t mx, int16_t my);

/** Set clipping to the scrolled content area. */
void ui_scroll_begin_content(ui_scroll_state_t *state, ui_rect_t viewport);

/** Restore clipping after scrolled content. */
void ui_scroll_end_content(void);

/* Tree View */

/** Draw tree view. Returns clicked node or NULL. */
ui_tree_node_t *ui_draw_treeview(ui_rect_t r, ui_tree_node_t *root,
                                  ui_tree_state_t *state, int16_t mx,
                                  int16_t my, bool clicked);

/** Flatten tree to visible (expanded) nodes. Returns count written. */
int ui_tree_flatten(ui_tree_node_t *root, ui_tree_node_t **out, int max);

/* Group Box & Containers */

/** Draw a labeled group box frame. Returns content rect inside. */
ui_rect_t ui_draw_groupbox(ui_rect_t r, const char *title);

/** Get content rect of a group box (without drawing). */
ui_rect_t ui_groupbox_content(ui_rect_t r, const char *title);

/** Draw a simple bordered container. Returns content rect. */
ui_rect_t ui_draw_container(ui_rect_t r, bool border);

/** Draw a collapsible panel. Returns true if the header was clicked
 *  (caller toggles collapsed state). */
bool ui_draw_collapsible(ui_rect_t r, const char *title,
                         ui_collapsible_state_t *state, int16_t mx,
                         int16_t my, bool clicked);

void gui_containers_init(void);

#endif /* GUI_CONTAINERS_H */
