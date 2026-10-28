#include "auth.h"
#include "sha256.h"
#include "net_stack.h"
#include "net_gui.h"
#include "http.h"
#include "git.h"
#include <string.h>

#define MAX_USERS 32
#define MAX_GROUPS 32
#define BUFFER_SIZE 4096

static char s_io_buf[BUFFER_SIZE];
static passwd_entry_t s_users[MAX_USERS];
static size_t s_user_count = 0;
static group_entry_t s_groups[MAX_GROUPS];
static size_t s_group_count = 0;

const kernel_api_t *g_kapi = NULL;

static void cprint(const char *str) {
    if (!g_kapi || !g_kapi->putc || !str) return;
    while (*str) {
        if (*str == '\n') g_kapi->putc('\r');
        g_kapi->putc(*str++);
    }
}

static void cprintln(const char *str) {
    cprint(str);
    cprint("\n");
}

void auth_cprint(const char *str) {
    cprint(str);
}

void auth_cprintln(const char *str) {
    cprintln(str);
}

static void cprint_u32(uint32_t val) {
    char buf[12];
    int idx = 0;
    if (val == 0) {
        cprint("0");
        return;
    }
    while (val > 0) {
        buf[idx++] = (char)('0' + (val % 10));
        val /= 10;
    }
    for (int i = 0; i < idx / 2; ++i) {
        char tmp = buf[i];
        buf[i] = buf[idx - 1 - i];
        buf[idx - 1 - i] = tmp;
    }
    buf[idx] = '\0';
    cprint(buf);
}

// Generate a 16-hex-character random salt using timer ticks and address mixing
static void generate_salt(char salt[17]) {
    static uint32_t seq = 0x12345678;
    uint32_t t = g_kapi && g_kapi->timer_ticks ? *g_kapi->timer_ticks : 0xABCDEF;
    seq = seq * 1664525 + 1013904223 + t;
    uint32_t v1 = seq ^ (t << 16);
    seq = seq * 1664525 + 1013904223;
    uint32_t v2 = seq ^ t;

    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 8; ++i) {
        salt[i] = hex[(v1 >> (i * 4)) & 0xF];
        salt[i + 8] = hex[(v2 >> (i * 4)) & 0xF];
    }
    salt[16] = '\0';
}

// Read line from keyboard without echoing characters (for password input)
static void read_password_silent(char *buf, size_t max_len) {
    if (!g_kapi || !g_kapi->getc || !buf || max_len == 0) return;
    size_t idx = 0;
    while (1) {
        char c = g_kapi->getc();
        if (c == 13 || c == 10) { // Enter
            break;
        } else if (c == 8) {      // Backspace
            if (idx > 0) idx--;
        } else if (c >= 32 && c <= 126) {
            if (idx + 1 < max_len) {
                buf[idx++] = c;
            }
        }
    }
    buf[idx] = '\0';
    cprint("\n");
}

// Read line from keyboard with normal echo
static void read_line_echo(char *buf, size_t max_len) {
    if (!g_kapi || !g_kapi->getc || !buf || max_len == 0) return;
    size_t idx = 0;
    while (1) {
        char c = g_kapi->getc();
        if (c == 13 || c == 10) {
            break;
        } else if (c == 8) {
            if (idx > 0) {
                idx--;
                cprint("\b \b");
            }
        } else if (c >= 32 && c <= 126) {
            if (idx + 1 < max_len) {
                buf[idx++] = c;
                if (g_kapi->putc) g_kapi->putc(c);
            }
        }
    }
    buf[idx] = '\0';
    cprint("\n");
}

// Validate username or groupname syntax
static int validate_name(const char *name) {
    if (!name || !*name) return 0;
    size_t len = strlen(name);
    if (len > 31) return 0;
    // First char must be a-z or _
    char first = name[0];
    if (!((first >= 'a' && first <= 'z') || first == '_')) return 0;
    for (size_t i = 0; i < len; ++i) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

// Convert string to uint16
static int parse_u16(const char *s, uint16_t *out) {
    if (!s || !*s) return 0;
    uint32_t val = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        val = val * 10 + (*s - '0');
        if (val > 65535) return 0;
        s++;
    }
    *out = (uint16_t)val;
    return 1;
}

// Parse octal string (e.g. "755", "0644")
static int parse_octal(const char *s, uint16_t *out) {
    if (!s || !*s) return 0;
    uint32_t val = 0;
    while (*s) {
        if (*s < '0' || *s > '7') return 0;
        val = (val << 3) | (*s - '0');
        if (val > 07777) return 0;
        s++;
    }
    *out = (uint16_t)val;
    return 1;
}

// Format 9-char permission string (e.g. "-rwxr-xr-x")
static void format_mode_str(uint16_t mode, uint8_t is_dir, char out[11]) {
    out[0] = is_dir ? 'd' : '-';
    out[1] = (mode & 0400) ? 'r' : '-';
    out[2] = (mode & 0200) ? 'w' : '-';
    out[3] = (mode & 0100) ? 'x' : '-';
    out[4] = (mode & 0040) ? 'r' : '-';
    out[5] = (mode & 0020) ? 'w' : '-';
    out[6] = (mode & 0010) ? 'x' : '-';
    out[7] = (mode & 0004) ? 'r' : '-';
    out[8] = (mode & 0002) ? 'w' : '-';
    out[9] = (mode & 0001) ? 'x' : '-';
    out[10] = '\0';
}

