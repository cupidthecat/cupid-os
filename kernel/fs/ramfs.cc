/**
 * ramfs.cc - In-memory filesystem for Cupid OS
 *
 * Implements a simple RAM-based filesystem with directory tree support.
 * Files are stored in dynamically allocated memory.  Used for root
 * filesystem (/), /bin, and /tmp mount points.
*/

#include "ramfs.h"
#include "vfs.h"
#include "string.h"
#include "memory.h"
#include "serial.h"


typedef struct ramfs_node {
    char     name[VFS_MAX_NAME];
    uint8_t  type;           /* VFS_TYPE_FILE or VFS_TYPE_DIR */
    uint8_t *data;           /* File content (NULL for dirs) */
    uint32_t size;           /* File size in bytes */
    uint32_t capacity;       /* Allocated capacity */
    uint32_t open_count;

    struct ramfs_node *parent;
    struct ramfs_node *children;  /* First child (dirs only) */
    struct ramfs_node *next;      /* Next sibling */
} ramfs_node_t;


typedef struct {
    ramfs_node_t *root;
} ramfs_t;


typedef struct {
    ramfs_node_t *node;
    uint32_t      position;
    uint32_t      flags;
    ramfs_node_t *readdir_cur;  /* For directory enumeration */
} ramfs_handle_t;

/*  *  Internal helpers
 **/

static int ramfs_reserved_component(const char *component, size_t length) {
    return (length == 1u && component[0] == '.') ||
           (length == 2u && component[0] == '.' && component[1] == '.');
}

