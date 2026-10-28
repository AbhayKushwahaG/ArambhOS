; AarambhFS v1: persistent directory tree for the PM32 test image.
; ATA primary-master PIO, dual metadata slots, CRC-protected commit superblocks.
; Limits: 2048 entries, 16 KiB per file, fixed double-buffered data extents.

%define AFS_SECTOR_SIZE 512
%define AFS_ENTRY_COUNT 2048
%define AFS_ENTRY_SIZE 64
%define AFS_TABLE_BYTES 131072
%define AFS_TABLE_SECTORS 256
%define AFS_SUPER_A 64
%define AFS_SUPER_B 65
%define AFS_TABLE_A 66
%define AFS_TABLE_B 322
%define AFS_DATA_START 578
%define AFS_FILE_SECTORS 32
%define AFS_FILE_BYTES 16384
%define AFS_DISK_SECTORS 262144
%define AFS_FILE_TYPE 1
%define AFS_DIR_TYPE 2
fs_super_a equ 0x00002000
fs_super_b equ 0x00002200
fs_super_build equ 0x00002400

fs_init:
    mov eax, AFS_TABLE_BYTES
    call kmalloc
    test eax, eax
    jz .no_memory
    mov [fs_entries_ptr], eax
    mov eax, AFS_FILE_BYTES
    call kmalloc
    test eax, eax
    jz .no_memory
    mov [fs_file_buffer], eax
    ; QEMU can briefly starve device emulation while a host chardev connects.
    ; Resetting once and waiting for DRDY avoids failing the first metadata write.
    call ata_bus_reset
    call ata_wait_ready
    call fs_mount
    jc .not_ready
    mov esi, fs_ready_text
    call console_write
    ret
.not_ready:
    mov esi, fs_mount_error_text
    call console_write
    ret
.no_memory:
    mov esi, fs_memory_error_text
    call console_write
    ret

; Sector reads/writes use primary IDE master, LBA28, one 512-byte sector.
; EAX=LBA, EDI=buffer. Carry reports timeout or device error.
ata_read_sector:
    mov [ata_lba], eax
    mov [ata_buffer], edi
    pushad
    mov byte [ata_retry_count], 8
.attempt:
    mov ebx, [ata_lba]
    mov ecx, ebx
    shr ecx, 24
    and cl, 0x0F
    or cl, 0xE0
    mov dx, 0x1F6
    mov al, cl
    out dx, al
    call ata_delay_400ns
    mov dx, 0x1F2
    mov al, 1
    out dx, al
    mov eax, ebx
    mov dx, 0x1F3
    out dx, al
    shr eax, 8
    mov dx, 0x1F4
    out dx, al
    shr eax, 8
    mov dx, 0x1F5
    out dx, al
    mov dx, 0x1F7
    mov al, 0x20
    out dx, al
    call ata_delay_400ns
    call ata_wait_drq
    jc .fail
    mov dx, 0x1F0
    mov edi, [ata_buffer]
    mov ecx, 256
    rep insw
    mov byte [ata_error], 0
    jmp .done
.fail:
    dec byte [ata_retry_count]
    jz .failed
    call ata_bus_reset
    jmp .attempt
.failed:
    mov byte [ata_error], 1
.done:
    popad
    cmp byte [ata_error], 0
    jne .bad
    clc
    ret
.bad:
    stc
    ret

ata_write_sector:
    mov [ata_lba], eax
    mov [ata_buffer], edi
    pushad
    mov byte [ata_retry_count], 8
.attempt:
    mov ebx, [ata_lba]
    mov ecx, ebx
    shr ecx, 24
    and cl, 0x0F
    or cl, 0xE0
    mov dx, 0x1F6
    mov al, cl
    out dx, al
    call ata_delay_400ns
    mov dx, 0x1F2
    mov al, 1
    out dx, al
    mov eax, ebx
    mov dx, 0x1F3
    out dx, al
    shr eax, 8
    mov dx, 0x1F4
    out dx, al
    shr eax, 8
    mov dx, 0x1F5
    out dx, al
    mov dx, 0x1F7
    mov al, 0x30
    out dx, al
    call ata_delay_400ns
    call ata_wait_drq
    jc .retry
    mov dx, 0x1F0
    mov esi, [ata_buffer]
    mov ecx, 256
    rep outsw
    call ata_wait_ready
    jc .retry
    mov byte [ata_error], 0
    jmp .done
.retry:
    dec byte [ata_retry_count]
    jz .failed
    call ata_bus_reset
    jmp .attempt
.failed:
    mov byte [ata_error], 1
.done:
    popad
    cmp byte [ata_error], 0
    jne .bad
    clc
    ret
.bad:
    stc
    ret
ata_bus_reset:
    push eax
    push ecx
    push edx
    mov dx, 0x3F6
    mov al, 0x04
    out dx, al
    mov ecx, 0x800000
.hold:
    loop .hold
    xor al, al
    out dx, al
    mov ecx, 0x800000
.release:
    loop .release
    pop edx
    pop ecx
    pop eax
    ret
ata_delay_400ns:
    push eax
    push edx
    mov dx, 0x3F6
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    pop edx
    pop eax
    ret

ata_wait_drq:
    mov ecx, 0x4000000
.poll:
    mov dx, 0x1F7
    in al, dx
    cmp al, 0xFF
    je .bad
    test al, 0x80           ; Ignore other status bits while device is busy.
    jnz .again
    test al, 0x01           ; ERR is meaningful after BSY clears.
    jnz .bad
    test al, 0x08
    jnz .ok
.again:
    loop .poll
.bad:
    stc
    ret
.ok:
    clc
    ret

ata_wait_ready:
    mov ecx, 0x4000000
.poll:
    mov dx, 0x1F7
    in al, dx
    cmp al, 0xFF
    je .bad
    test al, 0x80           ; Wait for BSY before checking ERR.
    jnz .again
    test al, 0x01
    jnz .bad
    clc
    ret
