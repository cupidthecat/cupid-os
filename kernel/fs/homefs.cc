/**
 * homefs.cc - Native persistent filesystem for Cupid OS /home
 *
 * Provides a tree-based filesystem with nested directories and long names,
 * while persisting all contents into a single FAT16-hosted container file.
 * This keeps FAT16 as a compatibility/storage backend instead of exposing
 * its namespace limitations directly to the OS.
*/

#include "homefs.h"

#include "blockcache.h"
#include "fat16.h"
#include "fat16_control.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"
#include "serial.h"

#define HOMEFS_MAGIC          0x31465348u /* "HFS1" */
#define HOMEFS_VERSION        1u
#define HOMEFS_CONTAINER_NAME "HOMEFS.SYS"
#define HOMEFS_MAX_DEPTH      32u

typedef struct homefs_node {
    char     name[VFS_MAX_NAME];
    uint8_t  type;
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;
    uint32_t open_count;
    uint32_t depth;

    struct homefs_node *parent;
    struct homefs_node *children;
    struct homefs_node *next;
} homefs_node_t;

typedef struct {
    homefs_node_t *root;
    bool           dirty;
    bool           seed_mode;
    uint32_t       batch_depth;
} homefs_t;

typedef struct {
    homefs_t      *fs;
    homefs_node_t *node;
    uint32_t       position;
    uint32_t       flags;
    homefs_node_t *readdir_cur;
} homefs_handle_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t node_count;
} homefs_disk_header_t;

typedef struct {
    uint32_t parent_index;
    uint32_t type;
    uint32_t size;
    uint32_t name_len;
} homefs_disk_node_t;

typedef struct {
    homefs_node_t **nodes;
    uint32_t        count;
    uint32_t        capacity;
} homefs_node_list_t;

typedef struct {
    homefs_t *fs;
    int       status;
} homefs_import_ctx_t;

typedef struct {
    homefs_t      *fs;
    homefs_node_t *dir;
    char           dir_name[VFS_MAX_NAME];
    int            status;
} homefs_import_dir_ctx_t;

static homefs_t *g_homefs = NULL;

static void homefs_detach_node(homefs_node_t *node);
static homefs_node_t *homefs_existing_parent(homefs_node_t *root,
                                             const char *path,
                                             const char **filename,
                                             size_t *filename_length);

static int homefs_reserved_component(const char *component, size_t length) {
    return (length == 1u && component[0] == '.') ||
           (length == 2u && component[0] == '.' && component[1] == '.');
}

static int homefs_valid_name_bytes(const char *name, uint32_t length) {
    if (!name || length == 0u || length >= VFS_MAX_NAME ||
        homefs_reserved_component(name, (size_t)length)) return 0;
    for (uint32_t index = 0u; index < length; index++) {
        if (name[index] == '\0' || name[index] == '/') return 0;
    }
    return 1;
}

static int homefs_checked_add_u32(uint32_t left, uint32_t right,
                                  uint32_t *result) {
    if (!result || right > 0xffffffffu - left) return 0;
    *result = left + right;
    return 1;
}

static homefs_node_t *homefs_alloc_node(const char *name, uint8_t type) {
    homefs_node_t *n = kmalloc(sizeof(homefs_node_t));
    if (!n) return NULL;
    memset(n, 0, sizeof(homefs_node_t));

    size_t i = 0;
    while (name[i] && i < VFS_MAX_NAME - 1) {
        n->name[i] = name[i];
        i++;
    }
    n->name[i] = '\0';
    n->type = type;
    return n;
}