// Load /etc/passwd from AarFS into s_users array
static int load_passwd(void) {
    s_user_count = 0;
    if (!g_kapi) return 0;
    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) return 0;
    uint32_t pwd_id = g_kapi->fs_find_child(etc_id, "passwd");
    if (!pwd_id) return 0;

    uint32_t size = 0;
    if (g_kapi->fs_read_file(pwd_id, s_io_buf, &size) != 0 || size == 0) {
        return 0;
    }
    if (size >= BUFFER_SIZE) size = BUFFER_SIZE - 1;
    s_io_buf[size] = '\0';

    char *line = s_io_buf;
    while (*line && s_user_count < MAX_USERS) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        // Format: username:salt$hash:uid:gid:gecos:home:shell
        char *p = line;
        char *f_user = p; p = strchr(p, ':');
        if (p) { *p++ = '\0'; char *f_hash = p; p = strchr(p, ':');
        if (p) { *p++ = '\0'; char *f_uid = p; p = strchr(p, ':');
        if (p) { *p++ = '\0'; char *f_gid = p; p = strchr(p, ':');
        if (p) { *p++ = '\0'; char *f_gecos = p; p = strchr(p, ':');
        if (p) { *p++ = '\0'; char *f_home = p; p = strchr(p, ':');
        if (p) { *p++ = '\0'; char *f_shell = p;

            passwd_entry_t *u = &s_users[s_user_count];
            strncpy(u->username, f_user, sizeof(u->username) - 1);
            u->username[sizeof(u->username) - 1] = '\0';

            // Split salt$hash
            char *dollar = strchr(f_hash, '$');
            if (dollar) {
                *dollar = '\0';
                strncpy(u->salt, f_hash, sizeof(u->salt) - 1);
                u->salt[sizeof(u->salt) - 1] = '\0';
                strncpy(u->hash, dollar + 1, sizeof(u->hash) - 1);
                u->hash[sizeof(u->hash) - 1] = '\0';
            } else {
                u->salt[0] = '\0';
                u->hash[0] = '\0';
            }

            parse_u16(f_uid, &u->uid);
            parse_u16(f_gid, &u->gid);
            strncpy(u->gecos, f_gecos, sizeof(u->gecos) - 1);
            strncpy(u->home, f_home, sizeof(u->home) - 1);
            strncpy(u->shell, f_shell, sizeof(u->shell) - 1);
            s_user_count++;
        }}}}}}
        if (!eol) break;
        line = eol + 1;
    }
    return (s_user_count > 0);
}

// Save s_users array back to /etc/passwd in AarFS
static int save_passwd(void) {
    if (!g_kapi) return 0;
    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) {
        etc_id = g_kapi->fs_create_entry("etc", 2, 0);
        if (!etc_id) return 0;
        aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(etc_id);
        if (e) { e->mode = 0755; e->uid = 0; e->gid = 0; }
    }
    uint32_t pwd_id = g_kapi->fs_find_child(etc_id, "passwd");
    if (!pwd_id) {
        pwd_id = g_kapi->fs_create_entry("passwd", 1, etc_id);
        if (!pwd_id) return 0;
        aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(pwd_id);
        if (e) { e->mode = 0644; e->uid = 0; e->gid = 0; }
    }

    char *dst = s_io_buf;
    size_t rem = sizeof(s_io_buf);
    for (size_t i = 0; i < s_user_count; ++i) {
        passwd_entry_t *u = &s_users[i];
        // Format: user:salt$hash:uid:gid:gecos:home:shell\n
        int n = 0;
        if (u->salt[0]) {
            n = snprintf(dst, rem, "%s:%s$%s:%u:%u:%s:%s:%s\n",
                         u->username, u->salt, u->hash,
                         (unsigned)u->uid, (unsigned)u->gid,
                         u->gecos, u->home, u->shell);
        } else {
            n = snprintf(dst, rem, "%s::%u:%u:%s:%s:%s\n",
                         u->username, (unsigned)u->uid, (unsigned)u->gid,
                         u->gecos, u->home, u->shell);
        }
        if (n < 0 || (size_t)n >= rem) break;
        dst += n;
        rem -= n;
    }
    size_t total = sizeof(s_io_buf) - rem;
    if (g_kapi->fs_write_file(pwd_id, s_io_buf, total) != 0) {
        return 0;
    }
    g_kapi->fs_commit();
    return 1;
}

// Load /etc/group from AarFS into s_groups array
static int load_group(void) {
    s_group_count = 0;
    if (!g_kapi) return 0;
    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) return 0;
    uint32_t grp_id = g_kapi->fs_find_child(etc_id, "group");
    if (!grp_id) return 0;

    uint32_t size = 0;
    if (g_kapi->fs_read_file(grp_id, s_io_buf, &size) != 0 || size == 0) {
        return 0;
    }
    if (size >= BUFFER_SIZE) size = BUFFER_SIZE - 1;
    s_io_buf[size] = '\0';

    char *line = s_io_buf;
    while (*line && s_group_count < MAX_GROUPS) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';
        // Format: groupname:gid:members
        char *p = line;
        char *f_grp = p; p = strchr(p, ':');
        if (p) { *p++ = '\0'; char *f_gid = p; p = strchr(p, ':');
        if (p) { *p++ = '\0'; char *f_mem = p;
            group_entry_t *g = &s_groups[s_group_count];
            strncpy(g->groupname, f_grp, sizeof(g->groupname) - 1);
            g->groupname[sizeof(g->groupname) - 1] = '\0';
            parse_u16(f_gid, &g->gid);
            strncpy(g->members, f_mem, sizeof(g->members) - 1);
            g->members[sizeof(g->members) - 1] = '\0';
            s_group_count++;
        }}
        if (!eol) break;
        line = eol + 1;
    }
    return (s_group_count > 0);
}

// Save s_groups array back to /etc/group in AarFS
static int save_group(void) {
    if (!g_kapi) return 0;
    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) return 0;
    uint32_t grp_id = g_kapi->fs_find_child(etc_id, "group");
    if (!grp_id) {
        grp_id = g_kapi->fs_create_entry("group", 1, etc_id);
        if (!grp_id) return 0;
        aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(grp_id);
        if (e) { e->mode = 0644; e->uid = 0; e->gid = 0; }
    }

    char *dst = s_io_buf;
    size_t rem = sizeof(s_io_buf);
    for (size_t i = 0; i < s_group_count; ++i) {
        group_entry_t *g = &s_groups[i];
        int n = snprintf(dst, rem, "%s:%u:%s\n", g->groupname, (unsigned)g->gid, g->members);
        if (n < 0 || (size_t)n >= rem) break;
        dst += n;
        rem -= n;
    }
    size_t total = sizeof(s_io_buf) - rem;
    if (g_kapi->fs_write_file(grp_id, s_io_buf, total) != 0) {
        return 0;
    }
    g_kapi->fs_commit();
    return 1;
}

