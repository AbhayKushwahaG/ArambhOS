; Host-assisted Python bridge over QEMU's dedicated COM2 virtual serial port.
; COM1 remains reserved for the kernel's existing diagnostic stream.
%define PY_BRIDGE_BASE 0x2F8
%define PY_BRIDGE_LSR  0x2FD
%define PY_BRIDGE_MAX_OUTPUT 8192
%define PY_BRIDGE_PING_TICKS 500
%define PY_BRIDGE_RUN_TICKS  30000

bridge_uart_init:
    mov dx, PY_BRIDGE_BASE+1
    xor al, al
    out dx, al
    mov dx, PY_BRIDGE_BASE+3
    mov al, 0x80
    out dx, al
    mov dx, PY_BRIDGE_BASE
    mov al, 1                 ; 115200 baud, same virtual UART format as COM1.
    out dx, al
    mov dx, PY_BRIDGE_BASE+1
    xor al, al
    out dx, al
    mov dx, PY_BRIDGE_BASE+3
    mov al, 3                 ; 8 data bits, no parity, one stop bit.
    out dx, al
    mov dx, PY_BRIDGE_BASE+2
    mov al, 0xC7              ; enable/reset FIFO, 14-byte trigger.
    out dx, al
    mov dx, PY_BRIDGE_BASE+4
    mov al, 0x0B              ; DTR, RTS, OUT2.
    out dx, al
    ret

py_command:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    mov esi, [command_args]
    mov edi, py_arg_help
    call string_equal
    test eax, eax
    jnz .help
    mov esi, [command_args]
    mov edi, py_arg_version
    call string_equal
    test eax, eax
    jnz .version

    ; The current AarFS directory is the supported working directory. Paths
    ; and extra arguments are rejected rather than interpreted ambiguously.
    call fs_get_arg
    test esi, esi
    jz .usage
    ; Check if first argument is "native" (py native <file.py>)
    push edi
    mov edi, py_arg_native
    call string_equal
    pop edi
    test eax, eax
    jz .not_native_mode
    mov esi, edi
    mov [command_args], esi
    jmp py_native_command
.not_native_mode:
    test edi, edi
    jnz .usage
    mov [bridge_filename], esi
    call fs_validate_name
    test eax, eax
    jz .bad_name
    cmp eax, 3
    jb .bad_name
    mov [bridge_name_len], al
    mov edx, esi
    add edx, eax
    sub edx, 3
    cmp byte [edx], '.'
    jne .bad_name
    mov al, [edx+1]
    or al, 0x20
    cmp al, 'p'
    jne .bad_name
    mov al, [edx+2]
    or al, 0x20
    cmp al, 'y'
    jne .bad_name

    ; Accept only printable ASCII filename characters understood by AarFS.
    mov esi, [bridge_filename]
    movzx ecx, byte [bridge_name_len]
.validate_char:
    mov al, [esi]
    cmp al, 'a'
    jb .upper_or_other
    cmp al, 'z'
    jbe .char_ok
.upper_or_other:
    cmp al, 'A'
    jb .digit_or_other
    cmp al, 'Z'
    jbe .char_ok
.digit_or_other:
    cmp al, '0'
    jb .punctuation
    cmp al, '9'
    jbe .char_ok
.punctuation:
    cmp al, '.'
    je .char_ok
    cmp al, '_'
    je .char_ok
    cmp al, '-'
    jne .bad_name
.char_ok:
    inc esi
    loop .validate_char

    mov eax, [fs_cwd]
    mov esi, [bridge_filename]
    call fs_find_child
    test eax, eax
    jz .missing
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .not_file
    call fs_read_file
    jc .read_error
    mov byte [bridge_kind], 1
    jmp bridge_execute
.version:
    mov byte [bridge_kind], 2
    mov dword [bridge_filename], 0
    mov byte [bridge_name_len], 0
    mov dword [fs_file_size], 0
    jmp bridge_execute
