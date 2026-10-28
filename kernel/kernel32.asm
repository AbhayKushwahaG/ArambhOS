; Aarambh OS protected-mode kernel prototype (x86 386+).
; BIOS loads this image at physical 0000:7E00. The first instructions execute in
; real mode only to capture BIOS memory information and enter 32-bit protected mode.
%define IDT_BASE 0x00001000

[BITS 16]
[ORG 0x7E00]

REAL_ENTRY:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    int 0x12
    mov [memory_kib], ax
    ; Save a bounded BIOS E820 map before leaving real mode.
    mov dword [e820_count], 0
    mov di, e820_map
    xor ebx, ebx
.e820_next:
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    mov dword [es:di+20], 1
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    inc dword [e820_count]
    add di, 24
    cmp dword [e820_count], 16
    jae .e820_done
    test ebx, ebx
    jnz .e820_next
.e820_done:
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    db 0xEA
    dw PM_ENTRY
    dw 0x08

[BITS 32]
PM_ENTRY:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    mov ebp, esp
    cld

    call serial_init
    call bridge_uart_init
    mov esi, serial_banner
    call serial_write
    call console_clear
    call memory_init
    mov esi, banner
    call console_write

    ; Initialize IDT entries (vectors 0-47 plus vector 128 for int 0x80 syscall)
    mov edi, IDT_BASE
    mov ecx, 48
.fill_idt:
    mov eax, fault_handler
    call idt_set_gate
    add edi, 8
    loop .fill_idt
    mov edi, IDT_BASE + 32*8
    mov eax, irq_timer
    call idt_set_gate
    mov edi, IDT_BASE + 33*8
    mov eax, irq_keyboard
    call idt_set_gate
    mov edi, IDT_BASE + 34*8
    mov ecx, 14
.fill_irqs:
    mov eax, irq_dummy
    call idt_set_gate
    add edi, 8
    loop .fill_irqs
    mov edi, IDT_BASE + 44*8
    mov eax, irq_mouse
    call idt_set_gate

    ; Install vector 128 (0x80) for syscalls with DPL=3
    mov edi, IDT_BASE + 128*8
    mov eax, syscall_handler
    call idt_set_user_gate
    lidt [idtr]

    call pic_init
    call pit_init
    call fs_init
    call auth_boot_init
    mov esi, ready_text
    call console_write
    mov esi, serial_ready
    call serial_write
    sti

.shell:
    call print_prompt
    mov byte [vga_attr], 0x0F
    call read_line
    cmp byte [command_buffer], 0
    je .shell
    call parse_command_line
    call command_dispatch
    jmp .shell

; Small shell commands are referenced from the central command registry.
shell_cmd_identity:
    mov esi, identity_text
    call console_write
    ret
shell_cmd_say:
    mov esi, [command_args]
    call console_write
    call console_newline
    ret
shell_cmd_fault:
    int3
    ret
shell_cmd_clear:
    call console_clear
    ret
shell_cmd_mem:
    mov esi, mem_label
    call console_write
    movzx eax, word [memory_kib]
    call print_u32
    mov esi, mem_suffix
    call console_write
    mov esi, heap_label
    call console_write
    mov eax, [heap_end]
    sub eax, [heap_ptr]
    call print_u32
    mov esi, heap_suffix
    call console_write
    ret
shell_cmd_ticks:
    mov esi, ticks_label
    call console_write
    mov eax, [timer_ticks]
    call print_u32
    call console_newline
    ret
shell_cmd_reboot:
    mov esi, reboot_text
    call console_write
    mov al, 0xFE
    out 0x64, al
.hang:
    cli
    hlt
    jmp .hang

; Select the largest BIOS E820 type-1 range below 4 GiB and expose a bounded
; 16-byte-aligned bump allocator. This allocator has no free operation yet.
memory_init:
    mov dword [heap_ptr], 0
    mov dword [heap_end], 0
    mov dword [heap_best_size], 0
    mov esi, e820_map
    mov ecx, [e820_count]
.scan:
    test ecx, ecx
    jz .finish
    cmp dword [esi+16], 1
    jne .next
    cmp dword [esi+4], 0
    jne .next
    cmp dword [esi+12], 0
    jne .next
    mov eax, [esi]
    mov edx, [esi+8]
    add edx, eax
    jc .next
    cmp eax, 0x100000
    jae .base_ok
    cmp edx, 0x100000
    jbe .next
    mov eax, 0x100000