static passwd_entry_t *find_user_by_name(const char *name) {
    for (size_t i = 0; i < s_user_count; ++i) {
        if (strcmp(s_users[i].username, name) == 0) return &s_users[i];
    }
    return NULL;
}

static passwd_entry_t *find_user_by_uid(uint16_t uid) {
    for (size_t i = 0; i < s_user_count; ++i) {
        if (s_users[i].uid == uid) return &s_users[i];
    }
    return NULL;
}

static group_entry_t *find_group_by_name(const char *name) {
    for (size_t i = 0; i < s_group_count; ++i) {
        if (strcmp(s_groups[i].groupname, name) == 0) return &s_groups[i];
    }
    return NULL;
}

static group_entry_t *find_group_by_gid(uint16_t gid) {
    for (size_t i = 0; i < s_group_count; ++i) {
        if (s_groups[i].gid == gid) return &s_groups[i];
    }
    return NULL;
}

// Switch kernel session to specified user
static void apply_session(const passwd_entry_t *u) {
    if (!g_kapi || !u) return;
    if (g_kapi->session_uid) *g_kapi->session_uid = u->uid;
    if (g_kapi->session_gid) *g_kapi->session_gid = u->gid;
    if (g_kapi->session_user) {
        strncpy(g_kapi->session_user, u->username, 31);
        g_kapi->session_user[31] = '\0';
    }
    if (g_kapi->session_home) {
        strncpy(g_kapi->session_home, u->home, 63);
        g_kapi->session_home[63] = '\0';
    }
    // Populate supplementary groups from s_groups
    if (g_kapi->session_supp_count && g_kapi->session_supp_gids) {
        uint8_t count = 0;
        g_kapi->session_supp_gids[count++] = u->gid; // primary is first
        for (size_t i = 0; i < s_group_count && count < MAX_SUPP_GROUPS; ++i) {
            if (s_groups[i].gid == u->gid) continue;
            // Check if username in s_groups[i].members
            char *m = s_groups[i].members;
            size_t ulen = strlen(u->username);
            while (*m) {
                char *comma = strchr(m, ',');
                size_t mlen = comma ? (size_t)(comma - m) : strlen(m);
                if (mlen == ulen && strncmp(m, u->username, ulen) == 0) {
                    g_kapi->session_supp_gids[count++] = s_groups[i].gid;
                    break;
                }
                if (!comma) break;
                m = comma + 1;
            }
        }
        *g_kapi->session_supp_count = count;
    }
}

// First-boot / missing database initialization
int auth_init(const kernel_api_t *kapi) {
    g_kapi = kapi;
    if (load_passwd() && load_group()) {
        return 1; // Already initialized
    }

    // Create /etc directory
    uint32_t etc_id = g_kapi->fs_find_child(0, "etc");
    if (!etc_id) {
        etc_id = g_kapi->fs_create_entry("etc", 2, 0);
        aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(etc_id);
        if (e) { e->mode = 0755; e->uid = 0; e->gid = 0; }
    }

    // Default root account: password "root123" with salted SHA-256
    s_user_count = 0;
    passwd_entry_t *root = &s_users[s_user_count++];
    strcpy(root->username, "root");
    generate_salt(root->salt);
    sha256_hash_password(root->salt, "root123", root->hash);
    root->uid = 0;
    root->gid = 0;
    strcpy(root->gecos, "Superuser");
    strcpy(root->home, "/root");
    strcpy(root->shell, "/bin/sh");

    // Default standard user: password "aarambh" with salted SHA-256
    passwd_entry_t *user = &s_users[s_user_count++];
    strcpy(user->username, "aarambh");
    generate_salt(user->salt);
    sha256_hash_password(user->salt, "aarambh", user->hash);
    user->uid = 1000;
    user->gid = 1000;
    strcpy(user->gecos, "Aarambh User");
    strcpy(user->home, "/home/aarambh");
    strcpy(user->shell, "/bin/sh");

    save_passwd();

    // Default groups
    s_group_count = 0;
    group_entry_t *g_root = &s_groups[s_group_count++];
    strcpy(g_root->groupname, "root");
    g_root->gid = 0;
    strcpy(g_root->members, "root");

    group_entry_t *g_user = &s_groups[s_group_count++];
    strcpy(g_user->groupname, "aarambh");
    g_user->gid = 1000;
    strcpy(g_user->members, "aarambh");

    group_entry_t *g_wheel = &s_groups[s_group_count++];
    strcpy(g_wheel->groupname, "wheel");
    g_wheel->gid = 10;
    strcpy(g_wheel->members, "root,aarambh");

    save_group();

    // Ensure /root directory exists (mode 0700, root:root)
    uint32_t root_dir_id = g_kapi->fs_find_child(0, "root");
    if (!root_dir_id) {
        root_dir_id = g_kapi->fs_create_entry("root", 2, 0);
    }
    if (root_dir_id) {
        aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(root_dir_id);
        if (e) { e->mode = 0700; e->uid = 0; e->gid = 0; }
    }

    // Ensure /home directory exists (mode 0755, root:root)
    uint32_t home_dir_id = g_kapi->fs_find_child(0, "home");
    if (!home_dir_id) {
        home_dir_id = g_kapi->fs_create_entry("home", 2, 0);
    }
    if (home_dir_id) {
        aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(home_dir_id);
        if (e) { e->mode = 0755; e->uid = 0; e->gid = 0; }
    }

    // Ensure /home/aarambh exists (mode 0750, aarambh:aarambh)
    if (home_dir_id) {
        uint32_t a_id = g_kapi->fs_find_child(home_dir_id, "aarambh");
        if (!a_id) {
            a_id = g_kapi->fs_create_entry("aarambh", 2, home_dir_id);
        }
        if (a_id) {
            aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(a_id);
            if (e) { e->mode = 0750; e->uid = 1000; e->gid = 1000; }
        }
    }

    g_kapi->fs_commit();
    return 1;
}

