#include "sh_runner.h"
#include "auth.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern const kernel_api_t *g_kapi;
int sys_dispatch(const char *cmd, const char *args, const kernel_api_t *kapi);

typedef struct {
    char name[32];
    char val[64];
} sh_var_t;

static sh_var_t s_vars[32];
static int s_var_count = 0;

static void set_var(const char *name, const char *val) {
    for (int i = 0; i < s_var_count; i++) {
        if (strcmp(s_vars[i].name, name) == 0) {
            strncpy(s_vars[i].val, val ? val : "", sizeof(s_vars[i].val) - 1);
            s_vars[i].val[sizeof(s_vars[i].val) - 1] = '\0';
            return;
        }
    }
    if (s_var_count < 32) {
        strncpy(s_vars[s_var_count].name, name, sizeof(s_vars[s_var_count].name) - 1);
        strncpy(s_vars[s_var_count].val, val ? val : "", sizeof(s_vars[s_var_count].val) - 1);
        s_var_count++;
    }
}

static const char *get_var(const char *name, int last_status) {
    if (strcmp(name, "?") == 0) {
        static char s_stat[16];
        snprintf(s_stat, sizeof(s_stat), "%d", last_status);
        return s_stat;
    }
    if (strcmp(name, "USER") == 0) {
        return (g_kapi && g_kapi->session_user) ? g_kapi->session_user : "root";
    }
    if (strcmp(name, "HOME") == 0) {
        return (g_kapi && g_kapi->session_home) ? g_kapi->session_home : "/root";
    }
    for (int i = 0; i < s_var_count; i++) {
        if (strcmp(s_vars[i].name, name) == 0) {
            return s_vars[i].val;
        }
    }
    return "";
}

static void sh_print(const char *s) {
    if (g_kapi && g_kapi->putc && s) {
        while (*s) {
            if (*s == '\n') g_kapi->putc('\r');
            g_kapi->putc(*s++);
        }
    }
}

static void expand_variables(const char *src, char *dst, size_t dst_sz, int last_status) {
    size_t out_idx = 0;
    while (*src && out_idx + 1 < dst_sz) {
        if (*src == '$' && *(src + 1)) {
            src++;
            char vname[32];
            size_t vi = 0;
            if (*src == '?') {
                vname[vi++] = *src++;
            } else {
                while (*src && ((*src >= 'A' && *src <= 'Z') || (*src >= 'a' && *src <= 'z') || (*src >= '0' && *src <= '9') || *src == '_') && vi < 31) {
                    vname[vi++] = *src++;
                }
            }
            vname[vi] = '\0';
            const char *val = get_var(vname, last_status);
            while (*val && out_idx + 1 < dst_sz) {
                dst[out_idx++] = *val++;
            }
        } else {
            dst[out_idx++] = *src++;
        }
    }
    dst[out_idx] = '\0';
}

int cmd_run(const char *args) {
    if (!args || !*args) {
        sh_print("Usage: run <script.sh>\n");
        return 1;
    }

    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_read_file) {
        sh_print("run: AarFS filesystem API unavailable.\n");
        return 1;
    }

    // Skip leading whitespace in script name
    while (*args == ' ') args++;
    char filename[64];
    strncpy(filename, args, sizeof(filename) - 1);
    char *sp = strchr(filename, ' ');
    if (sp) *sp = '\0';

    uint32_t file_id = g_kapi->fs_find_child(g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0, filename);
    if (!file_id) {
        // Try from root directory
        file_id = g_kapi->fs_find_child(0, filename);
    }
    if (!file_id) {
        sh_print("run: cannot open '");
        sh_print(filename);
        sh_print("': No such file or directory\n");
        return 1;
    }

    static char s_script_buf[16384];
    uint32_t size = 0;
    if (g_kapi->fs_read_file(file_id, s_script_buf, &size) != 0 || size == 0) {
        sh_print("run: failed reading script file.\n");
        return 1;
    }
    if (size >= sizeof(s_script_buf)) size = sizeof(s_script_buf) - 1;
    s_script_buf[size] = '\0';

    int stop_on_error = 0;
    int last_status = 0;
    int line_num = 0;

    char *line = s_script_buf;
    while (line && *line) {
        line_num++;
        char *next_line = strchr(line, '\n');
        if (next_line) {
            *next_line = '\0';
            next_line++;
        }

        // Trim carriage return and leading spaces
        char *end = line + strlen(line) - 1;
        while (end >= line && (*end == '\r' || *end == ' ' || *end == '\t')) {
            *end-- = '\0';
        }
        while (*line == ' ' || *line == '\t') line++;

        // Skip comments and empty lines
        if (!*line || *line == '#') {
            line = next_line;
            continue;
        }

        // Variable assignment: VAR=VAL
        char *eq = strchr(line, '=');
        if (eq && eq != line && !strchr(line, ' ') && eq < line + 31) {
            *eq = '\0';
            set_var(line, eq + 1);
            line = next_line;
            continue;
        }

        // Variable expansion
        char exp_line[256];
        expand_variables(line, exp_line, sizeof(exp_line), last_status);

        // Parse command and arguments
        char *cmd = exp_line;
        while (*cmd == ' ') cmd++;
        char *cargs = strchr(cmd, ' ');
        if (cargs) {
            *cargs = '\0';
            cargs++;
            while (*cargs == ' ') cargs++;
        } else {
            cargs = "";
        }

        // Built-in commands
        if (strcmp(cmd, "echo") == 0) {
            sh_print(cargs);
            sh_print("\n");
            last_status = 0;
        } else if (strcmp(cmd, "exit") == 0) {
            last_status = cargs[0] ? atoi(cargs) : 0;
            return last_status;
        } else if (strcmp(cmd, "set") == 0 && strcmp(cargs, "-e") == 0) {
            stop_on_error = 1;
            last_status = 0;
        } else {
            // Dispatch via sys_dispatch
            last_status = sys_dispatch(cmd, cargs, g_kapi);
            if (last_status != 0 && stop_on_error) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "%s: line %d: command '%s' returned status %d\n",
                         filename, line_num, cmd, last_status);
                sh_print(err_buf);
                return last_status;
            }
        }

        line = next_line;
    }

    return last_status;
}
