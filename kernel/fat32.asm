fat32_dispatch:
    mov si, input_buffer
    mov di, cmd_peek
    call fat_match
    jc fat_peek_cmd
    mov si, input_buffer
    mov di, cmd_read
    call fat_match
    jc fat_read_cmd
    mov si, input_buffer
    mov di, cmd_forge
    call fat_match
    jc fat_forge_cmd
    mov si, input_buffer
    mov di, cmd_erase
    call fat_match
    jc fat_erase_cmd
    mov si, input_buffer
    mov di, cmd_shift
    call fat_match
    jc fat_shift_cmd
    mov si, input_buffer
    mov di, cmd_scribe
    call fat_match
    jc fat_scribe_cmd
    mov si, input_buffer
    mov di, cmd_grow
    call fat_match
    jc fat_grow_cmd
    mov si, unknown_msg
    call print_string
    ret

fat_match:
.loop:
    mov al, [di]
    test al, al
    jz .boundary
    cmp al, [si]
    jne .no
    inc si
    inc di
    jmp .loop
.boundary:
    cmp byte [si], 0
    je .yes
    cmp byte [si], ' '
    jne .no
    inc si
.yes:
    stc
    ret
.no:
    clc
    ret

fat_mount:
    mov dword [fat_lba], 0
    mov word [fat_buf], fat_sector
    call fat_read_sector
    jc .bad
    cmp word [fat_sector+510], 0xAA55
    jne .bad
    mov al, [fat_sector+450]
    cmp al, 0x0B
    je .part_type_ok
    cmp al, 0x0C
    jne .bad
.part_type_ok:
    mov eax, [fat_sector+454]
    mov [fat_part], eax
    mov eax, [fat_sector+458]
    test eax, eax
    jz .bad
    mov [fat_part_size], eax
    mov eax, [fat_part]
    add eax, [fat_part_size]
    jc .bad
    mov [fat_part_end], eax
    mov eax, [fat_part]
    mov [fat_lba], eax
    call fat_read_sector
    jc .bad
    cmp word [fat_sector+510], 0xAA55
    jne .bad
    cmp word [fat_sector+11], 512
    jne .bad
    movzx eax, byte [fat_sector+13]
    test eax, eax
    jz .bad
    cmp eax, 128
    ja .bad
    mov edx, eax
    dec edx
    test eax, edx
    jnz .bad
    mov [fat_spc], al
    movzx eax, word [fat_sector+14]
    test eax, eax
    jz .bad
    add eax, [fat_part]
    jc .bad
    mov [fat_fat_start], eax
    cmp byte [fat_sector+16], 2
    jne .bad
    mov eax, [fat_sector+36]
    test eax, eax
    jz .bad
    mov [fat_fat_size], eax
    mov edx, eax
    shl eax, 1
    jc .bad
    add eax, [fat_fat_start]
    jc .bad
    mov [fat_data_start], eax
    cmp eax, [fat_part_end]
    jae .bad
    mov eax, [fat_sector+32]
    test eax, eax
    jz .bad
    cmp eax, [fat_part_size]
    ja .bad
    mov [fat_total_sectors], eax
    movzx edx, word [fat_sector+14]
    sub eax, edx
    jc .bad
    mov edx, [fat_fat_size]
    shl edx, 1
    sub eax, edx
    jc .bad
    movzx ecx, byte [fat_spc]
    xor edx, edx
    div ecx
    test eax, eax
    jz .bad
    mov [fat_cluster_count], eax
    mov eax, [fat_sector+44]
    cmp eax, 2
    jb .bad
    mov edx, [fat_cluster_count]
    add edx, 2
    cmp eax, edx
    jae .bad
    mov [fat_root], eax
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    jc .bad
    mov [fat_root_lba], eax
    clc
    ret
.bad:
    stc
    ret

fat_read_sector:
    mov byte [fat_retry_count], 3
.retry:
    mov byte [fat_dap], 16
    mov word [fat_dap+2], 1
    mov ax, [fat_buf]
    mov [fat_dap+4], ax
    mov word [fat_dap+6], 0
    mov eax, [fat_lba]
    mov [fat_dap+8], eax
    mov dword [fat_dap+12], 0
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, fat_dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    setnc byte [fat_io_status]
    jnc .restore
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
.restore:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [fat_io_status], 1
    je .ok
    dec byte [fat_retry_count]
    jnz .retry
    stc
    ret
.ok:
    clc
    ret

fat_read_cmd:
    call fat_mount
    jc fat_mount_error
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
    cmp eax, 4096
    ja fat_size_error
    mov [fat_remaining], ax
    test eax, eax
    jz .empty