// ---------------- COMMAND IMPLEMENTATIONS ----------------

// whoami
static int cmd_whoami(void) {
    if (g_kapi && g_kapi->session_user) {
        cprintln(g_kapi->session_user);
    } else {
        cprintln("unknown");
    }
    return 0;
}

// id [username]
static int cmd_id(const char *arg) {
    load_passwd();
    load_group();
    const char *target = arg && *arg ? arg : (g_kapi ? g_kapi->session_user : "root");
    passwd_entry_t *u = find_user_by_name(target);
    if (!u) {
        cprint("id: '"); cprint(target); cprintln("': no such user");
        return 1;
    }
    group_entry_t *g = find_group_by_gid(u->gid);

    cprint("uid="); cprint_u32(u->uid); cprint("("); cprint(u->username); cprint(") ");
    cprint("gid="); cprint_u32(u->gid); cprint("("); cprint(g ? g->groupname : "unknown"); cprint(") ");
    cprint("groups="); cprint_u32(u->gid); cprint("("); cprint(g ? g->groupname : "unknown"); cprint(")");

    // Supplementary groups
    for (size_t i = 0; i < s_group_count; ++i) {
        if (s_groups[i].gid == u->gid) continue;
        char *m = s_groups[i].members;
        size_t ulen = strlen(u->username);
        while (*m) {
            char *comma = strchr(m, ',');
            size_t mlen = comma ? (size_t)(comma - m) : strlen(m);
            if (mlen == ulen && strncmp(m, u->username, ulen) == 0) {
                cprint(","); cprint_u32(s_groups[i].gid);
                cprint("("); cprint(s_groups[i].groupname); cprint(")");
                break;
            }
            if (!comma) break;
            m = comma + 1;
        }
    }
    cprint("\n");
    return 0;
}

// users
static int cmd_users(void) {
    load_passwd();
    for (size_t i = 0; i < s_user_count; ++i) {
        cprint(s_users[i].username);
        cprint(" (uid="); cprint_u32(s_users[i].uid);
        cprint(", gid="); cprint_u32(s_users[i].gid);
        cprint(", home="); cprint(s_users[i].home);
        cprintln(")");
    }
    return 0;
}

// groups [username]
static int cmd_groups(const char *arg) {
    load_passwd();
    load_group();
    const char *target = arg && *arg ? arg : (g_kapi ? g_kapi->session_user : "root");
    passwd_entry_t *u = find_user_by_name(target);
    if (!u) {
        cprint("groups: '"); cprint(target); cprintln("': no such user");
        return 1;
    }
    group_entry_t *g = find_group_by_gid(u->gid);
    cprint(target); cprint(" : ");
    if (g) cprint(g->groupname);

    for (size_t i = 0; i < s_group_count; ++i) {
        if (s_groups[i].gid == u->gid) continue;
        char *m = s_groups[i].members;
        size_t ulen = strlen(u->username);
        while (*m) {
            char *comma = strchr(m, ',');
            size_t mlen = comma ? (size_t)(comma - m) : strlen(m);
            if (mlen == ulen && strncmp(m, u->username, ulen) == 0) {
                cprint(" "); cprint(s_groups[i].groupname);
                break;
            }
            if (!comma) break;
            m = comma + 1;
        }
    }
    cprint("\n");
    return 0;
}

// useradd <name> [-u uid] [-g gid] [-d home] [-p pass]
static int cmd_useradd(const char *args) {
    if (!g_kapi || (g_kapi->session_uid && *g_kapi->session_uid != 0)) {
        cprintln("useradd: permission denied (superuser required)");
        return 1;
    }
    if (!args || !*args) {
        cprintln("Usage: useradd <username> [-u uid] [-g gid] [-d home] [-p password]");
        return 1;
    }

    load_passwd();
    load_group();
    if (s_user_count >= MAX_USERS) {
        cprintln("useradd: maximum user limit reached");
        return 1;
    }

    char name[MAX_USERNAME_LEN] = {0};
    char pass[64] = {0};
    char home[MAX_PATH_LEN] = {0};
    uint16_t req_uid = 0;
    uint16_t req_gid = 0;
    int has_uid = 0, has_gid = 0;

    // Parse arguments
    char buf[256];
    strncpy(buf, args, sizeof(buf) - 1);
    char *token = strtok(buf, " ");
    while (token != NULL) {
        if (strcmp(token, "-u") == 0) {
            char *val = strtok(NULL, " ");
            if (val && parse_u16(val, &req_uid)) has_uid = 1;
        } else if (strcmp(token, "-g") == 0) {
            char *val = strtok(NULL, " ");
            if (val && parse_u16(val, &req_gid)) has_gid = 1;
        } else if (strcmp(token, "-d") == 0) {
            char *val = strtok(NULL, " ");
            if (val) strncpy(home, val, sizeof(home) - 1);
        } else if (strcmp(token, "-p") == 0) {
            char *val = strtok(NULL, " ");
            if (val) strncpy(pass, val, sizeof(pass) - 1);
        } else if (token[0] != '-') {
            strncpy(name, token, sizeof(name) - 1);
        }
        token = strtok(NULL, " ");
    }

    if (!validate_name(name)) {
        cprintln("useradd: invalid username; use 1-31 lowercase letters, digits, '_' or '-'");
        return 1;
    }
    if (find_user_by_name(name)) {
        cprintln("useradd: user already exists");
        return 1;
    }

    // Determine UID
    uint16_t final_uid = 1000;
    if (has_uid) {
        if (find_user_by_uid(req_uid)) {
            cprintln("useradd: UID already in use");
            return 1;
        }
        final_uid = req_uid;
    } else {
        while (find_user_by_uid(final_uid)) final_uid++;
    }

    // Determine GID & create primary group if missing
    uint16_t final_gid = final_uid;
    if (has_gid) {
        final_gid = req_gid;
    } else {
        if (!find_group_by_name(name) && s_group_count < MAX_GROUPS) {
            group_entry_t *ng = &s_groups[s_group_count++];
            strncpy(ng->groupname, name, sizeof(ng->groupname) - 1);
            ng->gid = final_gid;
            strncpy(ng->members, name, sizeof(ng->members) - 1);
            save_group();
        }
    }

    // Determine home directory
    if (!home[0]) {
        snprintf(home, sizeof(home), "/home/%s", name);
    }

    // Password
    if (!pass[0]) {
        cprint("Enter new password for "); cprint(name); cprint(": ");
        read_password_silent(pass, sizeof(pass));
        char confirm[64] = {0};
        cprint("Retype password: ");
        read_password_silent(confirm, sizeof(confirm));
        if (strcmp(pass, confirm) != 0) {
            cprintln("useradd: passwords do not match; user creation aborted");
            return 1;
        }
    }

    passwd_entry_t *nu = &s_users[s_user_count++];
    strncpy(nu->username, name, sizeof(nu->username) - 1);
    generate_salt(nu->salt);
    sha256_hash_password(nu->salt, pass, nu->hash);
    nu->uid = final_uid;
    nu->gid = final_gid;
    strncpy(nu->gecos, name, sizeof(nu->gecos) - 1);
    strncpy(nu->home, home, sizeof(nu->home) - 1);
    strcpy(nu->shell, "/bin/sh");

    if (!save_passwd()) {
        cprintln("useradd: failed to save /etc/passwd to disk");
        return 1;
    }

    // Create user home directory if it is under /home
    uint32_t home_dir_id = g_kapi->fs_find_child(0, "home");
    if (!home_dir_id) {
        home_dir_id = g_kapi->fs_create_entry("home", 2, 0);
        aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(home_dir_id);
        if (e) { e->mode = 0755; e->uid = 0; e->gid = 0; }
    }
    if (home_dir_id) {
        const char *dirname = name;
        if (strncmp(home, "/home/", 6) == 0 && home[6] != '\0') {
            dirname = home + 6;
        }
        uint32_t user_dir_id = g_kapi->fs_find_child(home_dir_id, dirname);
        if (!user_dir_id) {
            user_dir_id = g_kapi->fs_create_entry(dirname, 2, home_dir_id);
        }
        if (user_dir_id) {
            aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(user_dir_id);
            if (e) { e->mode = 0750; e->uid = final_uid; e->gid = final_gid; }
        }
    }
    g_kapi->fs_commit();

    cprint("useradd: user '"); cprint(name); cprint("' created with UID ");
    cprint_u32(final_uid); cprintln(".");
    return 0;
}

