; Aarambh PM32 full-screen editor.
; Edits fs_file_buffer in place, uses AarFS's normal read/write/commit path.
; Text area: rows 0..22. Message row: 23. Status bar: 24.
; Keys: printable, Enter, Backspace, Delete, arrows, Home, End, PgUp/PgDn,
;       Ctrl-S save, Esc/Ctrl-Q exit, F1 help.

editor_command:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    mov ebp, [fs_file_buffer]   ; EBP is the editor buffer base throughout
    call fs_get_arg
    test esi, esi
    jz .usage
    test edi, edi
    jnz .usage
    mov [ed_name], esi
    call fs_validate_name
    test eax, eax
    jz .bad_name
    mov [ed_name_len], eax
    mov eax, [fs_cwd]
    mov esi, [ed_name]
    call fs_find_child
    test eax, eax
    jz .new_file
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .not_file
    mov [ed_entry_id], eax
    call fs_read_file
    jc .read_error
    mov eax, [fs_file_size]
    cmp eax, AFS_FILE_BYTES
    ja .too_big
    mov [ed_size], eax
    mov byte [ed_new], 0
    jmp .loaded
.new_file:
    mov dword [ed_entry_id], 0
    mov dword [ed_size], 0
    mov byte [ed_new], 1
.loaded:
    call editor_normalize
    mov dword [ed_cursor], 0
    mov dword [ed_top_line], 0
    mov byte [ed_dirty], 0
    mov byte [ed_saved], 0
    mov byte [ed_discarded], 0
    mov byte [ed_confirm], 0
    mov dword [ed_msg], 0
    mov byte [ed_msg_attr], 0x0E
    call editor_loop
    call console_clear
    mov byte [vga_attr], 0x0F
    mov esi, ed_closed_text
    call console_write
    mov esi, [ed_name]
    call console_write
    cmp byte [ed_saved], 0
    jne .say_saved
    cmp byte [ed_discarded], 0
    jne .say_discarded
    mov esi, ed_closed_plain
    call console_write
    ret
.say_saved:
    mov esi, ed_closed_saved
    call console_write
    ret
.say_discarded:
    mov esi, ed_closed_discarded
    call console_write
    ret
.usage:
    mov esi, ed_usage_text
    call console_write
    ret
.bad_name:
    mov esi, ed_bad_name_text
    call console_write
    ret
.not_file:
    mov esi, ed_not_file_text
    call console_write
    ret
.read_error:
    mov esi, ed_read_error_text
    call console_write
    ret
.too_big:
    mov esi, ed_too_big_text
    call console_write
    ret

; Convert CRLF to LF and tabs to one space in place.
editor_normalize:
    mov esi, [fs_file_buffer]
    mov edi, esi
    mov ecx, [ed_size]
    xor ebx, ebx
.loop:
    test ecx, ecx
    jz .done
    lodsb
    dec ecx
    cmp al, 13
    jne .not_cr
    test ecx, ecx
    jz .cr_only
    cmp byte [esi], 10
    jne .cr_only
    inc esi
    dec ecx
.cr_only:
    mov al, 10
    jmp .store
.not_cr:
    cmp al, 9
    jne .store
    mov al, ' '
.store:
    stosb
    inc ebx
    jmp .loop
.done:
    mov [ed_size], ebx
    ret

editor_loop:
.loop:
    call editor_render
    mov dword [ed_msg], 0
    call get_key
    cmp al, 27
    je .escape
    cmp al, 0x11
    je .escape
    mov byte [ed_confirm], 0
    cmp al, 0x13
    je .save
    cmp al, 0x99
    je .help
    cmp al, 13
    je .newline
    cmp al, 8
    je .backspace
    cmp al, 0x96
    je .delete
    cmp al, 0x92
    je .left
    cmp al, 0x93
    je .right
    cmp al, 0x90
    je .up
    cmp al, 0x91
    je .down
    cmp al, 0x94
    je .home
    cmp al, 0x95
    je .end
    cmp al, 0x97
    je .pgup
    cmp al, 0x98
    je .pgdn
    cmp al, 32
    jb .loop
    cmp al, 126
    ja .loop
    call editor_insert_char
    jmp .loop