.help:
    mov esi, py_help_text
    call console_write
    ret
.usage:
    mov esi, py_usage_text
    call console_write
    ret
.bad_name:
    mov esi, py_bad_name_text
    call console_write
    ret
.missing:
    mov esi, py_missing_text
    call console_write
    ret
.not_file:
    mov esi, py_not_file_text
    call console_write
    ret
.read_error:
    mov esi, py_read_error_text
    call console_write
    ret
.perm_denied:
    mov esi, py_perm_denied_msg
    call console_write
    ret

; Send APNG and require APOK within the short ping deadline. CF=1 on failure.
bridge_ping:
    mov eax, [timer_ticks]
    add eax, PY_BRIDGE_PING_TICKS
    mov [bridge_deadline], eax
    mov al, 'A'
    call bridge_tx_byte
    jc .bad
    mov al, 'P'
    call bridge_tx_byte
    jc .bad
    mov al, 'N'
    call bridge_tx_byte
    jc .bad
    mov al, 'G'
    call bridge_tx_byte
    jc .bad
    mov edi, bridge_rx_magic
    mov ecx, 4
.read:
    call bridge_rx_byte
    jc .bad
    stosb
    loop .read
    cmp dword [bridge_rx_magic], 0x4B4F5041 ; "APOK"
    jne .bad
    clc
    ret
.bad:
    stc
    ret

bridge_execute:
    mov esi, py_connect_text
    call console_write
    call bridge_ping
    jc .unavailable
    mov esi, py_send_text
    call console_write

    inc dword [bridge_request_id]
    jnz .id_ready
    inc dword [bridge_request_id]
.id_ready:
    mov eax, [timer_ticks]
    add eax, PY_BRIDGE_RUN_TICKS
    mov [bridge_deadline], eax
    ; Request: APY1, version, request id, kind, name length, source length.
    mov al, 'A'
    call bridge_tx_byte
    jc .comm_error
    mov al, 'P'
    call bridge_tx_byte
    jc .comm_error
    mov al, 'Y'
    call bridge_tx_byte
    jc .comm_error
    mov al, '1'
    call bridge_tx_byte
    jc .comm_error
    mov al, 1
    call bridge_tx_byte
    jc .comm_error
    mov eax, [bridge_request_id]
    call bridge_tx_u32
    jc .comm_error
    mov al, [bridge_kind]
    call bridge_tx_byte
    jc .comm_error
    mov al, [bridge_name_len]
    call bridge_tx_byte
    jc .comm_error
    mov eax, [fs_file_size]
    cmp byte [bridge_kind], 2
    jne .send_source_length
    xor eax, eax
.send_source_length:
    call bridge_tx_u32
    jc .comm_error
    cmp byte [bridge_name_len], 0
    je .source
    mov esi, [bridge_filename]
    movzx ecx, byte [bridge_name_len]
.send_name:
    lodsb
    call bridge_tx_byte
    jc .comm_error
    loop .send_name
.source:
    mov ecx, [fs_file_size]
    cmp byte [bridge_kind], 2
    jne .source_loop
    xor ecx, ecx
.source_loop:
    test ecx, ecx
    jz .receive_header
    mov esi, [fs_file_buffer]
.send_source:
    lodsb
    call bridge_tx_byte
    jc .comm_error
    dec ecx
    jnz .send_source

.receive_header:
    mov esi, py_wait_text
    call console_write
    mov edi, bridge_response_header
    mov ecx, 22