.cluster:
    mov eax, [fat_cluster]
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov word [fat_buf], fat_file
    call fat_read_sector
    jc fat_disk_error
    mov cx, 512
    cmp word [fat_remaining], 512
    jae .print
    mov cx, [fat_remaining]
.print:
    mov [fat_chunk], cx
    mov si, fat_file
.byte:
    jcxz .next_cluster
    lodsb
    call fat_putc
    loop .byte
.next_cluster:
    mov ax, [fat_chunk]
    sub [fat_remaining], ax
    cmp word [fat_remaining], 0
    je .done
    mov eax, [fat_cluster]
    call fat_get_next
    jc fat_disk_error
    cmp eax, 0x0FFFFFF8
    jae fat_disk_error
    mov [fat_cluster], eax
    jmp .cluster
.done:
    call newline
    ret
.empty:
    call newline
    ret

fat_peek_cmd:
    call fat_mount
    jc fat_mount_error
    mov eax, [fat_root]
    mov [fat_dir_cluster], eax
    mov si, fat_header
    call print_string
.next_cluster:
    mov eax, [fat_dir_cluster]
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov [fat_root_current_lba], eax
    mov word [fat_buf], fat_sector
    call fat_read_sector
    jc fat_disk_error
    mov si, fat_sector
    mov word [fat_entry], 0
.scan:
    cmp byte [si], 0
    je .done
    cmp byte [si], 0xE5
    je .next
    cmp byte [si+11], 0x0F
    je .next
    test byte [si+11], 8
    jnz .next
    push si
    mov cx, 8
.base:
    lodsb
    cmp al, ' '
    je .base_skip
    call fat_putc
.base_skip:
    loop .base
    cmp byte [si], ' '
    je .line
    mov al, '.'
    call fat_putc
    mov cx, 3
.ext:
    lodsb
    cmp al, ' '
    je .ext_skip
    call fat_putc
.ext_skip:
    loop .ext
.line:
    call newline
    pop si
.next:
    add si, 32
    inc word [fat_entry]
    cmp word [fat_entry], 16
    jb .scan
    mov eax, [fat_dir_cluster]
    mov [fat_cluster], eax
    call fat_get_next
    jc fat_disk_error
    cmp eax, 0x0FFFFFF8
    jae .done
    mov [fat_dir_cluster], eax
    jmp .next_cluster
.done:
    ret

fat_putc:
    mov ah, 0x0E
    mov bh, 0
    mov bl, 7
    int 0x10
    ret

fat_parse_name:
    push di
    mov di, fat_name
    mov cx, 11
    mov al, ' '
    rep stosb
    xor bx, bx
    xor bp, bp
.loop:
    lodsb
    test al, al
    jz .done
    cmp al, ' '
    je .done
    cmp al, '.'
    jne .char
    cmp bp, 0
    jne .bad
    mov bp, 1
    mov bx, 8
    jmp .loop
.char:
    cmp bx, 11
    jae .bad
    and al, 0xDF
    mov [fat_name+bx], al
    inc bx
    jmp .loop
.done:
    cmp byte [fat_name], ' '
    je .bad
    pop di
    stc
    ret
.bad:
    pop di
    clc
    ret

fat_find:
    mov eax, [fat_root]
    mov [fat_dir_cluster], eax
.sector:
    mov eax, [fat_dir_cluster]
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov [fat_root_current_lba], eax
    mov word [fat_buf], fat_sector
    call fat_read_sector
    jc .miss
    mov di, fat_sector
    mov word [fat_entry], 0
.loop:
    mov al, [di]
    test al, al
    jz .empty
    cmp al, 0xE5
    je .next
    cmp byte [di+11], 0x0F
    je .next
    push cx
    push di
    mov si, fat_name
    mov cx, 11
    repe cmpsb
    pop di
    pop cx
    je .found
.next:
    add di, 32
    inc word [fat_entry]
    cmp word [fat_entry], 16
    jb .loop
    mov eax, [fat_dir_cluster]
    mov [fat_cluster], eax
    call fat_get_next
    jc .miss
    cmp eax, 0x0FFFFFF8
    jae .extend
    mov [fat_dir_cluster], eax
    jmp .sector
.empty:
    mov byte [fat_free], 1
    clc
    ret
.found:
    mov byte [fat_free], 0
    stc
    ret
