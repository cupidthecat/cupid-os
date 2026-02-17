//icon: "My Computer"
//icon_desc: "File Manager"
//icon_x: 10
//icon_y: 130
//icon_type: folder
//icon_color: 0xFFFF00

/* fm.cc - CupidFM: Graphical File Manager
 *
 * A Win95-style file manager written entirely in CupidC.
 * Features: directory navigation, file ops (copy/paste/delete/rename),
 * new folder creation, list view, context menus, keyboard shortcuts.
 */

int FM_FILES_INIT_CAP = 64;
int FM_FILES_MAX_CAP  = 256;
int FM_MAX_PATH     = 128;
int FM_MAX_NAME     = 64;
int FM_ITEM_H       = 16;
int FM_TOOLBAR_H    = 28;
int FM_TITLEBAR_H   = 20;
int FM_STATUSBAR_H  = 18;
int FM_ADDR_H       = 20;
int FM_SCROLLBAR_W  = 14;

int FM_COL_ICON_W   = 24;
int FM_COL_NAME_W   = 320;
int FM_COL_SIZE_W   = 80;
int FM_COL_TYPE_W   = 100;

/* Colors (matching CupidOS Win95 palette) */
int COL_BG       = 0x00FFF0F5;  /* Soft rose white */
int COL_TITLEBAR = 0x00B8DDFF;  /* Powder blue */
int COL_BLACK    = 0x00000000;
int COL_WHITE    = 0x00F8F8F8;
int COL_TEXT     = 0x00282830;
int COL_BORDER   = 0x009898A0;
int COL_SELECT   = 0x00C0D8FF;  /* Periwinkle */
int COL_HOVER    = 0x00D8E8FF;  /* Light periwinkle */
int COL_TOOLBAR  = 0x00E8D8F8;  /* Lavender mist */
int COL_LIST_BG  = 0x00FFFFFF;
int COL_HEADER   = 0x00D0D0D8;
int COL_ICON_DIR = 0x00EECC44;  /* Golden folder */
int COL_ICON_FIL = 0x00AAAACC;  /* File blue-gray */
int COL_SHADOW   = 0x00404040;

/* VFS type flags */
int VFS_TYPE_DIR = 1;

struct fm_entry {
  char name[64];
  int size;
  int is_dir;
  int selected;
};

char cwd[128];
struct fm_entry files[256];
int file_count = 0;
int scroll_off = 0;
int cursor_idx = 0;
int fm_should_close = 0;
int fm_frame_count = 0;
int startup_input_grace_until = 0;

/* Clipboard for copy/cut */
char clip_paths[8][128];
int clip_is_dir[8];  /* 1 if the clipboard item is a directory */
int clip_count = 0;
int clip_cut = 0;  /* 1 = cut mode (move), 0 = copy mode */

/* Screen dimensions */
int scrw = 640;
int scrh = 480;

/* Layout positions (computed once) */
int list_x = 0;
int list_y = 0;
int list_w = 640;
int list_h = 380;
int visible_items = 0;

int fm_reserve_files(int needed) {
  if (needed <= FM_FILES_MAX_CAP) return 1;
  return 0;
}

void fm_release_files() {
  file_count = 0;
}

void fm_strcpy(char *dst, char *src) {
  int i = 0;
  while (src[i]) { dst[i] = src[i]; i++; }
  dst[i] = 0;
}

void fm_copy_name64(char *dst, char *src) {
  int i = 0;
  while (i < 63 && src[i]) {
    dst[i] = src[i];
    i = i + 1;
  }
  dst[i] = 0;
}

int fm_strlen(char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}

void fm_strcat(char *dst, char *src) {
  int d = fm_strlen(dst);
  int i = 0;
  while (src[i]) { dst[d + i] = src[i]; i++; }
  dst[d + i] = 0;
}

