; Central command registry shared by the shell dispatcher and the help system.
; Each entry is 36 bytes:
;   +0 name ptr, +4 alias ptr (or 0), +8 handler, +12 category, +13 modifies,
;   +14 padding, +16 syntax, +20 description, +24 example+result,
;   +28 errors, +32 limits.
%define CMD_SIZE 36

%macro CMD 10
    dd %1
    dd %2
    dd %3
    db %4
    db %5
    dw 0
    dd %6
    dd %7
    dd %8
    dd %9
    dd %10
%endmacro

commands:
    CMD c_help_name, 0, help_command, 1, 0, c_help_syntax, c_help_text, c_help_example, c_help_errors, c_help_limits
    CMD c_guide_name, 0, guide_command, 1, 0, c_guide_syntax, c_guide_text, c_guide_example, c_guide_errors, c_guide_limits
    CMD c_identity_name, 0, shell_cmd_identity, 1, 0, c_identity_syntax, c_identity_text_doc, c_identity_example, c_identity_errors, c_identity_limits
    CMD c_say_name, 0, shell_cmd_say, 2, 0, c_say_syntax, c_say_text, c_say_example, c_say_errors, c_say_limits
    CMD c_cd_name, 0, fs_cmd_cd, 2, 0, c_cd_syntax, c_cd_text, c_cd_example, c_cd_errors, c_cd_limits
    CMD c_pwd_name, c_where_name, fs_cmd_pwd, 2, 0, c_pwd_syntax, c_pwd_text, c_pwd_example, c_pwd_errors, c_pwd_limits
    CMD c_ls_name, c_peek_name, fs_cmd_ls, 3, 0, c_ls_syntax, c_ls_text, c_ls_example, c_ls_errors, c_ls_limits
    CMD c_mkdir_name, 0, fs_cmd_mkdir, 3, 1, c_mkdir_syntax, c_mkdir_text, c_mkdir_example, c_mkdir_errors, c_mkdir_limits
    CMD c_touch_name, c_forge_name, fs_cmd_touch, 3, 1, c_touch_syntax, c_touch_text, c_touch_example, c_touch_errors, c_touch_limits
    CMD c_read_name, c_cat_name, fs_cmd_read, 3, 0, c_read_syntax, c_read_text, c_read_example, c_read_errors, c_read_limits
    CMD c_scribe_name, 0, fs_cmd_scribe, 3, 1, c_scribe_syntax, c_scribe_text, c_scribe_example, c_scribe_errors, c_scribe_limits
    CMD c_grow_name, 0, fs_cmd_grow, 3, 1, c_grow_syntax, c_grow_text, c_grow_example, c_grow_errors, c_grow_limits
    CMD c_shift_name, c_mv_name, fs_cmd_mv, 3, 1, c_shift_syntax, c_shift_text, c_shift_example, c_shift_errors, c_shift_limits
    CMD c_cp_name, 0, fs_cmd_cp, 3, 1, c_cp_syntax, c_cp_text, c_cp_example, c_cp_errors, c_cp_limits
    CMD c_rm_name, c_erase_name, fs_cmd_rm, 3, 1, c_rm_syntax, c_rm_text, c_rm_example, c_rm_errors, c_rm_limits
    CMD c_df_name, 0, fs_cmd_df, 3, 0, c_df_syntax, c_df_text, c_df_example, c_df_errors, c_df_limits
    CMD c_edit_name, 0, editor_command, 4, 1, c_edit_syntax, c_edit_text, c_edit_example, c_edit_errors, c_edit_limits
    CMD c_py_name, 0, py_command, 5, 0, c_py_syntax, c_py_text, c_py_example, c_py_errors, c_py_limits
    CMD c_python_name, 0, py_native_command, 5, 0, c_python_syntax, c_python_text, c_python_example, c_python_errors, c_python_limits
    CMD c_calc_name, 0, calc_command, 6, 0, c_calc_syntax, c_calc_text, c_calc_example, c_calc_errors, c_calc_limits
    CMD c_mem_name, 0, shell_cmd_mem, 7, 0, c_mem_syntax, c_mem_text, c_mem_example, c_mem_errors, c_mem_limits
    CMD c_ticks_name, 0, shell_cmd_ticks, 7, 0, c_ticks_syntax, c_ticks_text, c_ticks_example, c_ticks_errors, c_ticks_limits
    CMD c_clear_name, c_wipe_name, shell_cmd_clear, 8, 0, c_clear_syntax, c_clear_text, c_clear_example, c_clear_errors, c_clear_limits
    CMD c_reboot_name, 0, shell_cmd_reboot, 8, 0, c_reboot_syntax, c_reboot_text_doc, c_reboot_example, c_reboot_errors, c_reboot_limits
    CMD c_fault_name, 0, shell_cmd_fault, 9, 0, c_fault_syntax, c_fault_text, c_fault_example, c_fault_errors, c_fault_limits
