//icon: "My Computer"
//icon_desc: "File Manager"
//icon_x: 10
//icon_y: 130
//icon_type: folder
//icon_color: 0xFFFF00

/* fm.cc - CupidFM (rewritten)
 *
 * Clean windowed File Manager using the same GUI window loop pattern as
 * Notepad/Terminal:
 *   gui_win_create -> gui_win_is_open -> gui_win_can_draw ->
 *   gui_win_draw_frame -> gui_win_flip
 */

int FM_MAX_FILES     = 256;
int FM_MAX_PATH      = 128;
int FM_ITEM_H        = 16;
int FM_TOOLBAR_H     = 28;
int FM_ADDR_H        = 20;
int FM_STATUSBAR_H   = 18;
int FM_SCROLLBAR_W   = 14;

int FM_COL_ICON_W    = 24;
int FM_COL_NAME_W    = 300;
int FM_COL_SIZE_W    = 80;
int FM_COL_TYPE_W    = 90;

int COL_BG           = 0x00FFF0F5;
int COL_BLACK        = 0x00000000;
int COL_WHITE        = 0x00F8F8F8;
int COL_TEXT         = 0x00282830;
int COL_BORDER       = 0x009898A0;
int COL_SELECT       = 0x00C0D8FF;
int COL_HOVER        = 0x00D8E8FF;
int COL_TOOLBAR      = 0x00E8D8F8;
int COL_LIST_BG      = 0x00FFFFFF;
int COL_HEADER       = 0x00D0D0D8;
int COL_ICON_DIR     = 0x00EECC44;
int COL_ICON_FIL     = 0x00AAAACC;

int VFS_TYPE_DIR     = 1;

struct fm_entry {
  char name[64];
  int size;
  int is_dir;
  int selected;
};

int win = -1;
int should_close = 0;
int frame_count = 0;

char cwd[128];
struct fm_entry files[256];
int file_count = 0;
int scroll_off = 0;
int cursor_idx = 0;

/* Clipboard */
char clip_paths[8][128];
int clip_is_dir[8];
int clip_count = 0;
int clip_cut = 0;

/* Cached content rect each frame */
int view_x = 0;
int view_y = 0;
int view_w = 0;
int view_h = 0;

/* Layout cache each frame */
int list_x = 0;
int list_y = 0;
int list_w = 0;
int list_h = 0;

int dbl_click_time = 0;
int dbl_click_idx = -1;

int fm_strlen(char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}