.again:
    loop .poll
.bad:
    stc
    ret

; EAX=start LBA, EDI=buffer, ECX=sector count.
ata_read_many:
    mov [ata_many_lba], eax
    mov [ata_many_ptr], edi
    mov [ata_many_count], ecx
.loop:
    cmp dword [ata_many_count], 0
    je .ok
    mov eax, [ata_many_lba]
    mov edi, [ata_many_ptr]
    call ata_read_sector
    jc .bad
    inc dword [ata_many_lba]
    add dword [ata_many_ptr], 512
    dec dword [ata_many_count]
    jmp .loop
.ok:
    clc
    ret
.bad:
    stc
    ret

; EAX=start LBA, EDI=buffer, ECX=sector count.
ata_write_many:
    mov [ata_many_lba], eax
    mov [ata_many_ptr], edi
    mov [ata_many_count], ecx
.loop:
    cmp dword [ata_many_count], 0
    je .flush
    mov eax, [ata_many_lba]
    mov edi, [ata_many_ptr]
    call ata_write_sector
    jc .bad
    inc dword [ata_many_lba]
    add dword [ata_many_ptr], 512
    dec dword [ata_many_count]
    jmp .loop
.flush:
    call ata_flush_cache
    ret
.bad:
    stc
    ret

ata_flush_cache:
    call ata_wait_ready
    jc .bad
    mov dx, 0x1F7
    mov al, 0xE7
    out dx, al
    call ata_delay_400ns
    call ata_wait_ready
    ret
.bad:
    stc
    ret

; Standard reflected CRC-32. ESI=bytes, ECX=count, returns EAX.
fs_crc32:
    push ebx
    push edx
    mov eax, 0xFFFFFFFF
.byte:
    test ecx, ecx
    jz .finish
    movzx ebx, byte [esi]
    xor eax, ebx
    mov edx, 8
.bit:
    shr eax, 1
    jnc .next_bit
    xor eax, 0xEDB88320
.next_bit:
    dec edx
    jnz .bit
    inc esi
    dec ecx
    jmp .byte
.finish:
    not eax
    pop edx
    pop ebx
    ret

fs_validate_super:
    ; ESI=superblock; EAX=1 when geometry and checksum are valid.
    cmp dword [esi], 0x46524141 ; 'AARF'
    jne .bad
    cmp dword [esi+4], 0x00323353 ; 'S32\0'
    jne .bad
    cmp dword [esi+8], 1
    jne .bad
    cmp dword [esi+20], AFS_DISK_SECTORS
    jne .bad
    cmp dword [esi+36], AFS_DATA_START
    jne .bad
    cmp dword [esi+40], AFS_ENTRY_COUNT
    jne .bad
    cmp dword [esi+44], AFS_FILE_SECTORS
    jne .bad
    cmp dword [esi+16], 1
    ja .bad
    mov eax, [esi+16]
    test eax, eax
    jnz .table_b
    cmp dword [esi+28], AFS_TABLE_A
    jne .bad
    jmp .crc
.table_b:
    cmp dword [esi+32], AFS_TABLE_B
    jne .bad
.crc:
    push esi
    mov ecx, 508
    call fs_crc32
    pop esi
    cmp eax, [esi+508]
    jne .bad
    mov eax, 1
    ret
.bad:
    xor eax, eax
    ret

fs_mount:
    mov byte [fs_valid_a], 0
    mov byte [fs_valid_b], 0
    mov eax, AFS_SUPER_A
    mov edi, fs_super_a
    call ata_read_sector
    jc .read_b
    mov esi, fs_super_a
    call fs_validate_super
    mov [fs_valid_a], al
.read_b:
    mov eax, AFS_SUPER_B
    mov edi, fs_super_b
    call ata_read_sector
    jc .choose
    mov esi, fs_super_b
    call fs_validate_super
    mov [fs_valid_b], al
.choose:
    cmp byte [fs_valid_a], 0
    jne .maybe_b
    cmp byte [fs_valid_b], 0
    je .bad
    mov esi, fs_super_b
    mov byte [fs_super_copy], 1
    call fs_load_super
    jmp .result
.maybe_b:
    cmp byte [fs_valid_b], 0
    je .use_a
    mov eax, [fs_super_a+12]
    cmp eax, [fs_super_b+12]
    jae .use_a
    mov esi, fs_super_b
    mov byte [fs_super_copy], 1
    call fs_load_super
    jnc .result
.use_a:
    mov esi, fs_super_a
    mov byte [fs_super_copy], 0
    call fs_load_super
    jnc .result
    cmp byte [fs_valid_b], 0
    je .bad
    mov esi, fs_super_b
    mov byte [fs_super_copy], 1
    call fs_load_super
.result:
    ret
.bad:
    mov byte [fs_mounted], 0
    stc
    ret

fs_load_super:
    mov [fs_super_selected], esi
    mov eax, [esi+16]
    mov [fs_active_slot], eax
    mov eax, [esi+12]
    mov [fs_generation], eax
    mov eax, [esi+16]
    test eax, eax
    jnz .table_b
    mov eax, AFS_TABLE_A
    jmp .load
.table_b:
    mov eax, AFS_TABLE_B
.load:
    mov edi, [fs_entries_ptr]
    mov ecx, AFS_TABLE_SECTORS
    call ata_read_many
    jc .bad
    mov esi, [fs_entries_ptr]
    mov ecx, AFS_TABLE_BYTES
    call fs_crc32
    mov esi, [fs_super_selected]
    cmp eax, [esi+24]
    jne .bad
    mov byte [fs_mounted], 1
    clc
    ret
.bad:
    stc
    ret

; Write alternate metadata table first, then publish it in the other
; superblock. A torn metadata write leaves the previous superblock usable.
fs_commit:
    cmp byte [fs_mounted], 1
    jne .bad
    mov eax, [fs_active_slot]
    xor eax, 1
    mov [fs_new_slot], eax
    test eax, eax
    jnz .table_b
    mov eax, AFS_TABLE_A
    jmp .write_table