.header_loop:
    call bridge_rx_byte
    jc .timeout
    stosb
    loop .header_loop
    cmp dword [bridge_response_header], 0x31525041 ; "APR1"
    jne .protocol
    cmp byte [bridge_response_header+4], 1
    jne .protocol
    mov eax, [bridge_response_header+5]
    cmp eax, [bridge_request_id]
    jne .protocol
    movzx eax, byte [bridge_response_header+9]
    cmp eax, 4
    ja .protocol
    mov [bridge_status], al
    mov eax, [bridge_response_header+14]
    mov [bridge_stdout_len], eax
    mov edx, [bridge_response_header+18]
    add eax, edx
    jc .protocol
    cmp eax, PY_BRIDGE_MAX_OUTPUT
    ja .protocol
    mov [bridge_stderr_len], edx

    cmp dword [bridge_stdout_len], 0
    je .stderr
    mov esi, py_stdout_label
    call console_write
    mov eax, [bridge_stdout_len]
    mov [bridge_stream_remaining], eax
    call bridge_print_payload
    jc .timeout
.stderr:
    cmp dword [bridge_stderr_len], 0
    je .report_status
    mov esi, py_stderr_label
    call console_write
    mov eax, [bridge_stderr_len]
    mov [bridge_stream_remaining], eax
    call bridge_print_payload
    jc .timeout
.report_status:
    cmp byte [bridge_status], 0
    je .success
    cmp byte [bridge_status], 1
    je .exit_error
    cmp byte [bridge_status], 2
    je .script_timeout
    cmp byte [bridge_status], 3
    je .output_limit
    mov esi, py_rejected_text
    call console_write
    ret
.success:
    cmp dword [bridge_response_header+10], 0
    jne .exit_error
    mov esi, py_success_text
    call console_write
    ret
.exit_error:
    mov esi, py_exit_text
    call console_write
    mov eax, [bridge_response_header+10]
    call print_u32
    mov al, '.'
    call console_putc
    call console_newline
    ret
.script_timeout:
    mov esi, py_script_timeout_text
    call console_write
    ret
.output_limit:
    mov esi, py_output_limit_text
    call console_write
    ret
.unavailable:
    cmp byte [bridge_kind], 1
    jne .unavailable_msg
    mov esi, py_fallback_native_text
    call console_write
    jmp py_native_execute
.unavailable_msg:
    mov esi, py_unavailable_text
    call console_write
    ret
.protocol:
    mov esi, py_protocol_text
    call console_write
    ret
.comm_error:
    mov esi, py_comm_text
    call console_write
    ret
.timeout:
    mov esi, py_timeout_text
    call console_write
    ret

; AL=byte to send; bounded wait for THR. Preserves caller registers.
bridge_tx_byte:
    mov [bridge_byte], al
    pushad
    mov ebx, [timer_ticks]
    add ebx, 1000
.wait:
    mov dx, PY_BRIDGE_LSR
    in al, dx
    test al, 0x20
    jnz .send
    cmp dword [timer_ticks], ebx
    jae .failed
    jmp .wait
.send:
    mov dx, PY_BRIDGE_BASE
    mov al, [bridge_byte]
    out dx, al
    popad
    clc
    ret
.failed:
    popad
    stc
    ret

; EAX=32-bit unsigned value; transmit little-endian.
bridge_tx_u32:
    mov [bridge_tx_value], eax
    mov ecx, 4
.loop:
    mov al, [bridge_tx_value]
    call bridge_tx_byte
    jc .bad
    shr dword [bridge_tx_value], 8
    dec ecx
    jnz .loop
    clc
    ret
.bad:
    stc
    ret

; Read AL from COM2 before the fixed PIT deadline. CF=timeout.
bridge_rx_byte:
    pushad
.poll:
    mov dx, PY_BRIDGE_LSR
    in al, dx
    test al, 1
    jnz .read
    mov eax, [timer_ticks]
    cmp eax, [bridge_deadline]
    jae .failed
    jmp .poll
.read:
    mov dx, PY_BRIDGE_BASE
    in al, dx
    mov [bridge_byte], al
    mov byte [bridge_io_error], 0
    jmp .done
.failed:
    mov byte [bridge_io_error], 1
.done:
    popad
    cmp byte [bridge_io_error], 0
    jne .bad
    movzx eax, byte [bridge_byte]
    clc
    ret
.bad:
    stc
    ret

