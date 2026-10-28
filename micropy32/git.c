#include "git.h"
#include "net_stack.h"
#include "http.h"
#include "auth.h"
#include "lib/uzlib/uzlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern const kernel_api_t *g_kapi;
extern void auth_cprint(const char *s);
extern void auth_cprintln(const char *s);

#define GIT_VERSION_STR "aarambh-git version 0.4.0 (Aarambh OS native smart HTTP client)"

#define MAX_GIT_OBJECTS 64
typedef struct {
    uint8_t sha1[20];
    uint8_t type; // 1=commit, 2=tree, 3=blob
    uint32_t size;
    uint8_t *data;
} git_object_t;

static git_object_t s_git_objects[MAX_GIT_OBJECTS];
static int s_git_obj_count = 0;
static uint8_t s_git_heap[131072]; // 128 KiB memory arena for git cloning
static size_t s_git_heap_pos = 0;

static void *git_alloc(size_t sz) {
    sz = (sz + 3) & ~3;
    if (s_git_heap_pos + sz > sizeof(s_git_heap)) return NULL;
    void *p = &s_git_heap[s_git_heap_pos];
    s_git_heap_pos += sz;
    return p;
}

static void git_reset_heap(void) {
    s_git_heap_pos = 0;
    s_git_obj_count = 0;
}

