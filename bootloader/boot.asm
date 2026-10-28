; Aarambh v0.4 BIOS hard-disk/USB boot sector.
; Loads kernel sectors 1-63 with INT 13h extensions (LBA). Sector 64 is the
; first AarFS metadata sector, so the protected-mode kernel may use up to
; 32256 bytes while the legacy 16-bit kernel remains far below that limit.
[BITS 16]
[ORG 0x7C00]

KERNEL_SEG equ 0x0000
KERNEL_OFF equ 0x7E00
KERNEL_SECTORS equ 63

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    cld
    sti
    mov [boot_drive], dl

    mov si, loading_msg
    call print_string

    ; Verify INT 13h extensions are available.
    mov dl, [boot_drive]
    mov bx, 0x55AA
    mov ah, 0x41
    int 0x13
    jc disk_error
    cmp bx, 0xAA55
    jne disk_error
    test cx, 1
    jz disk_error

    ; Read LBA 1..8 (kernel) into 0000:7E00.
    mov byte [disk_retries], 3
.read_kernel:
    mov byte [dap], 0x10
    mov byte [dap+1], 0
    mov word [dap+2], KERNEL_SECTORS
    mov word [dap+4], KERNEL_OFF
    mov word [dap+6], KERNEL_SEG
    mov dword [dap+8], 1
    mov dword [dap+12], 0
    mov si, dap
    mov dl, [boot_drive]
    mov ah, 0x42
    int 0x13
    jnc .kernel_loaded
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec byte [disk_retries]
    jnz .read_kernel
    jmp disk_error

.kernel_loaded:
    mov dl, [boot_drive]
    jmp KERNEL_SEG:KERNEL_OFF

print_string:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0
    int 0x10
    jmp print_string
.done:
    ret

disk_error:
    mov si, error_msg
    call print_string
.halt:
    cli
    hlt
    jmp .halt

boot_drive db 0
disk_retries db 0
loading_msg db 'Aarambh: loading from BIOS disk...',13,10,0
error_msg db 'Aarambh: BIOS LBA disk read failed.',13,10,0
dap: db 16,0
dap_count dw KERNEL_SECTORS
dap_offset dw KERNEL_OFF
dap_segment dw KERNEL_SEG
dap_lba dq 1

times 446-($-$$) db 0
; Partition table will be written by the image builder.
times 64 db 0
dw 0xAA55