.extend:
    ; Grow the root directory by linking a newly allocated cluster.
    mov eax, [fat_dir_cluster]
    mov [fat_saved_root_cluster], eax
    call fat_allocate
    jc .miss
    mov eax, [fat_cluster]
    mov [fat_new_root_cluster], eax
    mov eax, [fat_saved_root_cluster]
    mov [fat_cluster], eax
    mov eax, [fat_new_root_cluster]
    mov [fat_fat_value], eax
    call fat_update_entry
    jc .miss
    mov eax, [fat_new_root_cluster]
    mov [fat_dir_cluster], eax
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov [fat_root_current_lba], eax
    mov di, fat_sector
    mov cx, 512
    xor al, al
    rep stosb
    mov word [fat_buf], fat_sector
    call fat_disk_write
    jc .miss
    mov word [fat_entry], 0
    mov byte [fat_free], 1
    clc
    ret
.miss:
    mov byte [fat_free], 0
    clc
    ret

fat_mount_error:
    mov si, fat_mount_msg
    jmp fat_error
fat_disk_error:
    mov si, fat_disk_msg
    jmp fat_error
fat_name_error:
    mov si, fat_name_msg
    jmp fat_error
fat_missing_error:
    mov si, fat_missing_msg
    jmp fat_error
fat_size_error:
    mov si, fat_size_msg
fat_error:
    call print_string
    ret

fat_header db "FAT32 root directory:",13,10,0
fat_write_pending db "FAT32 storage grows as files and directories are added.",13,10,0
fat_created_msg db "File created.",13,10,0
fat_erased_msg db "File erased.",13,10,0
fat_renamed_msg db "File renamed.",13,10,0
fat_saved_msg db "File saved.",13,10,0
fat_exists_msg db "File already exists.",13,10,0
fat_full_msg db "Root directory or test FAT area is full.",13,10,0
fat_append db 0
fat_retry_count db 0
fat_io_status db 0
fat_is_new db 0
fat_free db 0
fat_text_ptr dw 0
fat_saved_entry dw 0
fat_saved_root_lba dd 0
fat_new_size dw 0
fat_fat_count db 2
fat_fat_value dd 0
fat_scan_sector dd 0
fat_sector_index dd 0
fat_byte_offset dw 0
fat_mount_msg db "FAT32 mount failed.",13,10,0
fat_disk_msg db "FAT32 disk read failed.",13,10,0
fat_name_msg db "Use an 8.3 filename, such as NOTE.TXT.",13,10,0
fat_missing_msg db "File not found.",13,10,0
fat_size_msg db "This FAT32 operation exceeds its supported size limit.",13,10,0
fat_part dd 0
fat_part_size dd 0
fat_part_end dd 0
fat_total_sectors dd 0
fat_cluster_count dd 0
fat_chain_steps dd 0
fat_fat_start dd 0
fat_fat_size dd 0
fat_data_start dd 0
fat_root dd 2
fat_root_lba dd 0
fat_root_current_lba dd 0
fat_dir_cluster dd 0
fat_saved_root_cluster dd 0
fat_new_root_cluster dd 0
fat_lba dd 0
fat_buf dw 0
fat_entry dw 0
fat_cluster dd 0
fat_size dw 0
fat_remaining dw 0
fat_chunk dw 0
fat_next_cluster dd 0
fat_spc db 1
fat_name times 11 db ' '
fat_dap db 16,0
         dw 1,0,0
         dq 0
fat_sector times 512 db 0
fat_file times 512 db 0


fat_disk_write:
    mov byte [fat_retry_count], 3
.retry:
    mov byte [fat_dap], 16
    mov word [fat_dap+2], 1
    mov ax, [fat_buf]
    mov [fat_dap+4], ax
    mov word [fat_dap+6], 0
    mov eax, [fat_lba]
    mov [fat_dap+8], eax
    mov dword [fat_dap+12], 0
    push ax
    push bx
    push cx
    push dx
    push si
    mov si, fat_dap
    mov dl, [boot_drive]
    mov ah, 0x43
    xor al, al
    int 0x13
    setnc byte [fat_io_status]
    jnc .restore
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
.restore:
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    cmp byte [fat_io_status], 1
    je .ok
    dec byte [fat_retry_count]
    jnz .retry
    stc
    ret
.ok:
    clc
    ret

fat_forge_cmd:
    call fat_mount
    jc fat_mount_error
    call fat_parse_name
    jnc fat_name_error
    call fat_find
    jc fat_exists_error
    cmp byte [fat_free], 1
    jne fat_full_error
    mov di, fat_sector
    mov ax, [fat_entry]
    shl ax, 5
    add di, ax
    push di
    mov cx, 32
    xor al, al
    rep stosb
    pop di
    mov si, fat_name
    mov cx, 11
    rep movsb
    mov byte [di], 0x20
    call fat_write_root
    jc fat_disk_error
    mov si, fat_created_msg
    call print_string
    ret