.escape:
    cmp byte [ed_dirty], 0
    je .exit
    cmp byte [ed_confirm], 0
    jne .discard
    mov byte [ed_confirm], 1
    mov dword [ed_msg], ed_msg_unsaved
    mov byte [ed_msg_attr], 0x0E
    jmp .loop
.discard:
    mov byte [ed_discarded], 1
    jmp .exit
.exit:
    ret
.save:
    call editor_save
    jmp .loop
.help:
    call editor_show_help
    jmp .loop
.newline:
    mov al, 10
    call editor_insert_char
    jmp .loop
.backspace:
    call editor_backspace
    jmp .loop
.delete:
    call editor_delete
    jmp .loop
.left:
    cmp dword [ed_cursor], 0
    je .loop
    dec dword [ed_cursor]
    jmp .loop
.right:
    mov eax, [ed_cursor]
    cmp eax, [ed_size]
    jae .loop
    inc dword [ed_cursor]
    jmp .loop
.up:
    call editor_move_up
    jmp .loop
.down:
    call editor_move_down
    jmp .loop
.home:
    call editor_cursor_info
    mov eax, [ed_cursor_start]
    mov [ed_cursor], eax
    jmp .loop
.end:
    call editor_end_of_line
    jmp .loop
.pgup:
    mov ecx, 20
.pgup_loop:
    push ecx
    call editor_move_up
    pop ecx
    dec ecx
    jnz .pgup_loop
    jmp .loop
.pgdn:
    mov ecx, 20
.pgdn_loop:
    push ecx
    call editor_move_down
    pop ecx
    dec ecx
    jnz .pgdn_loop
    jmp .loop

editor_insert_char:
    cmp dword [ed_size], AFS_FILE_BYTES
    jae editor_full
    mov [ed_char], al
    mov ecx, [ed_size]
    sub ecx, [ed_cursor]
    jz .store
    mov esi, [fs_file_buffer]
    add esi, [ed_size]
    dec esi
    mov edi, esi
    inc edi
.shift:
    mov al, [esi]
    mov [edi], al
    dec esi
    dec edi
    dec ecx
    jnz .shift
.store:
    mov edi, [fs_file_buffer]
    add edi, [ed_cursor]
    mov al, [ed_char]
    mov [edi], al
    inc dword [ed_cursor]
    inc dword [ed_size]
    call editor_mark_dirty
    ret

editor_backspace:
    cmp dword [ed_cursor], 0
    je .done
    mov ecx, [ed_size]
    sub ecx, [ed_cursor]
    mov esi, [fs_file_buffer]
    add esi, [ed_cursor]
    mov edi, esi
    dec edi
.copy:
    test ecx, ecx
    jz .shifted
    mov al, [esi]
    mov [edi], al
    inc esi
    inc edi
    dec ecx
    jmp .copy
.shifted:
    dec dword [ed_cursor]
    dec dword [ed_size]
    call editor_mark_dirty
.done:
    ret

editor_delete:
    mov eax, [ed_cursor]
    cmp eax, [ed_size]
    jae .done
    mov ecx, [ed_size]
    sub ecx, eax
    dec ecx
    test ecx, ecx
    jz .shifted
    mov esi, [fs_file_buffer]
    add esi, eax
    inc esi
    mov edi, esi
    dec edi
.copy:
    mov al, [esi]
    mov [edi], al
    inc esi
    inc edi
    dec ecx
    jnz .copy
.shifted:
    dec dword [ed_size]
    call editor_mark_dirty
.done:
    ret

editor_mark_dirty:
    mov byte [ed_dirty], 1
    mov byte [ed_saved], 0
    mov byte [ed_discarded], 0
    ret

editor_full:
    mov dword [ed_msg], ed_msg_full
    mov byte [ed_msg_attr], 0x0C
    ret