.table_b:
    mov eax, AFS_TABLE_B
.write_table:
    mov edi, [fs_entries_ptr]
    mov ecx, AFS_TABLE_SECTORS
    call ata_write_many
    jc .rollback_table
    mov esi, [fs_entries_ptr]
    mov ecx, AFS_TABLE_BYTES
    call fs_crc32
    mov [fs_table_crc], eax
    mov edi, fs_super_build
    xor eax, eax
    mov ecx, 128
    rep stosd
    mov dword [fs_super_build], 0x46524141
    mov dword [fs_super_build+4], 0x00323353
    mov dword [fs_super_build+8], 1
    mov eax, [fs_generation]
    inc eax
    mov [fs_new_generation], eax
    mov [fs_super_build+12], eax
    mov eax, [fs_new_slot]
    mov [fs_super_build+16], eax
    mov dword [fs_super_build+20], AFS_DISK_SECTORS
    mov eax, [fs_table_crc]
    mov [fs_super_build+24], eax
    mov dword [fs_super_build+28], AFS_TABLE_A
    mov dword [fs_super_build+32], AFS_TABLE_B
    mov dword [fs_super_build+36], AFS_DATA_START
    mov dword [fs_super_build+40], AFS_ENTRY_COUNT
    mov dword [fs_super_build+44], AFS_FILE_SECTORS
    mov esi, fs_super_build
    mov ecx, 508
    call fs_crc32
    mov [fs_super_build+508], eax
    mov eax, [fs_super_copy]
    xor eax, 1
    mov [fs_new_super_copy], eax
    add eax, AFS_SUPER_A
    mov edi, fs_super_build
    call ata_write_sector
    jc .rollback_super
    mov eax, [fs_new_slot]
    mov [fs_active_slot], eax
    mov eax, [fs_new_generation]
    mov [fs_generation], eax
    mov eax, [fs_new_super_copy]
    mov [fs_super_copy], eax
    clc
    ret
.rollback_table:
    mov eax, [ata_many_lba]
    mov [fs_failed_lba], eax
    call fs_mount
    mov esi, fs_debug_table
    call console_write
    mov esi, fs_debug_at
    call console_write
    mov eax, [fs_failed_lba]
    call print_u32
    call console_newline
    stc
    ret
.rollback_super:
    call fs_mount
    mov esi, fs_debug_super
    call console_write
    stc
    ret
.bad:
    stc
    ret

fs_entry_ptr:
    ; EAX=one-based entry id; returns EDI pointer or zero.
    cmp eax, 1
    jb .bad
    cmp eax, AFS_ENTRY_COUNT
    ja .bad
    dec eax
    shl eax, 6
    add eax, [fs_entries_ptr]
    mov edi, eax
    ret
.bad:
    xor edi, edi
    ret

fs_find_child:
    ; EAX=parent id, ESI=name. Returns EAX=id, EDI=entry or zero.
    mov [fs_find_parent], eax
    mov [fs_find_name], esi
    xor ebx, ebx
.loop:
    cmp ebx, AFS_ENTRY_COUNT
    jae .miss
    mov edi, ebx
    shl edi, 6
    add edi, [fs_entries_ptr]
    cmp byte [edi], 1
    jne .next
    mov eax, [edi+4]
    cmp eax, [fs_find_parent]
    jne .next
    mov esi, [fs_find_name]
    lea edx, [edi+8]
    push edi
    mov edi, edx
    call fs_string_equal_ci
    pop edi
    test eax, eax
    jnz .found
.next:
    inc ebx
    jmp .loop
.found:
    mov eax, ebx
    inc eax
    ret
.miss:
    xor eax, eax
    xor edi, edi
    ret

fs_string_equal_ci:
    ; ESI and EDI NUL-terminated strings; returns EAX=1 if same ignoring case.
    push ebx
.loop:
    mov al, [esi]
    mov bl, [edi]
    cmp al, 'A'
    jb .a_ready
    cmp al, 'Z'
    ja .a_ready
    add al, 32
.a_ready:
    cmp bl, 'A'
    jb .b_ready
    cmp bl, 'Z'
    ja .b_ready
    add bl, 32
.b_ready:
    cmp al, bl
    jne .no
    test al, al
    jz .yes
    inc esi
    inc edi
    jmp .loop
.yes:
    mov eax, 1
    pop ebx
    ret
.no:
    xor eax, eax
    pop ebx
    ret

fs_validate_name:
    ; ESI=name; returns EAX=length, or zero for invalid name.
    xor ecx, ecx
.loop:
    mov al, [esi+ecx]
    test al, al
    jz .end
    cmp al, '/'
    je .bad
    cmp al, 0x5C
    je .bad
    cmp ecx, 39
    jae .bad
    inc ecx
    jmp .loop
.end:
    test ecx, ecx
    jz .bad
    cmp ecx, 1
    jne .valid
    cmp byte [esi], '.'
    je .bad
.valid:
    cmp ecx, 2
    jne .ok
    cmp word [esi], '..'
    je .bad
.ok:
    mov eax, ecx
    ret
.bad:
    xor eax, eax
    ret

fs_check_perm:
    push eax
    push edx
    cmp word [session_uid], 0
    je .allowed
    movzx eax, word [edi+56]
    test eax, eax
    jnz .have_mode
    cmp byte [edi+1], AFS_DIR_TYPE
    je .default_dir
    mov eax, 0x01A4
    jmp .have_mode
.default_dir:
    mov eax, 0x01ED
.have_mode:
    mov dx, [session_uid]
    cmp dx, [edi+52]
    jne .check_group
    shr eax, 6
    and eax, 7
    jmp .test_bits
.check_group:
    mov dx, [session_gid]
    cmp dx, [edi+54]
    je .group_match
    movzx edx, byte [session_supp_count]
    test edx, edx
    jz .check_other
    push ebx
    push ecx
    mov ebx, session_supp_gids
