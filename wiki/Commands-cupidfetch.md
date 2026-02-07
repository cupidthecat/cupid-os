# cupidfetch

Display system information with ASCII art in a neofetch-style format.

## Synopsis

```
cupidfetch
```

## Description

`cupidfetch` displays system information alongside a colorful ASCII art cat mascot. The output includes:

- OS and kernel version
- Shell name
- Display resolution and color depth
- Terminal type (GUI or VGA Text)

The command also displays a color palette showing all 16 ANSI colors.

## Examples

```
> cupidfetch
   /\_/\
  ( o.o )  cupid-os
   > ^ <   -----------
  /|   |\
 (_|   |_)
OS: cupid-os x86
Kernel: 1.0.0
Shell: cupid shell
Display: 320x200 256c
Term: GUI
████████
████████
```

## Implementation

`cupidfetch` is implemented as a CupidC program (`/bin/cupidfetch.cc`) and demonstrates:

- ANSI color codes using `\x1B` hexadecimal escapes
- String concatenation and formatting
- While loops for color palette generation
- Conditional statements

## Source Code Location

`/bin/cupidfetch.cc`

## See Also

- [CupidC Language Reference](CupidC-Language-Reference.md)
- [Commands Overview](Commands.md)

## Version History

- **1.0.0** - Initial CupidC implementation
  - Basic system information display
  - ASCII art cat mascot
  - ANSI color support
  - Color palette display