// userdel <name>
static int cmd_userdel(const char *arg) {
    if (!g_kapi || (g_kapi->session_uid && *g_kapi->session_uid != 0)) {
        cprintln("userdel: permission denied (superuser required)");
        return 1;
    }
    if (!arg || !*arg) {
        cprintln("Usage: userdel <username>");
        return 1;
    }
    load_passwd();
    load_group();
    if (strcmp(arg, "root") == 0) {
        cprintln("userdel: cannot delete root user");
        return 1;
    }
    int found_idx = -1;
    for (size_t i = 0; i < s_user_count; ++i) {
        if (strcmp(s_users[i].username, arg) == 0) {
            found_idx = (int)i;
            break;
        }
    }
    if (found_idx < 0) {
        cprint("userdel: user '"); cprint(arg); cprintln("' not found");
        return 1;
    }
    for (size_t i = found_idx; i + 1 < s_user_count; ++i) {
        s_users[i] = s_users[i + 1];
    }
    s_user_count--;
    save_passwd();
    cprint("userdel: user '"); cprint(arg); cprintln("' deleted.");
    return 0;
}

// usermod <name> [-g gid] [-d home]
static int cmd_usermod(const char *args) {
    if (!g_kapi || (g_kapi->session_uid && *g_kapi->session_uid != 0)) {
        cprintln("usermod: permission denied (superuser required)");
        return 1;
    }
    if (!args || !*args) {
        cprintln("Usage: usermod <username> [-g gid] [-d home]");
        return 1;
    }
    load_passwd();
    char buf[128];
    strncpy(buf, args, sizeof(buf) - 1);
    char *name = strtok(buf, " ");
    if (!name) return 1;
    passwd_entry_t *u = find_user_by_name(name);
    if (!u) {
        cprint("usermod: user '"); cprint(name); cprintln("' not found");
        return 1;
    }
    char *token;
    while ((token = strtok(NULL, " ")) != NULL) {
        if (strcmp(token, "-g") == 0) {
            char *v = strtok(NULL, " ");
            uint16_t gid;
            if (v && parse_u16(v, &gid)) u->gid = gid;
        } else if (strcmp(token, "-d") == 0) {
            char *v = strtok(NULL, " ");
            if (v) strncpy(u->home, v, sizeof(u->home) - 1);
        }
    }
    save_passwd();
    cprintln("usermod: user updated.");
    return 0;
}

// groupadd <name> [-g gid]
static int cmd_groupadd(const char *args) {
    if (!g_kapi || (g_kapi->session_uid && *g_kapi->session_uid != 0)) {
        cprintln("groupadd: permission denied (superuser required)");
        return 1;
    }
    if (!args || !*args) {
        cprintln("Usage: groupadd <groupname> [-g gid]");
        return 1;
    }
    load_group();
    if (s_group_count >= MAX_GROUPS) {
        cprintln("groupadd: maximum group limit reached");
        return 1;
    }
    char name[MAX_GROUPNAME_LEN] = {0};
    uint16_t req_gid = 0;
    int has_gid = 0;

    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1);
    char *token = strtok(buf, " ");
    while (token != NULL) {
        if (strcmp(token, "-g") == 0) {
            char *v = strtok(NULL, " ");
            uint16_t gid;
            if (v && parse_u16(v, &gid)) {
                has_gid = 1;
                req_gid = gid;
            }
        } else if (token[0] != '-') {
            strncpy(name, token, sizeof(name) - 1);
        }
        token = strtok(NULL, " ");
    }

    if (!name[0] || !validate_name(name)) {
        cprintln("groupadd: invalid group name");
        return 1;
    }
    if (find_group_by_name(name)) {
        cprintln("groupadd: group already exists");
        return 1;
    }
    uint16_t final_gid = 1000;
    if (has_gid) {
        if (find_group_by_gid(req_gid)) {
            cprintln("groupadd: GID already in use");
            return 1;
        }
        final_gid = req_gid;
    } else {
        while (find_group_by_gid(final_gid)) final_gid++;
    }

    group_entry_t *g = &s_groups[s_group_count++];
    strncpy(g->groupname, name, sizeof(g->groupname) - 1);
    g->gid = final_gid;
    g->members[0] = '\0';
    save_group();
    cprint("groupadd: group '"); cprint(name); cprint("' created with GID ");
    cprint_u32(final_gid); cprintln(".");
    return 0;
}