.supp_loop:
    mov cx, [ebx]
    cmp cx, [edi+54]
    je .supp_match
    add ebx, 2
    dec edx
    jnz .supp_loop
    pop ecx
    pop ebx
    jmp .check_other
.supp_match:
    pop ecx
    pop ebx
.group_match:
    shr eax, 3
    and eax, 7
    jmp .test_bits
.check_other:
    and eax, 7
.test_bits:
    movzx edx, cl
    and edx, eax
    cmp dl, cl
    jne .denied
.allowed:
    pop edx
    pop eax
    clc
    ret
.denied:
    pop edx
    pop eax
    stc
    ret

fs_dir_check_write:
    push edi
    push ecx
    cmp word [session_uid], 0
    je .dir_ok
    test eax, eax
    jz .dir_root
    call fs_entry_ptr
    test edi, edi
    jz .dir_err
    mov cl, 2
    call fs_check_perm
    jc .dir_err
.dir_ok:
    pop ecx
    pop edi
    clc
    ret
.dir_root:
    cmp word [session_uid], 0
    je .dir_ok
.dir_err:
    pop ecx
    pop edi
    stc
    ret

fs_create_entry:
    ; ESI=name, EBX=type; uses current directory. Returns EAX=id or 0.
    mov [fs_new_name], esi
    mov [fs_new_type], bl
    call fs_validate_name
    test eax, eax
    jz .bad_name
    mov esi, [fs_new_name]
    mov eax, [fs_cwd]
    call fs_find_child
    test eax, eax
    jnz .exists
    xor ebx, ebx
.find_free:
    cmp ebx, AFS_ENTRY_COUNT
    jae .full
    mov edi, ebx
    shl edi, 6
    add edi, [fs_entries_ptr]
    cmp byte [edi], 0
    je .found
    inc ebx
    jmp .find_free
.found:
    push ebx
    mov ecx, 16
    xor eax, eax
    rep stosd
    pop ebx
    mov edi, ebx
    shl edi, 6
    add edi, [fs_entries_ptr]
    mov byte [edi], 1
    mov al, [fs_new_type]
    mov [edi+1], al
    mov eax, [fs_cwd]
    mov [edi+4], eax
    mov ax, [session_uid]
    mov [edi+52], ax
    mov ax, [session_gid]
    mov [edi+54], ax
    cmp byte [fs_new_type], AFS_DIR_TYPE
    je .new_dir_mode
    mov word [edi+56], 0x01A4
    jmp .new_mode_done
.new_dir_mode:
    mov word [edi+56], 0x01ED
.new_mode_done:
    lea edi, [edi+8]
    mov esi, [fs_new_name]
.copy:
    lodsb
    stosb
    test al, al
    jnz .copy
    mov eax, ebx
    inc eax
    ret
.exists:
    mov eax, 0
    ret
.full:
    mov eax, 0
    ret
.bad_name:
    mov eax, 0
    ret

fs_get_arg:
    ; First whitespace-delimited argument -> ESI, remainder -> EDI. In-place split.
    mov esi, [command_args]
.skip:
    cmp byte [esi], ' '
    jne .start
    inc esi
    jmp .skip
.start:
    mov [fs_arg1], esi
    mov dword [fs_arg2], 0
    cmp byte [esi], 0
    je .none
.scan:
    cmp byte [esi], 0
    je .done
    cmp byte [esi], ' '
    je .split
    inc esi
    jmp .scan
.split:
    mov byte [esi], 0
    inc esi
    jmp .skip2
.skip2:
    cmp byte [esi], ' '
    jne .rest
    inc esi
    jmp .skip2
.rest:
    mov [fs_arg2], esi
.done:
    mov esi, [fs_arg1]
    mov edi, [fs_arg2]
    ret
.none:
    mov dword [fs_arg1], 0
    mov dword [fs_arg2], 0
    xor esi, esi
    xor edi, edi
    ret

fs_cmd_ls:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    mov ebx, 0
.loop:
    cmp ebx, AFS_ENTRY_COUNT
    jae .done
    mov edi, ebx
    shl edi, 6
    add edi, [fs_entries_ptr]
    cmp byte [edi], 1
    jne .next
    mov eax, [edi+4]
    cmp eax, [fs_cwd]
    jne .next
    lea esi, [edi+8]
    call console_write
    cmp byte [edi+1], AFS_DIR_TYPE
    jne .newline
    mov al, '/'
    call console_putc
.newline:
    call console_newline
.next:
    inc ebx
    jmp .loop
.done:
    ret

fs_print_path:
    mov dword [fs_path_count], 0
    mov eax, [fs_cwd]
.collect:
    test eax, eax
    jz .print
    mov ecx, [fs_path_count]
    cmp ecx, 32
    jae .bad
    mov [fs_path_stack+ecx*4], eax
    inc dword [fs_path_count]
    call fs_entry_ptr
    test edi, edi
    jz .bad
    mov eax, [edi+4]
    jmp .collect
.print:
    mov al, '/'
    call console_putc
    mov ecx, [fs_path_count]
.loop:
    test ecx, ecx
    jz .end
    dec ecx
    mov eax, [fs_path_stack+ecx*4]
    call fs_entry_ptr
    lea esi, [edi+8]
    call console_write
    cmp ecx, 0
    je .loop
    mov al, '/'
    call console_putc
    jmp .loop
.end:
    ret
.bad:
    mov esi, fs_path_error
    call console_write
    ret

fs_cmd_pwd:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_print_path
    call console_newline
    ret

fs_cmd_cd:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .use_home
    cmp byte [esi], 0
    je .use_home
    cmp byte [esi], '~'
    jne .start_walk
    cmp byte [esi+1], 0
    je .use_home
    jmp .start_walk
.use_home:
    mov esi, session_home
.start_walk:
    mov eax, [fs_cwd]
    mov [fs_walk_parent], eax
    cmp byte [esi], '/'
    jne .component
    mov dword [fs_walk_parent], 0
    inc esi