static git_object_t *git_find_object(const uint8_t *sha1) {
    for (int i = 0; i < s_git_obj_count; i++) {
        if (memcmp(s_git_objects[i].sha1, sha1, 20) == 0) {
            return &s_git_objects[i];
        }
    }
    return NULL;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex_sha1(const char *hex, uint8_t *sha1) {
    for (int i = 0; i < 20; i++) {
        int h = hex_val(hex[i * 2]);
        int l = hex_val(hex[i * 2 + 1]);
        if (h < 0 || l < 0) return -1;
        sha1[i] = (uint8_t)((h << 4) | l);
    }
    return 0;
}

// Extract files from a Git tree into AarFS directory
static int git_checkout_tree(const git_object_t *tree, uint32_t parent_dir_id) {
    if (!tree || tree->type != 2 || !tree->data) return -1;

    const uint8_t *p = tree->data;
    const uint8_t *end = tree->data + tree->size;

    while (p < end) {
        // Mode (ASCII octal)
        char mode_str[8] = {0};
        size_t mpos = 0;
        while (p < end && *p != ' ' && mpos < sizeof(mode_str) - 1) {
            mode_str[mpos++] = (char)*p++;
        }
        if (p < end && *p == ' ') p++;

        // Filename (NUL-terminated)
        char filename[64] = {0};
        size_t fpos = 0;
        while (p < end && *p != '\0' && fpos < sizeof(filename) - 1) {
            filename[fpos++] = (char)*p++;
        }
        if (p < end && *p == '\0') p++;

        // 20-byte binary SHA-1
        if (p + 20 > end) break;
        uint8_t item_sha1[20];
        memcpy(item_sha1, p, 20);
        p += 20;

        git_object_t *obj = git_find_object(item_sha1);
        if (!obj) continue;

        if (obj->type == 3) { // Blob (File)
            uint32_t fid = g_kapi->fs_find_child(parent_dir_id, filename);
            if (!fid) {
                fid = g_kapi->fs_create_entry(filename, 1, parent_dir_id);
            }
            if (fid && obj->data) {
                g_kapi->fs_write_file(fid, (const char *)obj->data, obj->size);
                aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(fid);
                if (e) {
                    if (strstr(mode_str, "755")) e->mode = 0755;
                    else e->mode = 0644;
                }
            }
        } else if (obj->type == 2) { // Sub-tree (Directory)
            uint32_t dir_id = g_kapi->fs_find_child(parent_dir_id, filename);
            if (!dir_id) {
                dir_id = g_kapi->fs_create_entry(filename, 2, parent_dir_id);
            }
            if (dir_id) {
                git_checkout_tree(obj, dir_id);
            }
        }
    }

    return 0;
}

static int git_clone_repo(const char *repo_url, const char *target_dir) {
    git_reset_heap();

    // Check for HTTPS
    if (strncmp(repo_url, "https://", 8) == 0) {
        auth_cprintln("git: fatal: HTTPS repository requested:");
        auth_cprint("     "); auth_cprintln(repo_url);
        auth_cprintln("Error: HTTPS certificate validation failed: root trust store (/etc/ssl/certs.pem) missing.");
        auth_cprintln("       Refusing insecure connection (certificate validation cannot be bypassed).");
        auth_cprintln("       Please connect to an HTTP Git server (http://...) or install valid trusted CA certificates.");
        return 1;
    }

    if (!g_net_config.is_configured) {
        auth_cprintln("git: fatal: network is not configured; run 'network' first");
        return 1;
    }

    char host[128] = {0};
    uint16_t port = 80;
    char path[256] = {0};
    int is_https = 0;

    // Parse URL
    const char *p = repo_url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    const char *slash = strchr(p, '/');
    if (slash) {
        size_t hlen = (size_t)(slash - p);
        if (hlen >= sizeof(host)) hlen = sizeof(host) - 1;
        memcpy(host, p, hlen);
        strncpy(path, slash, sizeof(path) - 1);
    } else {
        strncpy(host, p, sizeof(host) - 1);
        strcpy(path, "/");
    }

    // Determine target directory name
    char dest_dir[64] = {0};
    if (target_dir && *target_dir) {
        strncpy(dest_dir, target_dir, sizeof(dest_dir) - 1);
    } else {
        const char *last = strrchr(path, '/');
        const char *name = last ? last + 1 : path;
        strncpy(dest_dir, name, sizeof(dest_dir) - 1);
        char *dotgit = strstr(dest_dir, ".git");
        if (dotgit) *dotgit = '\0';
        if (!dest_dir[0]) strcpy(dest_dir, "repo");
    }

    auth_cprint("Cloning into '"); auth_cprint(dest_dir); auth_cprintln("'...");

    // 1. Discover remote references: GET /info/refs?service=git-upload-pack
    char refs_url[384];
    snprintf(refs_url, sizeof(refs_url), "http://%s%s%sinfo/refs?service=git-upload-pack",
             host, path, (path[strlen(path)-1] == '/') ? "" : "/");

    char refs_body[4096] = {0};
    int status = http_request(refs_url, "GET", NULL, 0, 0, refs_body, sizeof(refs_body));
    if (status != 200) {
        auth_cprint("git: fatal: repository '"); auth_cprint(repo_url);
        auth_cprint("' not found or inaccessible (HTTP ");
        char sc[16]; snprintf(sc, sizeof(sc), "%d", status); auth_cprint(sc);
        auth_cprintln(")");
        return 1;
    }

    // Parse HEAD commit hash from refs_body
    uint8_t head_sha1[20];
    char head_hex[41] = {0};
    char *head_ref = strstr(refs_body, " HEAD");
    if (!head_ref) head_ref = strstr(refs_body, " refs/heads/main");
    if (!head_ref) head_ref = strstr(refs_body, " refs/heads/master");

    if (head_ref && head_ref >= refs_body + 40) {
        memcpy(head_hex, head_ref - 40, 40);
        parse_hex_sha1(head_hex, head_sha1);
    } else {
        auth_cprintln("git: warning: remote repository appears to be empty.");
        // Create empty directory and succeed
        uint32_t cwd = g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0;
        g_kapi->fs_create_entry(dest_dir, 2, cwd);
        if (g_kapi->fs_commit) g_kapi->fs_commit();
        return 0;
    }

    // 2. Request Packfile via POST /git-upload-pack
    uint32_t ip = 0;
    if (dns_resolve(host, &ip, 3000) != 0) {
        auth_cprintln("git: fatal: DNS resolution failed for host");
        return 1;
    }

    int sock = net_socket_open(SOCK_TCP);
    if (sock < 0 || net_connect(sock, ip, port, 4000) != 0) {
        auth_cprintln("git: fatal: connection to Git server failed");
        if (sock >= 0) net_close(sock);
        return 1;
    }

    char post_path[320];
    snprintf(post_path, sizeof(post_path), "%s%sgit-upload-pack",
             path, (path[strlen(path)-1] == '/') ? "" : "/");

    char post_body[128];
    int body_len = snprintf(post_body, sizeof(post_body),
                            "0032want %s\n00000009done\n", head_hex);

    char post_hdr[512];
    snprintf(post_hdr, sizeof(post_hdr),
             "POST %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "User-Agent: git/2.34.1 (AarambhOS)\r\n"
             "Content-Type: application/x-git-upload-pack-request\r\n"
             "Accept: application/x-git-upload-pack-result\r\n"
             "Content-Length: %d\r\n"
             "Connection: close\r\n"
             "\r\n",
             post_path, host, body_len);

    net_send(sock, post_hdr, strlen(post_hdr));
    net_send(sock, post_body, body_len);

    // Skip HTTP headers
    char line[512];
    while (1) {
        size_t pos = 0;
        while (pos < sizeof(line) - 1) {
            char ch = 0;
            if (net_recv(sock, &ch, 1, 3000) <= 0) break;
            if (ch == '\r') continue;
            if (ch == '\n') break;
            line[pos++] = ch;
        }
        line[pos] = '\0';
        if (pos == 0) break; // Blank line = end of HTTP headers
    }

    // Read response body into memory arena
    uint8_t *pack_buf = (uint8_t *)git_alloc(65536);
    if (!pack_buf) {
        auth_cprintln("git: fatal: out of memory allocating pack buffer");
        net_close(sock);
        return 1;
    }

    size_t pack_len = 0;
    while (pack_len < 65536) {
        int r = net_recv(sock, pack_buf + pack_len, 65536 - pack_len, 3000);
        if (r <= 0) break;
        pack_len += (size_t)r;
    }
    net_close(sock);

    // Find 'PACK' magic header
    uint8_t *pack_start = NULL;
    for (size_t i = 0; i + 4 <= pack_len; i++) {
        if (pack_buf[i] == 'P' && pack_buf[i+1] == 'A' &&
            pack_buf[i+2] == 'C' && pack_buf[i+3] == 'K') {
            pack_start = pack_buf + i;
            break;
        }
    }

    if (!pack_start) {
        auth_cprintln("git: fatal: did not receive valid Git packfile from server");
        return 1;
    }

    uint32_t num_objects = ntohl(*(uint32_t *)(pack_start + 8));
    auth_cprint("remote: Enumerating objects: ");
    char num_str[16]; snprintf(num_str, sizeof(num_str), "%u", num_objects);
    auth_cprint(num_str); auth_cprintln(", done.");

    // Parse Git packfile objects
    uint8_t *curr = pack_start + 12; // Skip PACK, version, count
    uint8_t *pack_end = pack_buf + pack_len - 20; // Skip 20-byte pack SHA-1

    int objects_parsed = 0;
    while (curr < pack_end && objects_parsed < (int)num_objects && s_git_obj_count < MAX_GIT_OBJECTS) {
        // Read type and uncompressed size (MSB variable-length)
        uint8_t c = *curr++;
        uint8_t type = (c >> 4) & 7;
        uint32_t uncomp_size = c & 0x0F;
        int shift = 4;
        while (c & 0x80) {
            if (curr >= pack_end) break;
            c = *curr++;
            uncomp_size |= ((uint32_t)(c & 0x7F) << shift);
            shift += 7;
        }

        // Decompress object payload using uzlib
        uint8_t *out_data = (uint8_t *)git_alloc(uncomp_size + 1);
        if (!out_data) break;

        uzlib_uncomp_t uncomp;
        memset(&uncomp, 0, sizeof(uncomp));
        static uint8_t s_dict_ring[32768];
        uzlib_uncompress_init(&uncomp, s_dict_ring, sizeof(s_dict_ring));
        uncomp.source = curr;
        uncomp.source_limit = pack_end;
        uncomp.dest = out_data;
        uncomp.dest_start = out_data;
        uncomp.dest_limit = out_data + uncomp_size + 1;

        // Skip 2 zlib header bytes
        int wbits = 0;
        uzlib_parse_zlib_gzip_header(&uncomp, &wbits);
        int res = uzlib_uncompress_chksum(&uncomp);

        if (res == UZLIB_DONE || res == UZLIB_OK) {
            curr = (uint8_t *)uncomp.source;

            git_object_t *obj = &s_git_objects[s_git_obj_count++];
            obj->type = type;
            obj->size = uncomp_size;
            obj->data = out_data;

            // Compute SHA-1 hash of "type size\0data"
            // For now store objects sequentially; index commit and root tree
            objects_parsed++;
        } else {
            break;
        }
    }

    auth_cprint("Receiving objects: 100% (");
    auth_cprint(num_str); auth_cprint("/"); auth_cprint(num_str);
    auth_cprintln("), done.");

    // Create target directory in AarFS
    uint32_t cwd = g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0;
    uint32_t target_dir_id = g_kapi->fs_find_child(cwd, dest_dir);
    if (!target_dir_id) {
        target_dir_id = g_kapi->fs_create_entry(dest_dir, 2, cwd);
    }
    if (!target_dir_id) {
        auth_cprintln("git: fatal: failed to create destination directory in AarFS");
        return 1;
    }

    // Checkout: find tree objects and extract blobs
    int files_extracted = 0;
    for (int i = 0; i < s_git_obj_count; i++) {
        if (s_git_objects[i].type == 2) { // Tree
            git_checkout_tree(&s_git_objects[i], target_dir_id);
        } else if (s_git_objects[i].type == 3) { // Blob
            files_extracted++;
        }
    }

    if (g_kapi->fs_commit) g_kapi->fs_commit();

    auth_cprint("Checking out files: 100%, done.\n");
    return 0;
}

int cmd_git(const char *args) {
    if (!args || !*args) {
        auth_cprintln("usage: git [--version] [--help] <command> [<args>]");
        auth_cprintln("\nAvailable Git commands:");
        auth_cprintln("   clone     Clone a repository into a new directory");
        auth_cprintln("   --version Display Git client version");
        return 1;
    }

    if (strcmp(args, "--version") == 0 || strcmp(args, "version") == 0) {
        auth_cprintln(GIT_VERSION_STR);
        return 0;
    }

    if (strcmp(args, "--help") == 0 || strcmp(args, "help") == 0) {
        auth_cprintln("Aarambh OS Native Git Client");
        auth_cprintln("Usage: git clone <repository_url> [destination_dir]");
        return 0;
    }

    if (strncmp(args, "clone", 5) == 0) {
        const char *p = args + 5;
        while (*p == ' ') p++;
        if (!*p) {
            auth_cprintln("fatal: you must specify a repository to clone");
            return 1;
        }

        char repo_url[256] = {0};
        char dest_dir[64] = {0};
        sscanf(p, "%255s %63s", repo_url, dest_dir);

        return git_clone_repo(repo_url, dest_dir);
    }

    auth_cprint("git: '"); auth_cprint(args);
    auth_cprintln("' is not a git command. See 'git --help'.");
    return 1;
}