int fm_strcmp(char *a, char *b) {
  while (*a && *b && *a == *b) { a++; b++; }
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
  if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
  char tmp[16];
  int i = 0;
  int neg = 0;
  if (val < 0) { neg = 1; val = -val; }
  while (val > 0 && i < 15) {
    tmp[i] = '0' + (val % 10);
    val = val / 10;
    i++;
  }
  int p = 0;
  if (neg) { buf[p] = '-'; p++; }
  while (i > 0) { i--; buf[p] = tmp[i]; p++; }
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

/* Build a full path from cwd + filename */
void fm_build_path(char *out, char *dir, char *name) {
  fm_strcpy(out, dir);
  int len = fm_strlen(out);
  if (len > 0 && out[len - 1] != '/') {
    out[len] = '/';
    out[len + 1] = 0;
  }
  fm_strcat(out, name);
}

/* Recursively delete a path. For directories, delete children first. */
int fm_delete_path_recursive(char *path, int is_dir) {
  if (!path || !path[0]) return -1;

  /* Safety: never allow deleting root from FM */
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

      /* Skip . and .. */
      if (dname[0] == '.' && dname[1] == 0) continue;
      if (dname[0] == '.' && dname[1] == '.' && dname[2] == 0) continue;

      char child[128];
      fm_build_path(child, path, dname);
      int child_is_dir = (ent[68] == VFS_TYPE_DIR) ? 1 : 0;
      fm_delete_path_recursive(child, child_is_dir);
    }
    vfs_close(fd);
  }

  /* Remove the directory itself (now expected empty). */
  return vfs_unlink(path);
}