.component:
    cmp byte [esi], 0
    je .finish
    cmp byte [esi], '/'
    jne .copy
    inc esi
    jmp .component
.copy:
    mov edi, fs_component
    xor ecx, ecx
.copy_loop:
    mov al, [esi]
    test al, al
    jz .copied
    cmp al, '/'
    je .separator
    cmp ecx, 39
    jae .syntax
    stosb
    inc ecx
    inc esi
    jmp .copy_loop
.separator:
    inc esi
.copied:
    mov byte [edi], 0
    test ecx, ecx
    jz .component
    cmp byte [fs_component], '.'
    jne .lookup
    cmp byte [fs_component+1], 0
    je .component
    cmp word [fs_component], '..'
    jne .lookup
    mov eax, [fs_walk_parent]
    test eax, eax
    jz .component
    call fs_entry_ptr
    mov eax, [edi+4]
    mov [fs_walk_parent], eax
    jmp .component
.lookup:
    mov [fs_walk_ptr], esi
    mov eax, [fs_walk_parent]
    mov esi, fs_component
    call fs_find_child
    test eax, eax
    jz .missing
    cmp byte [edi+1], AFS_DIR_TYPE
    jne .not_dir
    mov cl, 1
    call fs_check_perm
    jc .cd_perm_denied
    mov [fs_walk_parent], eax
    mov esi, [fs_walk_ptr]
    jmp .component
.finish:
    mov eax, [fs_walk_parent]
    mov [fs_cwd], eax
    ret
.syntax:
    mov esi, fs_path_syntax
    call console_write
    ret
.missing:
    mov esi, fs_missing_dir
    call console_write
    ret
.not_dir:
    mov esi, fs_not_dir
    call console_write
    ret
.cd_perm_denied:
    mov esi, fs_cd_perm_err
    call console_write
    ret

fs_cmd_mkdir:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .syntax
    mov eax, [fs_cwd]
    call fs_dir_check_write
    jc .mkdir_perm_denied
    mov ebx, AFS_DIR_TYPE
    call fs_create_entry
    test eax, eax
    jz .create_error
    call fs_commit
    jc fs_disk_error
    mov esi, fs_mkdir_ok
    call console_write
    ret
.create_error:
    mov esi, fs_create_error
    call console_write
    ret
.syntax:
    mov esi, fs_mkdir_syntax
    call console_write
    ret
.mkdir_perm_denied:
    mov esi, fs_mkdir_perm_err
    call console_write
    ret

fs_cmd_forge:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .syntax
    mov eax, [fs_cwd]
    call fs_find_child
    test eax, eax
    jnz .exists
    mov esi, [fs_arg1]
    mov ebx, AFS_FILE_TYPE
    call fs_create_entry
    test eax, eax
    jz .create_error
    call fs_commit
    jc fs_disk_error
    mov esi, fs_touch_ok
    call console_write
    ret
.exists:
    mov esi, fs_exists_error
    call console_write
    ret
.create_error:
    mov esi, fs_create_error
    call console_write
    ret
.syntax:
    mov esi, fs_touch_syntax
    call console_write
    ret

fs_cmd_touch:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .syntax
    mov eax, [fs_cwd]
    call fs_find_child
    test eax, eax
    jz .create
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .type_error
    ret
.create:
    mov esi, [fs_arg1]
    mov ebx, AFS_FILE_TYPE
    call fs_create_entry
    test eax, eax
    jz .create_error
    call fs_commit
    jc fs_disk_error
    mov esi, fs_touch_ok
    call console_write
    ret
.type_error:
    mov esi, fs_type_error
    call console_write
    ret
.create_error:
    mov esi, fs_create_error
    call console_write
    ret
.syntax:
    mov esi, fs_touch_syntax
    call console_write
    ret

fs_cmd_read:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .syntax
    mov eax, [fs_cwd]
    call fs_find_child
    test eax, eax
    jz .missing
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .type_error
    mov cl, 4
    call fs_check_perm
    jc .read_perm_denied
    call fs_read_file
    jc fs_disk_error
    mov ecx, [fs_file_size]
    mov esi, [fs_file_buffer]
.print:
    test ecx, ecx
    jz .done
    lodsb
    call console_putc
    dec ecx
    jmp .print
.done:
    call console_newline
    ret
.missing:
    mov esi, fs_missing_file
    call console_write
    ret
.type_error:
    mov esi, fs_type_error
    call console_write
    ret
.syntax:
    mov esi, fs_read_syntax
    call console_write
    ret
.read_perm_denied:
    mov esi, fs_read_perm_err
    call console_write
    ret

fs_cmd_scribe:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .syntax
    mov esi, [fs_arg2]
    test esi, esi
    jz .empty
    call fs_strlen
    cmp eax, AFS_FILE_BYTES
    ja .large
    mov [fs_new_size], eax
    mov esi, [fs_arg2]
    mov edi, [fs_file_buffer]
    mov ecx, [fs_new_size]
    rep movsb
    jmp .locate
.empty:
    mov dword [fs_new_size], 0
.locate:
    mov eax, [fs_cwd]
    mov esi, [fs_arg1]
    call fs_find_child
    test eax, eax
    jnz .existing
    mov eax, [fs_cwd]
    call fs_dir_check_write
    jc .scribe_perm_denied
    mov esi, [fs_arg1]
    mov ebx, AFS_FILE_TYPE
    call fs_create_entry
    test eax, eax
    jz .create_error
.existing:
    mov [fs_target_id], eax
    call fs_entry_ptr
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .type_error
    mov cl, 2
    call fs_check_perm
    jc .scribe_perm_denied
    call fs_write_file
    jc fs_disk_error
    mov esi, fs_scribe_ok
    call console_write
    ret
.large:
    mov esi, fs_file_large
    call console_write
    ret
.type_error:
    mov esi, fs_type_error
    call console_write
    ret