; EAX=line index -> EAX=byte offset of its start, or ed_size if past EOF.
editor_line_start_by_index:
    mov ecx, eax
    xor eax, eax
    xor edx, edx
    test ecx, ecx
    jz .done
.loop:
    cmp eax, [ed_size]
    jae .done
    cmp byte [ebp+eax], 10
    jne .next
    inc eax
    inc edx
    cmp edx, ecx
    jb .loop
    ret
.next:
    inc eax
    jmp .loop
.done:
    ret

; EAX=number of lines (empty file counts as one).
editor_line_count:
    mov eax, 1
    xor ecx, ecx
.loop:
    cmp ecx, [ed_size]
    jae .done
    cmp byte [ebp+ecx], 10
    jne .next
    inc eax
.next:
    inc ecx
    jmp .loop
.done:
    ret

; Fills ed_cursor_start, ed_cursor_col and ed_cursor_line.
editor_cursor_info:
    xor edi, edi
    xor ebx, ebx
    xor ecx, ecx
    mov edx, [ed_cursor]
.scan:
    cmp ecx, edx
    jae .done
    cmp byte [ebp+ecx], 10
    jne .next
    lea edi, [ecx+1]
    inc ebx
.next:
    inc ecx
    jmp .scan
.done:
    mov [ed_cursor_start], edi
    mov eax, edx
    sub eax, edi
    mov [ed_cursor_col], eax
    mov [ed_cursor_line], ebx
    ret

editor_end_of_line:
    call editor_cursor_info
    mov ecx, [ed_cursor_start]
.scan:
    cmp ecx, [ed_size]
    jae .set
    cmp byte [ebp+ecx], 10
    je .set
    inc ecx
    jmp .scan
.set:
    mov [ed_cursor], ecx
    ret

editor_move_up:
    call editor_cursor_info
    mov eax, [ed_cursor_line]
    test eax, eax
    jz editor_home_line
    dec eax
    call editor_line_start_by_index
    mov edi, eax
    mov ecx, edi
.find_end:
    cmp ecx, [ed_size]
    jae .end_found
    cmp byte [ebp+ecx], 10
    je .end_found
    inc ecx
    jmp .find_end
.end_found:
    mov eax, edi
    add eax, [ed_cursor_col]
    cmp eax, ecx
    jbe .set
    mov eax, ecx
.set:
    mov [ed_cursor], eax
    call editor_fix_view
    ret

editor_home_line:
    mov eax, [ed_cursor_start]
    mov [ed_cursor], eax
    ret

editor_move_down:
    call editor_cursor_info
    call editor_line_count
    mov [ed_total_lines], eax
    mov eax, [ed_cursor_line]
    inc eax
    cmp eax, [ed_total_lines]
    jae editor_end_of_line
    call editor_line_start_by_index
    mov edi, eax
    mov ecx, edi
.find_end:
    cmp ecx, [ed_size]
    jae .end_found
    cmp byte [ebp+ecx], 10
    je .end_found
    inc ecx
    jmp .find_end
.end_found:
    mov eax, edi
    add eax, [ed_cursor_col]
    cmp eax, ecx
    jbe .set
    mov eax, ecx
.set:
    mov [ed_cursor], eax
    call editor_fix_view
    ret

editor_fix_view:
    call editor_cursor_info
    mov eax, [ed_cursor_line]
    cmp eax, [ed_top_line]
    jae .below
    mov [ed_top_line], eax
    ret
.below:
    sub eax, [ed_top_line]
    cmp eax, 23
    jb .done
    mov eax, [ed_cursor_line]
    sub eax, 22
    mov [ed_top_line], eax
.done:
    ret

editor_save:
    mov eax, [ed_size]
    cmp eax, AFS_FILE_BYTES
    ja .too_big
    cmp dword [ed_entry_id], 0
    jne .have_entry
    mov eax, [fs_cwd]
    mov esi, [ed_name]
    call fs_find_child
    test eax, eax
    jz .create
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .not_file
    mov [ed_entry_id], eax
    jmp .have_entry
