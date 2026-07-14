# Desktop Environment

cupid-os has a graphical desktop built on a 640x480, 32-bit VBE framebuffer. It includes a window manager, a taskbar and desktop icons, a mouse-driven terminal, and Notepad.

---

## Overview

The desktop is the default interface after boot. It provides:

- **Pastel-themed background** (Temple OS / OsakaOS-inspired aesthetic, 32bpp true color)
- **Taskbar** at the bottom with "cupid-os" branding and window buttons
- **Clickable desktop icons** that launch applications
- **Draggable overlapping windows** with close buttons
- **Mouse cursor** with outline for visibility

---

## VBE Graphics

### VBE 640x480 32bpp

| Property | Value |
|----------|-------|
| Resolution | 640 x 480 pixels |
| Color depth | 32bpp true color (XRGB 0x00RRGGBB) |
| Framebuffer | Linear at 0xFD000000 (QEMU PCI BAR0) |
| Buffering | Double-buffered (1.2MB heap-allocated back buffer) |
| Mode set | Bochs VBE I/O ports 0x01CE / 0x01CF |
| LFB discovery | PCI config space BAR0 read in bootloader |

The UI uses a soft pastel color scheme (`drivers/vga.h`):

| Constant | XRGB Value | Use |
|----------|-----------|-----|
| `COLOR_DESKTOP_BG` | `#FFE8F0` | Pink blush desktop |
| `COLOR_TASKBAR` | `#E8D8F8` | Lavender mist taskbar |
| `COLOR_TITLEBAR` | `#B8DDFF` | Powder blue focused title |
| `COLOR_TITLE_UNFOC` | `#C8C8D0` | Silver unfocused title |
| `COLOR_WINDOW_BG` | `#FFF0F5` | Soft rose white window body |
| `COLOR_CLOSE_BG` | `#FF9090` | Coral red close button |
| `COLOR_TERM_BG` | `#141418` | Near-black terminal |
| `COLOR_TEXT` | `#282830` | Dark charcoal text |
| `COLOR_BORDER` | `#9898A0` | Cool gray borders |

### Graphics Primitives

- **Pixel** - Plot with bounds clipping
- **Line** - Bresenham's algorithm for arbitrary angles; per-pixel loop for horizontal/vertical
- **Rectangle** - Filled and outlined
- **3D rect** - Windows-95-style raised/sunken edges
- **Text** - 8x8 monospaced bitmap font (ASCII 0-127), scalable 1x/2x/3x

---

## Window Manager

Up to **16 overlapping windows** with z-ordered rendering.

### Window Features

- **Title bar** - Displays window name, changes color based on focus (cyan = focused, gray = unfocused)
- **Close button** - "X" in the title bar with hit testing
- **Dragging** - Click and drag title bar to reposition; clamped to screen bounds
- **Focus** - Click a window to raise it to the top
- **Redraw callbacks** - Each application registers its own rendering function

### Window Lifecycle

1. **Create** - `gui_create_window(x, y, w, h, title, callback)`
2. **Focus** - Click to bring to front
3. **Drag** - Click title bar and move
4. **Close** - Click "X" button
5. **Destroy** - Window removed from manager

---

## Desktop Shell

### Taskbar

The taskbar sits at the bottom of the screen (24 pixels high):

- Left side: "cupid-os" branding text
- Middle: Buttons for each open window (click to focus)
- Right side: **Real-time clock** showing time (12-hour AM/PM) and short date
- Active window is highlighted
- Click the clock to open the **calendar popup**

### Clock Display

The clock reads from the CMOS Real-Time Clock (RTC) hardware:

| Property | Value |
|----------|-------|
| Format | `2:35 PM  Feb 6` |
| Position | Right-aligned in taskbar |
| Update | Every minute (polls RTC each redraw, caches formatted strings) |
| Click | Opens interactive calendar popup |

### Calendar Popup

Click the taskbar clock to open a 440x320 pixel calendar popup centered on screen:

```
┌────────────────────────────────────┐
│  <  February 2026  >               │
│                        2:35:47 PM  │
├────────────────────────────────────┤
│  Thursday, February 6, 2026        │
│  Su Mo Tu We Th Fr Sa              │
│                        1           │
│   2  3  4  5 [6] 7  8              │
│   9 10 11 12 13 14 15              │
│  16 17 18 19 20 21 22              │
│  23 24 25 26 27 28                 │
└────────────────────────────────────┘
```

The calendar shows:

- Current day highlighted with colored background
- Month/year header with `<` and `>` navigation arrows
- Full date line (e.g., "Thursday, February 6, 2026")
- Time display with seconds (updates each redraw)
- Handles leap years and all month lengths correctly
- Uses Zeller's congruence for weekday calculation

Close the popup in either of these ways:

- Click outside the calendar area
- Press Escape

### Desktop Icons

Clickable icons on the desktop that launch applications:

| Icon | Application |
|------|-------------|
| Terminal | Opens a GUI terminal window running the shell |
| Notepad | Opens a text editor window |
| Browser | Opens the graphical HTTP/HTTPS browser, when the icon registry includes it |
| Paint / FM / Font Switch | Launches the paint app, file manager, and font picker from the app registry |

---

## GUI Terminal

The terminal application runs the shell inside a graphical window:

