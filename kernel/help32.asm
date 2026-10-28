; Indexed, paged, colour help and beginner guide for Aarambh PM32.
; All command documentation comes from the central registry in commands32.asm.

%define HELP_LINES_PER_PAGE 22

; ---- Pager --------------------------------------------------------------
pager_init:
    mov dword [pager_lines], 0
    mov dword [pager_col], 0
    mov byte [pager_abort], 0
    mov byte [pager_index], 0
    mov byte [vga_attr], 0x07
    ret

pager_puts:
    lodsb
    test al, al
    jz .done
    call pager_putc
    cmp byte [pager_abort], 0
    jne .done
    jmp pager_puts
.done:
    ret

pager_putc:
    cmp byte [pager_abort], 0
    jne .ret
    cmp al, 13
    je .cr
    cmp al, 10
    je .lf
    call console_putc
    inc dword [pager_col]
    cmp dword [pager_col], 80
    jb .ret
    mov dword [pager_col], 0
    inc dword [pager_lines]
    cmp dword [pager_lines], HELP_LINES_PER_PAGE
    jae .more
.ret:
    ret
.cr:
    mov dword [pager_col], 0
    call console_putc
    ret
.lf:
    mov dword [pager_col], 0
    call console_putc
    inc dword [pager_lines]
    cmp dword [pager_lines], HELP_LINES_PER_PAGE
    jb .ret
.more:
    call pager_more
    ret

pager_more:
    push eax
    push esi
    mov byte [vga_attr], 0x70
    mov esi, pager_more_text
    call console_write
    mov byte [vga_attr], 0x07
    call get_key
    cmp al, 13
    je .cont
    cmp al, 32
    je .cont
    cmp al, 'q'
    je .quit
    cmp al, 'Q'
    je .quit
    cmp al, 'i'
    je .index
    cmp al, 'I'
    je .index
    cmp al, 27
    je .quit
.cont:
    mov esi, help_crlf
    call console_write
    mov dword [pager_lines], 0
    mov dword [pager_col], 0
    pop esi
    pop eax
    ret
.quit:
    mov byte [pager_abort], 1
    jmp .cont
.index:
    mov byte [pager_abort], 1
    mov byte [pager_index], 1
    jmp .cont

; ESI=label, EDI=value. Label is yellow, value light grey.
help_field:
    push edi
    mov byte [vga_attr], 0x0E
    call pager_puts
    pop esi
    mov byte [vga_attr], 0x07
    call pager_puts
    mov esi, help_crlf
    call pager_puts
    ret

; ---- help command -------------------------------------------------------
help_command:
    mov esi, [command_args]
.skip:
    cmp byte [esi], ' '
    jne .start
    inc esi
    jmp .skip
.start:
    cmp byte [esi], 0
    je .index
    mov edi, help_word
    xor ecx, ecx
.copy:
    mov al, [esi]
    test al, al
    jz .copied
    cmp al, ' '
    je .copied
    cmp ecx, 22
    jae .copied
    mov [edi], al
    inc edi
    inc esi
    inc ecx
    jmp .copy
.copied:
    mov byte [edi], 0
    mov esi, help_word
    call help_find_entry
    test eax, eax
    jnz .entry
    mov esi, help_word
    movzx eax, byte [esi]
    sub eax, '0'
    cmp eax, 0
    je .index
    cmp eax, 1
    jb .keyword
    cmp eax, 9
    ja .keyword
    cmp byte [help_word+1], 0
    jne .keyword
    mov [help_cat], eax
    call help_print_category
    jmp .after
.keyword:
    mov esi, help_word
    call help_find_category_word
    test eax, eax
    jz .unknown
    mov [help_cat], eax
    call help_print_category
    jmp .after
.entry:
    call help_print_entry
    jmp .after
.index:
    call help_index_body
.after:
    cmp byte [pager_index], 0
    je .done
    mov byte [pager_index], 0
    call help_index_body
    jmp .done
.unknown:
    mov byte [vga_attr], 0x0C
    mov esi, help_unknown_text
    call console_write
.done:
    mov byte [vga_attr], 0x07
    ret

; ESI=word; returns category number 1..9 or 0.
help_find_category_word:
    mov [help_word_ptr], esi
    mov ebx, help_category_words
    mov ecx, 9
    mov edx, 1
.loop:
    mov esi, [help_word_ptr]
    mov edi, [ebx]
    call string_equal
    test eax, eax
    jnz .found
    add ebx, 4
    inc edx
    loop .loop
    xor eax, eax
    ret
.found:
    mov eax, edx
    ret

