# CupidDoc CTXT

CupidOS renders `.ctxt` files in Notepad through a TempleOS-inspired subset of
DolDoc. Documents can contain formatted text, executable actions, code blocks,
widgets, and images.

The implementation uses these files from the checked-in TempleOS tree as
design references:

- `TempleOS/Doc/DolDocOverview.DD`
- `TempleOS/Doc/Widget.DD`
- `TempleOS/Demo/DolDoc/ClickCallBack.HC`
- `TempleOS/Demo/DolDoc/MenuSprite.HC`

## What is implemented

- Rendered `.ctxt` pages support headings, boxes, inline styles, and file links.
- Markdown links can trigger `open:`, `shell:`, or `repl:` actions inline.
- `>code` / `>endcode` creates a runnable CupidC code block.
- Code blocks execute when you click the header `Run` button or the code body itself.
- `>button` creates a clickable document button.
- `>tree` / `>endtree` creates a collapsible widget section.
- `>sprite` renders a BMP image inline in the document.
- `>sprite ... | action` attaches an action to the sprite, as in TempleOS `MenuSprite.HC`.
- Notepad shows status feedback after document-triggered execution.

## Action model

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

Like TempleOS tree widgets, `.CTXT` trees are collapsed unless explicitly
opened.

### Sprite

```text
>sprite /docs/image.bmp
>sprite /docs/image.bmp 160 120
>sprite /docs/image.bmp 96 96 | open:04CUPIDC.CTXT
>sprite /docs/image.bmp 96 96 | repl:6 * 7;
```

Sprites use BMP files and render inline. The in-OS manuals embed `image.bmp` at
`/docs/image.bmp` for the examples.

When an action suffix is present, the whole sprite becomes clickable.

## Usage in the shipped manuals

Every manual under `/docs/*.ctxt` uses the DolDoc-lite renderer. This table
records the features exercised by each file.

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

The Makefile and `kernel/util/docs_programs_gen.c` install the top-level
`image.bmp` file as `/docs/image.bmp` at boot. The filesystem and embedding
manuals use the same asset, covering both document rendering and boot-time
asset installation.

## Notes

- Notepad hosts `.ctxt` files; there is no separate DolDoc application.
- CupidDoc implements a DolDoc-like subset rather than full TempleOS `$...$` command coverage.
- The directives remain readable in raw source form.