// groupdel <name>
static int cmd_groupdel(const char *arg) {
    if (!g_kapi || (g_kapi->session_uid && *g_kapi->session_uid != 0)) {
        cprintln("groupdel: permission denied (superuser required)");
        return 1;
    }
    if (!arg || !*arg) {
        cprintln("Usage: groupdel <groupname>");
        return 1;
    }
    load_group();
    if (strcmp(arg, "root") == 0) {
        cprintln("groupdel: cannot delete root group");
        return 1;
    }
    int idx = -1;
    for (size_t i = 0; i < s_group_count; ++i) {
        if (strcmp(s_groups[i].groupname, arg) == 0) {
            idx = (int)i;
            break;
        }
    }
    if (idx < 0) {
        cprint("groupdel: group '"); cprint(arg); cprintln("' not found");
        return 1;
    }
    for (size_t i = idx; i + 1 < s_group_count; ++i) {
        s_groups[i] = s_groups[i + 1];
    }
    s_group_count--;
    save_group();
    cprint("groupdel: group '"); cprint(arg); cprintln("' deleted.");
    return 0;
}

// login [username]
static int cmd_login(const char *arg) {
    load_passwd();
    load_group();
    char user[MAX_USERNAME_LEN] = {0};
    if (arg && *arg) {
        strncpy(user, arg, sizeof(user) - 1);
    } else {
        cprint("Aarambh OS login: ");
        read_line_echo(user, sizeof(user));
    }
    if (!user[0]) return 1;

    passwd_entry_t *u = find_user_by_name(user);
    char pass[64] = {0};
    cprint("Password: ");
    read_password_silent(pass, sizeof(pass));

    if (!u || !sha256_verify_password(u->salt, pass, u->hash)) {
        cprintln("Login incorrect.");
        return 1;
    }

    apply_session(u);

    // Switch CWD to user's home directory if possible
    if (g_kapi && g_kapi->fs_cwd && u->home[0] == '/') {
        // Simple navigation to home directory
        if (strcmp(u->home, "/root") == 0) {
            uint32_t rid = g_kapi->fs_find_child(0, "root");
            if (rid) *g_kapi->fs_cwd = rid;
        } else if (strncmp(u->home, "/home/", 6) == 0) {
            uint32_t hid = g_kapi->fs_find_child(0, "home");
            if (hid) {
                uint32_t uid = g_kapi->fs_find_child(hid, u->home + 6);
                if (uid) *g_kapi->fs_cwd = uid;
                else *g_kapi->fs_cwd = hid;
            }
        }
    }

    cprint("Welcome to Aarambh OS, "); cprint(u->username); cprintln("!");
    return 0;
}

// logout
static int cmd_logout(void) {
    cprintln("Logging out...");
    load_passwd();
    load_group();
    // Default to aarambh standard user if present, else root
    passwd_entry_t *u = find_user_by_name("aarambh");
    if (!u) u = find_user_by_name("root");
    if (u) {
        apply_session(u);
    } else {
        if (g_kapi->session_uid) *g_kapi->session_uid = 1000;
        if (g_kapi->session_gid) *g_kapi->session_gid = 1000;
        if (g_kapi->session_user) strcpy(g_kapi->session_user, "aarambh");
        if (g_kapi->session_home) strcpy(g_kapi->session_home, "/home/aarambh");
    }
    // Return CWD to root directory
    if (g_kapi && g_kapi->fs_cwd) *g_kapi->fs_cwd = 0;
    cprintln("Logged out. Switched to default session.");
    return 0;
}

// su [username]
static int cmd_su(const char *arg) {
    load_passwd();
    load_group();
    const char *target = (arg && *arg) ? arg : "root";
    passwd_entry_t *u = find_user_by_name(target);
    if (!u) {
        cprint("su: user '"); cprint(target); cprintln("' does not exist");
        return 1;
    }

    // Root (UID 0) can su to any user without entering password
    uint16_t cur_uid = g_kapi && g_kapi->session_uid ? *g_kapi->session_uid : 0;
    if (cur_uid != 0) {
        char pass[64] = {0};
        cprint("Password: ");
        read_password_silent(pass, sizeof(pass));
        if (!sha256_verify_password(u->salt, pass, u->hash)) {
            cprintln("su: Authentication failure");
            return 1;
        }
    }

    apply_session(u);
    cprint("Switched to user "); cprint(u->username); cprintln(".");
    return 0;
}

// passwd [username]
static int cmd_passwd(const char *arg) {
    load_passwd();
    uint16_t cur_uid = g_kapi && g_kapi->session_uid ? *g_kapi->session_uid : 0;
    const char *target = (arg && *arg) ? arg : (g_kapi ? g_kapi->session_user : "root");
    passwd_entry_t *u = find_user_by_name(target);
    if (!u) {
        cprint("passwd: user '"); cprint(target); cprintln("' not found");
        return 1;
    }
    if (cur_uid != 0 && u->uid != cur_uid) {
        cprintln("passwd: permission denied (cannot change password for other users)");
        return 1;
    }
    if (cur_uid != 0) {
        char curr_pass[64] = {0};
        cprint("Current password: ");
        read_password_silent(curr_pass, sizeof(curr_pass));
        if (!sha256_verify_password(u->salt, curr_pass, u->hash)) {
            cprintln("passwd: Authentication failure");
            return 1;
        }
    }

    char new_pass[64] = {0};
    char confirm[64] = {0};
    cprint("New password: ");
    read_password_silent(new_pass, sizeof(new_pass));
    cprint("Retype new password: ");
    read_password_silent(confirm, sizeof(confirm));
    if (strcmp(new_pass, confirm) != 0) {
        cprintln("passwd: passwords do not match; password unchanged");
        return 1;
    }

    generate_salt(u->salt);
    sha256_hash_password(u->salt, new_pass, u->hash);
    save_passwd();
    cprintln("passwd: password updated successfully.");
    return 0;
}

