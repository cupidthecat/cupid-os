# rm

Remove (delete) files from the filesystem.

## Synopsis

```
rm <file1> [file2] [file3] ...
```

## Description

`rm` removes (deletes) one or more files from the filesystem. The command accepts multiple filenames as arguments and will attempt to delete each one.

**Warning**: Deleted files cannot be recovered! Use with caution.

## Arguments

- `file1, file2, ...`: One or more files to delete. Paths can be absolute or relative to the current working directory.

## Return Values

For each file, `rm` will either successfully delete it or report an error:

- **Success**: File is removed from the filesystem
- **Error codes**:
  - `-2`: No such file or directory
  - `-13`: Permission denied
  - `-21`: Is a directory (use `rmdir` for directories)
  - Other: Error code number displayed

## Examples

### Delete a single file

```
> rm test.txt
```

### Delete multiple files

```
> rm file1.txt file2.txt file3.txt
Removed 3 files
```

### Handle errors

```
> rm nonexistent.txt
rm: cannot remove 'nonexistent.txt': No such file or directory
```

### Mixed success and errors

```
> rm good.txt bad.txt another.txt
rm: cannot remove 'bad.txt': No such file or directory
Removed 2 files, 1 error
```

## Implementation Details

`rm` is implemented in CupidC (`/bin/rm.cc`) and demonstrates:

### Token Parsing

Custom `parse_token()` function to split space-separated arguments:

```c
int parse_token(char *str, int start, char *out, int maxlen) {
    // Skip whitespace, extract token, return length
}
```

This function handles:
- Leading/trailing whitespace
- Multiple arguments in a single string
- Null termination

### VFS Integration

Uses `vfs_unlink(path)` syscall to delete files:

```c
int result = vfs_unlink(path);
if (result == 0) {
    // Success
} else {
    // Handle error codes
}
```

### Error Handling

Comprehensive error reporting with human-readable messages for common POSIX error codes.

## Source Code Location

`/bin/rm.cc`

## See Also

- [vfs_unlink](VFS-API.md#vfs_unlink) - VFS unlink syscall
- [CupidC Language Reference](CupidC-Language-Reference.md)
- `ls` - List directory contents
- `cat` - Display file contents

## Limitations

- Cannot remove directories (use `rmdir` instead)
- No `-r` recursive flag (yet)
- No `-f` force flag (yet)
- No wildcard support (yet)

## Version History

- **1.0.0** - Initial CupidC implementation
  - Multi-file deletion support
  - Path resolution
  - Error code handling with human-readable messages
  - Summary output for multiple files