help_print_entry:
    call pager_init
    mov byte [vga_attr], 0x0E
    mov esi, [ebx+16]
    call pager_puts
    mov esi, help_crlf
    call pager_puts
    mov byte [vga_attr], 0x07
    mov esi, [ebx+20]
    call pager_puts
    mov esi, help_crlf
    call pager_puts

    mov byte [vga_attr], 0x0E
    mov esi, help_example_label
    call pager_puts
    mov byte [vga_attr], 0x07
    mov esi, [ebx+24]
    call pager_puts
    mov esi, help_crlf
    call pager_puts

    mov byte [vga_attr], 0x0E
    mov esi, help_errors_label
    call pager_puts
    mov byte [vga_attr], 0x0C
    mov esi, [ebx+28]
    call pager_puts
    mov esi, help_crlf
    call pager_puts

    mov byte [vga_attr], 0x0E
    mov esi, help_changes_label
    call pager_puts
    cmp byte [ebx+13], 0
    jne .modifies
    mov byte [vga_attr], 0x0A
    mov esi, help_no
    call pager_puts
    jmp .changes_done
.modifies:
    mov byte [vga_attr], 0x0C
    mov esi, help_yes
    call pager_puts
.changes_done:
    mov esi, help_crlf
    call pager_puts

    mov byte [vga_attr], 0x0E
    mov esi, help_limits_label
    call pager_puts
    mov byte [vga_attr], 0x07
    mov esi, [ebx+32]
    call pager_puts
    mov esi, help_crlf
    call pager_puts

    mov byte [vga_attr], 0x08
    mov esi, help_page_hint
    call pager_puts
    mov byte [vga_attr], 0x07
    ret

help_index_body:
    call pager_init
    mov byte [vga_attr], 0x0B
    mov esi, help_index_title
    call pager_puts
    mov byte [vga_attr], 0x07
    mov esi, help_index_intro
    call pager_puts
    mov dword [help_cat_iter], 1
.cat_loop:
    cmp dword [help_cat_iter], 9
    ja .index_done
    mov byte [vga_attr], 0x0E
    mov esi, help_index_prefix
    call pager_puts
    mov eax, [help_cat_iter]
    call pager_print_u32
    mov esi, help_index_dot
    call pager_puts
    mov eax, [help_cat_iter]
    dec eax
    mov esi, [category_titles+eax*4]
    call pager_puts
    mov esi, help_crlf
    call pager_puts
    mov byte [vga_attr], 0x07
    mov eax, [help_cat_iter]
    mov [help_cat], eax
    mov esi, help_names_indent
    call pager_puts
    call help_list_names
    mov esi, help_crlf
    call pager_puts
    inc dword [help_cat_iter]
    jmp .cat_loop
.index_done:
    mov byte [vga_attr], 0x08
    mov esi, help_index_footer
    call pager_puts
    mov byte [vga_attr], 0x07
    ret

; Print names (and aliases) of commands in [help_cat].
help_list_names:
    mov ebx, commands
    mov ecx, COMMAND_COUNT
    mov dword [help_name_first], 0
.loop:
    mov eax, [help_cat]
    cmp byte [ebx+12], al
    jne .next
    cmp dword [help_name_first], 0
    jne .comma
    mov dword [help_name_first], 1
    jmp .print
.comma:
    mov esi, help_comma
    call pager_puts
.print:
    mov byte [vga_attr], 0x0F
    mov esi, [ebx]
    call pager_puts
    mov byte [vga_attr], 0x07
    mov esi, [ebx+4]
    test esi, esi
    jz .no_alias
    mov esi, help_alias_open
    call pager_puts
    mov esi, [ebx+4]
    call pager_puts
    mov esi, help_alias_close
    call pager_puts
.no_alias:
.next:
    add ebx, CMD_SIZE
    loop .loop
    cmp dword [help_name_first], 0
    jne .done
    mov esi, help_none
    call pager_puts
.done:
    ret

help_print_category:
    call pager_init
    mov eax, [help_cat]
    dec eax
    mov esi, [category_titles+eax*4]
    mov byte [vga_attr], 0x0B
    call pager_puts
    mov esi, help_crlf
    call pager_puts
    mov byte [vga_attr], 0x08
    mov esi, help_category_hint
    call pager_puts
    mov byte [vga_attr], 0x07
    mov ebx, commands
    mov ecx, COMMAND_COUNT
.loop:
    mov eax, [help_cat]
    cmp byte [ebx+12], al
    jne .next
    mov byte [vga_attr], 0x0E
    mov esi, help_indent
    call pager_puts
    mov esi, [ebx+16]
    call pager_puts
    mov esi, help_crlf
    call pager_puts
    mov byte [vga_attr], 0x07
    mov esi, help_details_indent
    call pager_puts
    mov esi, [ebx+20]
    call pager_puts
    mov esi, help_crlf
    call pager_puts