// chmod <octal> <file>
static int cmd_chmod(const char *args) {
    if (!args || !*args) {
        cprintln("Usage: chmod <octal_mode> <file>");
        return 1;
    }
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1);
    char *mode_str = strtok(buf, " ");
    char *file = strtok(NULL, " ");
    if (!mode_str || !file) {
        cprintln("Usage: chmod <octal_mode> <file>");
        return 1;
    }
    uint16_t mode;
    if (!parse_octal(mode_str, &mode)) {
        cprintln("chmod: invalid octal mode (expected 0000..0777)");
        return 1;
    }
    if (!g_kapi) return 1;
    uint32_t id = g_kapi->fs_find_child(g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0, file);
    if (!id) {
        cprint("chmod: cannot access '"); cprint(file); cprintln("': No such file or directory");
        return 1;
    }
    aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(id);
    if (!e || e->status == 0) return 1;

    uint16_t cur_uid = g_kapi->session_uid ? *g_kapi->session_uid : 0;
    if (cur_uid != 0 && cur_uid != e->uid) {
        cprintln("chmod: changing permissions: Operation not permitted");
        return 1;
    }
    e->mode = mode;
    g_kapi->fs_commit();
    return 0;
}

// chown <user>[:group] <file>
static int cmd_chown(const char *args) {
    if (!g_kapi || (g_kapi->session_uid && *g_kapi->session_uid != 0)) {
        cprintln("chown: changing ownership: Operation not permitted (superuser required)");
        return 1;
    }
    if (!args || !*args) {
        cprintln("Usage: chown <user>[:group] <file>");
        return 1;
    }
    load_passwd();
    load_group();
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1);
    char *ug = strtok(buf, " ");
    char *file = strtok(NULL, " ");
    if (!ug || !file) {
        cprintln("Usage: chown <user>[:group] <file>");
        return 1;
    }

    char *colon = strchr(ug, ':');
    char user_part[32] = {0};
    char grp_part[32] = {0};
    if (colon) {
        *colon = '\0';
        strncpy(user_part, ug, sizeof(user_part) - 1);
        strncpy(grp_part, colon + 1, sizeof(grp_part) - 1);
    } else {
        strncpy(user_part, ug, sizeof(user_part) - 1);
    }

    passwd_entry_t *u = find_user_by_name(user_part);
    if (!u) {
        cprint("chown: invalid user: '"); cprint(user_part); cprintln("'");
        return 1;
    }
    uint16_t new_gid = u->gid;
    if (grp_part[0]) {
        group_entry_t *g = find_group_by_name(grp_part);
        if (!g) {
            cprint("chown: invalid group: '"); cprint(grp_part); cprintln("'");
            return 1;
        }
        new_gid = g->gid;
    }

    uint32_t id = g_kapi->fs_find_child(g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0, file);
    if (!id) {
        cprint("chown: cannot access '"); cprint(file); cprintln("': No such file or directory");
        return 1;
    }
    aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(id);
    if (!e || e->status == 0) return 1;

    e->uid = u->uid;
    if (colon || !grp_part[0]) {
        e->gid = new_gid;
    }
    g_kapi->fs_commit();
    return 0;
}

// chgrp <group> <file>
static int cmd_chgrp(const char *args) {
    if (!args || !*args) {
        cprintln("Usage: chgrp <group> <file>");
        return 1;
    }
    load_group();
    char buf[64];
    strncpy(buf, args, sizeof(buf) - 1);
    char *grp_str = strtok(buf, " ");
    char *file = strtok(NULL, " ");
    if (!grp_str || !file) return 1;

    group_entry_t *g = find_group_by_name(grp_str);
    if (!g) {
        cprint("chgrp: invalid group: '"); cprint(grp_str); cprintln("'");
        return 1;
    }
    uint32_t id = g_kapi->fs_find_child(g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0, file);
    if (!id) {
        cprint("chgrp: cannot access '"); cprint(file); cprintln("': No such file or directory");
        return 1;
    }
    aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(id);
    if (!e || e->status == 0) return 1;

    uint16_t cur_uid = g_kapi->session_uid ? *g_kapi->session_uid : 0;
    if (cur_uid != 0 && cur_uid != e->uid) {
        cprintln("chgrp: changing group: Operation not permitted");
        return 1;
    }
    e->gid = g->gid;
    g_kapi->fs_commit();
    return 0;
}

// stat <file>
static int cmd_stat(const char *arg) {
    if (!arg || !*arg) {
        cprintln("Usage: stat <file>");
        return 1;
    }
    if (!g_kapi) return 1;
    load_passwd();
    load_group();

    uint32_t id = g_kapi->fs_find_child(g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0, arg);
    if (!id) {
        cprint("stat: cannot stat '"); cprint(arg); cprintln("': No such file or directory");
        return 1;
    }
    aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(id);
    if (!e || e->status == 0) return 1;

    uint16_t mode = e->mode;
    if (mode == 0) {
        mode = (e->type == 2) ? 0755 : 0644;
    }
    passwd_entry_t *u = find_user_by_uid(e->uid);
    group_entry_t *g = find_group_by_gid(e->gid);
    char modestr[11];
    format_mode_str(mode, (e->type == 2), modestr);

    cprint("  File: "); cprintln(e->name);
    cprint("  Size: "); cprint_u32(e->size);
    cprint("  Type: "); cprintln(e->type == 2 ? "directory" : "regular file");
    cprint(" Inode: "); cprint_u32(id);
    cprint("  Parent: "); cprint_u32(e->parent_id); cprint("\n");
    cprint("Access: (0");
    cprint_u32((mode >> 6) & 7);
    cprint_u32((mode >> 3) & 7);
    cprint_u32(mode & 7);
    cprint("/"); cprint(modestr); cprint(")  ");
    cprint("Uid: ("); cprint_u32(e->uid); cprint("/"); cprint(u ? u->username : "unknown"); cprint(")   ");
    cprint("Gid: ("); cprint_u32(e->gid); cprint("/"); cprint(g ? g->groupname : "unknown"); cprintln(")");
    return 0;
}

