# Desktop Environment

cupid-os features a complete graphical desktop environment built on VGA Mode 13h (320×200, 256 colors). It includes a window manager, desktop shell with taskbar and icons, a mouse-driven GUI terminal, and a notepad application.

---

## Overview

The desktop is the default interface after boot. It provides:

- **Pastel-themed background** (OsakaOS-inspired aesthetic)
- **Taskbar** at the bottom with "cupid-os" branding and window buttons
- **Clickable desktop icons** that launch applications
- **Draggable overlapping windows** with close buttons
- **Mouse cursor** with outline for visibility

---

## VGA Graphics

### Mode 13h

| Property | Value |
|----------|-------|
| Resolution | 320 × 200 pixels |
| Colors | 256 (indexed) |
| Framebuffer | Linear at 0xA0000 |
| Buffering | Double-buffered (heap-allocated back buffer) |

The color palette is programmed with soft pastel colors for the UI:

| Index | Color | Use |
|-------|-------|-----|
| 0 | Black | Terminal background |
| 1 | White | Text |
| 2 | Light pink | Desktop background |
| 3 | Soft cyan | Focused title bars |
| 4 | Light gray | Unfocused title bars |
| 5 | Dark gray | Window borders |
| ... | ... | Various UI elements |

### Graphics Primitives

- **Pixel** — Plot with bounds clipping
- **Line** — Bresenham's algorithm for arbitrary angles; fast `memset` for horizontal/vertical
- **Rectangle** — Filled and outlined
- **Text** — 8×8 monospaced bitmap font (ASCII 0–127)

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

The taskbar sits at the bottom of the screen (20 pixels high):

- Left side: "cupid-os" branding text
- Right side: Buttons for each open window (click to focus)
- Active window is highlighted

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
| Default size | 280 × 160 pixels |
| Character buffer | 80 columns × 50 rows |
| Background | Dark (terminal-style) |
| Text | White on dark background |
| Cursor | Underline at current position |

### How It Works

1. Shell writes to a **character buffer** (80×50) instead of VGA text memory
2. Terminal app **renders** the buffer as graphical text inside its window
3. Keyboard events are **forwarded** from the desktop event loop to the shell
4. Shell has a **dual output mode**: text (VGA) or GUI (character buffer)

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
- Position clamped to screen bounds (0–319 horizontal, 0–199 vertical)

### Mouse Interactions

| Action | Result |
|--------|--------|
| Click window title bar | Focus window, start drag |
| Drag title bar | Move window |
| Click window "X" | Close window |
| Click window body | Focus window |
| Click desktop icon | Launch application |
| Click taskbar button | Focus corresponding window |

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