static void homefs_free_node(homefs_node_t *node) {
    while (node) {
        homefs_node_t *next = node->next;
        if (node->children) {
            homefs_free_node(node->children);
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

static homefs_node_t *homefs_find_child(homefs_node_t *dir,
                                        const char *name, size_t len) {
    homefs_node_t *child = dir->children;
    while (child) {
        size_t nlen = strlen(child->name);
        if (nlen == len && strncmp(child->name, name, len) == 0) {
            return child;
        }
        child = child->next;
    }
    return NULL;
}

static homefs_node_t *homefs_lookup(homefs_node_t *dir, const char *path) {
    if (!path || path[0] == '\0') return dir;

    homefs_node_t *cur = dir;
    const char *p = path;

    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;

        const char *end = p;
        while (*end && *end != '/') end++;
        size_t len = (size_t)(end - p);

        if (len == 0u || len >= VFS_MAX_NAME ||
            homefs_reserved_component(p, len) ||
            cur->type != VFS_TYPE_DIR) return NULL;

        cur = homefs_find_child(cur, p, len);
        if (!cur) return NULL;

        p = end;
    }

    return cur;
}

static homefs_node_t *homefs_mkdirs(homefs_node_t *root, const char *path,
                                    const char **filename) {
    homefs_node_t *cur = root;
    const char *p = path;

    while (*p == '/') p++;

    while (*p) {
        const char *end = p;
        while (*end && *end != '/') end++;

        const char *after = end;
        while (*after == '/') after++;

        size_t len = (size_t)(end - p);
        if (len == 0u || len >= VFS_MAX_NAME ||
            homefs_reserved_component(p, len)) return NULL;

        if (*after == '\0') {
            if (cur->depth >= HOMEFS_MAX_DEPTH) return NULL;
            if (filename) *filename = p;
            return cur;
        }

        if (cur->type != VFS_TYPE_DIR) return NULL;
        homefs_node_t *child = homefs_find_child(cur, p, len);
        if (!child) {
            if (cur->depth >= HOMEFS_MAX_DEPTH) return NULL;
            char dirname[VFS_MAX_NAME];
            size_t i;
            for (i = 0; i < len && i < VFS_MAX_NAME - 1; i++) {
                dirname[i] = p[i];
            }
            dirname[i] = '\0';

            child = homefs_alloc_node(dirname, VFS_TYPE_DIR);
            if (!child) return NULL;
            child->parent = cur;
            child->depth = cur->depth + 1u;
            child->next = cur->children;
            cur->children = child;
        }
        cur = child;
        p = after;
    }

    if (filename) *filename = "";
    return cur;
}

static int homefs_node_ensure_capacity(homefs_node_t *node, uint32_t need) {
    if (!node || node->type != VFS_TYPE_FILE) return VFS_EINVAL;
    if (need <= node->capacity) return VFS_OK;

    uint32_t new_cap = node->capacity ? node->capacity : 256u;
    while (new_cap < need) {
        if (new_cap > 0xffffffffu / 2u) {
            new_cap = need;
            break;
        }
        new_cap *= 2u;
    }

    uint8_t *new_data = kmalloc(new_cap);
    if (!new_data) return VFS_ENOSPC;

    if (node->data && node->size > 0) {
        memcpy(new_data, node->data, node->size);
    }
    if (new_cap > node->size) {
        memset(new_data + node->size, 0, new_cap - node->size);
    }

    if (node->data) {
        kfree(node->data);
    }
    node->data = new_data;
    node->capacity = new_cap;
    return VFS_OK;
}

static void homefs_mark_dirty(homefs_t *fs) {
    if (fs && !fs->seed_mode) {
        fs->dirty = true;
    }
}

static int homefs_count_nodes(homefs_node_t *node, uint32_t *count) {
    if (!count) return VFS_EINVAL;
    while (node) {
        if (*count == 0xffffffffu) return VFS_ENOSPC;
        (*count)++;
        if (node->children) {
            int rc = homefs_count_nodes(node->children, count);
            if (rc < 0) return rc;
        }
        node = node->next;
    }
    return VFS_OK;
}

static int homefs_collect_nodes(homefs_node_t *node, homefs_node_list_t *list) {
    while (node) {
        if (list->count >= list->capacity) {
            return VFS_ENOSPC;
        }
        list->nodes[list->count++] = node;
        if (node->children) {
            int rc = homefs_collect_nodes(node->children, list);
            if (rc < 0) return rc;
        }
        node = node->next;
    }
    return VFS_OK;
}

static int homefs_index_of(homefs_node_list_t *list, homefs_node_t *node) {
    for (uint32_t i = 0; i < list->count; i++) {
        if (list->nodes[i] == node) {
            return (int)i;
        }
    }
    return -1;
}

static int homefs_serialize(homefs_t *fs, uint8_t **out_buf,
                            uint32_t *out_size) {
    if (!fs || !fs->root || !out_buf || !out_size) return VFS_EINVAL;

    uint32_t node_count = 0u;
    int rc = homefs_count_nodes(fs->root, &node_count);
    if (rc < 0 || node_count == 0u ||
        node_count > 0xffffffffu / (uint32_t)sizeof(homefs_node_t *)) {
        return VFS_ENOSPC;
    }
    uint32_t nodes_size = node_count * (uint32_t)sizeof(homefs_node_t *);
    homefs_node_t **nodes = kmalloc(nodes_size);
    if (!nodes) return VFS_ENOSPC;

    homefs_node_list_t list;
    list.nodes = nodes;
    list.count = 0;
    list.capacity = node_count;
    rc = homefs_collect_nodes(fs->root, &list);
    if (rc < 0) {
        kfree(nodes);
        return rc;
    }

    uint32_t total = (uint32_t)sizeof(homefs_disk_header_t);
    for (uint32_t i = 0; i < list.count; i++) {
        homefs_node_t *node = list.nodes[i];
        uint32_t name_length = (uint32_t)strlen(node->name);
        if ((node->type != VFS_TYPE_DIR && node->type != VFS_TYPE_FILE) ||
            name_length >= VFS_MAX_NAME ||
            (node->parent &&
             !homefs_valid_name_bytes(node->name, name_length)) ||
            (node->type == VFS_TYPE_FILE && node->size > 0u && !node->data) ||
            !homefs_checked_add_u32(
                total, (uint32_t)sizeof(homefs_disk_node_t), &total) ||
            !homefs_checked_add_u32(total, name_length, &total) ||
            (node->type == VFS_TYPE_FILE &&
             !homefs_checked_add_u32(total, node->size, &total))) {
            kfree(nodes);
            return VFS_ENOSPC;
        }
    }

    uint8_t *buf = kmalloc(total);
    if (!buf) {
        kfree(nodes);
        return VFS_ENOSPC;
    }

    uint32_t pos = 0;
    homefs_disk_header_t hdr;
    hdr.magic = HOMEFS_MAGIC;
    hdr.version = HOMEFS_VERSION;
    hdr.node_count = list.count;
    memcpy(buf + pos, &hdr, sizeof(hdr));
    pos += (uint32_t)sizeof(hdr);

    for (uint32_t i = 0; i < list.count; i++) {
        homefs_node_t *node = list.nodes[i];
        homefs_disk_node_t rec;
        int parent_index = -1;
        if (node->parent) {
            parent_index = homefs_index_of(&list, node->parent);
        }
        rec.parent_index = parent_index >= 0 ? (uint32_t)parent_index : 0xFFFFFFFFu;
        rec.type = node->type;
        rec.size = node->type == VFS_TYPE_FILE ? node->size : 0;
        rec.name_len = (uint32_t)strlen(node->name);
        memcpy(buf + pos, &rec, sizeof(rec));
        pos += (uint32_t)sizeof(rec);
        if (rec.name_len > 0) {
            memcpy(buf + pos, node->name, rec.name_len);
            pos += rec.name_len;
        }
        if (rec.size > 0) {
            memcpy(buf + pos, node->data, rec.size);
            pos += rec.size;
        }
    }

    kfree(nodes);
    *out_buf = buf;
    *out_size = total;
    return VFS_OK;
}

static void homefs_clear_fs(homefs_t *fs) {
    if (!fs) return;
    if (fs->root) {
        homefs_free_node(fs->root);
        fs->root = NULL;
    }
    fs->dirty = false;
}

static int homefs_deserialize(homefs_t *fs, const uint8_t *data, uint32_t size) {
    if (!fs || !data || size < sizeof(homefs_disk_header_t)) return VFS_EINVAL;

    homefs_disk_header_t hdr;
    memcpy(&hdr, data, sizeof(hdr));
    if (hdr.magic != HOMEFS_MAGIC || hdr.version != HOMEFS_VERSION ||
        hdr.node_count == 0) {
        return VFS_EIO;
    }

    uint32_t remaining = size - (uint32_t)sizeof(homefs_disk_header_t);
    if (hdr.node_count >
            remaining / (uint32_t)sizeof(homefs_disk_node_t) ||
        hdr.node_count >
            0xffffffffu / (uint32_t)sizeof(homefs_node_t *)) {
        return VFS_EIO;
    }
    uint32_t nodes_size =
        hdr.node_count * (uint32_t)sizeof(homefs_node_t *);
    homefs_node_t **nodes = kmalloc(nodes_size);
    if (!nodes) return VFS_ENOSPC;
    memset(nodes, 0, nodes_size);

    int failure = VFS_EIO;
    uint32_t pos = (uint32_t)sizeof(hdr);
    for (uint32_t i = 0; i < hdr.node_count; i++) {
        if (pos > size ||
            (uint32_t)sizeof(homefs_disk_node_t) > size - pos) {
            goto fail;
        }

        homefs_disk_node_t rec;
        memcpy(&rec, data + pos, sizeof(rec));
        pos += (uint32_t)sizeof(rec);

        if (pos > size || rec.name_len >= VFS_MAX_NAME ||
            rec.name_len > size - pos) {
            goto fail;
        }

        char name[VFS_MAX_NAME];
        memset(name, 0, sizeof(name));
        if (rec.name_len > 0) {
            memcpy(name, data + pos, rec.name_len);
        }
        name[rec.name_len] = '\0';
        pos += rec.name_len;

        if ((rec.type != VFS_TYPE_DIR && rec.type != VFS_TYPE_FILE) ||
            (rec.type == VFS_TYPE_DIR && rec.size != 0u) ||
            (i == 0u && (rec.type != VFS_TYPE_DIR || rec.name_len != 0u)) ||
            (i != 0u && !homefs_valid_name_bytes(name, rec.name_len))) {
            goto fail;
        }

        homefs_node_t *node = homefs_alloc_node(name, (uint8_t)rec.type);
        if (!node) {
            failure = VFS_ENOSPC;
            goto fail;
        }

        if (rec.type == VFS_TYPE_FILE && rec.size > 0) {
            if (pos > size || rec.size > size - pos) {
                kfree(node);
                goto fail;
            }
            node->data = kmalloc(rec.size);
            if (!node->data) {
                kfree(node);
                failure = VFS_ENOSPC;
                goto fail;
            }
            memcpy(node->data, data + pos, rec.size);
            node->size = rec.size;
            node->capacity = rec.size;
            pos += rec.size;
        }

        nodes[i] = node;
        if (rec.parent_index == 0xFFFFFFFFu) {
            if (i != 0) {
                goto fail;
            }
        } else {
            if (rec.parent_index >= i || !nodes[rec.parent_index] ||
                nodes[rec.parent_index]->type != VFS_TYPE_DIR ||
                nodes[rec.parent_index]->depth >= HOMEFS_MAX_DEPTH ||
                homefs_find_child(nodes[rec.parent_index], name,
                                  rec.name_len)) {
                goto fail;
            }
            node->parent = nodes[rec.parent_index];
            node->depth = node->parent->depth + 1u;
            node->next = node->parent->children;
            node->parent->children = node;
        }
    }

    if (pos != size) goto fail;

    homefs_clear_fs(fs);
    fs->root = nodes[0];
    fs->dirty = false;
    kfree(nodes);
    return VFS_OK;

fail:
    for (uint32_t i = 0; i < hdr.node_count; i++) {
        if (nodes[i]) {
            nodes[i]->next = NULL;
            nodes[i]->children = NULL;
            if (nodes[i]->data) {
                kfree(nodes[i]->data);
                nodes[i]->data = NULL;
            }
            kfree(nodes[i]);
        }
    }
    kfree(nodes);
    return failure;
}

static int homefs_flush(homefs_t *fs) {
    if (!fs || !fs->root) return VFS_EINVAL;
    if (fs->batch_depth > 0u) return VFS_OK;
    if (!fs->dirty) return VFS_OK;

    /* Clear dirty BEFORE the write to break re-entrancy. fat16_write_file
     * calls blockcache_sync() internally, which calls homefs_sync(), which
     * calls back into homefs_flush(). If dirty were still set the inner
     * call would re-flush, allocating a fresh FAT cluster each time and
     * eventually overflowing the kernel stack via unbounded recursion.
     * Restore the flag if the write fails so the data isn't lost.*/
    fs->dirty = false;

    uint8_t *buf = NULL;
    uint32_t size = 0;
    int rc = homefs_serialize(fs, &buf, &size);
    if (rc < 0) {
        fs->dirty = true;
        return rc;
    }

    rc = fat16_write_reserved_file(HOMEFS_CONTAINER_NAME, buf, size);
    kfree(buf);
    if (rc < 0 || (uint32_t)rc != size) {
        fs->dirty = true;
        serial_printf("[homefs] flush failed rc=%d size=%u\n", rc, size);
        return VFS_EIO;
    }

    serial_printf("[homefs] flushed %u bytes to %s\n", size,
                  HOMEFS_CONTAINER_NAME);
    return VFS_OK;
}

static int homefs_read_fat_file(const char *path, uint8_t **out_data,
                                uint32_t *out_size) {
    fat16_file_t *file = NULL;
    int open_status = fat16_open_checked(path, &file);
    if (open_status == FAT16_OPEN_NOT_FOUND) return VFS_ENOENT;
    if (open_status == FAT16_OPEN_NO_HANDLES) return VFS_EMFILE;
    if (open_status == FAT16_OPEN_INVALID) return VFS_EINVAL;
    if (open_status != FAT16_OPEN_OK) return VFS_EIO;

    uint32_t size = file->file_size;
    uint8_t *buf = NULL;
    if (size > 0) {
        buf = kmalloc(size);
        if (!buf) {
            fat16_close(file);
            return VFS_ENOSPC;
        }
        int rd = fat16_read(file, buf, size);
        if (rd < 0 || (uint32_t)rd != size) {
            kfree(buf);
            fat16_close(file);
            return VFS_EIO;
        }
    }

    fat16_close(file);
    *out_data = buf;
    *out_size = size;
    return VFS_OK;
}

static int homefs_strieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int homefs_add_imported_file(homefs_node_t *parent, const char *name,
                                    const uint8_t *data, uint32_t size) {
    size_t nlen = strlen(name);
    if (!homefs_valid_name_bytes(name, (uint32_t)nlen) ||
        parent->depth >= HOMEFS_MAX_DEPTH) return VFS_EINVAL;
    if (homefs_find_child(parent, name, nlen)) return VFS_EEXIST;

    homefs_node_t *node = homefs_alloc_node(name, VFS_TYPE_FILE);
    if (!node) return VFS_ENOSPC;

    if (size > 0) {
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
    node->depth = parent->depth + 1u;
    node->next = parent->children;
    parent->children = node;
    return VFS_OK;
}

static int homefs_import_subdir_entry(const char *name, uint32_t size,
                                      uint8_t attr, void *ctx) {
    homefs_import_dir_ctx_t *ictx = (homefs_import_dir_ctx_t *)ctx;
    if (!ictx || !name || name[0] == '\0') return 0;
    if (ictx->status < 0) return 1;
    if (attr & FAT_ATTR_DIRECTORY) return 0;

    char fat_path[2 * VFS_MAX_NAME];
    int p = 0;
    while (ictx->dir_name[p] && p < (int)sizeof(fat_path) - 2) {
        fat_path[p] = ictx->dir_name[p];
        p++;
    }
    fat_path[p++] = '/';
    int i = 0;
    while (name[i] && p < (int)sizeof(fat_path) - 1) {
        fat_path[p++] = name[i++];
    }
    fat_path[p] = '\0';

    uint8_t *data = NULL;
    uint32_t file_size = 0;
    int status = homefs_read_fat_file(fat_path, &data, &file_size);
    if (status < 0) {
        ictx->status = status;
        return 1;
    }
    (void)size;
    status = homefs_add_imported_file(ictx->dir, name, data, file_size);
    if (data) kfree(data);
    if (status < 0) {
        ictx->status = status;
        return 1;
    }
    return 0;
}

static int homefs_import_root_entry(const char *name, uint32_t size,
                                    uint8_t attr, void *ctx) {
    homefs_import_ctx_t *ictx = (homefs_import_ctx_t *)ctx;
    if (!ictx || !ictx->fs || !name || name[0] == '\0') return 0;
    if (ictx->status < 0) return 1;
    if (homefs_strieq(name, HOMEFS_CONTAINER_NAME)) return 0;
    /* /wads/ holds DOOM IWAD/PWAD files. They stay on FAT16 and are
     * read directly via /disk/wads/<file>. Importing them into the
     * homefs container would inflate HOMEFS.SYS by ~57 MB.*/
    if (homefs_strieq(name, "WADS")) return 0;

    if (attr & FAT_ATTR_DIRECTORY) {
        homefs_node_t *dir = homefs_alloc_node(name, VFS_TYPE_DIR);
        if (!dir) {
            ictx->status = VFS_ENOSPC;
            return 1;
        }
        dir->parent = ictx->fs->root;
        dir->depth = ictx->fs->root->depth + 1u;
        dir->next = ictx->fs->root->children;
        ictx->fs->root->children = dir;

        homefs_import_dir_ctx_t dctx;
        memset(&dctx, 0, sizeof(dctx));
        dctx.fs = ictx->fs;
        dctx.dir = dir;
        dctx.status = VFS_OK;
        strncpy(dctx.dir_name, name, VFS_MAX_NAME - 1);
        int enumerate_status = fat16_enumerate_subdir(
            name, homefs_import_subdir_entry, &dctx);
        if (dctx.status < 0 || enumerate_status < 0) {
            ictx->status = dctx.status < 0 ? dctx.status : VFS_EIO;
            return 1;
        }
        return 0;
    }

    {
        uint8_t *data = NULL;
        uint32_t file_size = 0;
        int status = homefs_read_fat_file(name, &data, &file_size);
        if (status < 0) {
            ictx->status = status;
            return 1;
        }
        (void)size;
        status = homefs_add_imported_file(
            ictx->fs->root, name, data, file_size);
        if (data) kfree(data);
        if (status < 0) {
            ictx->status = status;
            return 1;
        }
    }

    return 0;
}

static int homefs_import_from_fat(homefs_t *fs) {
    homefs_import_ctx_t ctx;
    ctx.fs = fs;
    ctx.status = VFS_OK;
    int enumerate_status = fat16_enumerate_root(
        homefs_import_root_entry, &ctx);
    if (ctx.status < 0) return ctx.status;
    if (enumerate_status < 0) return VFS_EIO;
    homefs_mark_dirty(fs);
    return homefs_flush(fs);
}

static int homefs_mount(const char *source, void **fs_private) {
    (void)source;
    if (!fs_private) return VFS_EINVAL;
    if (g_homefs) return VFS_EBUSY;
    if (!fat16_is_initialized()) {
        if (fat16_init() != 0) {
            return VFS_EIO;
        }
    }

    homefs_t *fs = kmalloc(sizeof(homefs_t));
    if (!fs) return VFS_ENOSPC;
    memset(fs, 0, sizeof(homefs_t));

    fs->root = homefs_alloc_node("", VFS_TYPE_DIR);
    if (!fs->root) {
        kfree(fs);
        return VFS_ENOSPC;
    }

    uint8_t *data = NULL;
    uint32_t size = 0;
    int read_status = homefs_read_fat_file(
        HOMEFS_CONTAINER_NAME, &data, &size);
    if (read_status == VFS_OK) {
        int load_status = (data && size > 0u)
                              ? homefs_deserialize(fs, data, size)
                              : VFS_EIO;
        if (load_status < 0) {
            serial_printf("[homefs] refusing invalid container (%d)\n",
                          load_status);
            if (data) kfree(data);
            homefs_clear_fs(fs);
            kfree(fs);
            return load_status;
        }
        if (data) kfree(data);
    } else if (read_status == VFS_ENOENT) {
        serial_printf("[homefs] no container found, importing FAT16 contents\n");
        int import_status = homefs_import_from_fat(fs);
        if (import_status < 0) {
            homefs_clear_fs(fs);
            kfree(fs);
            return import_status;
        }
    } else {
        homefs_clear_fs(fs);
        kfree(fs);
        return read_status;
    }

    int reserve_status = fat16_reserve_file(HOMEFS_CONTAINER_NAME);
    if (reserve_status < 0) {
        homefs_clear_fs(fs);
        kfree(fs);
        return reserve_status == FAT16_BUSY ? VFS_EBUSY : VFS_EIO;
    }

    g_homefs = fs;
    *fs_private = fs;
    return VFS_OK;
}

static int homefs_unmount(void *fs_private) {
    homefs_t *fs = (homefs_t *)fs_private;
    if (!fs) return VFS_OK;
    if (fs->batch_depth > 0u) return VFS_EBUSY;
    int rc = homefs_flush(fs);
    if (rc < 0) return rc;
    if (fat16_release_file_reservation(HOMEFS_CONTAINER_NAME) < 0)
        return VFS_EIO;
    homefs_clear_fs(fs);
    if (g_homefs == fs) g_homefs = NULL;
    kfree(fs);
    return VFS_OK;
}

static int homefs_open(void *fs_private, const char *path,
                       uint32_t flags, void **file_handle) {
    homefs_t *fs = (homefs_t *)fs_private;
    homefs_node_t *node = homefs_lookup(fs->root, path);
    homefs_handle_t *h = kmalloc(sizeof(homefs_handle_t));
    if (!h) return VFS_EIO;
    memset(h, 0, sizeof(homefs_handle_t));

    if (!node && (flags & O_CREAT)) {
        const char *fname = NULL;
        size_t name_length = 0u;
        homefs_node_t *parent = homefs_existing_parent(
            fs->root, path, &fname, &name_length);
        if (!parent || !fname || name_length == 0u) {
            kfree(h);
            return VFS_ENOENT;
        }

        char name[VFS_MAX_NAME];
        size_t i = 0u;
        while (i < name_length) {
            name[i] = fname[i];
            i++;
        }
        name[i] = '\0';

        node = homefs_alloc_node(name, VFS_TYPE_FILE);
        if (!node) {
            kfree(h);
            return VFS_ENOSPC;
        }
        node->parent = parent;
        node->depth = parent->depth + 1u;
        node->next = parent->children;
        parent->children = node;
        homefs_mark_dirty(fs);
    }

    if (!node) {
        kfree(h);
        return VFS_ENOENT;
    }
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
        homefs_mark_dirty(fs);
    }

    h->fs = fs;
    h->node = node;
    h->flags = flags;
    h->position = (flags & O_APPEND) ? node->size : 0;
    h->readdir_cur = (node->type == VFS_TYPE_DIR) ? node->children : NULL;
    node->open_count++;

    *file_handle = h;
    return VFS_OK;
}

static int homefs_close(void *file_handle) {
    homefs_handle_t *h = (homefs_handle_t *)file_handle;
    int rc = VFS_OK;
    if (h) {
        if (h->node && h->node->open_count > 0u) {
            h->node->open_count--;
        }
        if (h->fs && h->fs->dirty && !h->fs->seed_mode &&
            h->fs->batch_depth == 0u) {
            rc = homefs_flush(h->fs);
        }
        kfree(h);
    }
    return rc;
}

static int homefs_read(void *file_handle, void *buffer, uint32_t count) {
    homefs_handle_t *h = (homefs_handle_t *)file_handle;
    if (!h || !h->node) return VFS_EINVAL;
    if (h->node->type == VFS_TYPE_DIR) return VFS_EISDIR;
    if (h->position >= h->node->size) return 0;

    if (count > h->node->size - h->position) {
        count = h->node->size - h->position;
    }
    memcpy(buffer, h->node->data + h->position, count);
    h->position += count;
    return (int)count;
}

static int homefs_write(void *file_handle, const void *buffer, uint32_t count) {
    homefs_handle_t *h = (homefs_handle_t *)file_handle;
    if (!h || !h->node) return VFS_EINVAL;
    if (h->node->type == VFS_TYPE_DIR) return VFS_EISDIR;

    if (count > 0xffffffffu - h->position) return VFS_ENOSPC;
    uint32_t end = h->position + count;
    int rc = homefs_node_ensure_capacity(h->node, end);
    if (rc < 0) return rc;

    memcpy(h->node->data + h->position, buffer, count);
    h->position = end;
    if (end > h->node->size) {
        h->node->size = end;
    }
    homefs_mark_dirty(h->fs);
    return (int)count;
}

static int homefs_seek(void *file_handle, int32_t offset, int whence) {
    homefs_handle_t *h = (homefs_handle_t *)file_handle;
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
    if (new_pos > 0x7fffffffll) return VFS_EINVAL;
    h->position = (uint32_t)new_pos;
    return (int)h->position;
}

static int homefs_stat(void *fs_private, const char *path, vfs_stat_t *st) {
    homefs_t *fs = (homefs_t *)fs_private;
    homefs_node_t *node = homefs_lookup(fs->root, path);
    if (!node) return VFS_ENOENT;
    st->size = node->size;
    st->type = node->type;
    return VFS_OK;
}

static int homefs_readdir(void *file_handle, vfs_dirent_t *dirent) {
    homefs_handle_t *h = (homefs_handle_t *)file_handle;
    if (!h || !h->node) return VFS_EINVAL;
    if (h->node->type != VFS_TYPE_DIR) return VFS_ENOTDIR;
    if (!h->readdir_cur) return 0;

    homefs_node_t *child = h->readdir_cur;
    size_t i = 0;
    while (child->name[i] && i < VFS_MAX_NAME - 1) {
        dirent->name[i] = child->name[i];
        i++;
    }
    dirent->name[i] = '\0';
    dirent->size = child->size;
    dirent->type = child->type;
    h->readdir_cur = child->next;
    return 1;
}

static int homefs_mkdir_op(void *fs_private, const char *path) {
    homefs_t *fs = (homefs_t *)fs_private;
    homefs_node_t *existing = homefs_lookup(fs->root, path);
    if (existing) return VFS_EEXIST;

    const char *fname = NULL;
    size_t name_length = 0u;
    homefs_node_t *parent = homefs_existing_parent(
        fs->root, path, &fname, &name_length);
    if (!parent || !fname || name_length == 0u) return VFS_ENOENT;

    char name[VFS_MAX_NAME];
    size_t i = 0u;
    while (i < name_length) {
        name[i] = fname[i];
        i++;
    }
    name[i] = '\0';

    homefs_node_t *node = homefs_alloc_node(name, VFS_TYPE_DIR);
    if (!node) return VFS_ENOSPC;
    node->parent = parent;
    node->depth = parent->depth + 1u;
    node->next = parent->children;
    parent->children = node;
    homefs_mark_dirty(fs);
    int rc = homefs_flush(fs);
    if (rc < 0) {
        homefs_detach_node(node);
        kfree(node);
        fs->dirty = true;
    }
    return rc;
}

static int homefs_unlink_op(void *fs_private, const char *path) {
    homefs_t *fs = (homefs_t *)fs_private;
    homefs_node_t *node = homefs_lookup(fs->root, path);
    if (!node) return VFS_ENOENT;
    if (node == fs->root) return VFS_EINVAL;
    if (node->type == VFS_TYPE_DIR && node->children) return VFS_EINVAL;

    homefs_node_t *parent = node->parent;
    if (node->open_count > 0u ||
        (parent && parent->open_count > 0u)) return VFS_EBUSY;
    homefs_detach_node(node);
    homefs_mark_dirty(fs);
    int rc = homefs_flush(fs);
    if (rc < 0) {
        node->parent = parent;
        node->next = parent->children;
        parent->children = node;
        fs->dirty = true;
        return rc;
    }
    if (node->data) kfree(node->data);
    kfree(node);
    return VFS_OK;
}

static void homefs_detach_node(homefs_node_t *node) {
    homefs_node_t *parent = node ? node->parent : NULL;
    if (!parent) return;
    if (parent->children == node) {
        parent->children = node->next;
    } else {
        homefs_node_t *previous = parent->children;
        while (previous && previous->next != node) previous = previous->next;
        if (previous) previous->next = node->next;
    }
    node->parent = NULL;
    node->next = NULL;
}

static homefs_node_t *homefs_existing_parent(homefs_node_t *root,
                                             const char *path,
                                             const char **filename,
                                             size_t *filename_length) {
    homefs_node_t *current = root;
    const char *component = path;
    while (*component == '/') component++;

    while (*component) {
        const char *end = component;
        while (*end && *end != '/') end++;
        const char *after = end;
        while (*after == '/') after++;
        size_t length = (size_t)(end - component);
        if (length == 0u || length >= VFS_MAX_NAME ||
            homefs_reserved_component(component, length)) return NULL;
        if (*after == '\0') {
            if (*end == '/' || current->type != VFS_TYPE_DIR ||
                current->depth >= HOMEFS_MAX_DEPTH) return NULL;
            *filename = component;
            *filename_length = length;
            return current;
        }
        if (current->type != VFS_TYPE_DIR) return NULL;
        current = homefs_find_child(current, component, length);
        if (!current || current->type != VFS_TYPE_DIR) return NULL;
        component = after;
    }
    return NULL;
}

static int homefs_rename_op(void *fs_private, const char *old_path,
                            const char *new_path) {
    homefs_t *fs = (homefs_t *)fs_private;
    homefs_node_t *source = homefs_lookup(fs->root, old_path);
    homefs_node_t *destination = homefs_lookup(fs->root, new_path);
    const char *filename = NULL;
    char new_name[VFS_MAX_NAME];
    size_t name_length = 0;
    char old_name[VFS_MAX_NAME];
    uint32_t old_depth;

    if (!source) return VFS_ENOENT;
    if (source == fs->root || source->type == VFS_TYPE_DIR) return VFS_EISDIR;
    if (destination == source) return VFS_OK;
    if (destination && destination->type == VFS_TYPE_DIR) return VFS_EISDIR;

    homefs_node_t *new_parent = homefs_existing_parent(
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
    strcpy(old_name, source->name);

    homefs_node_t *old_parent = source->parent;
    homefs_node_t *destination_parent = destination ? destination->parent : NULL;
    old_depth = source->depth;
    homefs_detach_node(source);
    if (destination) homefs_detach_node(destination);
    strcpy(source->name, new_name);
    source->parent = new_parent;
    source->depth = new_parent->depth + 1u;
    source->next = new_parent->children;
    new_parent->children = source;
    homefs_mark_dirty(fs);
    int rc = homefs_flush(fs);
    if (rc < 0) {
        homefs_detach_node(source);
        strcpy(source->name, old_name);
        source->parent = old_parent;
        source->depth = old_depth;
        source->next = old_parent->children;
        old_parent->children = source;
        if (destination) {
            destination->parent = destination_parent;
            destination->next = destination_parent->children;
            destination_parent->children = destination;
        }
        fs->dirty = true;
        return rc;
    }
    if (destination) {
        if (destination->data) kfree(destination->data);
        kfree(destination);
    }
    return VFS_OK;
}

static vfs_fs_ops_t homefs_ops = {
    .name     = "homefs",
    .mount    = homefs_mount,
    .unmount  = homefs_unmount,
    .open     = homefs_open,
    .close    = homefs_close,
    .read     = homefs_read,
    .write    = homefs_write,
    .seek     = homefs_seek,
    .stat     = homefs_stat,
    .readdir  = homefs_readdir,
    .mkdir    = homefs_mkdir_op,
    .unlink   = homefs_unlink_op,
    .rename   = homefs_rename_op
};

vfs_fs_ops_t *homefs_get_ops(void) {
    return &homefs_ops;
}

int homefs_sync(void) {
    if (!g_homefs) return VFS_OK;
    return homefs_flush(g_homefs);
}

int homefs_batch_begin(void) {
    if (!g_homefs) return VFS_ENOENT;
    if (g_homefs->batch_depth == 0xffffffffu) return VFS_ENOSPC;
    g_homefs->batch_depth++;
    return VFS_OK;
}

int homefs_batch_end(void) {
    if (!g_homefs || g_homefs->batch_depth == 0u) return VFS_EINVAL;
    g_homefs->batch_depth--;
    if (g_homefs->batch_depth == 0u && g_homefs->dirty &&
        !g_homefs->seed_mode) {
        return homefs_flush(g_homefs);
    }
    return VFS_OK;
}

void homefs_seed_begin(void) {
    if (g_homefs) {
        g_homefs->seed_mode = true;
    }
}

void homefs_seed_end(void) {
    if (g_homefs) {
        g_homefs->seed_mode = false;
    }
}