static ramfs_node_t *ramfs_alloc_node(const char *name, uint8_t type) {
    ramfs_node_t *n = kmalloc(sizeof(ramfs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(ramfs_node_t));

    size_t i = 0;
    while (name[i] && i < VFS_MAX_NAME - 1) {
        n->name[i] = name[i];
        i++;
    }
    n->name[i] = '\0';
    n->type = type;
    return n;
}

/**
 * Look up a child node by name within a directory.
*/
static ramfs_node_t *ramfs_find_child(ramfs_node_t *dir,
                                      const char *name, size_t len) {
    ramfs_node_t *child = dir->children;
    while (child) {
        size_t nlen = strlen(child->name);
        if (nlen == len && strncmp(child->name, name, len) == 0) {
            return child;
        }
        child = child->next;
    }
    return NULL;
}

/**
 * Walk a relative path from `dir` to find a node.
 * Path components separated by '/'.  Empty path returns `dir`.
*/
static ramfs_node_t *ramfs_lookup(ramfs_node_t *dir, const char *path) {
    if (!path || path[0] == '\0') return dir;

    ramfs_node_t *cur = dir;
    const char *p = path;

    while (*p) {
        /* Skip leading slashes */
        while (*p == '/') p++;
        if (*p == '\0') break;

        /* Find end of this component */
        const char *end = p;
        while (*end && *end != '/') end++;
        size_t len = (size_t)(end - p);

        if (len == 0u || len >= VFS_MAX_NAME ||
            ramfs_reserved_component(p, len) ||
            cur->type != VFS_TYPE_DIR) return NULL;

        cur = ramfs_find_child(cur, p, len);
        if (!cur) return NULL;

        p = end;
    }

    return cur;
}

static void ramfs_free_node(ramfs_node_t *node) {
    while (node) {
        ramfs_node_t *next = node->next;
        if (node->children) {
            ramfs_free_node(node->children);
            node->children = NULL;
        }
        if (node->data) {
            kfree(node->data);
            node->data = NULL;
        }
        kfree(node);
        node = next;
    }
}

/**
 * Ensure parent directories exist for a path, create them if needed.
 * Returns the parent directory node.
*/
static ramfs_node_t *ramfs_mkdirs(ramfs_node_t *root, const char *path,
                                  const char **filename) {
    ramfs_node_t *cur = root;
    const char *p = path;

    while (*p == '/') p++;

    while (*p) {
        /* Find end of component */
        const char *end = p;
        while (*end && *end != '/') end++;

        /* Is there more after this? */
        const char *after = end;
        while (*after == '/') after++;

        size_t len = (size_t)(end - p);
        if (len == 0u || len >= VFS_MAX_NAME ||
            ramfs_reserved_component(p, len)) return NULL;

        if (*after == '\0') {
            /* This is the filename */
            if (filename) *filename = p;
            return cur;
        }

        /* This is a directory component */
        if (cur->type != VFS_TYPE_DIR) return NULL;
        ramfs_node_t *child = ramfs_find_child(cur, p, len);
        if (!child) {
            /* Create directory */
            char dirname[VFS_MAX_NAME];
            size_t i;
            for (i = 0; i < len && i < VFS_MAX_NAME - 1; i++) {
                dirname[i] = p[i];
            }
            dirname[i] = '\0';

            child = ramfs_alloc_node(dirname, VFS_TYPE_DIR);
            if (!child) return NULL;
            child->parent = cur;
            child->next = cur->children;
            cur->children = child;
        }
        cur = child;
        p = after;
    }

    if (filename) *filename = "";
    return cur;
}

/*  *  VFS operations implementation
 **/

static int ramfs_mount(const char *source, void **fs_private) {
    (void)source;

    ramfs_t *fs = kmalloc(sizeof(ramfs_t));
    if (!fs) return VFS_EIO;
    memset(fs, 0, sizeof(ramfs_t));

    /* Create root directory node */
    fs->root = ramfs_alloc_node("", VFS_TYPE_DIR);
    if (!fs->root) {
        kfree(fs);
        return VFS_EIO;
    }

    *fs_private = fs;
    return VFS_OK;
}

static int ramfs_unmount(void *fs_private) {
    ramfs_t *fs = (ramfs_t *)fs_private;
    if (!fs) return VFS_OK;
    if (fs->root) {
        ramfs_free_node(fs->root);
        fs->root = NULL;
    }
    kfree(fs);
    return VFS_OK;
}

static int ramfs_open(void *fs_private, const char *path,
                      uint32_t flags, void **file_handle) {
    ramfs_t *fs = (ramfs_t *)fs_private;
    ramfs_node_t *node = ramfs_lookup(fs->root, path);
    ramfs_handle_t *h = kmalloc(sizeof(ramfs_handle_t));
    if (!h) return VFS_ENOSPC;
    memset(h, 0, sizeof(ramfs_handle_t));

    if (!node && (flags & O_CREAT)) {
        /* Create the file */
        const char *fname = NULL;
        ramfs_node_t *parent = ramfs_mkdirs(fs->root, path, &fname);
        if (!parent || !fname || fname[0] == '\0') {
            kfree(h);
            return VFS_EINVAL;
        }

        /* Extract just the filename (no slashes) */
        char name[VFS_MAX_NAME];
        size_t i = 0;
        while (fname[i] && fname[i] != '/' && i < VFS_MAX_NAME - 1) {
            name[i] = fname[i];
            i++;
        }
        name[i] = '\0';

        node = ramfs_alloc_node(name, VFS_TYPE_FILE);
        if (!node) {
            kfree(h);
            return VFS_ENOSPC;
        }
        node->parent = parent;
        node->next = parent->children;
        parent->children = node;
    }

    if (!node) {
        kfree(h);
        return VFS_ENOENT;
    }

    /* Truncate if requested */
    if ((flags & O_TRUNC) && node->type == VFS_TYPE_FILE) {
        if (node->open_count > 0u) {
            kfree(h);
            return VFS_EBUSY;
        }
        if (node->data) {
            kfree(node->data);
            node->data = NULL;
        }
        node->size = 0;
        node->capacity = 0;
    }

    h->node = node;
    h->position = (flags & O_APPEND) ? node->size : 0;
    h->flags = flags;
    h->readdir_cur = (node->type == VFS_TYPE_DIR) ? node->children : NULL;
    node->open_count++;

    *file_handle = h;
    return VFS_OK;
}

static int ramfs_close(void *file_handle) {
    if (file_handle) {
        ramfs_handle_t *h = (ramfs_handle_t *)file_handle;
        if (h->node && h->node->open_count > 0u) {
            h->node->open_count--;
        }
        kfree(h);
    }
    return VFS_OK;
}

static int ramfs_read(void *file_handle, void *buffer, uint32_t count) {
    ramfs_handle_t *h = (ramfs_handle_t *)file_handle;
    if (!h || !h->node) return VFS_EINVAL;
    if (h->node->type == VFS_TYPE_DIR) return VFS_EISDIR;

    if (h->position >= h->node->size) return 0; /* EOF */

    uint32_t avail = h->node->size - h->position;
    if (count > avail) count = avail;

    memcpy(buffer, h->node->data + h->position, count);
    h->position += count;
    return (int)count;
}

static int ramfs_write(void *file_handle, const void *buffer,
                       uint32_t count) {
    ramfs_handle_t *h = (ramfs_handle_t *)file_handle;
    if (!h || !h->node) return VFS_EINVAL;
    if (h->node->type == VFS_TYPE_DIR) return VFS_EISDIR;

    if (count > 0xffffffffu - h->position) return VFS_ENOSPC;
    uint32_t end = h->position + count;
    if (end > RAMFS_MAX_DATA) return VFS_ENOSPC;

    /* Grow buffer if needed */
    if (end > h->node->capacity) {
        uint32_t new_cap = end > RAMFS_MAX_DATA / 2u
                               ? RAMFS_MAX_DATA
                               : end * 2u;
        if (new_cap < 256) new_cap = 256;
        if (new_cap > RAMFS_MAX_DATA) new_cap = RAMFS_MAX_DATA;

        uint8_t *new_data = kmalloc(new_cap);
        if (!new_data) return VFS_ENOSPC;

        if (h->node->data && h->node->size > 0) {
            memcpy(new_data, h->node->data, h->node->size);
        }
        /* Zero the new part */
        if (new_cap > h->node->size) {
            memset(new_data + h->node->size, 0, new_cap - h->node->size);
        }

        if (h->node->data) kfree(h->node->data);
        h->node->data = new_data;
        h->node->capacity = new_cap;
    }

    memcpy(h->node->data + h->position, buffer, count);
    h->position = end;
    if (end > h->node->size) h->node->size = end;

    return (int)count;
}

static int ramfs_seek(void *file_handle, int32_t offset, int whence) {
    ramfs_handle_t *h = (ramfs_handle_t *)file_handle;
    if (!h || !h->node) return VFS_EINVAL;

    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)h->position; break;
        case SEEK_END: base = (int64_t)h->node->size; break;
        default: return VFS_EINVAL;
    }
    int64_t new_pos = base + (int64_t)offset;
    if (new_pos < 0) new_pos = 0;
    if (new_pos > RAMFS_MAX_DATA) return VFS_ENOSPC;
    h->position = (uint32_t)new_pos;
    return (int)h->position;
}