.create:
    mov esi, [ed_name]
    mov ebx, AFS_FILE_TYPE
    call fs_create_entry
    test eax, eax
    jz .name_error
    mov [ed_entry_id], eax
.have_entry:
    mov eax, [ed_entry_id]
    mov [fs_target_id], eax
    mov eax, [ed_size]
    mov [fs_new_size], eax
    call fs_write_file
    jc .disk_error
    mov byte [ed_dirty], 0
    mov byte [ed_saved], 1
    mov byte [ed_new], 0
    mov dword [ed_msg], ed_msg_saved
    mov byte [ed_msg_attr], 0x0A
    ret
.too_big:
    mov dword [ed_msg], ed_msg_save_big
    mov byte [ed_msg_attr], 0x0C
    ret
.not_file:
    mov dword [ed_msg], ed_msg_save_dir
    mov byte [ed_msg_attr], 0x0C
    ret
.name_error:
    mov dword [ed_msg], ed_msg_save_name
    mov byte [ed_msg_attr], 0x0C
    ret
.disk_error:
    mov dword [ed_msg], ed_msg_save_disk
    mov byte [ed_msg_attr], 0x0C
    ret

; ---- rendering ----------------------------------------------------------
editor_render:
    call editor_fix_view
    call editor_cursor_info
    call editor_line_count
    mov [ed_total_lines], eax
    mov eax, [ed_cursor_col]
    cmp eax, 76
    jbe .hs_zero
    sub eax, 76
    jmp .hs_set
.hs_zero:
    xor eax, eax
.hs_set:
    mov [ed_hscroll], eax
    call editor_clear_screen
    mov eax, [ed_top_line]
    call editor_line_start_by_index
    mov [ed_render_off], eax
    mov eax, [ed_top_line]
    mov [ed_render_line], eax
    mov dword [ed_row], 0
.line_loop:
    cmp dword [ed_row], 23
    jae .text_done
    mov eax, [ed_render_line]
    cmp eax, [ed_total_lines]
    jae .tilde
    mov edi, [ed_render_off]
    mov byte [ed_put_attr], 0x07
.char_loop:
    cmp edi, [ed_size]
    jae .line_done
    mov al, [ebp+edi]
    cmp al, 10
    je .line_done
    mov [ed_char], al
    mov ecx, edi
    sub ecx, [ed_render_off]
    sub ecx, [ed_hscroll]
    js .skip
    cmp ecx, 80
    jae .skip
    mov eax, [ed_row]
    imul eax, eax, 80
    add eax, ecx
    mov cl, [ed_char]
    call editor_put
.skip:
    inc edi
    jmp .char_loop
.line_done:
    cmp edi, [ed_size]
    jae .advance
    cmp byte [ebp+edi], 10
    jne .advance
    inc edi
.advance:
    mov [ed_render_off], edi
    inc dword [ed_render_line]
    inc dword [ed_row]
    jmp .line_loop
.tilde:
    mov eax, [ed_row]
    imul eax, eax, 80
    mov byte [ed_put_attr], 0x08
    mov cl, '~'
    call editor_put
    inc dword [ed_render_line]
    inc dword [ed_row]
    jmp .line_loop
.text_done:
    call editor_draw_message
    call editor_draw_status
    call editor_set_hw_cursor
    call editor_snapshot
    ret

editor_clear_screen:
    push eax
    push ecx
    push edi
    mov eax, 0x07200720
    mov edi, 0xB8000
    mov ecx, 1000
    rep stosd
    pop edi
    pop ecx
    pop eax
    ret

; EAX=linear cell offset, CL=character, [ed_put_attr]=attribute.
editor_put:
    push eax
    push edx
    push edi
    mov edi, eax
    shl edi, 1
    mov dl, cl
    mov dh, [ed_put_attr]
    mov [0xB8000+edi], dx
    pop edi
    pop edx
    pop eax
    ret

; Uses [ed_row], [ed_col], [ed_put_attr], stops at NUL or column 80.
editor_print_at:
    push esi