commands_end:

%define COMMAND_COUNT ((commands_end-commands)/CMD_SIZE)

category_titles:
    dd c_cat_1, c_cat_2, c_cat_3, c_cat_4, c_cat_5, c_cat_6, c_cat_7, c_cat_8, c_cat_9

; Dispatch on command_name using the same table that supplies help.
command_dispatch:
    mov ebx, commands
    mov ecx, COMMAND_COUNT
.loop:
    mov esi, command_name
    mov edi, [ebx]
    call string_equal
    test eax, eax
    jnz .hit
    mov edi, [ebx+4]
    test edi, edi
    jz .next
    mov esi, command_name
    call string_equal
    test eax, eax
    jnz .hit
.next:
    add ebx, CMD_SIZE
    loop .loop
    jmp command_unknown
.hit:
    call [ebx+8]
    ret

command_unknown:
    call sys_native_dispatch
    test eax, eax
    jnz .handled
    mov byte [vga_attr], 0x0C
    mov esi, c_unknown_1
    call console_write
    mov esi, command_name
    call console_write
    mov esi, c_unknown_2
    call console_write
    mov byte [vga_attr], 0x0E
    call command_suggest
    mov byte [vga_attr], 0x07
.handled:
    ret

; Practical suggestion: match the first two characters of a registered name.
command_suggest:
    cmp byte [command_name], 0
    je .done
    cmp byte [command_name+1], 0
    je .done
    mov ebx, commands
    mov ecx, COMMAND_COUNT
.loop:
    mov edi, [ebx]
    mov esi, command_name
    mov al, [esi]
    cmp al, [edi]
    jne .next
    mov al, [esi+1]
    cmp al, [edi+1]
    jne .next
    mov esi, c_did_you_mean_1
    call console_write
    mov esi, [ebx]
    call console_write
    mov esi, c_did_you_mean_2
    call console_write
    ret
.next:
    add ebx, CMD_SIZE
    loop .loop
.done:
    ret

; ESI=word, returns EAX=1 and EBX=entry when the name or alias matches.
help_find_entry:
    mov [help_lookup_ptr], esi
    mov ebx, commands
    mov ecx, COMMAND_COUNT
.loop:
    mov esi, [help_lookup_ptr]
    mov edi, [ebx]
    call string_equal
    test eax, eax
    jnz .found
    mov edi, [ebx+4]
    test edi, edi
    jz .next
    mov esi, [help_lookup_ptr]
    call string_equal
    test eax, eax
    jnz .found
.next:
    add ebx, CMD_SIZE
    loop .loop
    xor eax, eax
    ret
.found:
    mov eax, 1
    ret

help_lookup_ptr dd 0

; ---- Registry text -------------------------------------------------------
c_unknown_1 db 13,10,'Unknown command ',0x27,0
c_unknown_2 db 0x27,'. Type help for the index.',13,10,0
c_did_you_mean_1 db 'Did you mean ',0x27,0
c_did_you_mean_2 db 0x27,'?',13,10,0

c_cat_1 db 'Getting started',0
c_cat_2 db 'Shell and navigation',0
c_cat_3 db 'Files and directories',0
c_cat_4 db 'Text editor',0
c_cat_5 db 'Python',0
c_cat_6 db 'Calculator',0
c_cat_7 db 'System information',0
c_cat_8 db 'Screen and session',0
c_cat_9 db 'Troubleshooting',0

c_help_name db 'help',0
c_guide_name db 'guide',0
c_identity_name db 'identity',0
c_say_name db 'say',0
c_cd_name db 'cd',0
c_where_name db 'where',0
c_pwd_name db 'pwd',0
c_peek_name db 'peek',0
c_ls_name db 'ls',0
c_mkdir_name db 'mkdir',0
c_touch_name db 'touch',0
c_forge_name db 'forge',0
c_read_name db 'read',0
c_cat_name db 'cat',0
c_scribe_name db 'scribe',0
c_grow_name db 'grow',0
c_shift_name db 'shift',0
c_mv_name db 'mv',0
c_cp_name db 'cp',0
c_rm_name db 'rm',0
c_erase_name db 'erase',0
c_df_name db 'df',0
c_edit_name db 'edit',0
c_py_name db 'py',0
c_python_name db 'python',0
c_calc_name db 'calc',0
c_mem_name db 'mem',0
c_ticks_name db 'ticks',0
c_clear_name db 'clear',0
c_wipe_name db 'wipe',0
c_reboot_name db 'reboot',0
c_fault_name db 'fault',0