fat_erase_cmd:
    call fat_mount
    jc fat_mount_error
    call fat_parse_name
    jnc fat_name_error
    call fat_find
    jnc fat_missing_error
    call fat_entry_ptr
    mov ax, [di+26]
    mov [fat_cluster], ax
    mov ax, [di+20]
    mov [fat_cluster+2], ax
    cmp dword [fat_cluster], 0
    je .mark
    call fat_free_chain
    jc fat_disk_error
.mark:
    call fat_read_root
    jc fat_disk_error
    call fat_entry_ptr
    mov byte [di], 0xE5
    call fat_write_root
    jc fat_disk_error
    mov si, fat_erased_msg
    call print_string
    ret

fat_shift_cmd:
    call fat_mount
    jc fat_mount_error
    call fat_parse_name
    jnc fat_name_error
    call fat_find
    jnc fat_missing_error
    mov ax, [fat_entry]
    mov [fat_saved_entry], ax
    mov eax, [fat_root_current_lba]
    mov [fat_saved_root_lba], eax
    call fat_skip_spaces
    call fat_parse_name
    jnc fat_name_error
    ; Refuse to overwrite a different existing entry.
    call fat_find
    jc fat_exists_error
    mov ax, [fat_saved_entry]
    mov [fat_entry], ax
    mov eax, [fat_saved_root_lba]
    mov [fat_root_current_lba], eax
    call fat_read_root
    jc fat_disk_error
    call fat_entry_ptr
    push di
    mov si, fat_name
    mov cx, 11
    rep movsb
    pop di
    call fat_write_root
    jc fat_disk_error
    mov si, fat_renamed_msg
    call print_string
    ret

fat_scribe_cmd:
    mov byte [fat_append], 0
    jmp fat_write_cmd
fat_grow_cmd:
    mov byte [fat_append], 1
fat_write_cmd:
    call fat_mount
    jc fat_mount_error
    call fat_parse_name
    jnc fat_name_error
    call fat_skip_spaces
    mov [fat_text_ptr], si
    call fat_find
    jc .existing
    cmp byte [fat_append], 1
    je fat_missing_error
    cmp byte [fat_free], 1
    jne fat_full_error
    mov byte [fat_is_new], 1
    mov ax, [fat_entry]
    mov [fat_saved_entry], ax
    mov word [fat_size], 0
    mov dword [fat_cluster], 0
    jmp .prepare
.existing:
    mov byte [fat_is_new], 0
    mov ax, [fat_entry]
    mov [fat_saved_entry], ax
    call fat_entry_ptr
    mov ax, [di+26]
    mov [fat_cluster], ax
    mov ax, [di+20]
    mov [fat_cluster+2], ax
    mov eax, [di+28]
    cmp eax, 512
    ja fat_size_error
    mov [fat_size], ax
    cmp byte [fat_append], 1
    jne .rewrite
    cmp word [fat_size], 0
    je .prepare
    mov eax, [fat_cluster]
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov word [fat_buf], fat_file
    call fat_read_sector
    jc fat_disk_error
    jmp .copy
.rewrite:
    mov word [fat_size], 0
.prepare:
    mov di, fat_file
    mov cx, 512
    xor al, al
    rep stosb
.copy:
    mov si, [fat_text_ptr]
    mov di, fat_file
    add di, [fat_size]
    mov bx, [fat_size]
.copy_loop:
    lodsb
    test al, al
    jz .copied
    cmp bx, 512
    jae fat_size_error
    stosb
    inc bx
    jmp .copy_loop
.copied:
    mov [fat_new_size], bx
    cmp bx, 0
    je .save
    cmp dword [fat_cluster], 0
    jne .write_data
    call fat_allocate
    jc fat_full_error
.write_data:
    mov eax, [fat_cluster]
    sub eax, 2
    movzx ecx, byte [fat_spc]
    mul ecx
    add eax, [fat_data_start]
    mov [fat_lba], eax
    mov word [fat_buf], fat_file
    call fat_disk_write
    jc fat_disk_error
.save:
    mov ax, [fat_saved_entry]
    mov [fat_entry], ax
    call fat_read_root
    jc fat_disk_error
    call fat_entry_ptr
    cmp byte [fat_is_new], 0
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
    movzx eax, word [fat_new_size]
    mov [di+28], eax
    call fat_write_root
    jc fat_disk_error
    mov si, fat_saved_msg
    call print_string
    ret

