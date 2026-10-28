#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "auth.h"
#include "net_stack.h"
#include "http.h"
#include "port/micropython_embed.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objmodule.h"
#include "py/runtime.h"
#include "py/stackctrl.h"

typedef void (*aarambh_putc_fn)(char c);
static aarambh_putc_fn g_putc = NULL;

// 128 KiB GC heap for MicroPython inside Aarambh OS
#define HEAP_SIZE (128 * 1024)
static char s_gc_heap[HEAP_SIZE];

// Forward declarations
int micropy_entry(const char *src, size_t len, aarambh_putc_fn putc_fn, const kernel_api_t *kapi);
int sys_dispatch(const char *cmd, const char *args, const kernel_api_t *kapi);

// Binary header at 0x00200000:
// 0x00200000: "MPY32BIN" (8 bytes)
// 0x00200008: jmp _micropy_entry (5 bytes + 3 pad = 8 bytes)
// 0x00200010: jmp _sys_dispatch
__attribute__((section(".magic"), naked)) void _header(void) {
    __asm__ __volatile__ (
        ".ascii \"MPY32BIN\"\n\t"
        "jmp _micropy_entry\n\t"
        ".balign 16\n\t"
        "jmp _sys_dispatch\n\t"
    );
}

// Output character hook called by MicroPython print() and exception printer
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    if (!g_putc) return;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        if (c == '\n') {
            g_putc('\r');
        }
        g_putc(c);
    }
}

// Builtin functions for the native aarambh Python module
static mp_obj_t py_aarambh_getuid(void) {
    uint16_t uid = (g_kapi && g_kapi->session_uid) ? *g_kapi->session_uid : 0;
    return MP_OBJ_NEW_SMALL_INT(uid);
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_aarambh_getuid_obj, py_aarambh_getuid);

static mp_obj_t py_aarambh_getgid(void) {
    uint16_t gid = (g_kapi && g_kapi->session_gid) ? *g_kapi->session_gid : 0;
    return MP_OBJ_NEW_SMALL_INT(gid);
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_aarambh_getgid_obj, py_aarambh_getgid);

static mp_obj_t py_aarambh_getuser(void) {
    const char *u = (g_kapi && g_kapi->session_user) ? g_kapi->session_user : "root";
    return mp_obj_new_str(u, strlen(u));
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_aarambh_getuser_obj, py_aarambh_getuser);

static mp_obj_t py_aarambh_stat(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_entry_ptr) {
        mp_raise_OSError(1);
    }
    uint32_t cwd = g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0;
    uint32_t id = g_kapi->fs_find_child(cwd, path);
    if (!id) {
        mp_raise_OSError(2);
    }
    aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(id);
    if (!e || e->status == 0) {
        mp_raise_OSError(2);
    }
    uint16_t mode = e->mode ? e->mode : (e->type == 2 ? 0755 : 0644);
    mp_obj_t dict = mp_obj_new_dict(4);
    mp_obj_dict_store(dict, mp_obj_new_str("st_mode", 7), MP_OBJ_NEW_SMALL_INT(mode));
    mp_obj_dict_store(dict, mp_obj_new_str("st_uid", 6), MP_OBJ_NEW_SMALL_INT(e->uid));
    mp_obj_dict_store(dict, mp_obj_new_str("st_gid", 6), MP_OBJ_NEW_SMALL_INT(e->gid));
    mp_obj_dict_store(dict, mp_obj_new_str("st_size", 7), MP_OBJ_NEW_SMALL_INT(e->size));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_aarambh_stat_obj, py_aarambh_stat);