.loop:
    lodsb
    test al, al
    jz .done
    cmp dword [ed_col], 80
    jae .done
    mov [ed_char], al
    mov eax, [ed_row]
    imul eax, eax, 80
    add eax, [ed_col]
    mov cl, [ed_char]
    call editor_put
    inc dword [ed_col]
    jmp .loop
.done:
    pop esi
    ret

editor_draw_message:
    mov dword [ed_row], 23
    mov dword [ed_col], 0
    mov esi, [ed_msg]
    test esi, esi
    jz .shortcuts
    mov al, [ed_msg_attr]
    mov [ed_put_attr], al
    call editor_print_at
    ret
.shortcuts:
    mov byte [ed_put_attr], 0x08
    mov esi, ed_shortcut_text
    call editor_print_at
    ret

editor_draw_status:
    call editor_build_status
    mov dword [ed_row], 24
    mov dword [ed_col], 0
    mov byte [ed_put_attr], 0x1F
    mov esi, ed_status
    call editor_print_at
    ret

editor_build_status:
    mov dword [ed_status_len], 0
    mov esi, ed_status_edit
    call status_append_string
    mov esi, [ed_name]
    call status_append_string
    mov esi, ed_status_gap
    call status_append_string
    mov eax, [ed_size]
    call status_append_u32
    mov esi, ed_status_bytes
    call status_append_string
    mov eax, [ed_cursor_line]
    inc eax
    call status_append_u32
    mov esi, ed_status_lncol
    call status_append_string
    mov eax, [ed_cursor_col]
    inc eax
    call status_append_u32
    mov esi, ed_status_gap
    call status_append_string
    cmp byte [ed_dirty], 0
    jne .modified
    cmp byte [ed_saved], 0
    jne .saved
    mov esi, ed_status_ready
    jmp .append
.modified:
    mov esi, ed_status_modified
    jmp .append
.saved:
    mov esi, ed_status_saved
.append:
    call status_append_string
    mov ecx, [ed_status_len]
    mov byte [ed_status+ecx], 0
    ret

status_append_char:
    push ecx
    mov ecx, [ed_status_len]
    cmp ecx, 79
    jae .ret
    mov [ed_status+ecx], al
    inc dword [ed_status_len]
.ret:
    pop ecx
    ret

status_append_string:
    lodsb
    test al, al
    jz .done
    call status_append_char
    jmp status_append_string
.done:
    ret

status_append_u32:
    pushad
    mov ebx, 10
    xor ecx, ecx
    test eax, eax
    jnz .convert
    mov al, '0'
    call status_append_char
    jmp .done
.convert:
    xor edx, edx
    div ebx
    push edx
    inc ecx
    test eax, eax
    jnz .convert
.emit:
    pop eax
    add al, '0'
    call status_append_char
    loop .emit
.done:
    popad
    ret

editor_set_hw_cursor:
    mov eax, [ed_cursor_line]
    sub eax, [ed_top_line]
    cmp eax, 23
    jae .done
    mov ebx, eax
    imul ebx, ebx, 80
    mov eax, [ed_cursor_col]
    sub eax, [ed_hscroll]
    jns .col_ok
    xor eax, eax
.col_ok:
    cmp eax, 79
    jbe .col_set
    mov eax, 79
.col_set:
    add eax, ebx
    push eax
    mov ebx, eax
    mov dx, 0x3D4
    mov al, 0x0F
    out dx, al
    inc dx
    mov al, bl
    out dx, al
    dec dx
    mov al, 0x0E
    out dx, al
    inc dx
    mov al, bh
    out dx, al
    pop eax
.done:
    ret

; Dump the visible editor screen to COM1 so the existing serial harness can
; verify editor state; QEMU has no shared-memory channel for tests.
editor_snapshot:
    pushad
    mov esi, ed_snapshot_prefix
    call serial_write
    xor ebx, ebx
.row:
    xor ecx, ecx
