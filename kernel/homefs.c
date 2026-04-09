/**
 * homefs.c - Native persistent filesystem for CupidOS /home
 *
 * Provides a tree-based filesystem with nested directories and long names,
 * while persisting all contents into a single FAT16-hosted container file.
 * This keeps FAT16 as a compatibility/storage backend instead of exposing
 * its namespace limitations directly to the OS.
 */

#include "homefs.h"

#include "blockcache.h"
#include "fat16.h"
#include "memory.h"
#include "string.h"
#include "vfs.h"
#include "../drivers/serial.h"

#define HOMEFS_MAGIC          0x31465348u /* "HFS1" */
#define HOMEFS_VERSION        1u
#define HOMEFS_CONTAINER_NAME "HOMEFS.SYS"

typedef struct homefs_node {
    char     name[VFS_MAX_NAME];
    uint8_t  type;
    uint8_t *data;
    uint32_t size;
    uint32_t capacity;

    struct homefs_node *parent;
    struct homefs_node *children;
    struct homefs_node *next;
} homefs_node_t;

typedef struct {
    homefs_node_t *root;
    bool           dirty;
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
} homefs_import_ctx_t;

typedef struct {
    homefs_t      *fs;
    homefs_node_t *dir;
    char           dir_name[VFS_MAX_NAME];
} homefs_import_dir_ctx_t;