.create_error:
    mov esi, fs_create_error
    call console_write
    ret
.syntax:
    mov esi, fs_scribe_syntax
    call console_write
    ret
.scribe_perm_denied:
    mov esi, fs_scribe_perm_err
    call console_write
    ret

fs_cmd_grow:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .syntax
    mov eax, [fs_cwd]
    mov esi, [fs_arg1]
    call fs_find_child
    test eax, eax
    jz .missing
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .type_error
    mov cl, 2
    call fs_check_perm
    jc .grow_perm_denied
    mov [fs_target_id], eax
    call fs_read_file
    jc fs_disk_error
    mov eax, [fs_file_size]
    mov [fs_new_size], eax
    mov esi, [fs_arg2]
    test esi, esi
    jz .write
    call fs_strlen
    add eax, [fs_new_size]
    cmp eax, AFS_FILE_BYTES
    ja .large
    mov ecx, eax
    sub ecx, [fs_new_size]
    mov edi, [fs_file_buffer]
    add edi, [fs_new_size]
    mov esi, [fs_arg2]
    rep movsb
    mov [fs_new_size], eax
.write:
    call fs_write_file
    jc fs_disk_error
    mov esi, fs_grow_ok
    call console_write
    ret
.missing:
    mov esi, fs_missing_file
    call console_write
    ret
.type_error:
    mov esi, fs_type_error
    call console_write
    ret
.large:
    mov esi, fs_file_large
    call console_write
    ret
.syntax:
    mov esi, fs_grow_syntax
    call console_write
    ret
.grow_perm_denied:
    mov esi, fs_grow_perm_err
    call console_write
    ret

fs_cmd_erase:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .syntax
    mov eax, [fs_cwd]
    call fs_find_child
    test eax, eax
    jz .missing
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .type_error
    mov byte [edi], 0
    call fs_commit
    jc fs_disk_error
    mov esi, fs_erase_ok
    call console_write
    ret
.missing:
    mov esi, fs_missing_file
    call console_write
    ret
.type_error:
    mov esi, fs_type_error
    call console_write
    ret
.syntax:
    mov esi, fs_file_syntax
    call console_write
    ret
.rm_perm_denied:
    mov esi, fs_rm_perm_err
    call console_write
    ret

fs_cmd_mv:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_two_args
    test eax, eax
    jz .syntax
    mov eax, [fs_cwd]
    call fs_dir_check_write
    jc .mv_perm_denied
    mov eax, [fs_cwd]
    mov esi, [fs_arg1]
    call fs_find_child
    test eax, eax
    jz .missing
    mov cl, 2
    call fs_check_perm
    jc .mv_perm_denied
    test eax, eax
    jz .missing
    mov [fs_target_id], eax
    mov eax, [fs_cwd]
    mov esi, [fs_arg2]
    call fs_find_child
    test eax, eax
    jnz .exists
    mov eax, [fs_target_id]
    call fs_entry_ptr
    mov esi, [fs_arg2]
    call fs_validate_name
    test eax, eax
    jz .syntax
    mov edi, [fs_target_id]
    dec edi
    shl edi, 6
    add edi, [fs_entries_ptr]
    lea edi, [edi+8]
    mov esi, [fs_arg2]
    mov ecx, eax
    inc ecx
    rep movsb
    mov eax, [fs_target_id]
    call fs_entry_ptr
    call fs_commit
    jc fs_disk_error
    mov esi, fs_mv_ok
    call console_write
    ret
.missing:
    mov esi, fs_missing_file
    call console_write
    ret
.mv_perm_denied:
    mov esi, fs_mv_perm_err
    call console_write
    ret
.exists:
    mov esi, fs_exists_error
    call console_write
    ret
.syntax:
    mov esi, fs_mv_syntax
    call console_write
    ret

fs_cmd_cp:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_two_args
    test eax, eax
    jz .syntax
    mov eax, [fs_cwd]
    mov esi, [fs_arg1]
    call fs_find_child
    test eax, eax
    jz .missing
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .type_error
    mov [fs_source_id], eax
    call fs_read_file
    jc fs_disk_error
    mov eax, [fs_cwd]
    mov esi, [fs_arg2]
    call fs_find_child
    test eax, eax
    jnz .exists
    mov esi, [fs_arg2]
    mov ebx, AFS_FILE_TYPE
    call fs_create_entry
    test eax, eax
    jz .create_error
    mov [fs_target_id], eax
    mov eax, [fs_source_id]
    call fs_entry_ptr
    mov eax, [edi+48]
    mov [fs_new_size], eax
    call fs_write_file
    jc fs_disk_error
    mov esi, fs_cp_ok
    call console_write
    ret
.missing:
    mov esi, fs_missing_file
    call console_write
    ret
.type_error:
    mov esi, fs_type_error
    call console_write
    ret
.exists:
    mov esi, fs_exists_error
    call console_write
    ret
.create_error:
    mov esi, fs_create_error
    call console_write
    ret
.syntax:
    mov esi, fs_cp_syntax
    call console_write
    ret

fs_get_two_args:
    call fs_get_arg
    test esi, esi
    jz .bad
    test edi, edi
    jz .bad
    cmp byte [edi], 0
    je .bad
    mov eax, 1
    ret
.bad:
    xor eax, eax
    ret

fs_cmd_rm:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    call fs_get_arg
    test esi, esi
    jz .syntax
    mov eax, [fs_cwd]
    call fs_dir_check_write
    jc .rm_perm_denied
    mov eax, [fs_cwd]
    call fs_find_child
    test eax, eax
    jz .missing
    mov cl, 2
    call fs_check_perm
    jc .rm_perm_denied
    test eax, eax
    jz .missing
    cmp byte [edi+1], AFS_DIR_TYPE
    jne .file
    mov [fs_target_id], eax
    call fs_dir_empty
    test eax, eax
    jz .not_empty
    mov eax, [fs_target_id]
    call fs_entry_ptr
    mov byte [edi], 0
    jmp .commit
