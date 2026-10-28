#include "vfs.h"
#include <string.h>
#include <stdio.h>

extern const kernel_api_t *g_kapi;

static vfs_file_t s_fd_table[VFS_MAX_FD];
static vfs_mount_t s_mount_table[VFS_MAX_MOUNTS];

void vfs_init(const kernel_api_t *kapi) {
    if (kapi) g_kapi = kapi;

    for (int i = 0; i < VFS_MAX_FD; i++) {
        s_fd_table[i].in_use = 0;
        s_fd_table[i].path[0] = '\0';
    }

    // Reserve 0, 1, 2 for stdin, stdout, stderr
    s_fd_table[0].in_use = 1; s_fd_table[0].type = VFS_TYPE_DEV; strcpy(s_fd_table[0].path, "/dev/stdin");
    s_fd_table[1].in_use = 1; s_fd_table[1].type = VFS_TYPE_DEV; strcpy(s_fd_table[1].path, "/dev/stdout");
    s_fd_table[2].in_use = 1; s_fd_table[2].type = VFS_TYPE_DEV; strcpy(s_fd_table[2].path, "/dev/stderr");

    // Mount / to AarFS
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        s_mount_table[i].in_use = 0;
    }

    s_mount_table[0].in_use = 1;
    strcpy(s_mount_table[0].mount_point, "/");
    strcpy(s_mount_table[0].fs_name, "aarfs");
    if (g_kapi) {
        s_mount_table[0].read_file = g_kapi->fs_read_file;
        s_mount_table[0].write_file = g_kapi->fs_write_file;
        s_mount_table[0].create_entry = g_kapi->fs_create_entry;
        s_mount_table[0].find_child = g_kapi->fs_find_child;
    }
}

int vfs_normalize_path(const char *src, char *dst, size_t dst_sz) {
    if (!src || !dst || dst_sz < 2) return VFS_EINVAL;

    char temp[VFS_MAX_PATH];
    size_t ti = 0;

    // If relative path, start from root or cwd
    if (*src != '/') {
        temp[ti++] = '/';
    }

    while (*src && ti + 1 < sizeof(temp)) {
        if (*src == '/') {
            if (ti > 0 && temp[ti - 1] == '/') {
                src++;
                continue; // collapse duplicate slashes
            }
        }
        temp[ti++] = *src++;
    }
    temp[ti] = '\0';

    // Remove trailing slash if not root
    if (ti > 1 && temp[ti - 1] == '/') {
        temp[ti - 1] = '\0';
    }

    strncpy(dst, temp, dst_sz - 1);
    dst[dst_sz - 1] = '\0';
    return VFS_OK;
}