// ---------------- DISPATCHER ENTRY POINT ----------------

int sys_dispatch(const char *cmd, const char *args, const kernel_api_t *kapi) {
    g_kapi = kapi;
    auth_init(kapi);
    extern void vfs_init(const kernel_api_t *kapi);
    vfs_init(kapi);
    if (!cmd || !*cmd) {
        net_boot_init();
        return 0;
    }

    if (strcmp(cmd, "whoami") == 0) return cmd_whoami();
    if (strcmp(cmd, "id") == 0) return cmd_id(args);
    if (strcmp(cmd, "users") == 0) return cmd_users();
    if (strcmp(cmd, "groups") == 0) return cmd_groups(args);
    if (strcmp(cmd, "useradd") == 0) return cmd_useradd(args);
    if (strcmp(cmd, "userdel") == 0) return cmd_userdel(args);
    if (strcmp(cmd, "usermod") == 0) return cmd_usermod(args);
    if (strcmp(cmd, "groupadd") == 0) return cmd_groupadd(args);
    if (strcmp(cmd, "groupdel") == 0) return cmd_groupdel(args);
    if (strcmp(cmd, "login") == 0) return cmd_login(args);
    if (strcmp(cmd, "logout") == 0) return cmd_logout();
    if (strcmp(cmd, "su") == 0) return cmd_su(args);
    if (strcmp(cmd, "passwd") == 0) return cmd_passwd(args);
    if (strcmp(cmd, "chmod") == 0) return cmd_chmod(args);
    if (strcmp(cmd, "chown") == 0) return cmd_chown(args);
    if (strcmp(cmd, "chgrp") == 0) return cmd_chgrp(args);
    if (strcmp(cmd, "stat") == 0) return cmd_stat(args);

    if (strcmp(cmd, "network") == 0) return cmd_network(args);
    if (strcmp(cmd, "wget") == 0) return cmd_wget(args);
    if (strcmp(cmd, "curl") == 0) return cmd_curl(args);
    if (strcmp(cmd, "git") == 0) return cmd_git(args);
    if (strcmp(cmd, "ping") == 0) return cmd_ping(args);
    if (strcmp(cmd, "ifconfig") == 0) return cmd_ifconfig(args);
    if (strcmp(cmd, "desktop") == 0 || strcmp(cmd, "gui") == 0) {
        extern int cmd_desktop(const char *args);
        return cmd_desktop(args);
    }
    if (strcmp(cmd, "run") == 0 || strcmp(cmd, "sh") == 0) {
        extern int cmd_run(const char *args);
        return cmd_run(args);
    }
    if (strcmp(cmd, "dpkg") == 0 || strcmp(cmd, "deb") == 0) {
        extern int cmd_dpkg(const char *args);
        return cmd_dpkg(args);
    }
    if (strcmp(cmd, "elfinfo") == 0 || strcmp(cmd, "elf") == 0) {
        extern int cmd_elfinfo(const char *args);
        return cmd_elfinfo(args);
    }
    if (strcmp(cmd, "rpm") == 0) {
        extern int cmd_rpm(const char *args);
        return cmd_rpm(args);
    }
    if (strcmp(cmd, "peinfo") == 0 || strcmp(cmd, "wine-check") == 0) {
        extern int cmd_peinfo(const char *args);
        return cmd_peinfo(args);
    }
    if (strcmp(cmd, "sysinfo") == 0) {
        extern int cmd_sysinfo(const char *args);
        return cmd_sysinfo(args);
    }
    if (strcmp(cmd, "hwinfo") == 0) {
        extern int cmd_hwinfo(const char *args);
        return cmd_hwinfo(args);
    }
    if (strcmp(cmd, "compat") == 0) {
        extern int cmd_compat(const char *args);
        return cmd_compat(args);
    }

    cprint("sys_dispatch: unknown command '"); cprint(cmd); cprintln("'");
    return 1;
}

int auth_gui_verify(const char *user, const char *pass) {
    load_passwd();
    load_group();
    passwd_entry_t *u = find_user_by_name(user);
    if (!u) return 0;
    if (u->hash[0] == '\0' && pass[0] == '\0') {
        apply_session(u);
        return 1;
    }
    if (sha256_verify_password(u->salt, pass, u->hash)) {
        apply_session(u);
        return 1;
    }
    return 0;
}

int auth_gui_change_password(const char *user, const char *new_pass) {
    load_passwd();
    passwd_entry_t *u = find_user_by_name(user);
    if (!u) return 0;
    generate_salt(u->salt);
    sha256_hash_password(u->salt, new_pass, u->hash);
    save_passwd();
    return 1;
}

int auth_gui_ensure_gentix(void) {
    load_passwd();
    passwd_entry_t *u = find_user_by_name("Gentix");
    if (!u) {
        if (s_user_count < MAX_USERS) {
            u = &s_users[s_user_count++];
            strncpy(u->username, "Gentix", sizeof(u->username) - 1);
            generate_salt(u->salt);
            sha256_hash_password(u->salt, "password", u->hash);
            u->uid = 1000;
            u->gid = 1000;
            strncpy(u->gecos, "Gentix User", sizeof(u->gecos) - 1);
            strncpy(u->home, "/home/gentix", sizeof(u->home) - 1);
            strncpy(u->shell, "/bin/sh", sizeof(u->shell) - 1);
            save_passwd();
        }
    }
    return 1;
}