void fm_sort_files() {
  int i = 0;
  while (i < file_count - 1) {
    int j = i + 1;
    while (j < file_count) {
      int swap = 0;
      /* Directories before files */
      if (files[j].is_dir && !files[i].is_dir) swap = 1;
      /* Same type: alphabetical */
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

void fm_refresh() {
  file_count = 0;
  scroll_off = 0;
  cursor_idx = 0;

  if (!fm_reserve_files(FM_FILES_INIT_CAP)) {
    message_dialog("Out of memory");
    return;
  }

  int fd = vfs_open(cwd, 0);
  if (fd < 0) return;

  char ent[72];
  while (1) {
    int r = vfs_readdir(fd, ent);
    if (r <= 0) break;

    char *dname = ent;

    /* Skip "." entry */
    if (dname[0] == '.' && dname[1] == 0) continue;

    if (file_count >= FM_FILES_MAX_CAP) {
      if (!fm_reserve_files(file_count + 1)) break;
    }

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
  /* Verify directory exists before navigating */
  int fd = vfs_open(path, 0);
  if (fd < 0) {
    message_dialog("Cannot open directory");
    return;
  }
  vfs_close(fd);
  fm_strcpy(cwd, path);
  fm_refresh();
}

int fm_try_set_start_dir(char *path) {
  int fd = vfs_open(path, 0);
  if (fd < 0)
    return 0;
  vfs_close(fd);
  fm_strcpy(cwd, path);
  return 1;
}

void fm_go_up() {
  if (cwd[0] == '/' && cwd[1] == 0) return; /* Already at root */

  /* Find last '/' */
  int last = -1;
  int i = 0;
  while (cwd[i]) {
    if (cwd[i] == '/') last = i;
    i++;
  }
  if (last <= 0) {
    cwd[0] = '/'; cwd[1] = 0;
  } else {
    cwd[last] = 0;
  }
  fm_refresh();
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
    /* Open text/code files in GUI Notepad.
     * CupidFM is fullscreen; minimize it so desktop input can reach Notepad. */
    if (fm_ends_with_txt(files[cursor_idx].name) ||
        fm_ends_with_ctxt(files[cursor_idx].name) ||
        fm_ends_with_cc(files[cursor_idx].name)) {
      notepad_open_file(path);
      gfx2d_minimize("CupidFM");
      return;
    }

    /* Try running as a program first; if it fails, always fall back to
     * opening in Notepad so files are still viewable/editable. */
    if (exec(path, files[cursor_idx].name) < 0) {
      notepad_open_file(path);
      gfx2d_minimize("CupidFM");
    }
  }
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
    /* Extract just the filename from the source path */
    char *src = clip_paths[i];
    char *name = src;
    char *p = src;
    while (*p) {
      if (*p == '/') name = p + 1;
      p++;
    }

    char dst[128];
    fm_build_path(dst, cwd, name);

    /* Skip if source and destination are the same path */
    if (strcmp(src, dst) == 0) {
      i++;
      continue;
    }

    if (clip_is_dir[i]) {
      /* Directory: create at destination */
      vfs_mkdir(dst);
      /* For cut: remove source tree. (Copy is still shallow.) */
      if (clip_cut) {
        fm_delete_path_recursive(src, 1);
      }
    } else {
      if (clip_cut) {
        vfs_rename(src, dst);
      } else {
        vfs_copy_file(src, dst);
      }
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

  int deleted = 0;
  int failed = 0;
  int i = 0;
  while (i < file_count) {
    if (files[i].selected) {
      /* Never delete parent marker if present */
      if (fm_strcmp(files[i].name, "..") == 0) {
        i++;
        continue;
      }

      char path[128];
      fm_build_path(path, cwd, files[i].name);
      int rc = fm_delete_path_recursive(path, files[i].is_dir);
      if (rc == 0) deleted++;
      else failed++;
    }
    i++;
  }

  if (failed > 0) {
    char out[64];
    char n1[12];
    char n2[12];
    fm_strcpy(out, "Deleted ");
    fm_itoa(deleted, n1);
    fm_strcat(out, n1);
    fm_strcat(out, ", failed ");
    fm_itoa(failed, n2);
    fm_strcat(out, n2);
    message_dialog(out);
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

void draw_folder_icon(int x, int y) {
  /* Folder tab */
  gfx2d_rect_fill(x, y, 7, 3, COL_ICON_DIR);
  /* Folder body */
  gfx2d_rect_fill(x, y + 3, 14, 9, COL_ICON_DIR);
  gfx2d_rect(x, y + 3, 14, 9, COL_TEXT);
  /* Tab border */
  gfx2d_hline(x, y, 7, COL_TEXT);
  gfx2d_vline(x, y, 3, COL_TEXT);
  gfx2d_vline(x + 7, y, 3, COL_TEXT);
}

void draw_file_icon(int x, int y) {
  /* Page body */
  gfx2d_rect_fill(x + 1, y, 10, 13, COL_WHITE);
  gfx2d_rect(x + 1, y, 10, 13, COL_TEXT);
  /* Dog-ear corner */
  gfx2d_line(x + 8, y, x + 11, y + 3, COL_TEXT);
  /* Text lines */
  gfx2d_hline(x + 3, y + 4, 5, COL_ICON_FIL);
  gfx2d_hline(x + 3, y + 6, 6, COL_ICON_FIL);
  gfx2d_hline(x + 3, y + 8, 4, COL_ICON_FIL);
}

int tb_btn(int x, int y, int w, char *label, int mx, int my, int click) {
  int hover = (mx >= x && mx < x + w && my >= y && my < y + FM_TOOLBAR_H - 4);
  int color = hover ? COL_HOVER : COL_TOOLBAR;
  gfx2d_rect_fill(x, y, w, FM_TOOLBAR_H - 4, color);
  if (hover) gfx2d_rect(x, y, w, FM_TOOLBAR_H - 4, COL_BORDER);
  gfx2d_text(x + 4, y + 5, label, COL_BLACK, 1);
  return hover && click;
}

void fm_render(int mx, int my, int click) {
  /* Background */
  gfx2d_rect_fill(0, 0, scrw, scrh, COL_BG);

  /* App title bar with close/minimize buttons */
  int tb_action = gfx2d_app_toolbar("CupidFM", mx, my, click);
  if (tb_action == 1) {
    if (uptime_ms() > startup_input_grace_until && fm_frame_count > 30) {
      fm_should_close = 1; /* Close */
      return;
    }
  }
  if (tb_action == 2) {
    gfx2d_minimize("CupidFM"); /* Minimize to taskbar */
    return;
  }

  /* Toolbar */
  int ty = FM_TITLEBAR_H;
  gfx2d_rect_fill(0, ty, scrw, FM_TOOLBAR_H, COL_TOOLBAR);
  gfx2d_hline(0, ty + FM_TOOLBAR_H - 1, scrw, COL_BORDER);

  int tx = 4;
  if (tb_btn(tx, ty + 2, 26, "<-", mx, my, click)) fm_go_up();
  tx = tx + 30;
  if (tb_btn(tx, ty + 2, 50, "New", mx, my, click)) fm_new_folder();
  tx = tx + 54;
  if (tb_btn(tx, ty + 2, 50, "Copy", mx, my, click)) fm_copy();
  tx = tx + 54;
  if (tb_btn(tx, ty + 2, 46, "Cut", mx, my, click)) fm_cut();
  tx = tx + 50;
  if (tb_btn(tx, ty + 2, 54, "Paste", mx, my, click)) fm_paste();
  tx = tx + 58;
  if (tb_btn(tx, ty + 2, 46, "Del", mx, my, click)) fm_delete_selected();
  tx = tx + 50;
  if (tb_btn(tx, ty + 2, 60, "Rename", mx, my, click)) fm_rename_item();
  tx = tx + 64;
  if (tb_btn(tx, ty + 2, 62, "Refresh", mx, my, click)) fm_refresh();

  /* Address bar */
  int ay = ty + FM_TOOLBAR_H;
  gfx2d_rect_fill(0, ay, scrw, FM_ADDR_H, COL_WHITE);
  gfx2d_rect(0, ay, scrw, FM_ADDR_H, COL_BORDER);
  gfx2d_text(6, ay + 4, cwd, COL_BLACK, 1);

  /* List header */
  list_y = ay + FM_ADDR_H;
  list_x = 0;
  list_w = scrw - FM_SCROLLBAR_W;
  list_h = scrh - list_y - FM_STATUSBAR_H;
  visible_items = list_h / FM_ITEM_H;
  if (visible_items < 1) visible_items = 1;

  /* Column header bar */
  int hy = list_y;
  gfx2d_rect_fill(list_x, hy, list_w, FM_ITEM_H, COL_HEADER);
  gfx2d_hline(list_x, hy + FM_ITEM_H - 1, list_w, COL_BORDER);
  gfx2d_text(list_x + FM_COL_ICON_W + 2, hy + 2, "Name", COL_BLACK, 1);
  gfx2d_text(list_x + FM_COL_ICON_W + FM_COL_NAME_W + 4, hy + 2, "Size",
             COL_BLACK, 1);
  gfx2d_text(list_x + FM_COL_ICON_W + FM_COL_NAME_W + FM_COL_SIZE_W + 4,
             hy + 2, "Type", COL_BLACK, 1);

  /* File list */
  int items_y = list_y + FM_ITEM_H;
  int items_h = list_h - FM_ITEM_H;
  int items_visible = items_h / FM_ITEM_H;
  if (items_visible < 1) items_visible = 1;

  /* List background */
  gfx2d_rect_fill(list_x, items_y, list_w, items_h, COL_LIST_BG);

  /* Draw each visible item */
  int row = 0;
  while (row < items_visible && (scroll_off + row) < file_count) {
    int idx = scroll_off + row;
    int iy = items_y + row * FM_ITEM_H;

    /* Background: selected, cursor, or normal */
    if (files[idx].selected) {
      gfx2d_rect_fill(list_x, iy, list_w, FM_ITEM_H, COL_SELECT);
    } else if (idx == cursor_idx) {
      gfx2d_rect_fill(list_x, iy, list_w, FM_ITEM_H, COL_HOVER);
    }

    /* Icon */
    if (files[idx].is_dir) {
      draw_folder_icon(list_x + 4, iy + 1);
    } else {
      draw_file_icon(list_x + 4, iy + 1);
    }

    /* Name */
    gfx2d_text(list_x + FM_COL_ICON_W + 2, iy + 2, files[idx].name,
               COL_BLACK, 1);

    /* Size */
    if (!files[idx].is_dir) {
      char sbuf[16];
      fm_size_str(files[idx].size, sbuf);
      gfx2d_text(list_x + FM_COL_ICON_W + FM_COL_NAME_W + 4, iy + 2,
                 sbuf, COL_TEXT, 1);
    } else {
      gfx2d_text(list_x + FM_COL_ICON_W + FM_COL_NAME_W + 4, iy + 2,
                 "<DIR>", COL_TEXT, 1);
    }

    /* Type */
    char *ext = files[idx].is_dir ? "Folder" : "File";
    gfx2d_text(list_x + FM_COL_ICON_W + FM_COL_NAME_W + FM_COL_SIZE_W + 4,
               iy + 2, ext, COL_TEXT, 1);

    row++;
  }

  /* Draw border around list */
  gfx2d_rect(list_x, items_y, list_w, items_h, COL_BORDER);

  /* Simple scrollbar */
  int sb_x = scrw - FM_SCROLLBAR_W;
  int sb_h = items_h;
  gfx2d_rect_fill(sb_x, items_y, FM_SCROLLBAR_W, sb_h, COL_BG);
  gfx2d_rect(sb_x, items_y, FM_SCROLLBAR_W, sb_h, COL_BORDER);
  /* Thumb */
  if (file_count > 0) {
    int thumb_h = (items_visible * sb_h) / (file_count > items_visible ? file_count : items_visible);
    if (thumb_h < 12) thumb_h = 12;
    int thumb_y = items_y;
    if (file_count > items_visible) {
      thumb_y = items_y + (scroll_off * (sb_h - thumb_h)) / (file_count - items_visible);
    }
    gfx2d_rect_fill(sb_x + 1, thumb_y, FM_SCROLLBAR_W - 2, thumb_h, COL_BORDER);
  }

  /* Status bar */
  int sy = scrh - FM_STATUSBAR_H;
  gfx2d_rect_fill(0, sy, scrw, FM_STATUSBAR_H, COL_TOOLBAR);
  gfx2d_hline(0, sy, scrw, COL_BORDER);

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
  gfx2d_text(6, sy + 3, status, COL_TEXT, 1);
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

int main() {
  /* Enter fullscreen mode */
  gfx2d_fullscreen_enter();

  /* Reset key runtime state in case globals persist across JIT runs. */
  fm_should_close = 0;
  fm_frame_count = 0;
  file_count = 0;
  scroll_off = 0;
  cursor_idx = 0;
  clip_count = 0;
  clip_cut = 0;

  /* Start at root or cwd */
  char *start = get_cwd();
  if (start != 0 && start[0] != 0) {
    fm_strcpy(cwd, start);
  } else {
    cwd[0] = '/';
    cwd[1] = 0;
  }

  /* Prefer a content directory if root is empty in this environment. */
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

  if (!fm_reserve_files(FM_FILES_INIT_CAP)) {
    message_dialog("Out of memory");
    gfx2d_fullscreen_exit();
    return 1;
  }

  fm_refresh();

  /* If root shows empty, auto-fallback to common populated dirs. */
  if (file_count == 0 && cwd[0] == '/' && cwd[1] == 0) {
    if (fm_try_set_start_dir("/home") || fm_try_set_start_dir("/bin") ||
        fm_try_set_start_dir("/docs") || fm_try_set_start_dir("/cupidos-txt") ||
        fm_try_set_start_dir("/demos")) {
      fm_refresh();
    }
  }

  int prev_buttons = mouse_buttons();
  int dbl_click_time = 0;
  int dbl_click_idx = -1;
  startup_input_grace_until = uptime_ms() + 3000;

  /* Drain potentially stale key events from launcher context. */
  {
    int i = 0;
    while (i < 64) {
      poll_key();
      i++;
    }
  }

  while (!fm_should_close) {
    int mx = mouse_x();
    int my = mouse_y();
    int btns = mouse_buttons();
    int left_click = (btns & 1) && !(prev_buttons & 1);
    int right_click = (btns & 2) && !(prev_buttons & 2);

    /* Keyboard handling (non-blocking) */
    int ch = 0;
    char c = poll_key();
    if (c != 0) ch = c;

    if (uptime_ms() <= startup_input_grace_until) {
      left_click = 0;
      right_click = 0;
      ch = 0;
    }

    /* Key-based navigation uses getchar scan-code limitations -
     * we check for common key characters instead */

    /* Escape = quit */
    if (ch == 27) {
      if (uptime_ms() > startup_input_grace_until && fm_frame_count > 30) {
        fm_should_close = 1;
      }
    }

    /* Enter = open */
    if (ch == 13 || ch == 10) fm_open_selected();

    /* Backspace = go up */
    if (ch == 8) fm_go_up();

    /* Ctrl+C = copy (ASCII 3), Ctrl+X = cut (ASCII 24),
       Ctrl+V = paste (ASCII 22), Ctrl+A = select all (ASCII 1) */
    if (ch == 3) fm_copy();
    if (ch == 24) fm_cut();
    if (ch == 22) fm_paste();
    if (ch == 1) fm_select_all();

    /* Delete key (ASCII 127) */
    if (ch == 127) fm_delete_selected();

    /* 'n' for new folder (when not typing) */
    /* F2 rename - we can't easily detect F-keys via getchar,
       so we use keyboard_read would be needed. Skipping for now. */

    /* Mouse - list area clicks */
    int items_y = list_y + FM_ITEM_H;
    int items_h = list_h - FM_ITEM_H;
    int items_visible = items_h / FM_ITEM_H;
    if (items_visible < 1) items_visible = 1;

    if (left_click) {
      /* Check if click is in the list area */
      if (mx >= list_x && mx < list_x + list_w &&
          my >= items_y && my < items_y + items_h) {
        int clicked_row = (my - items_y) / FM_ITEM_H;
        int clicked_idx = scroll_off + clicked_row;

        if (clicked_idx >= 0 && clicked_idx < file_count) {
          /* Double-click detection */
          int now = uptime_ms();
          if (clicked_idx == dbl_click_idx && (now - dbl_click_time) < 400) {
            /* Double-click - open */
            cursor_idx = clicked_idx;
            fm_open_selected();
            dbl_click_idx = -1;
          } else {
            /* Single click - select */
            fm_deselect_all();
            cursor_idx = clicked_idx;
            files[cursor_idx].selected = 1;
            dbl_click_idx = clicked_idx;
            dbl_click_time = now;
          }
        }
      }

      /* Scrollbar clicks */
      int sb_x = scrw - FM_SCROLLBAR_W;
      if (mx >= sb_x && mx < scrw && my >= items_y && my < items_y + items_h) {
        int mid = items_y + items_h / 2;
        if (my < mid) {
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

    /* Right-click - context menu */
    if (right_click) {
      if (mx >= list_x && mx < list_x + list_w &&
          my >= items_y && my < items_y + items_h) {
        int clicked_row = (my - items_y) / FM_ITEM_H;
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

    /* Scroll wheel */
    int scroll_dz = mouse_scroll();
    if (scroll_dz != 0) {
      scroll_off = scroll_off + scroll_dz * 3;
      if (scroll_off < 0) scroll_off = 0;
      int max_off = file_count - items_visible;
      if (max_off < 0) max_off = 0;
      if (scroll_off > max_off) scroll_off = max_off;
    }

    prev_buttons = btns;

    /* Render */
    gfx2d_cursor_hide();
    fm_render(mx, my, left_click);
    gfx2d_draw_cursor();
    gfx2d_flip();
    fm_frame_count = fm_frame_count + 1;

    yield();
  }

  gfx2d_fullscreen_exit();
  fm_release_files();
  return 0;
}