; Print an already length-checked response segment, replacing control bytes
; except tab/newline/CR so helper output cannot backspace over the shell prompt.
bridge_print_payload:
.loop:
    cmp dword [bridge_stream_remaining], 0
    je .done
    call bridge_rx_byte
    jc .bad
    cmp al, 9
    je .emit
    cmp al, 10
    je .emit
    cmp al, 13
    je .emit
    cmp al, 32
    jae .emit
    mov al, '.'
.emit:
    call console_putc
    dec dword [bridge_stream_remaining]
    jmp .loop
.done:
    clc
    ret
.bad:
    stc
    ret

; Ask the host helper to launch the allow-listed Windows Calculator.
; Returns EAX=1 only after a status-0, exit-0 response from the helper.
bridge_launch_calc:
    call bridge_ping
    jc .fail
    inc dword [bridge_request_id]
    jnz .id_ok
    inc dword [bridge_request_id]
.id_ok:
    mov eax, [timer_ticks]
    add eax, PY_BRIDGE_RUN_TICKS
    mov [bridge_deadline], eax
    mov al, 'A'
    call bridge_tx_byte
    jc .fail
    mov al, 'P'
    call bridge_tx_byte
    jc .fail
    mov al, 'Y'
    call bridge_tx_byte
    jc .fail
    mov al, '1'
    call bridge_tx_byte
    jc .fail
    mov al, 1
    call bridge_tx_byte
    jc .fail
    mov eax, [bridge_request_id]
    call bridge_tx_u32
    jc .fail
    mov al, 3                 ; kind 3: launch an allow-listed host app
    call bridge_tx_byte
    jc .fail
    xor al, al                ; no filename
    call bridge_tx_byte
    jc .fail
    mov eax, 4
    call bridge_tx_u32
    jc .fail
    mov esi, bridge_calc_app
    mov ecx, 4
.send:
    lodsb
    call bridge_tx_byte
    jc .fail
    loop .send
    mov edi, bridge_response_header
    mov ecx, 22
.read:
    call bridge_rx_byte
    jc .fail
    stosb
    loop .read
    cmp dword [bridge_response_header], 0x31525041 ; "APR1"
    jne .fail
    cmp byte [bridge_response_header+4], 1
    jne .fail
    mov eax, [bridge_response_header+5]
    cmp eax, [bridge_request_id]
    jne .fail
    movzx eax, byte [bridge_response_header+9]
    cmp eax, 4
    ja .fail
    mov [bridge_status], al
    mov eax, [bridge_response_header+14]
    mov [bridge_stdout_len], eax
    mov edx, [bridge_response_header+18]
    add eax, edx
    jc .fail
    cmp eax, PY_BRIDGE_MAX_OUTPUT
    ja .fail
    mov [bridge_stderr_len], edx
    cmp dword [bridge_stdout_len], 0
    je .stderr
    mov esi, calc_host_stdout_label
    call console_write
    mov eax, [bridge_stdout_len]
    mov [bridge_stream_remaining], eax
    call bridge_print_payload
    jc .fail
.stderr:
    cmp dword [bridge_stderr_len], 0
    je .status
    mov esi, calc_host_stderr_label
    call console_write
    mov eax, [bridge_stderr_len]
    mov [bridge_stream_remaining], eax
    call bridge_print_payload
    jc .fail
.status:
    cmp byte [bridge_status], 0
    jne .fail
    cmp dword [bridge_response_header+10], 0
    jne .fail
    mov eax, 1
    ret
.fail:
    xor eax, eax
    ret

py_connect_text db "py: contacting Windows helper...",13,10,0
py_send_text db "py: sending AarFS script.",13,10,0
py_wait_text db "py: waiting for Python result.",13,10,0
py_arg_help db 'help',0
py_arg_version db '--version',0
py_help_text db 'Usage: py <file.py> | py --version | py help',13,10
              db 'Runs AarFS files with the real Windows Python host helper.',13,10
              db 'Start host_python_bridge.py before using the QEMU bridge.',13,10,0