static mp_obj_t py_aarambh_read(mp_obj_t path_obj) {
    const char *path = mp_obj_str_get_str(path_obj);
    if (!g_kapi || !g_kapi->fs_find_child || !g_kapi->fs_entry_ptr || !g_kapi->fs_read_file) {
        mp_raise_OSError(1);
    }
    uint32_t cwd = g_kapi->fs_cwd ? *g_kapi->fs_cwd : 0;
    uint32_t id = g_kapi->fs_find_child(cwd, path);
    if (!id) {
        mp_raise_OSError(2);
    }
    aarfs_entry_t *e = (aarfs_entry_t *)g_kapi->fs_entry_ptr(id);
    if (!e || e->status == 0) {
        mp_raise_OSError(2);
    }
    uint16_t cur_uid = g_kapi->session_uid ? *g_kapi->session_uid : 0;
    uint16_t cur_gid = g_kapi->session_gid ? *g_kapi->session_gid : 0;
    uint16_t mode = e->mode ? e->mode : 0644;
    int allowed = 0;
    if (cur_uid == 0) {
        allowed = 1;
    } else if (cur_uid == e->uid) {
        if (mode & 0400) allowed = 1;
    } else if (cur_gid == e->gid) {
        if (mode & 0040) allowed = 1;
    } else {
        if (mode & 0004) allowed = 1;
    }
    if (!allowed) {
        mp_raise_OSError(13);
    }
    static char s_file_buf[4096];
    uint32_t read_sz = 0;
    if (g_kapi->fs_read_file(id, s_file_buf, &read_sz) != 0) {
        mp_raise_OSError(5);
    }
    return mp_obj_new_str(s_file_buf, read_sz);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_aarambh_read_obj, py_aarambh_read);

