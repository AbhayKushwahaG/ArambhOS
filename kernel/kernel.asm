; Aarambh OS v0.4 - 16-bit BIOS terminal with FAT32 text applications.
; Loaded by boot.asm at physical address 0x7E00.

[BITS 16]
[ORG 0x7E00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    cld
    mov [boot_drive], dl
    mov word [0xE000], 0xA55A
    mov word [0x0000], panic_handler
    mov word [0x0002], 0
    mov word [0x000C], panic_handler
    mov word [0x000E], 0
    mov word [0x0018], panic_handler
    mov word [0x001A], 0
    sti

    mov si, banner
    call print_string

main_prompt:
    cmp word [0xE000], 0xA55A
    jne panic_stack
    mov si, prompt
    call print_string
    call read_line
    call newline

    cmp byte [input_buffer], 0
    je main_prompt

    cmp byte [py_mode], 0
    jne .python_line

    mov si, input_buffer
    mov di, cmd_python
    call str_equal
    jc .python_repl

    ; python NAME.PY runs one-line source from the FAT32 volume.
    cmp dword [input_buffer], 0x68747970 ; "pyth"
    jne .normal_command
    cmp word [input_buffer+4], 0x6E6F ; "on"
    jne .normal_command
    cmp byte [input_buffer+6], ' '
    jne .normal_command
    call py_run_file
    jmp main_prompt

.python_repl:
    mov byte [py_mode], 1
    mov si, py_banner
    call print_string
    jmp main_prompt
.python_line:
    call py_execute_line
    jmp main_prompt
.normal_command:
    mov si, input_buffer
    mov di, cmd_fault
    call str_equal
    jc .fault
    mov si, input_buffer
    mov di, cmd_mem
    call str_equal
    jc .memory

    mov si, input_buffer
    mov di, cmd_guide
    call str_equal
    jc .guide
    mov si, input_buffer
    mov di, cmd_help
    call str_equal
    jc .guide

    mov si, input_buffer
    mov di, cmd_edit
    call fat_match
    jc .edit

    mov si, input_buffer
    mov di, cmd_calc
    call str_equal
    jc .calculator
    mov si, input_buffer
    mov di, cmd_calculator
    call str_equal
    jc .calculator

    mov si, input_buffer
    mov di, cmd_identity
    call str_equal
    jc .identity

    mov si, input_buffer
    mov di, cmd_where
    call str_equal
    jc .where

    mov si, input_buffer
    mov di, cmd_wipe
    call str_equal
    jc .wipe

    mov si, input_buffer
    mov di, cmd_peek
    call str_equal
    jc .filesystem

    ; say TEXT
    cmp byte [input_buffer], 's'
    jne .check_fs_commands
    cmp byte [input_buffer+1], 'a'
    jne .check_fs_commands
    cmp byte [input_buffer+2], 'y'
    jne .check_fs_commands
    cmp byte [input_buffer+3], ' '
    jne .check_fs_commands
    mov si, input_buffer+4
    call print_string
    call newline
    jmp main_prompt

.check_fs_commands:
    mov si, input_buffer
    mov di, cmd_grow
    call str_equal
    jc .filesystem
    mov si, input_buffer
    mov di, cmd_forge
    call str_equal
    jc .filesystem
    mov si, input_buffer
    mov di, cmd_read
    call str_equal
    jc .filesystem
    mov si, input_buffer
    mov di, cmd_scribe
    call str_equal
    jc .filesystem
    mov si, input_buffer
    mov di, cmd_shift
    call str_equal
    jc .filesystem
    mov si, input_buffer
    mov di, cmd_erase
    call str_equal
    jc .filesystem

    call fat32_dispatch
    jmp main_prompt

.fault:
    int 3
    jmp main_prompt
.memory:
    mov si, mem_text
    call print_string
    int 0x12
    call calc_print_u16
    mov si, mem_suffix
    call print_string
    jmp main_prompt

.guide:
    mov si, help_text
    call print_string
    jmp main_prompt

.edit:
    call editor_open
    jmp main_prompt
.calculator:
    call calc_app
    jmp main_prompt

.identity:
    mov si, identity_text
    call print_string
    jmp main_prompt

.where:
    mov si, where_text
    call print_string
    jmp main_prompt

.wipe:
    mov ax, 0x0003
    int 0x10
    jmp main_prompt

.filesystem:
    call fat32_dispatch
    jmp main_prompt

panic_handler:
    cli
    xor ax, ax
    mov ds, ax
    mov ax, 0x0003
    int 0x10
    mov si, panic_text
    call print_string
.halt:
    cli
    hlt
    jmp .halt
panic_stack:
    cli
    mov ax, 0x0003
    int 0x10
    mov si, panic_stack_text
    call print_string
    jmp panic_handler.halt

; Read up to 63 printable characters, support backspace, NUL terminate.
read_line:
    mov di, input_buffer
    xor cx, cx
.key:
    xor ah, ah
    int 0x16

    cmp al, 0x0D
    je .finish
    cmp al, 0x08
    je .backspace
    cmp al, 0x20
    jb .key
    cmp al, 0x7E
    ja .key
    cmp cx, 63
    jae .key

    mov [di], al
    inc di
    inc cx
    push ax
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    pop ax
    jmp .key

.backspace:
    jcxz .key
    dec di
    dec cx
    mov byte [di], 0
    push ax
    mov al, 0x08
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    mov al, ' '
    int 0x10
    mov al, 0x08
    int 0x10
    pop ax
    jmp .key

.finish:
    mov byte [di], 0
    ret

; Print NUL-terminated string at DS:SI.
print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    jmp print_string
.done:
    ret

clear_screen:
    mov ax, 0x0600
    mov bh, 0x07
    xor cx, cx
    mov dx, 0x184F
    int 0x10
    mov ax, 0x0200
    mov bx, 0
    xor dx, dx
    int 0x10
    ret

newline:
    push si
    mov si, newline_text
    call print_string
    pop si
    ret

; Compare DS:SI and DS:DI. Carry set if equal, clear otherwise.
str_equal:
    push si
    push di
.loop:
    mov al, [si]
    cmp al, [di]
    jne .different
    test al, al
    jz .equal
    inc si
    inc di
    jmp .loop
.equal:
    pop di
    pop si
    stc
    ret
.different:
    pop di
    pop si
    clc
    ret

banner db 13,10,"AARAMBH OS v0.4",13,10
       db "BIOS terminal foundation",13,10
       db "Type guide for commands.",13,10,13,10,0
prompt db "AR:/ > ",0
newline_text db 13,10,0
cmd_python db "python",0
cmd_fault db "fault",0
cmd_mem db "mem",0
cmd_edit db "edit",0
cmd_calc db "calc",0
cmd_calculator db "calculator",0
cmd_guide db "guide",0
cmd_help db "help",0
cmd_identity db "identity",0
cmd_where db "where",0
cmd_wipe db "wipe",0
cmd_peek db "peek",0
cmd_grow db "grow",0
cmd_forge db "forge",0
cmd_read db "read",0
cmd_scribe db "scribe",0
cmd_shift db "shift",0
cmd_erase db "erase",0
panic_text db 13,10,"AARAMBH PANIC: CPU exception/breakpoint. System halted.",13,10,0
panic_stack_text db 13,10,"AARAMBH PANIC: kernel stack canary damaged.",13,10,0
mem_text db "Conventional memory reported by BIOS: ",0
mem_suffix db " KiB (not free heap).",13,10,0
help_text db "AARAMBH COMMAND GUIDE",13,10
          db "--------------------",13,10
          db "System: guide/help, identity, where, wipe, mem",13,10
          db "Debug: fault deliberately triggers a panic and halts.",13,10
          db "Files:  peek, edit NAME.TXT, read NAME.TXT",13,10
          db "        scribe NAME.TXT TEXT, grow, forge",13,10
          db "        shift, erase (FAT32 8.3 names)",13,10
          db "Tools:  calc (a op b), python [FILE.PY]",13,10
          db "        PyMini: print(text), print(2+3), expressions",13,10
          db "Editor: arrows, Enter, Backspace, Delete",13,10
          db "        Ctrl-S saves; Esc exits without saving",13,10
          db "Storage: 128 MiB FAT32; editor files up to 4096B",13,10
          db "        scribe/grow accept one command line",13,10,0
identity_text db "Aarambh OS v0.4 - 16-bit BIOS terminal",13,10,0
where_text db "AR:/",13,10,0
unknown_msg db "Unknown command. Type guide.",13,10,0
fs_not_ready db "FAT32 operation unavailable.",13,10,0

input_buffer times 64 db 0
py_mode db 0
py_quote db 0
py_banner db "Aarambh PyMini (Python subset; type exit() to return)",13,10,0
py_prompt db "... ",0
py_error db "PyMini: print('text'), print(2+3), or a+b/a-b/a*b/a/b.",13,10,0

; In-kernel Python-compatible mini REPL. This intentionally implements only
; print of a quoted literal; it is an interpreter in the guest, not CPython.
py_execute_line:
    cmp byte [input_buffer], 0
    je .done
    mov si, input_buffer
    mov di, py_exit
    call str_equal
    jc .exit
    mov si, input_buffer
    mov di, py_print_prefix
    call py_prefix
    jc .print_stmt
    call calc_eval
    jnc .unsupported
    call calc_print_result
    ret
.print_stmt:
    call py_print_statement
    ret
.unsupported:
    mov si, py_error
    call print_string
    ret
.exit:
    mov byte [py_mode], 0
.done:
    ret

py_exit db "exit()",0
py_print_prefix db "print(",0

py_print_statement:
    mov si, input_buffer
    mov di, py_print_prefix
    call py_prefix
    jnc .unsupported
    lodsb
    cmp al, 34
    je .double_quote
    cmp al, 39
    jne .number_expression
    mov byte [py_quote], 39
    jmp .copy
.double_quote:
    mov byte [py_quote], 34
.copy:
    mov di, py_output
.loop:
    lodsb
    test al, al
    jz .unsupported
    cmp al, [py_quote]
    je .closed
    stosb
    jmp .loop
.closed:
    cmp byte [si], ')'
    jne .unsupported
    inc si
    cmp byte [si], 0
    jne .unsupported
    mov byte [di], 0
    mov si, py_output
    call print_string
    call newline
    ret
.number_expression:
    ; Python-style print(integer expression), using the shared calculator.
    mov si, input_buffer+6
    mov di, input_buffer
.number_copy:
    lodsb
    test al, al
    jz .unsupported
    cmp al, ')'
    je .number_closed
    stosb
    jmp .number_copy
.number_closed:
    cmp byte [si], 0
    jne .unsupported
    mov byte [di], 0
    call calc_eval
    jnc .unsupported
    call calc_print_result
    ret
.unsupported:
    mov si, py_error
    call print_string
    ret

; Prefix compare; SI remains at the first character after DI.
py_prefix:
.loop:
    mov al, [di]
    test al, al
    jz .yes
    cmp al, [si]
    jne .no
    inc si
    inc di
    jmp .loop
.yes:
    stc
    ret
.no:
    clc
    ret

; Shared small integer evaluator used by the Python REPL and calculator.
; Grammar: unsigned_integer operator unsigned_integer; operator + - * /.
calc_eval:
    push bx
    push cx
    push dx
    push si
    mov si, input_buffer
    call calc_parse_number
    jnc .bad
    mov [calc_left], ax
    mov al, [si]
    cmp al, '+'
    je .operator_ok
    cmp al, '-'
    je .operator_ok
    cmp al, '*'
    je .operator_ok
    cmp al, '/'
    jne .bad
.operator_ok:
    mov [calc_operator], al
    inc si
    call calc_parse_number
    jnc .bad
    cmp byte [si], 0
    jne .bad
    mov bx, ax
    mov ax, [calc_left]
    mov dl, [calc_operator]
    cmp dl, '+'
    je .add
    cmp dl, '-'
    je .subtract
    cmp dl, '*'
    je .multiply
    test bx, bx
    jz .bad
    xor dx, dx
    div bx
    jmp .good
.add:
    add ax, bx
    jmp .good
.subtract:
    cmp ax, bx
    jb .bad
    sub ax, bx
    jmp .good
.multiply:
    mul bx
.good:
    mov [calc_result], ax
    pop si
    pop dx
    pop cx
    pop bx
    mov ax, [calc_result]
    stc
    ret
.bad:
    pop si
    pop dx
    pop cx
    pop bx
    clc
    ret

calc_parse_number:
    xor ax, ax
    xor cx, cx
.digit:
    mov bl, [si]
    cmp bl, '0'
    jb .end
    cmp bl, '9'
    ja .end
    mov dx, ax
    shl ax, 1
    shl dx, 3
    add ax, dx
    movzx bx, byte [si]
    sub bx, '0'
    add ax, bx
    inc si
    inc cx
    jmp .digit
.end:
    test cx, cx
    jz .bad
    stc
    ret
.bad:
    clc
    ret

calc_print_result:
    mov ax, [calc_result]
    call calc_print_u16
    call newline
    ret

calc_print_u16:
    push bx
    push cx
    push dx
    mov bx, 10
    xor cx, cx
    test ax, ax
    jnz .convert
    push word 0
    inc cx
    jmp .emit
.convert:
    xor dx, dx
    div bx
    push dx
    inc cx
    test ax, ax
    jnz .convert
.emit:
    pop ax
    add al, '0'
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x07
    int 0x10
    loop .emit
    pop dx
    pop cx
    pop bx
    ret

; Full-screen BIOS text calculator; uses the same expression evaluator as PyMini.
calc_app:
    call clear_screen
    mov si, calc_frame
    call print_string
.loop:
    mov si, calc_prompt
    call print_string
    call read_line
    mov si, input_buffer
    mov di, calc_quit
    call str_equal
    jc .done
    cmp byte [input_buffer], 0
    je .loop
    call calc_eval
    jnc .error
    mov si, calc_equals
    call print_string
    call calc_print_result
    jmp .loop
.error:
    mov si, calc_error
    call print_string
    jmp .loop
.done:
    call clear_screen
    ret

calc_frame db "+--------------------------------------+",13,10
           db "|       AARAMBH  CALCULATOR            |",13,10
           db "+--------------------------------------+",13,10
           db "|  [ 7 ] [ 8 ] [ 9 ] [ / ]            |",13,10
           db "|  [ 4 ] [ 5 ] [ 6 ] [ * ]            |",13,10
           db "|  [ 1 ] [ 2 ] [ 3 ] [ - ]            |",13,10
           db "|  [ 0 ] [ + ] [ = ] [ Q ]            |",13,10
           db "+--------------------------------------+",13,10
           db "Type two integers and an operator, e.g. 24*3",13,10
           db "Unsigned 16-bit integer arithmetic; q returns.",13,10,13,10,0
calc_prompt db "calc> ",0
calc_quit db "q",0
calc_equals db "= ",0
calc_error db "Invalid operation or division by zero.",13,10,0
calc_left dw 0
calc_result dw 0
calc_operator db 0

; Full-screen keyboard editor with a 4 KiB buffer and cluster-chain file I/O.
editor_open:
    call fat_mount
    jc fat_mount_error
    call fat_parse_name
    jnc fat_name_error
    call fat_find
    jnc .new_file
    mov di, fat_sector
    mov ax, [fat_entry]
    shl ax, 5
    add di, ax
    mov eax, [di+28]
    cmp eax, 4096
    ja editor_size_error
    mov [editor_size], ax
    mov ax, [di+26]
    mov [fat_cluster], ax
    mov ax, [di+20]
    mov [fat_cluster+2], ax
    cmp dword [fat_cluster], 0
    je .blank
    call editor_load_file
    jc fat_disk_error
    jmp .start
.new_file:
    cmp byte [fat_free], 1
    jne fat_full_error
.blank:
    mov word [editor_size], 0
    mov dword [fat_cluster], 0
.start:
    mov ax, [editor_size]
    mov [editor_cursor], ax
    mov word [editor_view_line], 0
    call editor_loop
    ret

editor_load_file:
    mov ax, [editor_size]
    mov [editor_remaining], ax
    mov word [editor_offset], 0
    mov eax, [fat_cluster]
    mov [editor_work_cluster], eax
.load:
    cmp word [editor_remaining], 0
    je .done
    mov eax, [editor_work_cluster]
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov word [fat_buf], fat_file
    call fat_read_sector
    jc .fail
    mov cx, 512
    cmp word [editor_remaining], 512
    jae .copy
    mov cx, [editor_remaining]
.copy:
    mov [editor_chunk], cx
    mov si, fat_file
    mov di, editor_buffer
    add di, [editor_offset]
    rep movsb
    mov ax, [editor_chunk]
    add [editor_offset], ax
    sub [editor_remaining], ax
    cmp word [editor_remaining], 0
    je .done
    mov eax, [editor_work_cluster]
    mov [fat_cluster], eax
    call fat_get_next
    jc .fail
    cmp eax, 0x0FFFFFF8
    jae .fail
    mov [editor_work_cluster], eax
    jmp .load
.done:
    clc
    ret
.fail:
    stc
    ret

editor_write_file:
    mov ax, [editor_size]
    mov [editor_remaining], ax
    mov word [editor_offset], 0
    mov eax, [editor_first_cluster]
    mov [editor_work_cluster], eax
.cluster:
    mov di, fat_file
    mov cx, 512
    xor al, al
    rep stosb
    mov cx, 512
    cmp word [editor_remaining], 512
    jae .copy
    mov cx, [editor_remaining]
.copy:
    mov [editor_chunk], cx
    mov si, editor_buffer
    add si, [editor_offset]
    mov di, fat_file
    rep movsb
    mov eax, [editor_work_cluster]
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov word [fat_buf], fat_file
    call fat_disk_write
    jc .fail
    mov ax, [editor_chunk]
    add [editor_offset], ax
    sub [editor_remaining], ax
    cmp word [editor_remaining], 0
    je .done
    mov eax, [editor_work_cluster]
    mov [fat_cluster], eax
    call fat_get_next
    jc .fail
    cmp eax, 0x0FFFFFF8
    jae .allocate
    mov [editor_work_cluster], eax
    jmp .cluster
.allocate:
    call fat_allocate
    jc .fail
    mov eax, [fat_cluster]
    mov [editor_next_cluster], eax
    mov eax, [editor_work_cluster]
    mov [fat_cluster], eax
    mov eax, [editor_next_cluster]
    mov [fat_fat_value], eax
    call fat_update_entry
    jc .fail
    mov eax, [editor_next_cluster]
    mov [editor_work_cluster], eax
    jmp .cluster
.done:
    clc
    ret
.fail:
    stc
    ret

editor_size_error:
    mov si, editor_size_msg
    call print_string
    ret
editor_size_msg db "Editor file limit is 4096 bytes.",13,10,0

editor_loop:
.draw:
    call editor_draw
    xor ah, ah
    int 0x16
    cmp al, 19
    je .save
    cmp al, 27
    je .done
    cmp al, 13
    je .newline
    cmp al, 8
    je .backspace
    test al, al
    jz .extended
    cmp al, 32
    jb .draw
    cmp al, 126
    ja .draw
    call editor_insert
    jmp .draw
.extended:
    cmp ah, 0x4B
    je .left
    cmp ah, 0x4D
    je .right
    cmp ah, 0x53
    je .delete
    cmp ah, 0x48
    je .up
    cmp ah, 0x50
    je .down
    jmp .draw
.left:
    cmp word [editor_cursor], 0
    je .draw
    dec word [editor_cursor]
    jmp .draw
.right:
    mov ax, [editor_cursor]
    cmp ax, [editor_size]
    jae .draw
    inc word [editor_cursor]
    jmp .draw
.up:
    call editor_move_up
    jmp .draw
.down:
    call editor_move_down
    jmp .draw
.backspace:
    cmp word [editor_cursor], 0
    je .draw
    dec word [editor_cursor]
    mov bx, [editor_cursor]
    jmp editor_remove_at
.delete:
    mov bx, [editor_cursor]
    cmp bx, [editor_size]
    jae .draw
    jmp editor_remove_at
.newline:
    mov al, 10
    call editor_insert
    jmp .draw
.save:
    call editor_save
    jmp .draw
.done:
    ret

editor_move_up:
    ; Preserve the current column, locate the start of the previous line.
    mov bx, [editor_cursor]
    xor dx, dx
.col:
    test bx, bx
    jz .column_done
    dec bx
    cmp byte [editor_buffer+bx], 10
    je .column_done
    inc dx
    jmp .col
.column_done:
    mov bx, [editor_cursor]
.find_current_start:
    test bx, bx
    jz .no_move
    dec bx
    cmp byte [editor_buffer+bx], 10
    jne .find_current_start
    ; bx points at current line's preceding newline; find previous one.
.find_previous_start:
    test bx, bx
    jz .start_zero
    dec bx
    cmp byte [editor_buffer+bx], 10
    jne .find_previous_start
    inc bx
    jmp .seek_column
.start_zero:
    xor bx, bx
.seek_column:
    mov cx, dx
.column_loop:
    jcxz .set
    cmp bx, [editor_size]
    jae .set
    cmp byte [editor_buffer+bx], 10
    je .set
    inc bx
    loop .column_loop
.set:
    mov [editor_cursor], bx
.no_move:
    ret

editor_move_down:
    ; Move to the same column on the following line, clamped to line end.
    mov bx, [editor_cursor]
    xor dx, dx
.col:
    test bx, bx
    jz .column_done
    dec bx
    cmp byte [editor_buffer+bx], 10
    je .column_done
    inc dx
    jmp .col
.column_done:
    mov bx, [editor_cursor]
.find_end:
    cmp bx, [editor_size]
    jae .done
    cmp byte [editor_buffer+bx], 10
    je .next_line
    inc bx
    jmp .find_end
.next_line:
    inc bx
    mov cx, dx
.column_loop:
    jcxz .set
    cmp bx, [editor_size]
    jae .set
    cmp byte [editor_buffer+bx], 10
    je .set
    inc bx
    loop .column_loop
.set:
    mov [editor_cursor], bx
.done:
    ret

editor_insert:
    cmp word [editor_size], 4096
    jae .full
    mov bx, [editor_size]
.shift:
    cmp bx, [editor_cursor]
    jbe .place
    mov si, editor_buffer
    mov al, [si+bx-1]
    mov [si+bx], al
    dec bx
    jmp .shift
.place:
    mov bx, [editor_cursor]
    mov [editor_buffer+bx], al
    inc word [editor_cursor]
    inc word [editor_size]
    ret
.full:
    mov si, editor_full
    call print_string
    ret

editor_remove_at:
    mov bx, [editor_cursor]
.loop:
    mov ax, [editor_size]
    dec ax
    cmp bx, ax
    jae .finish
    mov al, [editor_buffer+bx+1]
    mov [editor_buffer+bx], al
    inc bx
    jmp .loop
.finish:
    dec word [editor_size]
    ret

editor_draw:
    call clear_screen
    mov si, editor_title
    call print_string
    mov si, fat_name
    mov cx, 8
    call editor_print_name
    mov al, '.'
    call editor_putc
    mov cx, 3
    call editor_print_name
    call newline
    mov si, editor_help
    call print_string
    ; Find cursor's logical line and keep a 21-line viewport around it.
    xor bx, bx
    mov si, editor_buffer
    mov cx, [editor_cursor]
.line_scan:
    jcxz .cursor_line
    lodsb
    cmp al, 10
    jne .line_scan_next
    inc bx
.line_scan_next:
    loop .line_scan
.cursor_line:
    mov [editor_cursor_line], bx
    mov ax, [editor_view_line]
    cmp bx, ax
    jae .check_bottom
    mov [editor_view_line], bx
.check_bottom:
    mov ax, bx
    sub ax, [editor_view_line]
    cmp ax, 20
    jbe .render
    sub bx, 20
    mov [editor_view_line], bx
.render:
    mov si, editor_buffer
    mov cx, [editor_size]
    mov bx, [editor_view_line]
.skip_outer:
    test bx, bx
    jz .visible
.skip_inner:
    jcxz .visible
    lodsb
    dec cx
    cmp al, 10
    jne .skip_inner
    dec bx
    jmp .skip_outer
.visible:
    mov [editor_remaining], cx
    xor dx, dx
.visible_loop:
    cmp word [editor_remaining], 0
    je .cursor_position
    lodsb
    dec word [editor_remaining]
    cmp al, 10
    jne .visible_char
    call newline
    inc dx
    cmp dx, 21
    jae .cursor_position
    jmp .visible_loop
.visible_char:
    call editor_putc
    jmp .visible_loop
.cursor_position:
    mov ax, [editor_cursor_line]
    sub ax, [editor_view_line]
    add ax, 3
    mov [editor_row], ax
    mov word [editor_col], 0
    mov si, editor_buffer
    mov cx, [editor_cursor]
.col_scan:
    jcxz .set_cursor
    lodsb
    cmp al, 10
    jne .column
    mov word [editor_col], 0
    loop .col_scan
    jmp .set_cursor
.column:
    inc word [editor_col]
    loop .col_scan
.set_cursor:
    mov ax, [editor_col]
    mov bx, 80
    xor dx, dx
    div bx
    add [editor_row], ax
    mov [editor_col], dx
    cmp word [editor_row], 24
    jbe .position
    mov word [editor_row], 24
.position:
    mov ah, 0x02
    mov bh, 0
    mov dh, byte [editor_row]
    mov dl, byte [editor_col]
    int 0x10
    ret

editor_print_name:
.loop:
    lodsb
    cmp al, ' '
    je .skip
    call editor_putc
.skip:
    loop .loop
    ret

editor_putc:
    mov ah, 0x0E
    mov bh, 0
    mov bl, 0x0F
    int 0x10
    ret

editor_save:
    call fat_mount
    jc fat_mount_error
    ; SI is not meaningful on save; the filename already resides in fat_name.
    call fat_find
    jc .existing
    cmp byte [fat_free], 1
    jne fat_full_error
    mov byte [editor_is_new], 1
    mov ax, [fat_entry]
    mov [fat_saved_entry], ax
    mov dword [fat_cluster], 0
    jmp .write
.existing:
    mov byte [editor_is_new], 0
    mov ax, [fat_entry]
    mov [fat_saved_entry], ax
    call fat_entry_ptr
    mov ax, [di+26]
    mov [fat_cluster], ax
    mov ax, [di+20]
    mov [fat_cluster+2], ax
.write:
    cmp word [editor_size], 0
    je .metadata
    cmp dword [fat_cluster], 0
    jne .write_chain
    call fat_allocate
    jc fat_full_error
.write_chain:
    mov eax, [fat_cluster]
    mov [editor_first_cluster], eax
    call editor_write_file
    jc fat_disk_error
    mov eax, [editor_first_cluster]
    mov [fat_cluster], eax
.metadata:
    mov ax, [fat_saved_entry]
    mov [fat_entry], ax
    call fat_read_root
    jc fat_disk_error
    call fat_entry_ptr
    cmp byte [editor_is_new], 0
    je .fields
    push di
    mov cx, 32
    xor al, al
    rep stosb
    pop di
    push di
    mov si, fat_name
    mov cx, 11
    rep movsb
    pop di
    mov byte [di+11], 0x20
.fields:
    mov ax, [fat_cluster]
    mov [di+26], ax
    mov ax, [fat_cluster+2]
    mov [di+20], ax
    movzx eax, word [editor_size]
    mov [di+28], eax
    call fat_write_root
    jc fat_disk_error
    mov si, editor_saved
    call print_string
    ret

editor_title db " AARAMBH EDITOR  |  Ctrl-S save  |  Esc exit",13,10,0
editor_help db "Arrows move  Enter newline  Backspace/Delete edit",13,10,0
editor_saved db 13,10,"Saved to FAT32.",0
editor_full db 13,10,"Editor buffer full (512 bytes).",0
editor_size dw 0
editor_cursor dw 0
editor_row dw 0
editor_col dw 0
editor_view_line dw 0
editor_cursor_line dw 0
editor_is_new db 0
editor_first_cluster dd 0
editor_work_cluster dd 0
editor_next_cluster dd 0
editor_offset dw 0
editor_remaining dw 0
editor_chunk dw 0
editor_buffer times 4096 db 0

py_output times 64 db 0

; Execute a single-line .PY source file from the mounted FAT32 volume.
py_run_file:
    call fat_mount
    jc fat_mount_error
    mov si, input_buffer+7
    call fat_skip_spaces
    call fat_parse_name
    jnc fat_name_error
    call fat_find
    jnc fat_missing_error
    mov di, fat_sector
    mov ax, [fat_entry]
    shl ax, 5
    add di, ax
    mov ax, [di+26]
    mov [fat_cluster], ax
    mov ax, [di+20]
    mov [fat_cluster+2], ax
    mov eax, [di+28]
    cmp eax, 63
    ja py_script_size_error
    mov [fat_size], ax
    test eax, eax
    jz .empty
    mov eax, [fat_cluster]
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov word [fat_buf], fat_file
    call fat_read_sector
    jc fat_disk_error
    mov si, fat_file
    mov di, input_buffer
    mov cx, [fat_size]
    rep movsb
    mov byte [di], 0
    call py_print_statement
    ret
.empty:
    ret

py_script_size_error:
    mov si, py_size_error
    call print_string
    ret
py_size_error db "PyMini script limit is 63 bytes.",13,10,0

%include "fat32.asm"
boot_drive db 0