// Resolves a path to (parent_id, child_name) within AarFS
static int vfs_resolve_aarfs(const char *norm_path, uint32_t *out_parent, char *out_child, uint32_t *out_entry_id) {
    if (!g_kapi || !g_kapi->fs_find_child) return VFS_EIO;

    uint32_t curr_id = 0; // Root is 0
    if (strcmp(norm_path, "/") == 0) {
        if (out_parent) *out_parent = 0;
        if (out_child) out_child[0] = '\0';
        if (out_entry_id) *out_entry_id = 0;
        return VFS_OK;
    }

    char copy[VFS_MAX_PATH];
    strncpy(copy, norm_path, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *start = copy;
    if (*start == '/') start++;

    char *last_token = NULL;
    char *token = start;

    while (token && *token) {
        char *slash = strchr(token, '/');
        if (slash) {
            *slash = '\0';
            uint32_t next_id = g_kapi->fs_find_child(curr_id, token);
            if (!next_id) {
                return VFS_ENOENT; // Intermediate directory not found
            }
            curr_id = next_id;
            token = slash + 1;
        } else {
            last_token = token;
            break;
        }
    }

    if (out_parent) *out_parent = curr_id;
    if (out_child && last_token) {
        strncpy(out_child, last_token, 39);
        out_child[39] = '\0';
    }

    if (last_token && out_entry_id) {
        *out_entry_id = g_kapi->fs_find_child(curr_id, last_token);
    }

    return VFS_OK;
}

int vfs_open(const char *path, int flags, int mode) {
    char norm[VFS_MAX_PATH];
    if (vfs_normalize_path(path, norm, sizeof(norm)) != VFS_OK) {
        return VFS_EINVAL;
    }

    // Allocate free file descriptor (3..VFS_MAX_FD-1)
    int fd = -1;
    for (int i = 3; i < VFS_MAX_FD; i++) {
        if (!s_fd_table[i].in_use) {
            fd = i;
            break;
        }
    }
    if (fd < 0) return VFS_EMFILE;

    uint32_t parent_id = 0;
    char child_name[40] = "";
    uint32_t entry_id = 0;

    int res = vfs_resolve_aarfs(norm, &parent_id, child_name, &entry_id);
    if (res != VFS_OK) return res;

    if (!entry_id) {
        if (flags & VFS_O_CREAT) {
            if (!g_kapi || !g_kapi->fs_create_entry) return VFS_EIO;
            entry_id = g_kapi->fs_create_entry(child_name, 1, parent_id);
            if (!entry_id) return VFS_ENOSPC;
            if (g_kapi->fs_entry_ptr) {
                aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(entry_id);
                if (e) {
                    e->mode = mode ? (uint16_t)mode : 0644;
                    e->uid = (g_kapi->session_uid) ? *g_kapi->session_uid : 0;
                    e->gid = (g_kapi->session_gid) ? *g_kapi->session_gid : 0;
                }
            }
        } else {
            return VFS_ENOENT;
        }
    }

    aarfs_entry_t *e = (g_kapi && g_kapi->fs_entry_ptr) ? (aarfs_entry_t *)g_kapi->fs_entry_ptr(entry_id) : NULL;
    if (!e || e->status == 0) return VFS_ENOENT;

    // Permission check
    uint16_t cur_uid = (g_kapi && g_kapi->session_uid) ? *g_kapi->session_uid : 0;
    uint16_t cur_gid = (g_kapi && g_kapi->session_gid) ? *g_kapi->session_gid : 0;
    uint16_t fmode = e->mode ? e->mode : 0644;

    if (cur_uid != 0) {
        int can_read = 0, can_write = 0;
        if (cur_uid == e->uid) {
            if (fmode & 0400) can_read = 1;
            if (fmode & 0200) can_write = 1;
        } else if (cur_gid == e->gid) {
            if (fmode & 0040) can_read = 1;
            if (fmode & 0020) can_write = 1;
        } else {
            if (fmode & 0004) can_read = 1;
            if (fmode & 0002) can_write = 1;
        }
        if ((flags & VFS_O_RDWR) && (!can_read || !can_write)) return VFS_EACCES;
        if ((flags & VFS_O_WRONLY) && !can_write) return VFS_EACCES;
        if (!(flags & (VFS_O_WRONLY | VFS_O_RDWR)) && !can_read) return VFS_EACCES;
    }

    s_fd_table[fd].in_use = 1;
    s_fd_table[fd].flags = flags;
    s_fd_table[fd].offset = (flags & VFS_O_APPEND) ? e->size : 0;
    s_fd_table[fd].size = e->size;
    s_fd_table[fd].entry_id = entry_id;
    s_fd_table[fd].type = (e->type == 2) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    strncpy(s_fd_table[fd].path, norm, sizeof(s_fd_table[fd].path) - 1);

    return fd;
}

int vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_FD || !s_fd_table[fd].in_use) return VFS_EBADF;
    if (!buf) return VFS_EINVAL;
    if (count == 0) return 0;

    vfs_file_t *file = &s_fd_table[fd];
    if (file->type == VFS_TYPE_DIR) return VFS_EISDIR;

    if (!g_kapi || !g_kapi->fs_read_file) return VFS_EIO;

    static char s_vfs_io_buf[16384];
    uint32_t rsize = 0;
    if (g_kapi->fs_read_file(file->entry_id, s_vfs_io_buf, &rsize) != 0) {
        return VFS_EIO;
    }

    if (file->offset >= rsize) return 0; // EOF

    size_t available = rsize - file->offset;
    size_t to_read = (count < available) ? count : available;

    memcpy(buf, s_vfs_io_buf + file->offset, to_read);
    file->offset += to_read;
    return (int)to_read;
}