c_help_syntax db 'help [category|command]',0
c_help_text db 'Purpose: show the indexed manual. No argument shows the category index.',0
c_help_example db 'help files',13,10,'Result: lists the file commands; help read shows one page.',0
c_help_errors db 'Unknown category or command prints a short usage hint.',0
c_help_limits db 'Paged output: Space next, I index, Q quit.',0

c_guide_syntax db 'guide',0
c_guide_text db 'Purpose: beginner walkthrough of the shell, files, editor, calculator and Python.',0
c_guide_example db 'guide',13,10,'Result: paged tutorial from first boot onward.',0
c_guide_errors db 'None.',0
c_guide_limits db 'Static tutorial; use help for exact syntax.',0

c_identity_syntax db 'identity',0
c_identity_text_doc db 'Purpose: print the kernel name and execution mode. No arguments.',0
c_identity_example db 'identity',13,10,'Result: Aarambh OS PM32, 32-bit BIOS protected-mode kernel.',0
c_identity_errors db 'None.',0
c_identity_limits db 'No CPU, disk or device inventory.',0

c_say_syntax db 'say <text>',0
c_say_text db 'Purpose: echo text to the terminal. <text> is required and may contain spaces.',0
c_say_example db 'say hello Aarambh',13,10,'Result: hello Aarambh',0
c_say_errors db 'Missing text prints the usage line.',0
c_say_limits db '126 characters maximum; no escapes.',0

c_cd_syntax db 'cd <path>',0
c_cd_text db 'Purpose: change the current AarFS directory. <path> is required; /, . and .. work.',0
c_cd_example db 'cd work',13,10,'Result: pwd changes to /work.',0
c_cd_errors db 'Directory not found; not a directory; invalid path.',0
c_cd_limits db 'Current AarFS disk only; depth is bounded.',0

c_pwd_syntax db 'pwd',0
c_pwd_text db 'Purpose: print the absolute current directory. No arguments.',0
c_pwd_example db 'pwd',13,10,'Result: /work',0
c_pwd_errors db 'Mount or invalid-parent error.',0
c_pwd_limits db 'Alias: where.',0

c_ls_syntax db 'ls',0
c_ls_text db 'Purpose: list entries in the current directory. Directories end with /. No arguments.',0
c_ls_example db 'ls',13,10,'Result: notes.txt then work/',0
c_ls_errors db 'Mount error.',0
c_ls_limits db 'Alias: peek. Unsorted; no wildcards or long format.',0

c_mkdir_syntax db 'mkdir <name>',0
c_mkdir_text db 'Purpose: create a directory here. <name> is required.',0
c_mkdir_example db 'mkdir work',13,10,'Result: Directory created.',0
c_mkdir_errors db 'Invalid name; already exists; table full; disk error.',0
c_mkdir_limits db '39 characters; no slash; persistent change.',0

c_touch_syntax db 'touch <file>',0
c_touch_text db 'Purpose: create an empty file, or do nothing if it exists. <file> is required.',0
c_touch_example db 'touch notes.txt',13,10,'Result: File created.',0
c_touch_errors db 'Invalid name; entry is a directory; table full; disk error.',0
c_touch_limits db 'Alias: forge. Persistent change.',0

c_read_syntax db 'read <file>',0
c_read_text db 'Purpose: print a file stored in AarFS. <file> is required.',0
c_read_example db 'read notes.txt',13,10,'Result: prints the file text and a newline.',0
c_read_errors db 'File not found; entry is a directory; disk read error.',0
c_read_limits db 'Alias: cat. Current directory only; 16 KiB; no pager.',0

c_scribe_syntax db 'scribe <file> <text>',0
c_scribe_text db 'Purpose: replace a file with the rest of the line, creating it if needed. Both arguments required.',0
c_scribe_example db 'scribe notes.txt hello world',13,10,'Result: File saved.',0
c_scribe_errors db 'Invalid name; entry is a directory; disk error.',0
c_scribe_limits db 'One line only; 16 KiB file limit; persistent change.',0

c_grow_syntax db 'grow <file> <text>',0
c_grow_text db 'Purpose: append text to an existing file. Both arguments required.',0
c_grow_example db 'grow notes.txt  more',13,10,'Result: File appended.',0
c_grow_errors db 'File not found; not a file; size limit; disk error.',0
c_grow_limits db '16 KiB maximum; text stays on the same line; persistent change.',0

c_shift_syntax db 'shift <old> <new>',0
c_shift_text db 'Purpose: rename a file or directory. Both names required; <new> must not exist.',0
c_shift_example db 'shift old.txt new.txt',13,10,'Result: Entry renamed.',0
c_shift_errors db 'Source missing; destination exists; invalid name.',0
c_shift_limits db 'Alias: mv. Current directory only; persistent change.',0