; Find one free FAT32 cluster anywhere in the volume, then mark it EOC.
fat_allocate:
    mov dword [fat_scan_sector], 0
.scan_sector:
    mov eax, [fat_scan_sector]
    cmp eax, [fat_fat_size]
    jae .fail
    add eax, [fat_fat_start]
    mov [fat_lba], eax
    mov word [fat_buf], fat_sector
    call fat_read_sector
    jc .fail
    xor bx, bx
    cmp dword [fat_scan_sector], 0
    jne .entry
    mov bx, 3
.entry:
    mov di, bx
    shl di, 2
    cmp dword [fat_sector+di], 0
    je .found
    inc bx
    cmp bx, 128
    jb .entry
    inc dword [fat_scan_sector]
    jmp .scan_sector
.found:
    mov eax, [fat_scan_sector]
    shl eax, 7
    movzx edx, bx
    add eax, edx
    mov [fat_cluster], eax
    mov dword [fat_fat_value], 0x0FFFFFFF
    call fat_update_entry
    ret
.fail:
    stc
    ret

; Return the next FAT cluster for fat_cluster in EAX.
fat_get_next:
    mov eax, [fat_cluster]
    cmp eax, 2
    jb .fail
    mov edx, [fat_cluster_count]
    add edx, 2
    cmp eax, edx
    jae .fail
    mov edx, eax
    and eax, 0x7F
    shl ax, 2
    mov [fat_byte_offset], ax
    shr edx, 7
    mov [fat_sector_index], edx
    add edx, [fat_fat_start]
    mov [fat_lba], edx
    mov word [fat_buf], fat_sector
    call fat_read_sector
    jc .fail
    mov bx, [fat_byte_offset]
    mov eax, [fat_sector+bx]
    and eax, 0x0FFFFFFF
    cmp eax, 0x0FFFFFF8
    jae .eoc
    cmp eax, 2
    jb .fail
    mov edx, [fat_cluster_count]
    add edx, 2
    cmp eax, edx
    jae .fail
.eoc:
    clc
    ret
.fail:
    stc
    ret

; Release every cluster in the current file's FAT chain.
fat_free_chain:
    mov dword [fat_chain_steps], 0
.loop:
    inc dword [fat_chain_steps]
    mov eax, [fat_chain_steps]
    cmp eax, [fat_cluster_count]
    ja .fail
    call fat_get_next
    jc .fail
    mov [fat_next_cluster], eax
    mov dword [fat_fat_value], 0
    call fat_update_entry
    jc .fail
    mov eax, [fat_next_cluster]
    cmp eax, 0x0FFFFFF8
    jae .done
    mov [fat_cluster], eax
    jmp .loop
.done:
    clc
    ret
.fail:
    stc
    ret

; Write fat_fat_value to the selected cluster in both FAT copies.
fat_update_entry:
    mov eax, [fat_cluster]
    mov edx, eax
    and eax, 0x7F
    shl ax, 2
    mov [fat_byte_offset], ax
    shr edx, 7
    mov [fat_sector_index], edx
    add edx, [fat_fat_start]
    mov [fat_lba], edx
    mov word [fat_buf], fat_sector
    call fat_read_sector
    jc .fail
    mov bx, [fat_byte_offset]
    mov eax, [fat_fat_value]
    mov [fat_sector+bx], eax
    mov eax, [fat_sector_index]
    add eax, [fat_fat_start]
    mov [fat_lba], eax
    call fat_disk_write
    jc .fail
    cmp byte [fat_fat_count], 2
    jne .ok
    mov eax, [fat_sector_index]
    add eax, [fat_fat_start]
    add eax, [fat_fat_size]
    mov [fat_lba], eax
    call fat_disk_write
    jc .fail
.ok:
    clc
    ret
.fail:
    stc
    ret

fat_write_root:
    mov eax, [fat_root_current_lba]
    mov [fat_lba], eax
    mov word [fat_buf], fat_sector
    call fat_disk_write
    ret

fat_entry_ptr:
    mov di, fat_sector
    mov ax, [fat_entry]
    shl ax, 5
    add di, ax
    ret

fat_read_root:
    mov eax, [fat_root_current_lba]
    mov [fat_lba], eax
    mov word [fat_buf], fat_sector
    call fat_read_sector
    ret

fat_skip_spaces:
    cmp byte [si], ' '
    jne .done
    inc si
    jmp fat_skip_spaces
.done:
    ret

fat_exists_error:
    mov si, fat_exists_msg
    jmp fat_error
fat_full_error:
    mov si, fat_full_msg
    jmp fat_error