.base_ok:
    cmp edx, eax
    jbe .next
    mov ebx, edx
    sub ebx, eax
    cmp ebx, [heap_best_size]
    jbe .next
    mov [heap_best_size], ebx
    mov [heap_ptr], eax
    mov [heap_end], edx
.next:
    add esi, 24
    dec ecx
    jmp .scan
.finish:
    cmp dword [heap_ptr], 0
    je .unavailable
    add dword [heap_ptr], 0xFFF
    and dword [heap_ptr], 0xFFFFF000
    and dword [heap_end], 0xFFFFF000
    mov eax, [heap_ptr]
    cmp eax, [heap_end]
    jb .ok
.unavailable:
    mov dword [heap_ptr], 0
    mov dword [heap_end], 0
.ok:
    ret

; EAX=size, returns a 16-byte-aligned physical address or zero.
kmalloc:
    test eax, eax
    jz .fail
    push ebx
    mov ebx, [heap_ptr]
    test ebx, ebx
    jz .restore_fail
    add ebx, 15
    jc .restore_fail
    and ebx, 0xFFFFFFF0
    mov edx, ebx
    add edx, eax
    jc .restore_fail
    cmp edx, [heap_end]
    ja .restore_fail
    mov [heap_ptr], edx
    mov eax, ebx
    pop ebx
    ret
.restore_fail:
    pop ebx
.fail:
    xor eax, eax
    ret

parse_command_line:
    mov esi, command_buffer
    mov edi, command_name
    xor ecx, ecx
.skip:
    cmp byte [esi], ' '
    jne .copy
    inc esi
    jmp .skip
.copy:
    mov al, [esi]
    test al, al
    jz .finish_empty
    cmp al, ' '
    je .split
    stosb
    inc esi
    inc ecx
    cmp ecx, 31
    jb .copy
    jmp .split
.split:
    mov byte [edi], 0
    inc esi
.skip_args:
    cmp byte [esi], ' '
    jne .args
    inc esi
    jmp .skip_args
.args:
    mov [command_args], esi
    ret
.finish_empty:
    mov byte [edi], 0
    mov [command_args], esi
    ret

; EDI points to an 8-byte IDT descriptor; EAX is the handler address.
idt_set_gate:
    mov word [edi], ax
    mov word [edi+2], 0x08
    mov byte [edi+4], 0
    mov byte [edi+5], 0x8E
    shr eax, 16
    mov word [edi+6], ax
    ret

idt_set_user_gate:
    mov word [edi], ax
    mov word [edi+2], 0x08
    mov byte [edi+4], 0
    mov byte [edi+5], 0xEE
    shr eax, 16
    mov word [edi+6], ax
    ret

pic_init:
    mov al, 0x11
    out 0x20, al
    out 0xA0, al
    call io_wait
    mov al, 0x20
    out 0x21, al
    mov al, 0x28
    out 0xA1, al
    call io_wait
    mov al, 0x04
    out 0x21, al
    mov al, 0x02
    out 0xA1, al
    call io_wait
    mov al, 0x01
    out 0x21, al
    out 0xA1, al
    call io_wait
    mov al, 0xF8             ; unmask IRQ0, IRQ1, IRQ2 cascade
    out 0x21, al
    mov al, 0xEF             ; unmask IRQ12 mouse on slave PIC
    out 0xA1, al
    ret
io_wait:
    xor al, al
    out 0x80, al
    ret
pit_init:
    mov al, 0x36
    out 0x43, al
    mov ax, 1193             ; approximately 1 kHz from 1.193182 MHz
    out 0x40, al
    mov al, ah
    out 0x40, al
    ret

irq_timer:
    pushad
    inc dword [timer_ticks]
    mov al, 0x20
    out 0x20, al
    popad
    iretd

irq_keyboard:
    pushad
    in al, 0x64
    test al, 0x20
    jnz .mouse_data
    in al, 0x60
    cmp al, 0xE0
    jne .not_prefix
    mov byte [kbd_ext], 1
    jmp .eoi
.not_prefix:
    test al, 0x80
    jz .press
    ; Modifier releases update state; other releases are discarded.
    mov byte [kbd_ext], 0
    cmp al, 0xAA
    jne .rel_right_shift
    mov byte [kbd_shift_l], 0
    jmp .eoi
.rel_right_shift:
    cmp al, 0xB6
    jne .rel_ctrl
    mov byte [kbd_shift_r], 0
    jmp .eoi
