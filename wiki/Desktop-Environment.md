# Desktop Environment

cupid-os features a complete graphical desktop environment built on VBE 640×480 32-bit true color. It includes a window manager, desktop shell with taskbar and icons, a mouse-driven GUI terminal, and a notepad application.

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

### VBE 640×480 32bpp

| Property | Value |
|----------|-------|
| Resolution | 640 × 480 pixels |
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

- **Pixel** — Plot with bounds clipping
- **Line** — Bresenham's algorithm for arbitrary angles; per-pixel loop for horizontal/vertical
- **Rectangle** — Filled and outlined
- **3D rect** — Windows-95-style raised/sunken edges
- **Text** — 8×8 monospaced bitmap font (ASCII 0–127), scalable 1×/2×/3×

---

## Window Manager

Up to **16 overlapping windows** with z-ordered rendering.

### Window Features

- **Title bar** — Displays window name, changes color based on focus (cyan = focused, gray = unfocused)
- **Close button** — "X" in the title bar with hit testing
- **Dragging** — Click and drag title bar to reposition; clamped to screen bounds
- **Focus** — Click a window to raise it to the top
- **Redraw callbacks** — Each application registers its own rendering function

### Window Lifecycle

1. **Create** — `gui_create_window(x, y, w, h, title, callback)`
2. **Focus** — Click to bring to front
3. **Drag** — Click title bar and move
4. **Close** — Click "X" button
5. **Destroy** — Window removed from manager

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

Click the taskbar clock to open a 440×320 pixel calendar popup centered on screen:

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

**Features:**
- Current day highlighted with colored background
- Month/year header with `<` and `>` navigation arrows
- Full date line (e.g., "Thursday, February 6, 2026")
- Time display with seconds (updates each redraw)
- Handles leap years and all month lengths correctly
- Uses Zeller's congruence for weekday calculation

**Close the popup:**
- Click outside the calendar area
- Press Escape

### Desktop Icons

Clickable icons on the desktop that launch applications:

| Icon | Application |
|------|-------------|
| Terminal | Opens a GUI terminal window running the shell |
| Notepad | Opens a text editor window |

---

## GUI Terminal

The terminal application runs the shell inside a graphical window:

| Property | Value |
|----------|-------|
| Default size | 560 × 320 pixels |
| Character buffer | 80 columns × 50 rows |
| Background | Dark (`#141418`) |
| Default text | Light gray on dark background |
| Color support | 16 foreground + 8 background colors via ANSI codes |
| Cursor | Blinking vertical bar, 500ms toggle |
| Font zoom | Ctrl+`+` / Ctrl+`-` to scale 1×, 2×, 3× |

### How It Works

1. Shell writes to a **character buffer** (80×50) instead of VGA text memory
2. **ANSI escape sequences** are parsed and stripped from the output stream
3. A **parallel color buffer** stores per-character foreground/background VGA color indices
4. Terminal app **renders** each character using its individual color, mapped to the Mode 13h palette
5. Keyboard events are **forwarded** from the desktop event loop to the shell
6. Shell has a **dual output mode**: text (VGA) or GUI (character buffer)

### Color Rendering

Each character cell in the terminal has:
- **Character** — The ASCII character to display
- **Foreground color** — VGA color index (0–15), mapped to 32bpp XRGB via `ansi_vga_to_palette()`
- **Background color** — VGA color index (0–7), rendered as a colored rectangle behind the character

The ANSI parser handles:
- `\e[30m` – `\e[37m` — Standard foreground colors
- `\e[40m` – `\e[47m` — Background colors
- `\e[90m` – `\e[97m` — Bright foreground colors
- `\e[0m` — Reset to defaults
- `\e[1m` — Bold (bright foreground)
- `\e[2J` — Clear screen
- `\e[H` — Cursor home

### Interaction

- Click the terminal window to focus it
- Type commands as normal — they appear in the terminal window
- All shell commands work, including `ed`, `cupid`, debug commands, etc.
- Scroll support for output longer than the visible area

---

## Mouse

### PS/2 Mouse Driver

- **IRQ12** interrupt-driven
- 3-byte packet protocol with sign extension and overflow protection
- Button tracking: left, right, middle

### Cursor

- 8×10 pixel arrow bitmap with outline for visibility
- **Save-under buffer** — saves the pixels beneath the cursor for non-destructive rendering
- Position clamped to screen bounds (0–639 horizontal, 0–479 vertical)

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

- [Shell Commands](Shell-Commands) — Commands available in the terminal
- [Process Management](Process-Management) — How the desktop integrates with the scheduler
- [Architecture](Architecture) — System component overview