.file:
    mov byte [edi], 0
.commit:
    call fs_commit
    jc fs_disk_error
    mov esi, fs_erase_ok
    call console_write
    ret
.not_empty:
    mov esi, fs_not_empty
    call console_write
    ret
.missing:
    mov esi, fs_missing_entry
    call console_write
    ret
.syntax:
    mov esi, fs_file_syntax
    call console_write
    ret
.rm_perm_denied:
    mov esi, fs_rm_perm_err
    call console_write
    ret

fs_dir_empty:
    ; EAX=directory id; returns EAX=1 if no live children.
    mov [fs_walk_parent], eax
    xor ebx, ebx
.loop:
    cmp ebx, AFS_ENTRY_COUNT
    jae .empty
    mov edi, ebx
    shl edi, 6
    add edi, [fs_entries_ptr]
    cmp byte [edi], 1
    jne .next
    mov eax, [edi+4]
    cmp eax, [fs_walk_parent]
    je .no
.next:
    inc ebx
    jmp .loop
.empty:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret

fs_cmd_df:
    cmp byte [fs_mounted], 1
    jne fs_not_ready
    xor ebx, ebx
    xor ecx, ecx
.loop:
    cmp ecx, AFS_ENTRY_COUNT
    jae .done
    mov edi, ecx
    shl edi, 6
    add edi, [fs_entries_ptr]
    cmp byte [edi], 1
    jne .next
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .next
    inc ebx
.next:
    inc ecx
    jmp .loop
.done:
    mov esi, fs_df_total
    call console_write
    mov eax, AFS_ENTRY_COUNT
    imul eax, AFS_FILE_BYTES
    call print_u32
    mov esi, fs_df_used
    call console_write
    mov eax, ebx
    imul eax, AFS_FILE_BYTES
    call print_u32
    mov esi, fs_df_free
    call console_write
    mov eax, AFS_ENTRY_COUNT
    sub eax, ebx
    imul eax, AFS_FILE_BYTES
    call print_u32
    mov esi, fs_df_suffix
    call console_write
    ret

fs_read_file:
    ; EAX=entry id. Loads a full slot into buffer, returns file size in global.
    mov [fs_target_id], eax
    call fs_entry_ptr
    test edi, edi
    jz .bad
    cmp byte [edi], 1
    jne .bad
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .bad
    mov eax, [edi+48]
    cmp eax, AFS_FILE_BYTES
    ja .bad
    mov [fs_file_size], eax
    test eax, eax
    jz .empty
    movzx eax, byte [edi+2]
    cmp eax, 1
    ja .bad
    mov ecx, [fs_target_id]
    dec ecx
    shl ecx, 1
    add ecx, eax
    imul ecx, AFS_FILE_SECTORS
    add ecx, AFS_DATA_START
    mov eax, ecx
    mov edi, [fs_file_buffer]
    mov ecx, AFS_FILE_SECTORS
    call ata_read_many
    ret
.empty:
    mov edi, [fs_file_buffer]
    xor eax, eax
    mov ecx, AFS_FILE_BYTES/4
    rep stosd
    clc
    ret
.bad:
    stc
    ret

fs_write_file:
    ; fs_target_id, fs_new_size and fs_file_buffer contain a complete replacement.
    mov eax, [fs_new_size]
    cmp eax, AFS_FILE_BYTES
    ja .bad
    mov eax, [fs_target_id]
    call fs_entry_ptr
    test edi, edi
    jz .bad
    cmp byte [edi], 1
    jne .bad
    cmp byte [edi+1], AFS_FILE_TYPE
    jne .bad
    mov eax, [edi+48]
    test eax, eax
    jz .slot_zero
    mov al, [edi+2]
    xor al, 1
    jmp .slot_ready
.slot_zero:
    xor eax, eax
.slot_ready:
    mov [fs_new_data_slot], al
    movzx eax, al
    mov ecx, [fs_target_id]
    dec ecx
    shl ecx, 1
    add ecx, eax
    imul ecx, AFS_FILE_SECTORS
    add ecx, AFS_DATA_START
    mov eax, ecx
    mov edi, [fs_file_buffer]
    mov ecx, AFS_FILE_SECTORS
    call ata_write_many
    jc .data_error
    mov eax, [fs_target_id]
    call fs_entry_ptr
    mov al, [fs_new_data_slot]
    mov [edi+2], al
    mov eax, [fs_new_size]
    mov [edi+48], eax
    call fs_commit
    jnc .ok
    mov esi, fs_debug_commit
    call console_write
    stc
    ret
.ok:
    clc
    ret
.data_error:
    call fs_mount
    mov esi, fs_debug_data
    call console_write
.bad:
    stc
    ret

fs_strlen:
    xor eax, eax
.loop:
    cmp byte [esi+eax], 0
    je .done
    inc eax
    jmp .loop
.done:
    ret

fs_not_ready:
    mov esi, fs_mount_error_text
    call console_write
    ret
fs_disk_error:
    mov esi, fs_disk_error_text
    call console_write
    ret