.rel_ctrl:
    cmp al, 0x9D
    jne .eoi
    mov byte [kbd_ctrl], 0
    jmp .eoi
.press:
    cmp byte [kbd_ext], 0
    je .normal
    ; QEMU maps Ctrl to E0 1D; treat both left and right Ctrl as the modifier.
    mov byte [kbd_ext], 0
    cmp al, 0x1D
    jne .ext_key
    mov byte [kbd_ctrl], 1
    jmp .eoi
.ext_key:
    ; Encode other extended keys as 0x80|scancode so the shell never sees them.
    or al, 0x80
    jmp .enqueue
.normal:
    cmp al, 0x2A
    jne .press_right_shift
    mov byte [kbd_shift_l], 1
    jmp .eoi
.press_right_shift:
    cmp al, 0x36
    jne .press_ctrl
    mov byte [kbd_shift_r], 1
    jmp .eoi
.press_ctrl:
    cmp al, 0x1D
    jne .enqueue
    mov byte [kbd_ctrl], 1
    jmp .eoi
.enqueue:
    movzx ebx, byte [kbd_head]
    mov edx, ebx
    inc edx
    and edx, 63
    cmp dl, [kbd_tail]
    je .eoi                 ; drop newest key if the 63-key ring is full
    mov [kbd_ring+ebx], al
    mov [kbd_head], dl
    ; Snapshot Ctrl/Shift with the key so a quick release cannot change its meaning.
    xor ecx, ecx
    cmp byte [kbd_ctrl], 0
    je .mod_shift
    or ecx, 1
.mod_shift:
    cmp byte [kbd_shift_l], 0
    jne .mod_shift_set
    cmp byte [kbd_shift_r], 0
    je .mod_done
.mod_shift_set:
    or ecx, 2
.mod_done:
    mov [kbd_mod_ring+ebx], cl
    jmp .eoi
.mouse_data:
    in al, 0x60
    movzx ebx, byte [mouse_head]
    mov edx, ebx
    inc edx
    and edx, 63
    cmp dl, [mouse_tail]
    je .eoi
    mov [mouse_ring+ebx], al
    mov [mouse_head], dl
.eoi:
    mov al, 0x20
    out 0x20, al
    popad
    iretd

get_key:
    cli
.check:
    mov al, [kbd_head]
    cmp al, [kbd_tail]
    jne .pop
    sti
    hlt
    cli
    jmp .check
.pop:
    movzx eax, byte [kbd_tail]
    mov dl, [kbd_mod_ring+eax]
    mov al, [kbd_ring+eax]
    inc byte [kbd_tail]
    and byte [kbd_tail], 63
    sti
    test al, 0x80
    jnz .extended
    cmp al, 0x3B             ; F1 is a normal scancode with no ASCII mapping
    je .f1
    movzx eax, al
    mov dh, [kbd_map+eax]
    test dh, dh
    jz get_key
    test dl, 1
    jz .shift_check
    cmp dh, 'a'
    jb .shift_check
    cmp dh, 'z'
    ja .shift_check
    sub dh, 96               ; Ctrl-A..Ctrl-Z -> 0x01..0x1A
    mov al, dh
    movzx eax, al
    ret
.shift_check:
    test dl, 2
    jz .plain
    mov al, [kbd_map_shift+eax]
    jmp .mapped
.plain:
    mov al, dh
.mapped:
    test al, al
    jz get_key
    ret
.f1:
    mov al, 0x99
    ret
.extended:
    and eax, 0x7F
    mov al, [kbd_ext_map+eax]
    test al, al
    jz get_key
    ret

read_line:
    mov edi, command_buffer
    xor ecx, ecx
.next:
    call get_key
    cmp al, 13
    je .finish
    cmp al, 8
    je .backspace
    cmp al, 32
    jb .next
    cmp al, 126
    ja .next
    cmp ecx, 126
    jae .next
    mov [edi], al
    inc edi
    inc ecx
    call console_putc
    jmp .next
.backspace:
    test ecx, ecx
    jz .next
    dec edi
    dec ecx
    mov byte [edi], 0
    mov al, 8
    call console_putc
    jmp .next
.finish:
    mov byte [edi], 0
    call console_newline
    ret

string_equal:
.loop:
    mov al, [esi]
    cmp al, [edi]
    jne .no
    test al, al
    jz .yes
    inc esi
    inc edi
    jmp .loop
.yes:
    mov eax, 1
    ret