void fm_strcpy(char *dst, char *src) {
  int i = 0;
  while (src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

void fm_copy_name64(char *dst, char *src) {
  int i = 0;
  while (i < 63 && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

void fm_strcat(char *dst, char *src) {
  int d = fm_strlen(dst);
  int i = 0;
  while (src[i]) {
    dst[d + i] = src[i];
    i++;
  }
  dst[d + i] = 0;
}

int fm_strcmp(char *a, char *b) {
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}

char fm_tolower(char c) {
  if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
  return c;
}

int fm_ends_with_txt(char *name) {
  int n = fm_strlen(name);
  if (n < 4) return 0;
  return fm_tolower(name[n - 4]) == '.' &&
         fm_tolower(name[n - 3]) == 't' &&
         fm_tolower(name[n - 2]) == 'x' &&
         fm_tolower(name[n - 1]) == 't';
}

int fm_ends_with_ctxt(char *name) {
  int n = fm_strlen(name);
  if (n < 5) return 0;
  return fm_tolower(name[n - 5]) == '.' &&
         fm_tolower(name[n - 4]) == 'c' &&
         fm_tolower(name[n - 3]) == 't' &&
         fm_tolower(name[n - 2]) == 'x' &&
         fm_tolower(name[n - 1]) == 't';
}

int fm_ends_with_cc(char *name) {
  int n = fm_strlen(name);
  if (n < 3) return 0;
  return fm_tolower(name[n - 3]) == '.' &&
         fm_tolower(name[n - 2]) == 'c' &&
         fm_tolower(name[n - 1]) == 'c';
}

void fm_itoa(int val, char *buf) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = 0;
    return;
  }

  char tmp[16];
  int i = 0;
  int neg = 0;
  if (val < 0) {
    neg = 1;
    val = -val;
  }

  while (val > 0 && i < 15) {
    tmp[i] = '0' + (val % 10);
    val = val / 10;
    i++;
  }

  int p = 0;
  if (neg) {
    buf[p] = '-';
    p++;
  }
  while (i > 0) {
    i--;
    buf[p] = tmp[i];
    p++;
  }
  buf[p] = 0;
}

void fm_size_str(int size, char *buf) {
  if (size < 1024) {
    fm_itoa(size, buf);
    fm_strcat(buf, " B");
  } else if (size < 1024 * 1024) {
    fm_itoa(size / 1024, buf);
    fm_strcat(buf, " KB");
  } else {
    fm_itoa(size / (1024 * 1024), buf);
    fm_strcat(buf, " MB");
  }
}

void fm_build_path(char *out, char *dir, char *name) {
  fm_strcpy(out, dir);
  int len = fm_strlen(out);
  if (len > 0 && out[len - 1] != '/') {
    out[len] = '/';
    out[len + 1] = 0;
  }
  fm_strcat(out, name);
}

int fm_delete_path_recursive(char *path, int is_dir) {
  if (!path || !path[0]) return -1;
  if (path[0] == '/' && path[1] == 0) return -1;

  if (!is_dir) {
    return vfs_unlink(path);
  }

  int fd = vfs_open(path, 0);
  if (fd >= 0) {
    char ent[72];
    while (1) {
      int r = vfs_readdir(fd, ent);
      if (r <= 0) break;

      char *dname = ent;
      if (dname[0] == '.' && dname[1] == 0) continue;
      if (dname[0] == '.' && dname[1] == '.' && dname[2] == 0) continue;

      char child[128];
      fm_build_path(child, path, dname);
      int child_is_dir = (ent[68] == VFS_TYPE_DIR) ? 1 : 0;
      fm_delete_path_recursive(child, child_is_dir);
    }
    vfs_close(fd);
  }

  return vfs_unlink(path);
}

void fm_sort_files() {
  int i = 0;
  while (i < file_count - 1) {
    int j = i + 1;
    while (j < file_count) {
      int swap = 0;
      if (files[j].is_dir && !files[i].is_dir) swap = 1;
      if (files[i].is_dir == files[j].is_dir) {
        if (fm_strcmp(files[i].name, files[j].name) > 0) swap = 1;
      }
      if (swap) {
        struct fm_entry tmp;
        memcpy(&tmp, &files[i], 76);
        memcpy(&files[i], &files[j], 76);
        memcpy(&files[j], &tmp, 76);
      }
      j++;
    }
    i++;
  }
}

int fm_try_set_start_dir(char *path) {
  int fd = vfs_open(path, 0);
  if (fd < 0) return 0;
  vfs_close(fd);
  fm_strcpy(cwd, path);
  return 1;
}

void fm_refresh() {
  file_count = 0;
  scroll_off = 0;
  cursor_idx = 0;

  int fd = vfs_open(cwd, 0);
  if (fd < 0) return;

  char ent[72];
  while (1) {
    int r = vfs_readdir(fd, ent);
    if (r <= 0) break;

    char *dname = ent;
    if (dname[0] == '.' && dname[1] == 0) continue;

    if (file_count >= FM_MAX_FILES) break;

    fm_copy_name64(files[file_count].name, dname);
    {
      int b0 = ((int)ent[64]) & 255;
      int b1 = ((int)ent[65]) & 255;
      int b2 = ((int)ent[66]) & 255;
      int b3 = ((int)ent[67]) & 255;
      files[file_count].size = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    }
    files[file_count].is_dir = ((((int)ent[68]) & 255) == VFS_TYPE_DIR) ? 1 : 0;
    files[file_count].selected = 0;
    file_count++;
  }

  vfs_close(fd);
  fm_sort_files();
}

void fm_navigate(char *path) {
  int fd = vfs_open(path, 0);
  if (fd < 0) {
    message_dialog("Cannot open directory");
    return;
  }
  vfs_close(fd);
  fm_strcpy(cwd, path);
  fm_refresh();
}

void fm_go_up() {
  if (cwd[0] == '/' && cwd[1] == 0) return;

  int last = -1;
  int i = 0;
  while (cwd[i]) {
    if (cwd[i] == '/') last = i;
    i++;
  }

  if (last <= 0) {
    cwd[0] = '/';
    cwd[1] = 0;
  } else {
    cwd[last] = 0;
  }

  fm_refresh();
}

void fm_select_all() {
  int i = 0;
  while (i < file_count) {
    files[i].selected = 1;
    i++;
  }
}

void fm_deselect_all() {
  int i = 0;
  while (i < file_count) {
    files[i].selected = 0;
    i++;
  }
}

int fm_count_selected() {
  int c = 0;
  int i = 0;
  while (i < file_count) {
    if (files[i].selected) c++;
    i++;
  }
  return c;
}

void fm_open_selected() {
  if (cursor_idx < 0 || cursor_idx >= file_count) return;

  if (fm_strcmp(files[cursor_idx].name, "..") == 0) {
    fm_go_up();
    return;
  }

  char path[128];
  fm_build_path(path, cwd, files[cursor_idx].name);

  if (files[cursor_idx].is_dir) {
    fm_navigate(path);
  } else {
    if (fm_ends_with_txt(files[cursor_idx].name) ||
        fm_ends_with_ctxt(files[cursor_idx].name) ||
        fm_ends_with_cc(files[cursor_idx].name)) {
      notepad_open_file(path);
      return;
    }

    if (exec(path, files[cursor_idx].name) < 0) {
      notepad_open_file(path);
    }
  }
}

void fm_copy() {
  clip_count = 0;
  clip_cut = 0;

  int i = 0;
  while (i < file_count && clip_count < 8) {
    if (files[i].selected) {
      fm_build_path(clip_paths[clip_count], cwd, files[i].name);
      clip_is_dir[clip_count] = files[i].is_dir;
      clip_count++;
    }
    i++;
  }

  if (clip_count == 0 && cursor_idx >= 0 && cursor_idx < file_count) {
    fm_build_path(clip_paths[0], cwd, files[cursor_idx].name);
    clip_is_dir[0] = files[cursor_idx].is_dir;
    clip_count = 1;
  }
}

void fm_cut() {
  fm_copy();
  clip_cut = 1;
}

void fm_paste() {
  if (clip_count <= 0) return;

  int i = 0;
  while (i < clip_count) {
    char *src = clip_paths[i];
    char *name = src;
    char *p = src;
    while (*p) {
      if (*p == '/') name = p + 1;
      p++;
    }

    char dst[128];
    fm_build_path(dst, cwd, name);

    if (strcmp(src, dst) == 0) {
      i++;
      continue;
    }

    if (clip_is_dir[i]) {
      vfs_mkdir(dst);
      if (clip_cut) fm_delete_path_recursive(src, 1);
    } else {
      if (clip_cut) vfs_rename(src, dst);
      else vfs_copy_file(src, dst);
    }

    i++;
  }

  if (clip_cut) {
    clip_count = 0;
    clip_cut = 0;
  }

  fm_refresh();
}

void fm_delete_selected() {
  int count = fm_count_selected();
  if (count == 0 && cursor_idx >= 0 && cursor_idx < file_count) {
    files[cursor_idx].selected = 1;
    count = 1;
  }
  if (count == 0) return;

  char msg[64];
  fm_strcpy(msg, "Delete ");
  char num[8];
  fm_itoa(count, num);
  fm_strcat(msg, num);
  fm_strcat(msg, " item(s)?");

  if (!confirm_dialog(msg)) return;

  int i = 0;
  while (i < file_count) {
    if (files[i].selected) {
      if (fm_strcmp(files[i].name, "..") != 0) {
        char path[128];
        fm_build_path(path, cwd, files[i].name);
        fm_delete_path_recursive(path, files[i].is_dir);
      }
    }
    i++;
  }

  fm_refresh();
}

void fm_rename_item() {
  if (cursor_idx < 0 || cursor_idx >= file_count) return;

  char new_name[64];
  char prompt[96];
  fm_strcpy(prompt, "Rename: ");
  fm_strcat(prompt, files[cursor_idx].name);

  if (input_dialog(prompt, new_name, 64)) {
    char old_path[128];
    char new_path[128];
    fm_build_path(old_path, cwd, files[cursor_idx].name);
    fm_build_path(new_path, cwd, new_name);
    vfs_rename(old_path, new_path);
    fm_refresh();
  }
}

void fm_new_folder() {
  char name[64];
  if (input_dialog("New folder name:", name, 64)) {
    char path[128];
    fm_build_path(path, cwd, name);
    vfs_mkdir(path);
    fm_refresh();
  }
}

void draw_folder_icon(int ox, int oy, int x, int y) {
  gfx2d_rect_fill(ox + x, oy + y, 7, 3, COL_ICON_DIR);
  gfx2d_rect_fill(ox + x, oy + y + 3, 14, 9, COL_ICON_DIR);
  gfx2d_rect(ox + x, oy + y + 3, 14, 9, COL_TEXT);
  gfx2d_hline(ox + x, oy + y, 7, COL_TEXT);
  gfx2d_vline(ox + x, oy + y, 3, COL_TEXT);
  gfx2d_vline(ox + x + 7, oy + y, 3, COL_TEXT);
}

void draw_file_icon(int ox, int oy, int x, int y, int bg_col) {
  gfx2d_rect_fill(ox + x + 1, oy + y, 10, 13, bg_col);
  gfx2d_rect(ox + x + 1, oy + y, 10, 13, COL_TEXT);
  gfx2d_line(ox + x + 8, oy + y, ox + x + 11, oy + y + 3, COL_TEXT);
  gfx2d_hline(ox + x + 3, oy + y + 4, 5, COL_ICON_FIL);
  gfx2d_hline(ox + x + 3, oy + y + 6, 6, COL_ICON_FIL);
  gfx2d_hline(ox + x + 3, oy + y + 8, 4, COL_ICON_FIL);
}

int draw_button(int ox, int oy, int x, int y, int w,
                char *label, int mx, int my, int click) {
  int hover = (mx >= x && mx < x + w && my >= y && my < y + FM_TOOLBAR_H - 4);
  int color = hover ? COL_HOVER : COL_TOOLBAR;
  gfx2d_rect_fill(ox + x, oy + y, w, FM_TOOLBAR_H - 4, color);
  if (hover) gfx2d_rect(ox + x, oy + y, w, FM_TOOLBAR_H - 4, COL_BORDER);
  gfx2d_text(ox + x + 4, oy + y + 5, label, COL_BLACK, 1);
  return hover && click;
}

void fm_render(int ox, int oy, int cw, int ch,
               int mx, int my, int click) {
  gfx2d_rect_fill(ox, oy, cw, ch, COL_BG);

  int ty = 0;
  gfx2d_rect_fill(ox, oy + ty, cw, FM_TOOLBAR_H, COL_TOOLBAR);
  gfx2d_hline(ox, oy + ty + FM_TOOLBAR_H - 1, cw, COL_BORDER);

  int tx = 4;
  if (draw_button(ox, oy, tx, ty + 2, 26, "<-", mx, my, click)) fm_go_up();
  tx = tx + 30;
  if (draw_button(ox, oy, tx, ty + 2, 50, "New", mx, my, click)) fm_new_folder();
  tx = tx + 54;
  if (draw_button(ox, oy, tx, ty + 2, 50, "Copy", mx, my, click)) fm_copy();
  tx = tx + 54;
  if (draw_button(ox, oy, tx, ty + 2, 46, "Cut", mx, my, click)) fm_cut();
  tx = tx + 50;
  if (draw_button(ox, oy, tx, ty + 2, 54, "Paste", mx, my, click)) fm_paste();
  tx = tx + 58;
  if (draw_button(ox, oy, tx, ty + 2, 46, "Del", mx, my, click)) fm_delete_selected();
  tx = tx + 50;
  if (draw_button(ox, oy, tx, ty + 2, 60, "Rename", mx, my, click)) fm_rename_item();
  tx = tx + 64;
  if (draw_button(ox, oy, tx, ty + 2, 62, "Refresh", mx, my, click)) fm_refresh();

  int ay = ty + FM_TOOLBAR_H;
  gfx2d_rect_fill(ox, oy + ay, cw, FM_ADDR_H, COL_WHITE);
  gfx2d_rect(ox, oy + ay, cw, FM_ADDR_H, COL_BORDER);
  gfx2d_text(ox + 6, oy + ay + 4, cwd, COL_BLACK, 1);

  list_y = ay + FM_ADDR_H;
  list_x = 0;
  list_w = cw - FM_SCROLLBAR_W;
  list_h = ch - list_y - FM_STATUSBAR_H;

  int hy = list_y;
  gfx2d_rect_fill(ox + list_x, oy + hy, list_w, FM_ITEM_H, COL_HEADER);
  gfx2d_hline(ox + list_x, oy + hy + FM_ITEM_H - 1, list_w, COL_BORDER);
  gfx2d_text(ox + list_x + FM_COL_ICON_W + 2, oy + hy + 2, "Name", COL_BLACK, 1);
  gfx2d_text(ox + list_x + FM_COL_ICON_W + FM_COL_NAME_W + 4, oy + hy + 2,
             "Size", COL_BLACK, 1);
  gfx2d_text(ox + list_x + FM_COL_ICON_W + FM_COL_NAME_W + FM_COL_SIZE_W + 4,
             oy + hy + 2, "Type", COL_BLACK, 1);

  int items_y = list_y + FM_ITEM_H;
  int items_h = list_h - FM_ITEM_H;
  int items_visible = items_h / FM_ITEM_H;
  if (items_visible < 1) items_visible = 1;

  gfx2d_rect_fill(ox + list_x, oy + items_y, list_w, items_h, COL_LIST_BG);

  int row = 0;
  while (row < items_visible && (scroll_off + row) < file_count) {
    int idx = scroll_off + row;
    int iy = items_y + row * FM_ITEM_H;
    int row_bg = COL_LIST_BG;

    if (files[idx].selected) {
      gfx2d_rect_fill(ox + list_x, oy + iy, list_w, FM_ITEM_H, COL_SELECT);
      row_bg = COL_SELECT;
    } else if (idx == cursor_idx) {
      gfx2d_rect_fill(ox + list_x, oy + iy, list_w, FM_ITEM_H, COL_HOVER);
      row_bg = COL_HOVER;
    }

    if (files[idx].is_dir) draw_folder_icon(ox, oy, list_x + 4, iy + 1);
    else draw_file_icon(ox, oy, list_x + 4, iy + 1, row_bg);

    gfx2d_text(ox + list_x + FM_COL_ICON_W + 2, oy + iy + 2,
               files[idx].name, COL_BLACK, 1);

    if (!files[idx].is_dir) {
      char sbuf[16];
      fm_size_str(files[idx].size, sbuf);
      gfx2d_text(ox + list_x + FM_COL_ICON_W + FM_COL_NAME_W + 4,
                 oy + iy + 2, sbuf, COL_TEXT, 1);
    } else {
      gfx2d_text(ox + list_x + FM_COL_ICON_W + FM_COL_NAME_W + 4,
                 oy + iy + 2, "<DIR>", COL_TEXT, 1);
    }

    char *ext = files[idx].is_dir ? "Folder" : "File";
    gfx2d_text(ox + list_x + FM_COL_ICON_W + FM_COL_NAME_W + FM_COL_SIZE_W + 4,
               oy + iy + 2, ext, COL_TEXT, 1);

    row++;
  }

  gfx2d_rect(ox + list_x, oy + items_y, list_w, items_h, COL_BORDER);

  int sb_x = cw - FM_SCROLLBAR_W;
  int sb_h = items_h;
  gfx2d_rect_fill(ox + sb_x, oy + items_y, FM_SCROLLBAR_W, sb_h, COL_BG);
  gfx2d_rect(ox + sb_x, oy + items_y, FM_SCROLLBAR_W, sb_h, COL_BORDER);
  if (file_count > 0) {
    int thumb_h = (items_visible * sb_h) /
                  (file_count > items_visible ? file_count : items_visible);
    if (thumb_h < 12) thumb_h = 12;
    int thumb_y = items_y;
    if (file_count > items_visible) {
      thumb_y = items_y + (scroll_off * (sb_h - thumb_h)) /
                        (file_count - items_visible);
    }
    gfx2d_rect_fill(ox + sb_x + 1, oy + thumb_y,
                    FM_SCROLLBAR_W - 2, thumb_h, COL_BORDER);
  }

  int sy = ch - FM_STATUSBAR_H;
  gfx2d_rect_fill(ox, oy + sy, cw, FM_STATUSBAR_H, COL_TOOLBAR);
  gfx2d_hline(ox, oy + sy, cw, COL_BORDER);

  char status[80];
  fm_itoa(file_count, status);
  fm_strcat(status, " items");
  int sel = fm_count_selected();
  if (sel > 0) {
    fm_strcat(status, " | ");
    char s2[8];
    fm_itoa(sel, s2);
    fm_strcat(status, s2);
    fm_strcat(status, " selected");
  }
  gfx2d_text(ox + 6, oy + sy + 3, status, COL_TEXT, 1);
}

void fm_context_menu(int mx, int my) {
  char *items[8];
  items[0] = "Open";
  items[1] = "Copy";
  items[2] = "Cut";
  items[3] = "Paste";
  items[4] = "Delete";
  items[5] = "Rename";
  items[6] = "New Folder";
  items[7] = "Refresh";

  int choice = popup_menu(mx, my, (void*)items, 8);
  if (choice == 0) fm_open_selected();
  if (choice == 1) fm_copy();
  if (choice == 2) fm_cut();
  if (choice == 3) fm_paste();
  if (choice == 4) fm_delete_selected();
  if (choice == 5) fm_rename_item();
  if (choice == 6) fm_new_folder();
  if (choice == 7) fm_refresh();
}

void main() {
  should_close = 0;
  frame_count = 0;
  file_count = 0;
  scroll_off = 0;
  cursor_idx = 0;
  clip_count = 0;
  clip_cut = 0;

  char *start = get_cwd();
  if (start != 0 && start[0] != 0) {
    fm_strcpy(cwd, start);
  } else {
    cwd[0] = '/';
    cwd[1] = 0;
  }

  if (cwd[0] == '/' && cwd[1] == 0) {
    if (!fm_try_set_start_dir("/home")) {
      if (!fm_try_set_start_dir("/bin")) {
        if (!fm_try_set_start_dir("/docs")) {
          if (!fm_try_set_start_dir("/cupidos-txt")) {
            if (!fm_try_set_start_dir("/demos")) {
              cwd[0] = '/';
              cwd[1] = 0;
            }
          }
        }
      }
    }
  }

  win = gui_win_create("CupidFM", 80, 50, 520, 360);
  if (win == -1) {
    message_dialog("CupidFM: failed to create window");
    return;
  }

  fm_refresh();

  if (file_count == 0 && cwd[0] == '/' && cwd[1] == 0) {
    if (fm_try_set_start_dir("/home") || fm_try_set_start_dir("/bin") ||
        fm_try_set_start_dir("/docs") || fm_try_set_start_dir("/cupidos-txt") ||
        fm_try_set_start_dir("/demos")) {
      fm_refresh();
    }
  }

  int prev_buttons = mouse_buttons();

  while (gui_win_is_open(win) && !should_close) {
    int can_draw = gui_win_can_draw(win);

    if (!can_draw) {
      yield();
      continue;
    }

    int mx = mouse_x();
    int my = mouse_y();
    int btns = mouse_buttons();
    int left_click = (btns & 1) && !(prev_buttons & 1);
    int right_click = (btns & 2) && !(prev_buttons & 2);

    int cx = gui_win_content_x(win);
    int cy = gui_win_content_y(win);
    int cw = gui_win_content_w(win);
    int ch = gui_win_content_h(win);

    view_x = cx;
    view_y = cy;
    view_w = cw;
    view_h = ch;

    int lmx = mx - cx;
    int lmy = my - cy;
    int in_content = (lmx >= 0 && lmx < cw && lmy >= 0 && lmy < ch) ? 1 : 0;

    gui_win_draw_frame(win);

    if (can_draw) {
      /* Keyboard */
      {
      int key = gui_win_poll_key(win);
      while (key != -1) {
        int sc = (key >> 8) & 255;
        int chv = key & 255;

        if (chv == 27) should_close = 1;
        if (chv == 13 || chv == 10) fm_open_selected();
        if (chv == 8) fm_go_up();
        if (chv == 3) fm_copy();
        if (chv == 24) fm_cut();
        if (chv == 22) fm_paste();
        if (chv == 1) fm_select_all();
        if (chv == 127) fm_delete_selected();

        if (sc == 72) {
          if (cursor_idx > 0) cursor_idx = cursor_idx - 1;
        }
        if (sc == 80) {
          if (cursor_idx + 1 < file_count) cursor_idx = cursor_idx + 1;
        }

        key = gui_win_poll_key(win);
      }
      }

      /* Mouse */
      int items_y = list_y + FM_ITEM_H;
      int items_h = list_h - FM_ITEM_H;
      int items_visible = items_h / FM_ITEM_H;
      if (items_visible < 1) items_visible = 1;

      if (left_click && in_content) {
        /* Toolbar buttons are handled in render() through draw_button() hit test.
         * For list interactions we handle here. */
        if (lmx >= list_x && lmx < list_x + list_w &&
            lmy >= items_y && lmy < items_y + items_h) {
          int clicked_row = (lmy - items_y) / FM_ITEM_H;
          int clicked_idx = scroll_off + clicked_row;

          if (clicked_idx >= 0 && clicked_idx < file_count) {
            int now = uptime_ms();
            int same_idx = (clicked_idx == dbl_click_idx);
            int dt = now - dbl_click_time;
            if (same_idx && dt >= 0 && dt < 700) {
              cursor_idx = clicked_idx;
              fm_open_selected();
              dbl_click_idx = -1;
              dbl_click_time = 0;
            } else {
              fm_deselect_all();
              cursor_idx = clicked_idx;
              files[cursor_idx].selected = 1;
              dbl_click_idx = clicked_idx;
              dbl_click_time = now;
            }
          }
        }

        int sb_x = cw - FM_SCROLLBAR_W;
        if (lmx >= sb_x && lmx < cw && lmy >= items_y && lmy < items_y + items_h) {
          int mid = items_y + items_h / 2;
          if (lmy < mid) {
            scroll_off = scroll_off - items_visible;
            if (scroll_off < 0) scroll_off = 0;
          } else {
            scroll_off = scroll_off + items_visible;
            int max_off = file_count - items_visible;
            if (max_off < 0) max_off = 0;
            if (scroll_off > max_off) scroll_off = max_off;
          }
        }
      }

      if (right_click && in_content) {
        if (lmx >= list_x && lmx < list_x + list_w &&
            lmy >= items_y && lmy < items_y + items_h) {
          int clicked_row = (lmy - items_y) / FM_ITEM_H;
          int clicked_idx = scroll_off + clicked_row;
          if (clicked_idx >= 0 && clicked_idx < file_count) {
            cursor_idx = clicked_idx;
            if (!files[cursor_idx].selected) {
              fm_deselect_all();
              files[cursor_idx].selected = 1;
            }
          }
        }
        fm_context_menu(mx, my);
      }

      if (in_content) {
        int scroll_dz = mouse_scroll();
        if (scroll_dz != 0) {
          scroll_off = scroll_off + scroll_dz * 3;
          if (scroll_off < 0) scroll_off = 0;
          {
            int max_off = file_count - items_visible;
            if (max_off < 0) max_off = 0;
            if (scroll_off > max_off) scroll_off = max_off;
          }
        }
      }
    }

    prev_buttons = btns;

    fm_render(cx, cy, cw, ch, lmx, lmy, can_draw && left_click && in_content);
    gui_win_flip(win);

    frame_count = frame_count + 1;
    yield();
  }

  if (win != -1) {
    gui_win_close(win);
    win = -1;
  }
}