.next:
    add ebx, CMD_SIZE
    loop .loop
    mov byte [vga_attr], 0x08
    mov esi, help_category_footer
    call pager_puts
    mov byte [vga_attr], 0x07
    ret

pager_print_u32:
    pushad
    mov ebx, 10
    xor ecx, ecx
    test eax, eax
    jnz .convert
    mov al, '0'
    call pager_putc
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
    call pager_putc
    loop .emit
.done:
    popad
    ret

; ---- guide --------------------------------------------------------------
guide_command:
    call pager_init
    mov byte [vga_attr], 0x0B
    mov esi, guide_title
    call pager_puts
    mov byte [vga_attr], 0x07
    mov esi, guide_body
    call pager_puts
    cmp byte [pager_index], 0
    je .done
    mov byte [pager_index], 0
    call help_index_body
.done:
    mov byte [vga_attr], 0x07
    ret

; ---- data ---------------------------------------------------------------
pager_more_text db 13,10,'-- More: Space next   I index   Q quit --',0
help_crlf db 13,10,0
help_indent db '  ',0
help_names_indent db '     ',0
help_details_indent db '    ',0
help_comma db ', ',0
help_alias_open db ' (',0
help_alias_close db ')',0
help_none db '(none)',0
help_yes db 'yes',0
help_no db 'no',0
help_example_label db 'Example: ',0
help_errors_label db 'Errors: ',0
help_changes_label db 'Persistent changes: ',0
help_limits_label db 'Limits: ',0
help_page_hint db 'help index | help <category> | guide',13,10,0
help_unknown_text db 'help: no such command or category. Type help for the index.',13,10,0
help_index_title db 'AARAMBH PM32 HELP INDEX',13,10,0
help_index_intro db 'Commands are grouped by category. Type help <number>, help <category>,',13,10
                 db 'help <command>, or guide. Space pages, I returns here, Q quits.',13,10,0
help_index_prefix db '  ',0
help_index_dot db '. ',0
help_index_footer db 'Legend: <arg> required, [arg] optional. All file commands use the current',13,10
                  db 'AarFS directory unless stated. Native Python is not present.',13,10,0
help_category_hint db 'Syntax list. Use help <command> for details.',13,10,0
help_category_footer db 'Use help <command> for purpose, example, errors and limits.',13,10,0

help_category_words:
    dd help_kw_getting, help_kw_shell, help_kw_files, help_kw_editor
    dd help_kw_python, help_kw_calc, help_kw_system, help_kw_screen
    dd help_kw_trouble
help_kw_getting db 'getting',0
help_kw_shell db 'shell',0
help_kw_files db 'files',0
help_kw_editor db 'editor',0
help_kw_python db 'python',0
help_kw_calc db 'calc',0
help_kw_system db 'system',0
help_kw_screen db 'screen',0
help_kw_trouble db 'trouble',0

guide_title db 'AARAMBH OS GUIDE - FIRST STEPS',13,10,0
guide_body db '1. The prompt AR32:/ > shows the current AarFS directory.',13,10
           db '2. Type help for the index, help files for file commands, guide for this tour.',13,10
           db '3. pwd prints the path; ls lists entries (directories end with /).',13,10
           db '4. cd work enters a directory; cd .. goes up; cd / returns to the root.',13,10
           db '5. touch notes.txt creates an empty file. read notes.txt prints it.',13,10
           db '6. scribe notes.txt hello writes one line. grow appends without a newline.',13,10
           db '7. edit notes.txt opens the editor (Ctrl-S saves, Esc exits, F1 help).',13,10
           db '8. mkdir/cp/shift/rm change files and directories. df shows slot usage.',13,10
           db '9. calc evaluates arithmetic expressions. calc host requests host app.',13,10
           db '10. python <file.py> runs scripts natively via MicroPython. py uses host bridge.',13,10
           db '11. User management: useradd/del, users, groupadd/del, groups, id, whoami, su, login.',13,10
           db '12. File permissions: chmod sets mode, chown sets owner, stat displays attributes.',13,10
           db '13. mem and ticks show system state; clear/wipe clears screen; reboot restarts.',13,10
           db 13,10
           db 'Limits: files up to 16 KiB, 2048 entries. Space next, I index, Q quit.',13,10,0

pager_lines dd 0
pager_col dd 0
pager_abort db 0
pager_index db 0
help_cat dd 0
help_cat_iter dd 0
help_name_first dd 0
help_word_ptr dd 0
help_word times 24 db 0