.no:
    xor eax, eax
    ret

console_clear:
    push eax
    push ecx
    push edi
    mov eax, 0x0F200F20
    mov edi, 0xB8000
    mov ecx, 1000
    rep stosd
    mov dword [cursor_pos], 0
    pop edi
    pop ecx
    pop eax
    ret
console_newline:
    push eax
    mov al, 13
    call console_putc
    mov al, 10
    call console_putc
    pop eax
    ret
console_write:
    lodsb
    test al, al
    jz .done
    call console_putc
    jmp console_write
.done:
    ret
console_putc:
    push eax
    push ebx
    push ecx
    push edx
    push edi
    mov [putc_char], al
    cmp al, 13
    je .cr
    cmp al, 10
    je .lf
    cmp al, 8
    je .bs
    mov eax, [cursor_pos]
    cmp eax, 2000
    jb .store
    call console_clear
    xor eax, eax
.store:
    movzx ebx, byte [vga_attr]
    shl ebx, 8
    mov bl, [putc_char]
    mov edi, eax
    shl edi, 1
    mov [0xB8000+edi], bx
    inc dword [cursor_pos]
    cmp dword [cursor_pos], 2000
    jb .serial
    call console_clear
    jmp .serial
.cr:
    mov eax, [cursor_pos]
    xor edx, edx
    mov ecx, 80
    div ecx
    mul ecx
    mov [cursor_pos], eax
    jmp .serial
.lf:
    mov eax, [cursor_pos]
    xor edx, edx
    mov ecx, 80
    div ecx
    inc eax
    cmp eax, 25
    jb .set_row
    call console_clear
    jmp .serial
.set_row:
    imul eax, eax, 80
    mov [cursor_pos], eax
    jmp .serial
.bs:
    cmp dword [cursor_pos], 0
    je .serial
    dec dword [cursor_pos]
    mov edi, [cursor_pos]
    shl edi, 1
    mov word [0xB8000+edi], 0x0F20
.serial:
    mov al, [putc_char]
    call serial_putc
    pop edi
    pop edx
    pop ecx
    pop ebx
    pop eax
    ret

print_u32:
    pushad
    mov ebx, 10
    xor ecx, ecx
    test eax, eax
    jnz .convert
    push dword 0
    inc ecx
    jmp .emit
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
    call console_putc
    loop .emit
    popad
    ret

serial_init:
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 0x80
    out dx, al
    mov dx, 0x3F8
    mov al, 1                 ; 115200 baud
    out dx, al
    mov dx, 0x3F9
    xor al, al
    out dx, al
    mov dx, 0x3FB
    mov al, 3                 ; 8N1
    out dx, al
    mov dx, 0x3FA
    mov al, 0xC7
    out dx, al
    mov dx, 0x3FC
    mov al, 0x0B
    out dx, al
    ret
serial_putc:
    push eax
    push ecx
    push edx
    mov ecx, 0x10000
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jnz .send
    loop .wait
    jmp .done
.send:
    mov dx, 0x3F8
    mov al, [esp+8]           ; saved EAX low byte at top after 3 pushes
    out dx, al
.done:
    pop edx
    pop ecx
    pop eax
    ret
serial_write:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp serial_write
.done:
    ret

irq_dummy:
    push eax
    mov al, 0x20
    out 0x20, al
    out 0xA0, al
    pop eax
    iretd

fault_handler:
    cli
    mov byte [vga_attr], 0x4F
    call console_clear
    mov esi, panic_text
    call console_write
    mov esi, panic_text
    call serial_write
.halt:
    cli
    hlt
    jmp .halt

; Flat 4 GiB code/data segments.
gdt:
    dq 0
    dw 0xFFFF, 0
    db 0, 0x9A, 0xCF, 0        ; 0x08: Kernel Code (Ring 0)
    dw 0xFFFF, 0
    db 0, 0x92, 0xCF, 0        ; 0x10: Kernel Data (Ring 0)
    dw 0xFFFF, 0
    db 0, 0xFA, 0xCF, 0        ; 0x18 | 3 = 0x1B: User Code (Ring 3)
    dw 0xFFFF, 0
    db 0, 0xF2, 0xCF, 0        ; 0x20 | 3 = 0x23: User Data (Ring 3)
    dw 0xFFFF, 0
    db 0, 0x9A, 0x00, 0        ; 0x28: 16-bit Code (Ring 0)
    dw 0xFFFF, 0
    db 0, 0x92, 0x00, 0        ; 0x30: 16-bit Data (Ring 0)
