#ifndef AUTH_H
#define AUTH_H

#include <stddef.h>
#include <stdint.h>

int snprintf(char *buf, size_t size, const char *fmt, ...);

#define MAX_USERNAME_LEN 32
#define MAX_GROUPNAME_LEN 32
#define MAX_PASSWD_LINE 256
#define MAX_PATH_LEN 64
#define MAX_SUPP_GROUPS 8

typedef struct {
    void (*putc)(char c);
    char (*getc)(void);
    int (*fs_read_file)(uint32_t entry_id, void *buf, uint32_t *out_size);
    int (*fs_write_file)(uint32_t entry_id, const void *buf, uint32_t size);
    uint32_t (*fs_create_entry)(const char *name, uint8_t type, uint32_t parent_id);
    uint32_t (*fs_find_child)(uint32_t parent_id, const char *name);
    void *(*fs_entry_ptr)(uint32_t entry_id);
    int (*fs_commit)(void);
    uint32_t *fs_cwd;
    uint16_t *session_uid;
    uint16_t *session_gid;
    uint8_t *session_supp_count;
    uint16_t *session_supp_gids;
    char *session_user;
    char *session_home;
    uint32_t *timer_ticks;
    void (*set_video_mode)(uint16_t mode, uint16_t bx);
    int (*kbd_check)(void);
    int (*mouse_read)(void);
} kernel_api_t;

typedef struct __attribute__((packed)) {
    uint8_t status;       // +0: 1 = allocated, 0 = free
    uint8_t type;         // +1: 1 = file, 2 = dir
    uint8_t slot;         // +2: active data extent slot (0 or 1)
    uint8_t reserved;     // +3
    uint32_t parent_id;   // +4: 1-based parent entry id (0 = root)
    char name[40];        // +8: null-terminated name
    uint32_t size;        // +48: file size in bytes
    uint16_t uid;         // +52: user ID
    uint16_t gid;         // +54: group ID
    uint16_t mode;        // +56: octal permission mode (e.g. 0755, 0644)
    uint8_t unused[6];    // +58..+63
} aarfs_entry_t;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char salt[17];
    char hash[65];
    uint16_t uid;
    uint16_t gid;
    char gecos[32];
    char home[MAX_PATH_LEN];
    char shell[16];
} passwd_entry_t;

typedef struct {
    char groupname[MAX_GROUPNAME_LEN];
    uint16_t gid;
    char members[128];
} group_entry_t;

// Initialize user and permission system files on AarFS (/etc/passwd, /etc/group, home dirs)
int auth_init(const kernel_api_t *kapi);

// Dispatch a native security/user command from the shell
int sys_dispatch(const char *cmd, const char *args, const kernel_api_t *kapi);

extern const kernel_api_t *g_kapi;

#endif // AUTH_H