py_usage_text db 'Usage: py <file.py> | py --version | py help',13,10,0
py_bad_name_text db 'py: expected a simple .py filename (no path or extra arguments).',13,10,0
py_missing_text db 'py: script file not found in the current AarFS directory.',13,10,0
py_not_file_text db 'py: the named AarFS entry is not a file.',13,10,0
py_read_error_text db 'py: AarFS could not read the script.',13,10,0
py_unavailable_text db 'py: Python helper unavailable; start host_python_bridge.py on Windows.',13,10,0
py_protocol_text db 'py: malformed bridge protocol response.',13,10,0
py_comm_text db 'py: bridge communication failed while sending the request.',13,10,0
py_timeout_text db 'py: bridge response timed out or the helper disconnected.',13,10,0
py_script_timeout_text db 'py: Python script exceeded the helper execution timeout.',13,10,0
py_output_limit_text db 'py: Python output exceeded the configured limit.',13,10,0
py_rejected_text db 'py: helper rejected the request.',13,10,0
py_success_text db 'Python exited with code 0.',13,10,0
py_exit_text db 'Python exited with code ',0
py_stdout_label db 'Python stdout:',13,10,0
py_stderr_label db 'Python stderr:',13,10,0
bridge_calc_app db 'calc',0
calc_host_stdout_label db 'Host helper: ',0
calc_host_stderr_label db 'Host helper error: ',0

py_arg_native db 'native',0
py_native_help_text db 'Usage: python <file.py>',13,10
                    db 'Executes Python script natively inside Aarambh OS using MicroPython.',13,10,0
py_native_usage_text db 'Usage: python <file.py>',13,10,0
py_native_no_runtime_msg db 'python: native MicroPython runtime missing on disk.',13,10,0
py_native_disk_err_msg db 'python: disk read error loading native runtime.',13,10,0
py_native_ok_msg db 'Python exited with code 0.',13,10,0
py_native_err_msg db 'Python exited with code 1.',13,10,0
py_fallback_native_text db 'py: host helper unavailable; executing natively via MicroPython...',13,10,0
py_native_loaded db 0

py_native_command:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    mov esi, [command_args]
    mov edi, py_arg_help
    call string_equal
    test eax, eax
    jnz .help

    call fs_get_arg
    test esi, esi
    jz .usage
    test edi, edi
    jnz .usage
    mov [bridge_filename], esi
    call fs_validate_name
    test eax, eax
    jz .bad_name
    cmp eax, 3
    jb .bad_name
    mov [bridge_name_len], al
    mov edx, esi
    add edx, eax
    sub edx, 3
    cmp byte [edx], '.'
    jne .bad_name
    mov al, [edx+1]
    or al, 0x20
    cmp al, 'p'
    jne .bad_name
    mov al, [edx+2]
    or al, 0x20
    cmp al, 'y'
    jne .bad_name

    mov esi, [bridge_filename]
    movzx ecx, byte [bridge_name_len]
.validate_char:
    mov al, [esi]
    cmp al, 'a'
    jb .upper_or_other
    cmp al, 'z'
    jbe .char_ok
.upper_or_other:
    cmp al, 'A'
    jb .digit_or_other
    cmp al, 'Z'
    jbe .char_ok
.digit_or_other:
    cmp al, '0'
    jb .punct
    cmp al, '9'
    jbe .char_ok
.punct:
    cmp al, '.'
    je .char_ok
    cmp al, '_'
    je .char_ok
    cmp al, '-'
    jne .bad_name
.char_ok:
    inc esi
    loop .validate_char

    mov eax, [fs_cwd]
    mov esi, [bridge_filename]
    call fs_find_child
    test eax, eax
    jz .missing
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .not_file
    mov cl, 4
    call fs_check_perm
    jc .perm_denied
    call fs_read_file
    jc .read_error

    jmp py_native_execute