gdt_end:
gdt_descriptor:
    dw gdt_end-gdt-1
    dd gdt

idtr:
    dw 129*8-1
    dd IDT_BASE

syscall_handler:
    push ebx
    push ecx
    push edx
    push esi
    push edi
    push ebp

    cmp eax, 24                ; SYS_GETUID
    je .sys_getuid
    cmp eax, 47                ; SYS_GETGID
    je .sys_getgid
    cmp eax, 49                ; SYS_GETEUID
    je .sys_getuid
    cmp eax, 50                ; SYS_GETEGID
    je .sys_getgid
    cmp eax, 23                ; SYS_SETUID
    je .sys_setuid
    cmp eax, 46                ; SYS_SETGID
    je .sys_setgid
    cmp eax, 4                 ; SYS_WRITE (EBX=fd, ECX=buf, EDX=len)
    je .sys_write
    mov eax, -38               ; -ENOSYS
    jmp .done

.sys_getuid:
    movzx eax, word [session_uid]
    jmp .done

.sys_getgid:
    movzx eax, word [session_gid]
    jmp .done

.sys_setuid:
    cmp word [session_uid], 0  ; root only
    jne .eperm
    mov word [session_uid], bx
    xor eax, eax
    jmp .done

.sys_setgid:
    cmp word [session_uid], 0  ; root only
    jne .eperm
    mov word [session_gid], bx
    xor eax, eax
    jmp .done

.eperm:
    mov eax, -1                ; -EPERM
    jmp .done

.sys_write:
    test ecx, ecx
    jz .einval
    test edx, edx
    jz .write_zero
    push ecx
    push edx
    mov esi, ecx
.w_loop:
    test edx, edx
    jz .w_done
    lodsb
    call console_putc
    dec edx
    jmp .w_loop
.w_done:
    pop eax
    pop ecx
    jmp .done
.write_zero:
    xor eax, eax
    jmp .done
.einval:
    mov eax, -22               ; -EINVAL
    jmp .done

.done:
    pop ebp
    pop edi
    pop esi
    pop edx
    pop ecx
    pop ebx
    iretd

print_prompt:
    mov byte [vga_attr], 0x0B
    mov esi, session_user
    call console_write
    mov al, '@'
    call console_putc
    mov esi, prompt_prefix
    call console_write
    call fs_print_path
    mov esi, prompt_suffix
    call console_write
    ret

prompt_prefix db 'AR32:',0
prompt_suffix db ' > ',0

banner db 'AARAMBH OS 32-bit protected mode',13,10
       db 'GDT + IDT + PIC + PIT + keyboard + COM1 serial',13,10
       db 'AarFS provides persistent files and directories on this virtual disk.',13,10
       db 'Legacy FAT32 apps remain in the separate 16-bit image.',13,10,13,10,0
ready_text db 'Interrupts enabled. Type help for the command index or guide to begin.',13,10,0
serial_banner db 'Aarambh PM32: entering 32-bit kernel',13,10,0
serial_ready db 'Aarambh PM32: ready',13,10,0
prompt db 'AR32:/ > ',0
identity_text db 'Aarambh OS PM32 - 32-bit BIOS protected-mode kernel',13,10,0
mem_label db 'BIOS conventional memory: ',0
mem_suffix db ' KiB conventional memory',13,10,0
heap_label db 'Bump heap bytes free: ',0
heap_suffix db ' (no free yet).',13,10,0
ticks_label db 'PIT ticks: ',0
reboot_text db 'Rebooting...',13,10,0
panic_text db 'AARAMBH PM32 PANIC: exception or unhandled interrupt. Halted.',13,10,0