fs_magic db 'AARFS32',0
fs_ready_text db 'AarFS mounted: 2048 entries, 16 KiB max file, persistent virtual disk.',13,10,0
fs_mount_error_text db 'AarFS unavailable: virtual disk is unformatted or I/O failed.',13,10,0
fs_memory_error_text db 'AarFS unavailable: kernel heap allocation failed.',13,10,0
fs_debug_table db 'AarFS: inactive directory-table write failed.',13,10,0
fs_debug_super db 'AarFS: commit superblock write failed.',13,10,0
fs_debug_commit db 'AarFS: metadata commit failed.',13,10,0
fs_debug_at db ' at LBA ',0
fs_debug_data db 'AarFS: file-data write failed.',13,10,0
fs_disk_error_text db 'AarFS: disk I/O or metadata commit failed; previous metadata retained.',13,10,0
fs_path_error db 'Path too deep or invalid.',13,10,0
fs_path_syntax db 'cd: invalid path (names cannot contain slash escapes).',13,10,0
fs_missing_dir db 'cd: directory not found.',13,10,0
fs_not_dir db 'cd: not a directory.',13,10,0
fs_mkdir_syntax db 'Usage: mkdir <directory-name>',13,10,0
fs_touch_syntax db 'Usage: touch <file-name>',13,10,0
fs_read_syntax db 'Usage: read <file-name>',13,10,0
fs_scribe_syntax db 'Usage: scribe <file-name> <text>',13,10,0
fs_grow_syntax db 'Usage: grow <existing-file> <text>',13,10,0
fs_file_syntax db 'Usage: erase/rm <file-name>',13,10,0
fs_mv_syntax db 'Usage: shift/mv <old-name> <new-name>',13,10,0
fs_cp_syntax db 'Usage: cp <source-file> <new-name>',13,10,0
fs_missing_file db 'File not found.',13,10,0
fs_missing_entry db 'Entry not found.',13,10,0
fs_type_error db 'Operation is not valid for this entry type.',13,10,0
fs_exists_error db 'Destination already exists.',13,10,0
fs_create_error db 'Invalid name or directory table full.',13,10,0
fs_file_large db 'File exceeds the 16 KiB limit.',13,10,0
fs_not_empty db 'Directory is not empty.',13,10,0
fs_mkdir_ok db 'Directory created.',13,10,0
fs_touch_ok db 'File created.',13,10,0
fs_scribe_ok db 'File saved.',13,10,0
fs_grow_ok db 'File appended.',13,10,0
fs_erase_ok db 'Entry deleted.',13,10,0
fs_mv_ok db 'Entry renamed.',13,10,0
fs_cp_ok db 'File copied.',13,10,0
fs_df_total db 'Logical capacity: ',0
fs_df_used db ' bytes in use: ',0
fs_df_free db ' bytes available: ',0
fs_df_suffix db ' (2048 fixed 16 KiB slots).',13,10,0

fs_entries_ptr dd 0
fs_file_buffer dd 0
fs_cwd dd 0
fs_active_slot dd 0
fs_generation dd 0
fs_super_copy dd 0
fs_new_slot dd 0
fs_new_generation dd 0
fs_new_super_copy dd 0
fs_table_crc dd 0
fs_failed_lba dd 0
fs_super_selected dd 0
fs_valid_a db 0
fs_valid_b db 0
fs_mounted db 0
fs_new_data_slot db 0
fs_new_type db 0
fs_new_size dd 0
fs_target_id dd 0
fs_source_id dd 0
fs_file_size dd 0
fs_find_parent dd 0
fs_find_name dd 0
fs_new_name dd 0
fs_arg1 dd 0
fs_arg2 dd 0
fs_arg3 dd 0
fs_walk_parent dd 0
fs_walk_ptr dd 0
fs_path_count dd 0
fs_path_stack times 32 dd 0
fs_component times 40 db 0
ata_lba dd 0
ata_buffer dd 0
ata_error db 0
ata_retry_count db 0
ata_many_lba dd 0
ata_many_ptr dd 0
ata_many_count dd 0
super_copy_unused dd 0

fs_cd_perm_err db 'cd: Permission denied',13,10,0
fs_read_perm_err db 'read: Permission denied',13,10,0
fs_scribe_perm_err db 'scribe: Permission denied',13,10,0
fs_grow_perm_err db 'grow: Permission denied',13,10,0
fs_rm_perm_err db 'rm: Permission denied',13,10,0
fs_mkdir_perm_err db 'mkdir: Permission denied',13,10,0
fs_mv_perm_err db 'mv: Permission denied',13,10,0

kapi_putc:
    mov al, [esp+4]
    call console_putc
    ret

kapi_getc:
    call get_key
    movzx eax, al
    ret

kapi_fs_read_file:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    call fs_read_file
    jc .k_err
    mov esi, [fs_file_buffer]
    mov edi, [ebp+12]
    mov ecx, [fs_file_size]
    mov edx, [ebp+16]
    test edx, edx
    jz .k_skip_size
    mov [edx], ecx
.k_skip_size:
    rep movsb
    xor eax, eax
    jmp .k_done
.k_err:
    mov eax, -1
.k_done:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

kapi_fs_write_file:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    mov [fs_target_id], eax
    mov ecx, [ebp+16]
    mov [fs_new_size], ecx
    mov esi, [ebp+12]
    mov edi, [fs_file_buffer]
    rep movsb
    call fs_write_file
    jc .k_werr
    xor eax, eax
    jmp .k_wdone
.k_werr:
    mov eax, -1
.k_wdone:
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

kapi_fs_create_entry:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [fs_cwd]
    push eax
    mov eax, [ebp+16]
    mov [fs_cwd], eax
    mov esi, [ebp+8]
    movzx ebx, byte [ebp+12]
    call fs_create_entry
    pop edx
    mov [fs_cwd], edx
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

kapi_fs_find_child:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    mov esi, [ebp+12]
    call fs_find_child
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

kapi_fs_entry_ptr:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi
    mov eax, [ebp+8]
    call fs_entry_ptr
    mov eax, edi
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

kapi_fs_commit:
    call fs_commit
    jc .k_cerr
    xor eax, eax
    ret
.k_cerr:
    mov eax, -1
    ret

kernel_api_table:
    dd kapi_putc
    dd kapi_getc
    dd kapi_fs_read_file
    dd kapi_fs_write_file
    dd kapi_fs_create_entry
    dd kapi_fs_find_child
    dd kapi_fs_entry_ptr
    dd kapi_fs_commit
    dd fs_cwd
    dd session_uid
    dd session_gid
    dd session_supp_count
    dd session_supp_gids
    dd session_user
    dd session_home
    dd timer_ticks
    dd kernel_set_video_mode
    dd kapi_kbd_check
    dd kapi_mouse_read