static int ramfs_stat(void *fs_private, const char *path,
                      vfs_stat_t *st) {
    ramfs_t *fs = (ramfs_t *)fs_private;
    ramfs_node_t *node = ramfs_lookup(fs->root, path);
    if (!node) return VFS_ENOENT;

    st->size = node->size;
    st->type = node->type;
    return VFS_OK;
}

static int ramfs_readdir(void *file_handle, vfs_dirent_t *dirent) {
    ramfs_handle_t *h = (ramfs_handle_t *)file_handle;
    if (!h || !h->node) return VFS_EINVAL;
    if (h->node->type != VFS_TYPE_DIR) return VFS_ENOTDIR;

    if (!h->readdir_cur) return 0; /* No more entries */

    ramfs_node_t *child = h->readdir_cur;
    size_t i = 0;
    while (child->name[i] && i < VFS_MAX_NAME - 1) {
        dirent->name[i] = child->name[i];
        i++;
    }
    dirent->name[i] = '\0';
    dirent->size = child->size;
    dirent->type = child->type;

    h->readdir_cur = child->next;
    return 1; /* Got an entry */
}

static int ramfs_mkdir_op(void *fs_private, const char *path) {
    ramfs_t *fs = (ramfs_t *)fs_private;

    /* Check if it already exists */
    ramfs_node_t *existing = ramfs_lookup(fs->root, path);
    if (existing) return VFS_EEXIST;

    const char *fname = NULL;
    ramfs_node_t *parent = ramfs_mkdirs(fs->root, path, &fname);
    if (!parent || !fname || fname[0] == '\0') return VFS_EINVAL;

    char name[VFS_MAX_NAME];
    size_t i = 0;
    while (fname[i] && fname[i] != '/' && i < VFS_MAX_NAME - 1) {
        name[i] = fname[i];
        i++;
    }
    name[i] = '\0';

    ramfs_node_t *node = ramfs_alloc_node(name, VFS_TYPE_DIR);
    if (!node) return VFS_ENOSPC;
    node->parent = parent;
    node->next = parent->children;
    parent->children = node;

    return VFS_OK;
}

static int ramfs_unlink(void *fs_private, const char *path) {
    ramfs_t *fs = (ramfs_t *)fs_private;
    ramfs_node_t *node = ramfs_lookup(fs->root, path);
    if (!node) return VFS_ENOENT;
    if (node == fs->root) return VFS_EINVAL;
    if (node->type == VFS_TYPE_DIR && node->children) return VFS_EINVAL;

    /* Remove from parent's children list */
    ramfs_node_t *parent = node->parent;
    if (node->open_count > 0u ||
        (parent && parent->open_count > 0u)) return VFS_EBUSY;
    if (parent) {
        if (parent->children == node) {
            parent->children = node->next;
        } else {
            ramfs_node_t *prev = parent->children;
            while (prev && prev->next != node) prev = prev->next;
            if (prev) prev->next = node->next;
        }
    }

    /* Free data */
    if (node->data) kfree(node->data);
    kfree(node);
    return VFS_OK;
}

static void ramfs_detach_node(ramfs_node_t *node) {
    ramfs_node_t *parent = node ? node->parent : NULL;
    if (!parent) return;
    if (parent->children == node) {
        parent->children = node->next;
    } else {
        ramfs_node_t *previous = parent->children;
        while (previous && previous->next != node) previous = previous->next;
        if (previous) previous->next = node->next;
    }
    node->parent = NULL;
    node->next = NULL;
}