| Property | Value |
|----------|-------|
| Default size | 560 x 320 pixels |
| Character buffer | 80 columns x 500 rows (scrollback) |
| Background | Dark (`#141418`) |
| Default text | Light gray on dark background |
| Color support | ANSI/VT colors, 256-color fallback, truecolor fallback, reverse video |
| Cursor | Blinking vertical bar, 500ms toggle |
| Font zoom | Ctrl+`+` / Ctrl+`-` to scale 1x, 2x, 3x |
| Scrollback | PgUp/PgDn (10 rows), Home/End (top/bottom), mouse wheel (3 rows/notch) |

### Scrollback

The shell character buffer is 500 rows tall. Output from a long-running command
such as `curl http://example.com/` remains available for review:

| Input | Action |
|---|---|
| **PgUp** / **PgDn** | scroll up / down 10 rows |
| **Home** | jump to top of buffer |
| **End** | jump to bottom (most recent output) |
| **Mouse wheel** | 3 rows per notch, sign matches host wheel |
| Typing characters | preserves scroll position |
| **Enter** | resets scroll to bottom (so you see the new prompt + output) |

When the buffer fills (cursor reaches row 499), the oldest row is
discarded, so scrollback is finite.

Mouse wheel routing lives in `kernel/gui/desktop.c`: when the focused
window's title is `"Terminal"`, `mouse.scroll_z` is forwarded to
`terminal_handle_scroll()` instead of being silently consumed.
PgUp / PgDn / Home / End scancodes arrive via the extended-scancode
PS/2 path (or, for USB HID keyboards, via the `0xE0` prefix injected
by `usb_hid.c`'s `hid_is_extended[]` table - see [USB](USB)).

### Remote Terminal Support

The GUI terminal is also the interactive frontend for `ssh` and `telnet`.
Those clients use `kernel/lang/ssh_io.c` to route bytes, hidden password
input, window size, and special keys through the same focused Terminal window.

Supported remote-terminal behavior includes:

- xterm/VT arrow, Home/End, PageUp/PageDown, Delete, and application-cursor modes
- alternate screen buffer for full-screen tools such as `top` and `btop`
- cursor positioning, erase, scroll regions, insert/delete line/char, save/restore cursor
- OSC title changes, bracketed-paste private modes, ACS fallback, and UTF-8 cell fallback
- terminal rows/cols tracking and SSH `window-change` updates after resize/zoom

Command execution for blocking remote sessions runs in the terminal process,
so the desktop event loop keeps repainting while an SSH shell is active.

### How It Works

1. The shell writes to an 80x50 character buffer instead of VGA text memory.
2. The terminal parses ANSI escape sequences and removes them from the output stream.
3. A parallel color buffer stores each character's foreground and background VGA color indices.
4. The terminal renders each character in its stored color, mapped to the Mode 13h palette.
5. The desktop event loop forwards keyboard events to the shell.
6. The shell can write to VGA text mode or the GUI character buffer.

### Color Rendering

Each character cell in the terminal has:
- **Character** - The ASCII character to display
- **Foreground color** - VGA color index (0-15), mapped to 32bpp XRGB via `ansi_vga_to_palette()`
- **Background color** - VGA color index (0-7), rendered as a colored rectangle behind the character

The ANSI parser handles:
- `\e[30m` - `\e[37m` - Standard foreground colors
- `\e[40m` - `\e[47m` - Background colors
- `\e[90m` - `\e[97m` - Bright foreground colors
- `\e[0m` - Reset to defaults
- `\e[1m` - Bold (bright foreground)
- `\e[2J` - Clear screen
- `\e[H` - Cursor home
- cursor movement/positioning, scroll regions, alternate screen, save/restore cursor, and common xterm private modes used by full-screen terminal programs

### Interaction

- Click the terminal window to focus it
- Type commands as normal - they appear in the terminal window
- All shell commands work, including `ed`, `cupid`, debug commands, etc.
- Scroll support for output longer than the visible area

---

## Mouse

### PS/2 Mouse Driver

- **IRQ12** interrupt-driven
- 3-byte packet protocol with sign extension and overflow protection
- Button tracking: left, right, middle

### Cursor

- 8x10 pixel arrow bitmap with outline for visibility
- **Save-under buffer** - saves the pixels beneath the cursor for non-destructive rendering
- Position clamped to screen bounds (0-639 horizontal, 0-479 vertical)

### Mouse Interactions

| Action | Result |
|--------|--------|
| Click window title bar | Focus window, start drag |
| Drag title bar | Move window |
| Click window "X" | Close window |
| Click window body | Focus window |
| Click desktop icon | Launch application |
| Click taskbar button | Focus corresponding window |
| Click taskbar clock | Toggle calendar popup |
| Click calendar `<`/`>` | Navigate months |
| Click outside calendar | Close calendar popup |

---

## Event Loop

The desktop runs a main event loop (registered as **PID 2**):

```
while (1) {
    1. Process mouse events (movement, clicks)
    2. Process keyboard events (forward to focused window)
    3. Redraw dirty windows
    4. Render cursor
    5. Flip double buffer to screen
    6. Check scheduler reschedule flag
    7. HLT (wait for next interrupt)
}
```

---

## Process Integration

| Process | PID | Role |
|---------|-----|------|
| Idle | 1 | Always present, runs when nothing else is ready |
| Desktop | 2 | Main GUI event loop, registered via `process_register_current()` |
| Terminal | 3 | GUI terminal, spawned as its own process |

---

## See Also

- [Shell Commands](Shell-Commands) - Commands available in the terminal
- [Process Management](Process-Management) - How the desktop integrates with the scheduler
- [Architecture](Architecture) - System component overview