.help:
    mov esi, py_native_help_text
    call console_write
    ret
.usage:
    mov esi, py_native_usage_text
    call console_write
    ret
.bad_name:
    mov esi, py_bad_name_text
    call console_write
    ret
.missing:
    mov esi, py_missing_text
    call console_write
    ret
.not_file:
    mov esi, py_not_file_text
    call console_write
    ret
.read_error:
    mov esi, py_read_error_text
    call console_write
    ret
.perm_denied:
    mov esi, py_perm_denied_msg
    call console_write
    ret

py_native_load:
    cmp byte [py_native_loaded], 1
    je .done_ok
    mov eax, 132000
    mov edi, 0x00200000
    mov ecx, 6144
    call ata_read_many
    jc .disk_err
    cmp dword [0x00200000], 0x3359504D       ; "MPY3"
    jne .bad_magic
    cmp dword [0x00200004], 0x4E494232       ; "2BIN"
    jne .bad_magic
    mov byte [py_native_loaded], 1
.done_ok:
    clc
    ret
.bad_magic:
    mov esi, py_native_no_runtime_msg
    call console_write
    stc
    ret
.disk_err:
    mov esi, py_native_disk_err_msg
    call console_write
    stc
    ret

py_native_execute:
    call py_native_load
    jc .err_ret

    push dword kernel_api_table
    push dword console_putc
    push dword [fs_file_size]
    push dword [fs_file_buffer]
    call 0x00200008
    add esp, 16

    test eax, eax
    jnz .non_zero
    mov esi, py_native_ok_msg
    call console_write
    ret
.non_zero:
    mov esi, py_native_err_msg
    call console_write
.err_ret:
    ret

bridge_filename dd 0
bridge_name_len db 0
bridge_kind db 0
bridge_status db 0
bridge_request_id dd 0
bridge_deadline dd 0
bridge_stdout_len dd 0
bridge_stderr_len dd 0
bridge_stream_remaining dd 0
bridge_tx_value dd 0
bridge_byte db 0
bridge_io_error db 0
bridge_rx_magic times 4 db 0
bridge_response_header times 22 db 0
auth_boot_init:
    call py_native_load
    jc .done
    push dword kernel_api_table
    push dword 0
    push dword 0
    call 0x00200010
    add esp, 12
.done:
    ret

sys_native_dispatch:
    mov esi, command_name
    mov edi, sys_auth_commands
.check_loop:
    cmp byte [edi], 0
    je .not_auth
    push esi
    push edi
    call string_equal
    pop edi
    pop esi
    test eax, eax
    jnz .is_auth
.advance:
    inc edi
    cmp byte [edi-1], 0
    jne .advance
    jmp .check_loop
.is_auth:
    call sys_native_execute
    mov eax, 1
    ret
.not_auth:
    xor eax, eax
    ret

sys_native_execute:
    call py_native_load
    jc .err_ret
    push dword kernel_api_table
    push dword [command_args]
    push dword command_name
    call 0x00200010
    add esp, 12
    ret
.err_ret:
    ret

py_perm_denied_msg db 'python: Permission denied',13,10,0

sys_auth_commands:
    db 'whoami',0, 'id',0, 'users',0, 'groups',0
    db 'useradd',0, 'userdel',0, 'usermod',0
    db 'groupadd',0, 'groupdel',0
    db 'login',0, 'logout',0, 'su',0, 'passwd',0
    db 'chmod',0, 'chown',0, 'chgrp',0, 'stat',0
    db 'network',0, 'wget',0, 'curl',0, 'git',0
    db 'ping',0, 'ifconfig',0
    db 'desktop',0, 'gui',0
    db 'run',0, 'sh',0, 'dpkg',0, 'deb',0
    db 'elfinfo',0, 'elf',0, 'rpm',0
    db 'peinfo',0, 'wine-check',0
    db 'sysinfo',0, 'hwinfo',0, 'compat',0
    db 0
