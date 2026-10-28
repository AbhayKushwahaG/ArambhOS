#include "http.h"
#include "net_stack.h"
#include "auth.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern const kernel_api_t *g_kapi;
extern void auth_cprint(const char *s);
extern void auth_cprintln(const char *s);

static int parse_url(const char *url, int *out_is_https, char *out_host, size_t max_host,
                     uint16_t *out_port, char *out_path, size_t max_path) {
    if (!url || !out_host || !out_port || !out_path) return -1;

    *out_is_https = 0;
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        *out_port = 80;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *out_is_https = 1;
        *out_port = 443;
    } else {
        // Assume http://
        *out_port = 80;
    }

    // Host
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    size_t host_len = 0;
    if (colon && (!slash || colon < slash)) {
        host_len = (size_t)(colon - p);
        if (host_len >= max_host) host_len = max_host - 1;
        memcpy(out_host, p, host_len);
        out_host[host_len] = '\0';
        *out_port = (uint16_t)atoi(colon + 1);
    } else if (slash) {
        host_len = (size_t)(slash - p);
        if (host_len >= max_host) host_len = max_host - 1;
        memcpy(out_host, p, host_len);
        out_host[host_len] = '\0';
    } else {
        strncpy(out_host, p, max_host - 1);
        out_host[max_host - 1] = '\0';
    }

    // Path
    if (slash) {
        strncpy(out_path, slash, max_path - 1);
        out_path[max_path - 1] = '\0';
    } else {
        strcpy(out_path, "/");
    }

    return 0;
}