int vfs_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_FD || !s_fd_table[fd].in_use) return VFS_EBADF;
    if (!buf) return VFS_EINVAL;
    if (count == 0) return 0;

    vfs_file_t *file = &s_fd_table[fd];
    if (file->type == VFS_TYPE_DIR) return VFS_EISDIR;
    if (!(file->flags & (VFS_O_WRONLY | VFS_O_RDWR))) return VFS_EBADF;

    if (!g_kapi || !g_kapi->fs_read_file || !g_kapi->fs_write_file) return VFS_EIO;

    static char s_vfs_io_buf[16384];
    uint32_t rsize = 0;
    g_kapi->fs_read_file(file->entry_id, s_vfs_io_buf, &rsize);

    if (file->offset + count > sizeof(s_vfs_io_buf)) {
        return VFS_ENOSPC; // Exceeds 16 KiB slot limit
    }

    memcpy(s_vfs_io_buf + file->offset, buf, count);
    file->offset += count;
    if (file->offset > rsize) rsize = file->offset;
    file->size = rsize;

    if (g_kapi->fs_write_file(file->entry_id, s_vfs_io_buf, rsize) != 0) {
        return VFS_EIO;
    }
    if (g_kapi->fs_commit) g_kapi->fs_commit();

    return (int)count;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_FD || !s_fd_table[fd].in_use) return VFS_EBADF;
    if (fd <= 2) return VFS_OK; // Don't close standard streams

    s_fd_table[fd].in_use = 0;
    s_fd_table[fd].path[0] = '\0';
    return VFS_OK;
}

int vfs_stat(const char *path, vfs_stat_t *st) {
    if (!path || !st) return VFS_EINVAL;

    char norm[VFS_MAX_PATH];
    if (vfs_normalize_path(path, norm, sizeof(norm)) != VFS_OK) return VFS_EINVAL;

    uint32_t parent_id = 0;
    char child_name[40] = "";
    uint32_t entry_id = 0;

    int res = vfs_resolve_aarfs(norm, &parent_id, child_name, &entry_id);
    if (res != VFS_OK) return res;
    if (!entry_id && strcmp(norm, "/") != 0) return VFS_ENOENT;

    if (strcmp(norm, "/") == 0) {
        st->size = 0;
        st->type = VFS_TYPE_DIR;
        st->mode = 0755;
        st->uid = 0;
        st->gid = 0;
        return VFS_OK;
    }

    aarfs_entry_t *e = (g_kapi && g_kapi->fs_entry_ptr) ? (aarfs_entry_t *)g_kapi->fs_entry_ptr(entry_id) : NULL;
    if (!e || e->status == 0) return VFS_ENOENT;

    st->size = e->size;
    st->type = (e->type == 2) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
    st->mode = e->mode ? e->mode : (e->type == 2 ? 0755 : 0644);
    st->uid = e->uid;
    st->gid = e->gid;
    return VFS_OK;
}

int vfs_read_entire_file(const char *path, void *buf, size_t max_sz, size_t *out_sz) {
    int fd = vfs_open(path, VFS_O_RDONLY, 0);
    if (fd < 0) return fd;

    int r = vfs_read(fd, buf, max_sz);
    vfs_close(fd);
    if (r < 0) return r;

    if (out_sz) *out_sz = (size_t)r;
    return VFS_OK;
}

int vfs_write_entire_file(const char *path, const void *buf, size_t sz, int mode) {
    int fd = vfs_open(path, VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, mode);
    if (fd < 0) return fd;

    int w = vfs_write(fd, buf, sz);
    vfs_close(fd);
    if (w < 0) return w;

    return VFS_OK;
}