static homefs_t *g_homefs = NULL;

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

        if (cur->type != VFS_TYPE_DIR) return NULL;

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

        if (*after == '\0') {
            if (filename) *filename = p;
            return cur;
        }

        size_t len = (size_t)(end - p);
        homefs_node_t *child = homefs_find_child(cur, p, len);
        if (!child) {
            char dirname[VFS_MAX_NAME];
            size_t i;
            for (i = 0; i < len && i < VFS_MAX_NAME - 1; i++) {
                dirname[i] = p[i];
            }
            dirname[i] = '\0';

            child = homefs_alloc_node(dirname, VFS_TYPE_DIR);
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

static int homefs_node_ensure_capacity(homefs_node_t *node, uint32_t need) {
    if (!node || node->type != VFS_TYPE_FILE) return VFS_EINVAL;
    if (need <= node->capacity) return VFS_OK;

    uint32_t new_cap = node->capacity ? node->capacity : 256u;
    while (new_cap < need) {
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
    if (fs) {
        fs->dirty = true;
    }
}

static uint32_t homefs_count_nodes(homefs_node_t *node) {
    uint32_t count = 0;
    while (node) {
        count++;
        if (node->children) {
            count += homefs_count_nodes(node->children);
        }
        node = node->next;
    }
    return count;
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

    uint32_t node_count = homefs_count_nodes(fs->root);
    homefs_node_t **nodes = kmalloc(node_count * sizeof(homefs_node_t *));
    if (!nodes) return VFS_ENOSPC;

    homefs_node_list_t list;
    list.nodes = nodes;
    list.count = 0;
    list.capacity = node_count;
    if (homefs_collect_nodes(fs->root, &list) < 0) {
        kfree(nodes);
        return VFS_EIO;
    }

    uint32_t total = (uint32_t)sizeof(homefs_disk_header_t);
    for (uint32_t i = 0; i < list.count; i++) {
        homefs_node_t *node = list.nodes[i];
        total += (uint32_t)sizeof(homefs_disk_node_t);
        total += (uint32_t)strlen(node->name);
        if (node->type == VFS_TYPE_FILE) {
            total += node->size;
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

    homefs_node_t **nodes = kmalloc(hdr.node_count * sizeof(homefs_node_t *));
    if (!nodes) return VFS_ENOSPC;
    memset(nodes, 0, hdr.node_count * sizeof(homefs_node_t *));

    uint32_t pos = (uint32_t)sizeof(hdr);
    for (uint32_t i = 0; i < hdr.node_count; i++) {
        if (pos + sizeof(homefs_disk_node_t) > size) {
            goto fail;
        }

        homefs_disk_node_t rec;
        memcpy(&rec, data + pos, sizeof(rec));
        pos += (uint32_t)sizeof(rec);

        if (rec.name_len >= VFS_MAX_NAME || pos + rec.name_len > size) {
            goto fail;
        }

        char name[VFS_MAX_NAME];
        memset(name, 0, sizeof(name));
        if (rec.name_len > 0) {
            memcpy(name, data + pos, rec.name_len);
        }
        name[rec.name_len] = '\0';
        pos += rec.name_len;

        if (rec.type != VFS_TYPE_DIR && rec.type != VFS_TYPE_FILE) {
            goto fail;
        }

        homefs_node_t *node = homefs_alloc_node(name, (uint8_t)rec.type);
        if (!node) {
            goto fail;
        }

        if (rec.type == VFS_TYPE_FILE && rec.size > 0) {
            if (pos + rec.size > size) {
                kfree(node);
                goto fail;
            }
            node->data = kmalloc(rec.size);
            if (!node->data) {
                kfree(node);
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
                nodes[rec.parent_index]->type != VFS_TYPE_DIR) {
                goto fail;
            }
            node->parent = nodes[rec.parent_index];
            node->next = node->parent->children;
            node->parent->children = node;
        }
    }

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
    return VFS_EIO;
}

static int homefs_flush(homefs_t *fs) {
    if (!fs || !fs->root) return VFS_EINVAL;
    if (!fs->dirty) return VFS_OK;

    uint8_t *buf = NULL;
    uint32_t size = 0;
    int rc = homefs_serialize(fs, &buf, &size);
    if (rc < 0) return rc;

    rc = fat16_write_file(HOMEFS_CONTAINER_NAME, buf, size);
    kfree(buf);
    if (rc < 0 || (uint32_t)rc != size) {
        serial_printf("[homefs] flush failed rc=%d size=%u\n", rc, size);
        return VFS_EIO;
    }

    fs->dirty = false;
    blockcache_flush_all();
    serial_printf("[homefs] flushed %u bytes to %s\n", size,
                  HOMEFS_CONTAINER_NAME);
    return VFS_OK;
}

static int homefs_read_fat_file(const char *path, uint8_t **out_data,
                                uint32_t *out_size) {
    fat16_file_t *file = fat16_open(path);
    if (!file) return VFS_ENOENT;

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
    if (nlen == 0 || nlen >= VFS_MAX_NAME) return VFS_EINVAL;
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
    node->next = parent->children;
    parent->children = node;
    return VFS_OK;
}

static int homefs_import_subdir_entry(const char *name, uint32_t size,
                                      uint8_t attr, void *ctx) {
    homefs_import_dir_ctx_t *ictx = (homefs_import_dir_ctx_t *)ctx;
    if (!ictx || !name || name[0] == '\0') return 0;
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
    if (homefs_read_fat_file(fat_path, &data, &file_size) < 0) {
        return 0;
    }
    (void)size;
    (void)homefs_add_imported_file(ictx->dir, name, data, file_size);
    if (data) kfree(data);
    return 0;
}

static int homefs_import_root_entry(const char *name, uint32_t size,
                                    uint8_t attr, void *ctx) {
    homefs_import_ctx_t *ictx = (homefs_import_ctx_t *)ctx;
    if (!ictx || !ictx->fs || !name || name[0] == '\0') return 0;
    if (homefs_strieq(name, HOMEFS_CONTAINER_NAME)) return 0;

    if (attr & FAT_ATTR_DIRECTORY) {
        homefs_node_t *dir = homefs_alloc_node(name, VFS_TYPE_DIR);
        if (!dir) return 1;
        dir->parent = ictx->fs->root;
        dir->next = ictx->fs->root->children;
        ictx->fs->root->children = dir;

        homefs_import_dir_ctx_t dctx;
        memset(&dctx, 0, sizeof(dctx));
        dctx.fs = ictx->fs;
        dctx.dir = dir;
        strncpy(dctx.dir_name, name, VFS_MAX_NAME - 1);
        fat16_enumerate_subdir(name, homefs_import_subdir_entry, &dctx);
        return 0;
    }

    {
        uint8_t *data = NULL;
        uint32_t file_size = 0;
        if (homefs_read_fat_file(name, &data, &file_size) < 0) {
            return 0;
        }
        (void)size;
        (void)homefs_add_imported_file(ictx->fs->root, name, data, file_size);
        if (data) kfree(data);
    }

    return 0;
}

static int homefs_import_from_fat(homefs_t *fs) {
    homefs_import_ctx_t ctx;
    ctx.fs = fs;
    fat16_enumerate_root(homefs_import_root_entry, &ctx);
    homefs_mark_dirty(fs);
    return homefs_flush(fs);
}

static int homefs_mount(const char *source, void **fs_private) {
    (void)source;
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
    if (homefs_read_fat_file(HOMEFS_CONTAINER_NAME, &data, &size) == VFS_OK &&
        data && size > 0) {
        if (homefs_deserialize(fs, data, size) < 0) {
            serial_printf("[homefs] invalid container, starting fresh\n");
            homefs_clear_fs(fs);
            fs->root = homefs_alloc_node("", VFS_TYPE_DIR);
        }
        kfree(data);
    } else {
        serial_printf("[homefs] no container found, importing FAT16 contents\n");
        (void)homefs_import_from_fat(fs);
    }

    g_homefs = fs;
    *fs_private = fs;
    return VFS_OK;
}

static int homefs_unmount(void *fs_private) {
    homefs_t *fs = (homefs_t *)fs_private;
    if (!fs) return VFS_OK;
    (void)homefs_flush(fs);
    homefs_clear_fs(fs);
    if (g_homefs == fs) g_homefs = NULL;
    kfree(fs);
    return VFS_OK;
}

static int homefs_open(void *fs_private, const char *path,
                       uint32_t flags, void **file_handle) {
    homefs_t *fs = (homefs_t *)fs_private;
    homefs_node_t *node = homefs_lookup(fs->root, path);

    if (!node && (flags & O_CREAT)) {
        const char *fname = NULL;
        homefs_node_t *parent = homefs_mkdirs(fs->root, path, &fname);
        if (!parent || !fname || fname[0] == '\0') return VFS_EINVAL;

        char name[VFS_MAX_NAME];
        size_t i = 0;
        while (fname[i] && fname[i] != '/' && i < VFS_MAX_NAME - 1) {
            name[i] = fname[i];
            i++;
        }
        name[i] = '\0';

        node = homefs_alloc_node(name, VFS_TYPE_FILE);
        if (!node) return VFS_ENOSPC;
        node->parent = parent;
        node->next = parent->children;
        parent->children = node;
        homefs_mark_dirty(fs);
    }

    if (!node) return VFS_ENOENT;
    if ((flags & O_TRUNC) && node->type == VFS_TYPE_FILE) {
        if (node->data) {
            kfree(node->data);
            node->data = NULL;
        }
        node->size = 0;
        node->capacity = 0;
        homefs_mark_dirty(fs);
    }

    homefs_handle_t *h = kmalloc(sizeof(homefs_handle_t));
    if (!h) return VFS_EIO;
    memset(h, 0, sizeof(homefs_handle_t));
    h->fs = fs;
    h->node = node;
    h->flags = flags;
    h->position = (flags & O_APPEND) ? node->size : 0;
    h->readdir_cur = (node->type == VFS_TYPE_DIR) ? node->children : NULL;

    *file_handle = h;
    return VFS_OK;
}

static int homefs_close(void *file_handle) {
    homefs_handle_t *h = (homefs_handle_t *)file_handle;
    int rc = VFS_OK;
    if (h) {
        if (h->fs && h->fs->dirty) {
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

    int32_t new_pos;
    switch (whence) {
        case SEEK_SET: new_pos = offset; break;
        case SEEK_CUR: new_pos = (int32_t)h->position + offset; break;
        case SEEK_END: new_pos = (int32_t)h->node->size + offset; break;
        default: return VFS_EINVAL;
    }
    if (new_pos < 0) new_pos = 0;
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
    homefs_node_t *parent = homefs_mkdirs(fs->root, path, &fname);
    if (!parent || !fname || fname[0] == '\0') return VFS_EINVAL;

    char name[VFS_MAX_NAME];
    size_t i = 0;
    while (fname[i] && fname[i] != '/' && i < VFS_MAX_NAME - 1) {
        name[i] = fname[i];
        i++;
    }
    name[i] = '\0';

    homefs_node_t *node = homefs_alloc_node(name, VFS_TYPE_DIR);
    if (!node) return VFS_ENOSPC;
    node->parent = parent;
    node->next = parent->children;
    parent->children = node;
    homefs_mark_dirty(fs);
    return homefs_flush(fs);
}

static int homefs_unlink_op(void *fs_private, const char *path) {
    homefs_t *fs = (homefs_t *)fs_private;
    homefs_node_t *node = homefs_lookup(fs->root, path);
    if (!node) return VFS_ENOENT;
    if (node == fs->root) return VFS_EINVAL;
    if (node->type == VFS_TYPE_DIR && node->children) return VFS_EINVAL;

    homefs_node_t *parent = node->parent;
    if (parent) {
        if (parent->children == node) {
            parent->children = node->next;
        } else {
            homefs_node_t *prev = parent->children;
            while (prev && prev->next != node) prev = prev->next;
            if (prev) prev->next = node->next;
        }
    }

    if (node->data) kfree(node->data);
    kfree(node);
    homefs_mark_dirty(fs);
    return homefs_flush(fs);
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
    .unlink   = homefs_unlink_op
};

vfs_fs_ops_t *homefs_get_ops(void) {
    return &homefs_ops;
}

int homefs_sync(void) {
    if (!g_homefs) return VFS_OK;
    return homefs_flush(g_homefs);
}
