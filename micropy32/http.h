#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>
#include <stddef.h>

int cmd_wget(const char *args);
int cmd_curl(const char *args);

// Shared HTTP streaming interface
int http_request(const char *url, const char *method,
                 const char *output_file, int print_to_console,
                 int headers_only, char *out_body_buf, size_t max_body_len);

#endif // HTTP_H