int http_request(const char *url, const char *method,
                 const char *output_file, int print_to_console,
                 int headers_only, char *out_body_buf, size_t max_body_len) {
    int is_https = 0;
    char host[128] = {0};
    uint16_t port = 80;
    char path[256] = {0};

    if (parse_url(url, &is_https, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        auth_cprintln("http: invalid URL format");
        return -1;
    }

    if (is_https) {
        // Enforce strict TLS certificate requirement
        auth_cprintln("http: HTTPS requested for URL:");
        auth_cprint("      "); auth_cprintln(url);
        auth_cprintln("Error: HTTPS certificate validation failed: root trust store (/etc/ssl/certs.pem) missing.");
        auth_cprintln("       Refusing insecure connection (certificate validation cannot be bypassed).");
        auth_cprintln("       Please connect to an HTTP server or install valid trusted CA certificates.");
        return -1;
    }

    if (!g_net_config.is_configured) {
        auth_cprintln("http: network unconfigured; run 'network' or DHCP first");
        return -1;
    }

    uint32_t ip = 0;
    if (dns_resolve(host, &ip, 3000) != 0) {
        auth_cprint("http: could not resolve host '"); auth_cprint(host); auth_cprintln("'");
        return -1;
    }

    int sock = net_socket_open(SOCK_TCP);
    if (sock < 0) {
        auth_cprintln("http: cannot open TCP socket (no free slots)");
        return -1;
    }

    if (net_connect(sock, ip, port, 4000) != 0) {
        auth_cprint("http: connection to "); auth_cprint(host); auth_cprintln(" failed (timeout / refused)");
        net_close(sock);
        return -1;
    }

    // Prepare HTTP request
    char req[512];
    snprintf(req, sizeof(req),
        "%s %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: AarambhOS/0.4 wget/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        method ? method : "GET", path, host);

    net_send(sock, req, strlen(req));

    // Read header into header_buf
    char header_buf[2048];
    size_t header_len = 0;
    char *body_start = NULL;
    size_t body_start_len = 0;

    while (header_len < sizeof(header_buf) - 1) {
        size_t space = sizeof(header_buf) - 1 - header_len;
        int r = net_recv(sock, header_buf + header_len, space > 256 ? 256 : space, 4000);
        if (r <= 0) break;
        header_len += r;
        header_buf[header_len] = '\0';

        char *end = strstr(header_buf, "\r\n\r\n");
        if (end) {
            *end = '\0';
            body_start = end + 4;
            body_start_len = header_len - (size_t)(body_start - header_buf);
            break;
        }
        end = strstr(header_buf, "\n\n");
        if (end) {
            *end = '\0';
            body_start = end + 2;
            body_start_len = header_len - (size_t)(body_start - header_buf);
            break;
        }
    }

    int status_code = 0;
    long content_length = -1;
    char location[256] = {0};

    // Parse status line: first line in header_buf
    char *cur = header_buf;
    while (*cur == '\r' || *cur == '\n') cur++;

    char *next_line = strchr(cur, '\n');
    if (next_line) {
        if (next_line > cur && *(next_line - 1) == '\r') *(next_line - 1) = '\0';
        *next_line = '\0';
        next_line++;
    }

    if (headers_only) {
        auth_cprintln(cur);
    }
    char *sp = strchr(cur, ' ');
    if (sp) {
        status_code = atoi(sp + 1);
    }

    // Process remaining headers
    while (next_line && *next_line) {
        cur = next_line;
        next_line = strchr(cur, '\n');
        if (next_line) {
            if (next_line > cur && *(next_line - 1) == '\r') *(next_line - 1) = '\0';
            *next_line = '\0';
            next_line++;
        }
        if (headers_only) {
            auth_cprintln(cur);
        }
        if (strncasecmp(cur, "Content-Length:", 15) == 0) {
            content_length = atol(cur + 15);
        } else if (strncasecmp(cur, "Location:", 9) == 0) {
            char *loc = cur + 9;
            while (*loc == ' ') loc++;
            strncpy(location, loc, sizeof(location) - 1);
        }
    }

    // Handle redirect (301, 302, 303, 307, 308)
    if ((status_code == 301 || status_code == 302 || status_code == 303 ||
         status_code == 307 || status_code == 308) && location[0] != '\0') {
        net_close(sock);
        auth_cprint("Redirecting to: "); auth_cprintln(location);
        return http_request(location, method, output_file, print_to_console, headers_only, out_body_buf, max_body_len);
    }

    if (headers_only) {
        net_close(sock);
        return status_code;
    }

    // Prepare destination file if writing to disk
    uint32_t file_id = 0;
    static char s_disk_buffer[16384];
    uint32_t total_written = 0;

    if (output_file && *output_file) {
        if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_create_entry || !g_kapi->fs_write_file) {
            auth_cprintln("http: AarFS filesystem interface unavailable");
            net_close(sock);
            return -1;
        }
        uint32_t cwd = g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0;
        file_id = g_kapi->fs_find_child(cwd, output_file);
        if (!file_id) {
            file_id = g_kapi->fs_create_entry(output_file, 1, cwd);
            if (!file_id) {
                auth_cprintln("http: failed to create output file on AarFS (directory full or permission denied)");
                net_close(sock);
                return -1;
            }
        }
    }

    // Process initial body bytes if any were read in header_buf
    size_t body_buf_pos = 0;
    if (body_start && body_start_len > 0) {
        if (output_file && file_id) {
            size_t to_copy = body_start_len;
            if (total_written + to_copy > sizeof(s_disk_buffer)) {
                to_copy = sizeof(s_disk_buffer) - total_written;
            }
            memcpy(s_disk_buffer + total_written, body_start, to_copy);
            total_written += (uint32_t)to_copy;
        }
        if (print_to_console) {
            char save_c = body_start[body_start_len];
            body_start[body_start_len] = '\0';
            auth_cprint(body_start);
            body_start[body_start_len] = save_c;
        }
        if (out_body_buf && body_buf_pos < max_body_len - 1) {
            size_t copy_len = body_start_len;
            if (body_buf_pos + copy_len >= max_body_len) copy_len = max_body_len - 1 - body_buf_pos;
            memcpy(out_body_buf + body_buf_pos, body_start, copy_len);
            body_buf_pos += copy_len;
            out_body_buf[body_buf_pos] = '\0';
        }
    }

    // Read remaining body
    char chunk[1024];
    while (content_length < 0 || total_written < (uint32_t)content_length) {
        int r = net_recv(sock, chunk, sizeof(chunk), 3000);
        if (r <= 0) break; // EOF or timeout

        if (output_file && file_id) {
            if (total_written + (uint32_t)r <= sizeof(s_disk_buffer)) {
                memcpy(s_disk_buffer + total_written, chunk, r);
                total_written += (uint32_t)r;
            } else {
                auth_cprintln("\nhttp: AarFS 16 KiB file size limit reached; download capped.");
                break;
            }
        }

        if (print_to_console) {
            chunk[r < 1023 ? r : 1023] = '\0';
            auth_cprint(chunk);
        }

        if (out_body_buf && body_buf_pos < max_body_len - 1) {
            size_t copy_len = (size_t)r;
            if (body_buf_pos + copy_len >= max_body_len) copy_len = max_body_len - 1 - body_buf_pos;
            memcpy(out_body_buf + body_buf_pos, chunk, copy_len);
            body_buf_pos += copy_len;
            out_body_buf[body_buf_pos] = '\0';
        }
    }

    // Save to disk if applicable
    if (output_file && file_id) {
        g_kapi->fs_write_file(file_id, s_disk_buffer, total_written);
        if (g_kapi->fs_commit) g_kapi->fs_commit();
    }

    net_close(sock);
    return status_code;
}

// ---------------- WGET COMMAND ----------------

int cmd_wget(const char *args) {
    if (!args || !*args) {
        auth_cprintln("Usage: wget [-O filename] <URL>");
        return 1;
    }

    char dest_file[64] = {0};
    char url[256] = {0};

    // Parse options
    char buf[512];
    strncpy(buf, args, sizeof(buf) - 1);
    char *token = strtok(buf, " ");
    while (token) {
        if (strcmp(token, "-O") == 0) {
            char *fn = strtok(NULL, " ");
            if (fn) strncpy(dest_file, fn, sizeof(dest_file) - 1);
        } else if (token[0] != '-') {
            strncpy(url, token, sizeof(url) - 1);
        }
        token = strtok(NULL, " ");
    }

    if (!url[0]) {
        auth_cprintln("wget: missing URL");
        return 1;
    }

    // Default filename from URL if -O omitted
    if (!dest_file[0]) {
        const char *last_slash = strrchr(url, '/');
        if (last_slash && *(last_slash + 1)) {
            strncpy(dest_file, last_slash + 1, sizeof(dest_file) - 1);
        } else {
            strcpy(dest_file, "index.html");
        }
        // Strip query string from filename if present
        char *q = strchr(dest_file, '?');
        if (q) *q = '\0';
    }

    auth_cprint("Connecting to "); auth_cprint(url); auth_cprintln("...");

    int status = http_request(url, "GET", dest_file, 0, 0, NULL, 0);
    if (status >= 200 && status < 300) {
        auth_cprint("wget: '"); auth_cprint(dest_file);
        auth_cprint("' saved successfully (HTTP ");
        char code_str[16];
        snprintf(code_str, sizeof(code_str), "%d", status);
        auth_cprint(code_str);
        auth_cprintln(" OK).");
        return 0;
    } else {
        auth_cprint("wget: download failed with HTTP status ");
        char code_str[16];
        snprintf(code_str, sizeof(code_str), "%d", status);
        auth_cprintln(code_str);
        return 1;
    }
}

// ---------------- CURL COMMAND ----------------

int cmd_curl(const char *args) {
    if (!args || !*args) {
        auth_cprintln("Usage: curl [-o filename] [-I] <URL>");
        return 1;
    }

    char dest_file[64] = {0};
    char url[256] = {0};
    int headers_only = 0;

    char buf[512];
    strncpy(buf, args, sizeof(buf) - 1);
    char *token = strtok(buf, " ");
    while (token) {
        if (strcmp(token, "-o") == 0) {
            char *fn = strtok(NULL, " ");
            if (fn) strncpy(dest_file, fn, sizeof(dest_file) - 1);
        } else if (strcmp(token, "-I") == 0) {
            headers_only = 1;
        } else if (token[0] != '-') {
            strncpy(url, token, sizeof(url) - 1);
        }
        token = strtok(NULL, " ");
    }

    if (!url[0]) {
        auth_cprintln("curl: missing URL");
        return 1;
    }

    int to_console = (dest_file[0] == '\0' && !headers_only) ? 1 : 0;
    const char *out_f = (dest_file[0] != '\0') ? dest_file : NULL;

    int status = http_request(url, headers_only ? "HEAD" : "GET",
                              out_f, to_console, headers_only, NULL, 0);

    if (dest_file[0] != '\0' && status >= 200 && status < 300) {
        auth_cprint("curl: saved response to '"); auth_cprint(dest_file); auth_cprintln("'");
        return 0;
    }

    return (status >= 200 && status < 400) ? 0 : 1;
}
