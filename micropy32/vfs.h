#ifndef AARAMBH_VFS_H
#define AARAMBH_VFS_H

#include <stddef.h>
#include <stdint.h>
#include "auth.h"

// VFS Open Flags
#define VFS_O_RDONLY    0x0001
#define VFS_O_WRONLY    0x0002
#define VFS_O_RDWR      0x0003
#define VFS_O_CREAT     0x0040
#define VFS_O_TRUNC     0x0200
#define VFS_O_APPEND    0x0400

// VFS Error Codes
#define VFS_OK          0
#define VFS_EPERM      -1
#define VFS_ENOENT     -2
#define VFS_EIO        -5
#define VFS_EBADF      -9
#define VFS_ENOMEM     -12
#define VFS_EACCES     -13
#define VFS_EEXIST     -17
#define VFS_ENODEV     -19
#define VFS_ENOTDIR    -20
#define VFS_EISDIR     -21
#define VFS_EINVAL     -22
#define VFS_ENFILE     -23
#define VFS_EMFILE     -24
#define VFS_ENOSPC     -28
#define VFS_ENOSYS     -38

// File Types
#define VFS_TYPE_FILE   1
#define VFS_TYPE_DIR    2
#define VFS_TYPE_DEV    3

#define VFS_MAX_FD      16
#define VFS_MAX_MOUNTS  4
#define VFS_MAX_PATH    256

typedef struct {
    uint32_t size;
    uint8_t  type;
    uint16_t mode;
    uint16_t uid;
    uint16_t gid;
} vfs_stat_t;

typedef struct vfs_file {
    int      in_use;
    int      flags;
    uint32_t offset;
    uint32_t size;
    uint32_t entry_id; // AarFS entry ID or driver-specific handle
    uint8_t  type;
    char     path[VFS_MAX_PATH];
} vfs_file_t;

typedef struct vfs_mount {
    int  in_use;
    char mount_point[64];
    char fs_name[16];
    int (*read_file)(uint32_t id, void *buf, uint32_t *sz);
    int (*write_file)(uint32_t id, const void *buf, uint32_t sz);
    uint32_t (*create_entry)(const char *name, uint8_t type, uint32_t parent);
    uint32_t (*find_child)(uint32_t parent, const char *name);
} vfs_mount_t;

// Core VFS Interface
void vfs_init(const kernel_api_t *kapi);
int  vfs_normalize_path(const char *src, char *dst, size_t dst_sz);
int  vfs_open(const char *path, int flags, int mode);
int  vfs_read(int fd, void *buf, size_t count);
int  vfs_write(int fd, const void *buf, size_t count);
int  vfs_close(int fd);
int  vfs_stat(const char *path, vfs_stat_t *st);
int  vfs_read_entire_file(const char *path, void *buf, size_t max_sz, size_t *out_sz);
int  vfs_write_entire_file(const char *path, const void *buf, size_t sz, int mode);

#endif // AARAMBH_VFS_H