static mp_obj_t py_aarambh_syscall(size_t n_args, const mp_obj_t *args) {
    int num = mp_obj_get_int(args[0]);
    int a1 = n_args > 1 ? mp_obj_get_int(args[1]) : 0;
    int a2 = n_args > 2 ? mp_obj_get_int(args[2]) : 0;
    int a3 = n_args > 3 ? mp_obj_get_int(args[3]) : 0;
    int res = 0;
    __asm__ volatile (
        "int $0x80"
        : "=a"(res)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return mp_obj_new_int(res);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_aarambh_syscall_obj, 1, 4, py_aarambh_syscall);

static mp_obj_t py_aarambh_net_status(void) {
    mp_obj_t dict = mp_obj_new_dict(5);
    char ip[16] = "0.0.0.0", mask[16] = "0.0.0.0", gw[16] = "0.0.0.0", dns[16] = "0.0.0.0";
    if (g_net_config.is_configured) {
        inet_ntoa(g_net_config.ip, ip);
        inet_ntoa(g_net_config.netmask, mask);
        inet_ntoa(g_net_config.gateway, gw);
        inet_ntoa(g_net_config.dns, dns);
    }
    int link = g_active_net_driver && g_active_net_driver->get_link_status ?
               g_active_net_driver->get_link_status() : 0;
    mp_obj_dict_store(dict, mp_obj_new_str("ip", 2), mp_obj_new_str(ip, strlen(ip)));
    mp_obj_dict_store(dict, mp_obj_new_str("netmask", 7), mp_obj_new_str(mask, strlen(mask)));
    mp_obj_dict_store(dict, mp_obj_new_str("gateway", 7), mp_obj_new_str(gw, strlen(gw)));
    mp_obj_dict_store(dict, mp_obj_new_str("dns", 3), mp_obj_new_str(dns, strlen(dns)));
    mp_obj_dict_store(dict, mp_obj_new_str("link", 4), mp_obj_new_int(link));
    return dict;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_aarambh_net_status_obj, py_aarambh_net_status);

static mp_obj_t py_aarambh_gethostbyname(mp_obj_t host_obj) {
    const char *h = mp_obj_str_get_str(host_obj);
    uint32_t ip = 0;
    if (dns_resolve(h, &ip, 3000) != 0) {
        mp_raise_OSError(2);
    }
    char buf[16];
    inet_ntoa(ip, buf);
    return mp_obj_new_str(buf, strlen(buf));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_aarambh_gethostbyname_obj, py_aarambh_gethostbyname);

static mp_obj_t py_aarambh_ping(mp_obj_t host_obj) {
    const char *h = mp_obj_str_get_str(host_obj);
    uint32_t ip = 0;
    if (dns_resolve(h, &ip, 3000) != 0) {
        return mp_obj_new_int(-1);
    }
    int rtt = icmp_ping(ip, 1500);
    return mp_obj_new_int(rtt);
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_aarambh_ping_obj, py_aarambh_ping);

static mp_obj_t py_aarambh_http_get(mp_obj_t url_obj) {
    const char *url = mp_obj_str_get_str(url_obj);
    static char body[8192];
    body[0] = '\0';
    int status = http_request(url, "GET", NULL, 0, 0, body, sizeof(body));
    if (status < 200 || status >= 400) {
        mp_raise_OSError(5);
    }
    return mp_obj_new_str(body, strlen(body));
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_aarambh_http_get_obj, py_aarambh_http_get);

static mp_obj_t py_aarambh_socket_connect(mp_obj_t host_obj, mp_obj_t port_obj) {
    const char *h = mp_obj_str_get_str(host_obj);
    int port = mp_obj_get_int(port_obj);
    uint32_t ip = 0;
    if (dns_resolve(h, &ip, 3000) != 0) {
        mp_raise_OSError(2);
    }
    int sock = net_socket_open(SOCK_TCP);
    if (sock < 0) {
        mp_raise_OSError(24);
    }
    if (net_connect(sock, ip, (uint16_t)port, 4000) != 0) {
        net_close(sock);
        mp_raise_OSError(111);
    }
    return mp_obj_new_int(sock);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_aarambh_socket_connect_obj, py_aarambh_socket_connect);

static mp_obj_t py_aarambh_socket_send(mp_obj_t sock_obj, mp_obj_t data_obj) {
    int sock = mp_obj_get_int(sock_obj);
    size_t len;
    const char *data = mp_obj_str_get_data(data_obj, &len);
    int sent = net_send(sock, data, len);
    if (sent < 0) mp_raise_OSError(5);
    return mp_obj_new_int(sent);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_aarambh_socket_send_obj, py_aarambh_socket_send);

static mp_obj_t py_aarambh_socket_recv(mp_obj_t sock_obj, mp_obj_t max_len_obj) {
    int sock = mp_obj_get_int(sock_obj);
    int max_len = mp_obj_get_int(max_len_obj);
    if (max_len > 4096) max_len = 4096;
    static char buf[4096];
    int r = net_recv(sock, buf, (size_t)max_len, 3000);
    if (r < 0) mp_raise_OSError(11);
    return mp_obj_new_str(buf, (size_t)r);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_aarambh_socket_recv_obj, py_aarambh_socket_recv);

static mp_obj_t py_aarambh_socket_close(mp_obj_t sock_obj) {
    int sock = mp_obj_get_int(sock_obj);
    net_close(sock);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(py_aarambh_socket_close_obj, py_aarambh_socket_close);

// Entry point called by Aarambh OS kernel
int micropy_entry(const char *src, size_t len, aarambh_putc_fn putc_fn, const kernel_api_t *kapi) {
    g_putc = putc_fn;
    if (kapi) {
        g_kapi = kapi;
    }

    if (!src || len == 0) {
        return 0;
    }

    int stack_top;
    mp_embed_init(s_gc_heap, sizeof(s_gc_heap), &stack_top);

    // Register aarambh native module
    mp_obj_t aarambh_mod = mp_obj_new_module(qstr_from_str("aarambh"));
    mp_obj_dict_t *mod_dict = mp_obj_module_get_globals(aarambh_mod);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("getuid", 6), MP_OBJ_FROM_PTR(&py_aarambh_getuid_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("getgid", 6), MP_OBJ_FROM_PTR(&py_aarambh_getgid_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("getuser", 7), MP_OBJ_FROM_PTR(&py_aarambh_getuser_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("stat", 4), MP_OBJ_FROM_PTR(&py_aarambh_stat_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("read", 4), MP_OBJ_FROM_PTR(&py_aarambh_read_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("syscall", 7), MP_OBJ_FROM_PTR(&py_aarambh_syscall_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("net_status", 10), MP_OBJ_FROM_PTR(&py_aarambh_net_status_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("gethostbyname", 13), MP_OBJ_FROM_PTR(&py_aarambh_gethostbyname_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("ping", 4), MP_OBJ_FROM_PTR(&py_aarambh_ping_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("http_get", 8), MP_OBJ_FROM_PTR(&py_aarambh_http_get_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("socket_connect", 14), MP_OBJ_FROM_PTR(&py_aarambh_socket_connect_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("socket_send", 11), MP_OBJ_FROM_PTR(&py_aarambh_socket_send_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("socket_recv", 11), MP_OBJ_FROM_PTR(&py_aarambh_socket_recv_obj));
    mp_obj_dict_store(MP_OBJ_FROM_PTR(mod_dict), mp_obj_new_str("socket_close", 12), MP_OBJ_FROM_PTR(&py_aarambh_socket_close_obj));

    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)), mp_obj_new_str("aarambh", 7), aarambh_mod);
    mp_store_global(qstr_from_str("aarambh"), aarambh_mod);

    nlr_buf_t nlr;
    int exit_code = 0;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
        exit_code = 0;
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
        exit_code = 1;
    }

    mp_embed_deinit();
    return exit_code;
}