.col:
    mov eax, ebx
    imul eax, eax, 80
    add eax, ecx
    shl eax, 1
    mov al, [0xB8000+eax]
    call serial_putc
    inc ecx
    cmp ecx, 80
    jb .col
    mov al, 13
    call serial_putc
    mov al, 10
    call serial_putc
    inc ebx
    cmp ebx, 25
    jb .row
    popad
    ret

editor_show_help:
    call console_clear
    mov byte [vga_attr], 0x0B
    mov esi, ed_help_title
    call console_write
    mov byte [vga_attr], 0x07
    mov esi, ed_help_body
    call console_write
    call get_key
    ret

; ---- editor text --------------------------------------------------------
ed_closed_text db 'edit: closed ',0
ed_closed_saved db ' (saved).',13,10,0
ed_closed_discarded db ' (changes discarded).',13,10,0
ed_closed_plain db ' (no changes).',13,10,0
ed_usage_text db 'Usage: edit <file>',13,10,0
ed_bad_name_text db 'edit: invalid filename (1-39 chars, no slash or backslash).',13,10,0
ed_not_file_text db 'edit: that entry is a directory, not a file.',13,10,0
ed_read_error_text db 'edit: AarFS could not read the file.',13,10,0
ed_too_big_text db 'edit: file exceeds the 16384-byte editor limit.',13,10,0
ed_msg_unsaved db 'Unsaved changes: Ctrl-S save, Esc again discard',0
ed_msg_full db 'Buffer full: 16384-byte AarFS limit reached',0
ed_msg_saved db 'Saved to AarFS.',0
ed_msg_save_big db 'Save failed: 16384-byte limit.',0
ed_msg_save_dir db 'Save failed: that name is a directory.',0
ed_msg_save_name db 'Save failed: invalid name or directory table full.',0
ed_msg_save_disk db 'Save failed: disk or metadata error.',0
ed_shortcut_text db 'Ctrl-S Save   Esc Exit   F1 Help   Arrows Move   Enter Newline',0
ed_status_edit db ' EDIT ',0
ed_status_gap db '  ',0
ed_status_bytes db 'B  Ln ',0
ed_status_lncol db ',Col ',0
ed_status_ready db 'READY',0
ed_status_modified db 'MODIFIED',0
ed_status_saved db 'SAVED',0
ed_snapshot_prefix db 13,10,'[EDIT]',13,10,0
ed_help_title db 'AARAMBH EDITOR - KEYBOARD HELP',13,10,13,10,0
ed_help_body db 'Arrows      Move cursor (Up/Down keep the column)',13,10
             db 'Home / End  Start or end of the current line',13,10
             db 'PgUp / PgDn Move 20 lines',13,10
             db 'Enter       Insert a new line',13,10
             db 'Backspace   Delete the character before the cursor',13,10
             db 'Delete      Delete the character at the cursor',13,10
             db 'Tab         Insert a space',13,10
             db 'Ctrl-S      Save the file to AarFS',13,10
             db 'Esc Ctrl-Q  Exit; a second Esc discards unsaved changes',13,10
             db 'F1          Show this help',13,10,13,10
             db 'Files are limited to 16384 bytes. Long lines scroll horizontally.',13,10
             db 'This editor writes through the normal AarFS commit path.',13,10,13,10
             db 'Press any key to return to the editor.',13,10,0

ed_name dd 0
ed_name_len dd 0
ed_entry_id dd 0
ed_size dd 0
ed_cursor dd 0
ed_top_line dd 0
ed_total_lines dd 0
ed_cursor_start dd 0
ed_cursor_col dd 0
ed_cursor_line dd 0
ed_hscroll dd 0
ed_render_off dd 0
ed_render_line dd 0
ed_row dd 0
ed_col dd 0
ed_char db 0
ed_put_attr db 0x07
ed_dirty db 0
ed_new db 0
ed_saved db 0
ed_discarded db 0
ed_confirm db 0
ed_msg dd 0
ed_msg_attr db 0x0E
ed_status_len dd 0
ed_status times 96 db 0
