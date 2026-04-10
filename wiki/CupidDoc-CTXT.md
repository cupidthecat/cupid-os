# CupidDoc CTXT

CupidOS now ships a stronger TempleOS-inspired DolDoc layer on top of `.ctxt`
files in Notepad render mode. The implementation is still intentionally
bounded, but it now behaves much more like a live document system instead of a
static formatter.

North-star references from the checked-in TempleOS source:

- `TempleOS/Doc/DolDocOverview.DD`
- `TempleOS/Doc/Widget.DD`
- `TempleOS/Demo/DolDoc/ClickCallBack.HC`
- `TempleOS/Demo/DolDoc/MenuSprite.HC`

## What Is Implemented

- Rendered `.ctxt` pages still support headings, boxes, inline styles, and file links.
- Markdown links can now trigger `open:`, `shell:`, or `repl:` actions inline.
- `>code` / `>endcode` creates a runnable CupidC code block.
- Code blocks execute when you click the header `Run` button or the code body itself.
- `>button` creates a clickable document button.
- `>tree` / `>endtree` creates a collapsible widget section.
- `>sprite` renders a BMP image inline in the document.
- `>sprite ... | action` turns that sprite into a live widget, closer to TempleOS `MenuSprite.HC`.
- Notepad shows status feedback after document-triggered execution.

## Action Model

Buttons, markdown links, and code blocks use one of these prefixes:

- `open:<path>` opens another file in Notepad render/source mode.
- `shell:<command>` sends a command through the shell dispatcher.
- `repl:<cupidc>` evaluates CupidC through the live shell REPL state.

If no prefix is given on `>button`, the action defaults to `shell:`.

## Syntax

### Inline action links

```text
[Open Shell Guide](open:01SHELL.CTXT)
[List Docs](shell:ls /docs)
[Evaluate Math](repl:6 * 7;)
```

### Runnable CupidC block

```text
>code Run In REPL
U32 x = 7;
x + 5;
>endcode
```

Clicking the header button or the code body evaluates the whole snippet through
`repl_eval()`, so globals, functions, structs, and `ans` follow the same
persistent REPL behavior as the normal shell prompt.

### Button widget

```text
>button Open Shell Guide | open:01SHELL.CTXT
>button Run Calculator | repl:3 + 4;
>button Show Jobs | shell:jobs
```

### Tree widget

```text
>tree open Getting Started
This section starts expanded.
>endtree

>tree Advanced Notes
This one starts collapsed.
>endtree
```

TempleOS tree widgets default to collapsed unless opened explicitly; `.CTXT`
follows that same basic behavior.

### Sprite

```text
>sprite /docs/image.bmp
>sprite /docs/image.bmp 160 120
>sprite /docs/image.bmp 96 96 | open:04CUPIDC.CTXT
>sprite /docs/image.bmp 96 96 | repl:6 * 7;
```

Current sprite support is BMP-backed and renders inline inside the document.
The in-OS manual set embeds `image.bmp` at `/docs/image.bmp` so examples work
after boot.

When an action suffix is present, the whole sprite becomes clickable.

## Usage In The Shipped Manuals

Every manual under `/docs/*.ctxt` now uses the DolDoc-lite engine. The table
below shows which features each file exercises, so the engine is discoverable
from any manual.

| Manual                  | Trees | Code blocks | Buttons | Sprites | Inline links |
|-------------------------|:-----:|:-----------:|:-------:|:-------:|:------------:|
| `00INDEX.CTXT`          |   *   |      *      |    *    |    *    |      *       |
| `01SHELL.CTXT`          |   *   |      *      |    *    |    *    |      *       |
| `02FILESYS.CTXT`        |   *   |             |    *    |    *    |      *       |
| `03CUPIDSC.CTXT`        |   *   |             |    *    |         |      *       |
| `04CUPIDC.CTXT`         |   *   |      *      |    *    |    *    |      *       |
| `05ELFPROG.CTXT`        |   *   |             |    *    |         |      *       |
| `06GUITERM.CTXT`        |   *   |             |    *    |    *    |      *       |
| `07LABS.CTXT`           |   *   |      *      |    *    |    *    |      *       |
| `08CMDS.CTXT`           |   *   |             |    *    |         |      *       |
| `09EMBED.CTXT`          |   *   |      *      |    *    |    *    |      *       |
| `10ASM.CTXT`            |   *   |             |    *    |         |      *       |
| `11CTXT.CTXT`           |   *   |      *      |    *    |    *    |      *       |
| `12HOLYC-CUPIDC.CTXT`   |   *   |      *      |         |         |      *       |

The sprite asset is `/docs/image.bmp`, installed at boot from the top-level
`image.bmp` file via the Makefile and `kernel/docs_programs_gen.c`. The
filesystem and embedding manuals now use that same asset too, so the examples
exercise both live document rendering and the boot-time doc asset pipeline.

## Notes

- Notepad remains the host for `.ctxt`; there is no separate DolDoc app.
- This is a DolDoc-like subset, not full TempleOS `$...$` command coverage.
- The implementation is source-readable on purpose: docs still stay clean in raw text form.