static ramfs_node_t *ramfs_existing_parent(ramfs_node_t *root,
                                           const char *path,
                                           const char **filename,
                                           size_t *filename_length) {
    ramfs_node_t *current = root;
    const char *component = path;
    while (*component == '/') component++;

    while (*component) {
        const char *end = component;
        while (*end && *end != '/') end++;
        const char *after = end;
        while (*after == '/') after++;
        size_t length = (size_t)(end - component);
        if (length == 0u || length >= VFS_MAX_NAME ||
            ramfs_reserved_component(component, length)) return NULL;
        if (*after == '\0') {
            if (*end == '/' || current->type != VFS_TYPE_DIR) return NULL;
            *filename = component;
            *filename_length = length;
            return current;
        }
        if (current->type != VFS_TYPE_DIR) return NULL;
        current = ramfs_find_child(current, component, length);
        if (!current || current->type != VFS_TYPE_DIR) return NULL;
        component = after;
    }
    return NULL;
}

static int ramfs_rename(void *fs_private, const char *old_path,
                        const char *new_path) {
    ramfs_t *fs = (ramfs_t *)fs_private;
    ramfs_node_t *source = ramfs_lookup(fs->root, old_path);
    ramfs_node_t *destination = ramfs_lookup(fs->root, new_path);
    const char *filename = NULL;
    char new_name[VFS_MAX_NAME];
    size_t name_length = 0;

    if (!source) return VFS_ENOENT;
    if (source == fs->root || source->type == VFS_TYPE_DIR) return VFS_EISDIR;
    if (destination == source) return VFS_OK;
    if (destination && destination->type == VFS_TYPE_DIR) return VFS_EISDIR;

    ramfs_node_t *new_parent = ramfs_existing_parent(
        fs->root, new_path, &filename, &name_length);
    if (!new_parent || !filename || name_length == 0u) return VFS_ENOENT;
    if ((source->parent && source->parent->open_count > 0u) ||
        (destination && (destination->open_count > 0u ||
                         (destination->parent &&
                          destination->parent->open_count > 0u)))) {
        return VFS_EBUSY;
    }
    for (size_t index = 0u; index < name_length; index++) {
        new_name[index] = filename[index];
    }
    new_name[name_length] = '\0';

    ramfs_detach_node(source);
    if (destination) {
        ramfs_detach_node(destination);
        if (destination->data) kfree(destination->data);
        kfree(destination);
    }
    strcpy(source->name, new_name);
    source->parent = new_parent;
    source->next = new_parent->children;
    new_parent->children = source;
    return VFS_OK;
}

/*  *  VFS operations struct
 **/

static vfs_fs_ops_t ramfs_ops = {
    .name     = "ramfs",
    .mount    = ramfs_mount,
    .unmount  = ramfs_unmount,
    .open     = ramfs_open,
    .close    = ramfs_close,
    .read     = ramfs_read,
    .write    = ramfs_write,
    .seek     = ramfs_seek,
    .stat     = ramfs_stat,
    .readdir  = ramfs_readdir,
    .mkdir    = ramfs_mkdir_op,
    .unlink   = ramfs_unlink,
    .rename   = ramfs_rename
};

vfs_fs_ops_t *ramfs_get_ops(void) {
    return &ramfs_ops;
}

/*  *  Public helper: add pre-populated file
 **/

int ramfs_add_file(void *fs_private, const char *path,
                   const void *data, uint32_t size) {
    ramfs_t *fs = (ramfs_t *)fs_private;
    if (!fs || !path) return VFS_EINVAL;

    const char *fname = NULL;
    ramfs_node_t *parent = ramfs_mkdirs(fs->root, path, &fname);
    if (!parent || !fname || fname[0] == '\0') return VFS_EINVAL;

    char name[VFS_MAX_NAME];
    size_t i = 0;
    while (fname[i] && fname[i] != '/' && i < VFS_MAX_NAME - 1) {
        name[i] = fname[i];
        i++;
    }
    name[i] = '\0';

    /* Check if already exists */
    size_t nlen = strlen(name);
    ramfs_node_t *existing = ramfs_find_child(parent, name, nlen);
    if (existing) return VFS_EEXIST;

    ramfs_node_t *node = ramfs_alloc_node(name, VFS_TYPE_FILE);
    if (!node) return VFS_ENOSPC;

    if (data && size > 0) {
        node->data = kmalloc(size);
        if (!node->data) {
            kfree(node);
            return VFS_ENOSPC;
        }
        memcpy(node->data, data, size);
        node->size = size;
        node->capacity = size;
    }

    node->parent = parent;
    node->next = parent->children;
    parent->children = node;

    return VFS_OK;
}