memory_kib dw 0
e820_count dd 0
e820_map times 16*24 db 0
heap_ptr dd 0
heap_end dd 0
heap_best_size dd 0
timer_ticks dd 0
cursor_pos dd 0
vga_attr db 0x0F
putc_char db 0
command_buffer times 128 db 0
command_name times 32 db 0
command_args dd 0
session_uid dw 0
session_gid dw 0
session_supp_count db 0
session_supp_gids times 8 dw 0
session_user db 'root',0
times 27 db 0
session_home db '/root',0
times 58 db 0
kbd_head db 0
kbd_tail db 0
kbd_ext db 0
kbd_shift_l db 0
kbd_shift_r db 0
kbd_ctrl db 0
kbd_ring times 64 db 0
kbd_mod_ring times 64 db 0
kbd_map:
    db 0x00, 0x1B, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0x2D, 0x3D, 0x08, 0x09
    db 0x71, 0x77, 0x65, 0x72, 0x74, 0x79, 0x75, 0x69, 0x6F, 0x70, 0x5B, 0x5D, 0x0D, 0x00, 0x61, 0x73
    db 0x64, 0x66, 0x67, 0x68, 0x6A, 0x6B, 0x6C, 0x3B, 0x27, 0x60, 0x00, 0x5C, 0x7A, 0x78, 0x63, 0x76
    db 0x62, 0x6E, 0x6D, 0x2C, 0x2E, 0x2F, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
kbd_map_shift:
    db 0x00, 0x1B, 0x21, 0x40, 0x23, 0x24, 0x25, 0x5E, 0x26, 0x2A, 0x28, 0x29, 0x5F, 0x2B, 0x08, 0x09
    db 0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0x7B, 0x7D, 0x0D, 0x00, 0x41, 0x53
    db 0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0x3A, 0x22, 0x7E, 0x00, 0x7C, 0x5A, 0x58, 0x43, 0x56
    db 0x42, 0x4E, 0x4D, 0x3C, 0x3E, 0x3F, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    db 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
; Extended Set-1 keys decoded into codes above the printable ASCII range.
; 0x90..0x98 are editor movement/edit commands; 0x99 is F1.
kbd_ext_map:
    times 0x47 db 0
    db 0x94                  ; 0x47 Home
    db 0x90                  ; 0x48 Up
    db 0x97                  ; 0x49 Page Up
    times 1 db 0
    db 0x92                  ; 0x4B Left
    times 1 db 0
    db 0x93                  ; 0x4D Right
    times 1 db 0
    db 0x95                  ; 0x4F End
    db 0x91                  ; 0x50 Down
    db 0x98                  ; 0x51 Page Down
    times 1 db 0
    db 0x96                  ; 0x53 Delete
    times 0x2C db 0

kernel_set_video_mode:
    push ebp
    mov ebp, esp
    push ebx
    push esi
    push edi

    mov ax, [ebp+8]
    mov [rm_mode_val], ax
    mov bx, [ebp+12]
    mov [rm_bx_val], bx

    cli
    mov [rm_saved_esp], esp

    ; Transition to 16-bit protected mode
    mov ax, 0x30
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    jmp 0x28:.to_16bit

[BITS 16]
.to_16bit:
    mov eax, cr0
    and al, ~1
    mov cr0, eax

    jmp 0x0000:.to_real

.to_real:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00

    lidt [real_idtr]
    mov ax, [rm_mode_val]
    mov bx, [rm_bx_val]
    int 0x10
    lgdt [gdt_descriptor]
    mov eax, cr0
    or al, 1
    mov cr0, eax

    jmp 0x08:.back_to_32bit

[BITS 32]
.back_to_32bit:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    lidt [idtr]
    mov esp, [rm_saved_esp]

    call pic_init
    sti

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret

real_idtr:
    dw 0x03FF
    dd 0
rm_mode_val dw 0
rm_bx_val   dw 0
rm_saved_esp dd 0
mouse_head  db 0
mouse_tail  db 0
mouse_ring  times 64 db 0

kapi_kbd_check:
    mov al, [kbd_head]
    cmp al, [kbd_tail]
    je .k_empty
    mov eax, 1
    ret
.k_empty:
    xor eax, eax
    ret

kapi_mouse_read:
    mov al, [mouse_head]
    cmp al, [mouse_tail]
    je .m_empty
    movzx eax, byte [mouse_tail]
    movzx edx, byte [mouse_ring+eax]
    inc byte [mouse_tail]
    and byte [mouse_tail], 63
    mov eax, edx
    ret
.m_empty:
    mov eax, -1
    ret

irq_mouse:
    pushad
    in al, 0x60
    movzx ebx, byte [mouse_head]
    mov edx, ebx
    inc edx
    and edx, 63
    cmp dl, [mouse_tail]
    je .m_eoi
    mov [mouse_ring+ebx], al
    mov [mouse_head], dl
.m_eoi:
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    popad
    iretd

kernel_end:

%include "aarfs32.asm"
%include "python_bridge32.asm"
%include "editor32.asm"
%include "calc32.asm"
%include "commands32.asm"
%include "help32.asm"