c_cp_syntax db 'cp <src> <dst>',0
c_cp_text db 'Purpose: copy a file to a new name here. Both names required.',0
c_cp_example db 'cp notes.txt backup.txt',13,10,'Result: File copied.',0
c_cp_errors db 'Source missing; not a file; destination exists; disk error.',0
c_cp_limits db 'Files only; 16 KiB maximum; persistent change.',0

c_rm_syntax db 'rm <name>',0
c_rm_text db 'Purpose: delete a file or an empty directory. <name> is required.',0
c_rm_example db 'rm old.txt',13,10,'Result: Entry deleted.',0
c_rm_errors db 'Entry missing; directory not empty; disk error.',0
c_rm_limits db 'Alias: erase. Persistent; no undo.',0

c_df_syntax db 'df',0
c_df_text db 'Purpose: show logical AarFS capacity, used and free slots. No arguments.',0
c_df_example db 'df',13,10,'Result: capacity, used and free byte counts.',0
c_df_errors db 'Mount error.',0
c_df_limits db 'Counts fixed file slots, not physical free clusters.',0

c_edit_syntax db 'edit <file>',0
c_edit_text db 'Purpose: open the full-screen editor for an existing file or a new name. <file> required.',0
c_edit_example db 'edit notes.txt',13,10,'Result: editor opens; Ctrl-S saves and Esc exits.',0
c_edit_errors db 'Invalid name; entry is a directory; read, save or disk error.',0
c_edit_limits db '16 KiB max; current directory; arrows, Home/End, Delete, PgUp/PgDn, F1.',0

c_py_syntax db 'py <file.py> | py --version | py help',0
c_py_text db 'Purpose: run an AarFS .py file with the real Windows Python helper over COM2.',0
c_py_example db 'py hello.py',13,10,'Result: Python stdout/stderr and exit status shown in the terminal.',0
c_py_errors db 'Bad name; missing file; helper unavailable; timeout; protocol error.',0
c_py_limits db 'Host-assisted, not native. 16 KiB script, 8 KiB output, 15 s timeout.',0

c_python_syntax db 'python <file.py>',0
c_python_text db 'Purpose: Execute a Python script natively inside Aarambh OS using MicroPython.',0
c_python_example db 'python hello.py',13,10,'Result: prints script output natively in Aarambh OS.',0
c_python_errors db 'Bad name; missing file; disk read error; unhandled exception.',0
c_python_limits db 'Native in-guest MicroPython. 16 KiB script limit. No Windows helper needed.',0

c_calc_syntax db 'calc [expression|host|help]',0
c_calc_text db 'Purpose: built-in integer calculator. No argument opens calc>; host asks the helper for Windows Calculator.',0
c_calc_example db 'calc (12 + 8) * 3',13,10,'Result: 60',0
c_calc_errors db 'Syntax error; division by zero; integer overflow.',0
c_calc_limits db 'Signed 32-bit integers, + - * / % and parentheses; no floats.',0

c_mem_syntax db 'mem',0
c_mem_text db 'Purpose: show BIOS conventional memory and bytes left in the bump heap.',0
c_mem_example db 'mem',13,10,'Result: two labelled numbers.',0
c_mem_errors db 'None.',0
c_mem_limits db 'The bump heap has no free operation yet.',0

c_ticks_syntax db 'ticks',0
c_ticks_text db 'Purpose: show PIT timer ticks since boot. No arguments.',0
c_ticks_example db 'ticks',13,10,'Result: a decimal tick count.',0
c_ticks_errors db 'None.',0
c_ticks_limits db 'Approximately 1 kHz and wraps at 32 bits.',0

c_clear_syntax db 'clear',0
c_clear_text db 'Purpose: clear the VGA text screen. No arguments.',0
c_clear_example db 'clear',13,10,'Result: blank text screen with the prompt at the top.',0
c_clear_errors db 'None.',0
c_clear_limits db 'Alias: wipe. Does not erase the COM1 log or files.',0

c_reboot_syntax db 'reboot',0
c_reboot_text_doc db 'Purpose: reboot the virtual machine through the keyboard controller.',0
c_reboot_example db 'reboot',13,10,'Result: Aarambh boots again from the virtual disk.',0
c_reboot_errors db 'None.',0
c_reboot_limits db 'Unsaved editor changes are lost.',0

c_fault_syntax db 'fault',0
c_fault_text db 'Purpose: trigger a breakpoint to verify the panic handler. No arguments.',0
c_fault_example db 'fault',13,10,'Result: panic message; the VM halts until reboot.',0
c_fault_errors db 'None.',0
c_fault_limits db 'Diagnostic only; unsaved work is lost.',0
