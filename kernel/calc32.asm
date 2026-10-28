; Aarambh PM32 built-in integer calculator and Windows Calculator launcher.
; Grammar: expr := term (('+'|'-') term)*
;          term := unary (('*'|'/'|'%') unary)*
;          unary := ('+'|'-') unary | primary
;          primary := number | '(' expr ')'
; Integers are signed 32-bit. No host or Python evaluation is used here.

calc_command:
    mov esi, [command_args]
.skip:
    cmp byte [esi], ' '
    jne .start
    inc esi
    jmp .skip
.start:
    cmp byte [esi], 0
    je .no_args
    mov edi, calc_help_word
    call string_equal
    test eax, eax
    jnz .help
    mov esi, [command_args]
    mov edi, calc_host_word
    call string_equal
    test eax, eax
    jnz .host
    call calc_expression
    ret
.no_args:
    call bridge_launch_calc
    test eax, eax
    jnz .launched
    mov esi, calc_fallback_text
    call console_write
    jmp calc_repl
.launched:
    mov esi, calc_launched_text
    call console_write
    ret
.host:
    call bridge_launch_calc
    test eax, eax
    jnz .launched
    mov esi, calc_host_fail_text
    call console_write
    ret
.help:
    mov esi, calc_help_text
    call console_write
    ret

calc_expression:
    mov [calc_start], esi
    mov [calc_ptr], esi
    call calc_expr
    jc calc_print_error
    mov esi, [calc_ptr]
    call calc_skip
    cmp byte [esi], 0
    jne .syntax
    call calc_print_value
    ret
.syntax:
    mov byte [calc_err], 1
    jmp calc_print_error

calc_repl:
    mov esi, calc_banner
    call console_write
.loop:
    mov byte [vga_attr], 0x0B
    mov esi, calc_prompt
    call console_write
    mov byte [vga_attr], 0x07
    call read_line
    mov esi, command_buffer
    call calc_skip_buffer
    cmp byte [esi], 0
    je .loop
    mov edi, calc_q_word
    call string_equal
    test eax, eax
    jnz .done
    mov esi, command_buffer
    call calc_skip_buffer
    mov edi, calc_quit_word
    call string_equal
    test eax, eax
    jnz .done
    mov esi, command_buffer
    call calc_skip_buffer
    mov edi, calc_exit_word
    call string_equal
    test eax, eax
    jnz .done
    mov esi, command_buffer
    call calc_skip_buffer
    call calc_expression
    jmp .loop
.done:
    mov esi, calc_bye_text
    call console_write
    ret

calc_skip_buffer:
    cmp byte [esi], ' '
    jne .done
    inc esi
    jmp calc_skip_buffer
.done:
    ret

; Skip spaces at [calc_ptr].
calc_skip:
    mov esi, [calc_ptr]
.loop:
    cmp byte [esi], ' '
    jne .done
    inc esi
    jmp .loop
.done:
    mov [calc_ptr], esi
    ret

calc_expr:
    call calc_term
    jc .bad
    push eax
.loop:
    call calc_skip
    mov esi, [calc_ptr]
    mov al, [esi]
    cmp al, '+'
    je .add
    cmp al, '-'
    je .sub
    pop eax
    clc
    ret
.add:
    inc esi
    mov [calc_ptr], esi
    call calc_term
    jc .bad_pop
    pop ecx
    add ecx, eax
    jo .overflow_no_pop
    push ecx
    jmp .loop
.sub:
    inc esi
    mov [calc_ptr], esi
    call calc_term
    jc .bad_pop
    pop ecx
    sub ecx, eax
    jo .overflow_no_pop
    push ecx
    jmp .loop
.bad_pop:
    pop ecx
.bad:
    stc
    ret
.overflow_pop:
    pop ecx
.overflow_no_pop:
    mov byte [calc_err], 3
    stc
    ret

calc_term:
    call calc_unary
    jc .bad
    push eax
.loop:
    call calc_skip
    mov esi, [calc_ptr]
    mov al, [esi]
    cmp al, '*'
    je .mul
    cmp al, '/'
    je .div
    cmp al, '%'
    je .mod
    pop eax
    clc
    ret
.mul:
    inc esi
    mov [calc_ptr], esi
    call calc_unary
    jc .bad_pop
    pop ecx
    imul ecx, eax
    jo .overflow_no_pop
    push ecx
    jmp .loop
.div:
    inc esi
    mov [calc_ptr], esi
    call calc_unary
    jc .bad_pop
    mov ebx, eax
    test ebx, ebx
    jz .div_zero_pop
    pop eax
    cmp ebx, -1
    jne .div_ok
    cmp eax, 0x80000000
    je .overflow_no_pop
.div_ok:
    cdq
    idiv ebx
    push eax
    jmp .loop
.mod:
    inc esi
    mov [calc_ptr], esi
    call calc_unary
    jc .bad_pop
    mov ebx, eax
    test ebx, ebx
    jz .div_zero_pop
    pop eax
    cmp ebx, -1
    jne .mod_ok
    cmp eax, 0x80000000
    je .overflow_no_pop
.mod_ok:
    cdq
    idiv ebx
    push edx
    jmp .loop
.bad_pop:
    pop ecx
.bad:
    stc
    ret
.overflow_pop:
    pop ecx
.overflow_no_pop:
    mov byte [calc_err], 3
    stc
    ret
.div_zero_pop:
    pop ecx
    mov byte [calc_err], 2
    stc
    ret

calc_unary:
    call calc_skip
    mov esi, [calc_ptr]
    mov al, [esi]
    cmp al, '+'
    je .plus
    cmp al, '-'
    je .minus
    jmp calc_primary
.plus:
    inc esi
    mov [calc_ptr], esi
    jmp calc_unary
.minus:
    inc esi
    mov [calc_ptr], esi
    call calc_unary
    jc .bad
    neg eax
    jo .overflow
    clc
    ret
.bad:
    stc
    ret
.overflow:
    mov byte [calc_err], 3
    stc
    ret

calc_primary:
    call calc_skip
    mov esi, [calc_ptr]
    mov al, [esi]
    cmp al, '('
    je .paren
    cmp al, '0'
    jb .syntax
    cmp al, '9'
    ja .syntax
    xor eax, eax
.loop:
    mov esi, [calc_ptr]
    movzx ecx, byte [esi]
    cmp cl, '0'
    jb .done
    cmp cl, '9'
    ja .done
    cmp eax, 214748364
    ja .overflow
    imul eax, eax, 10
    jo .overflow
    sub ecx, '0'
    add eax, ecx
    jo .overflow
    inc esi
    mov [calc_ptr], esi
    jmp .loop
.done:
    clc
    ret
.paren:
    inc esi
    mov [calc_ptr], esi
    call calc_expr
    jc .bad
    call calc_skip
    mov esi, [calc_ptr]
    cmp byte [esi], ')'
    jne .syntax
    inc esi
    mov [calc_ptr], esi
    clc
    ret
.syntax:
    mov byte [calc_err], 1
.bad:
    stc
    ret
.overflow:
    mov byte [calc_err], 3
    stc
    ret

calc_print_value:
    test eax, eax
    jns .positive
    push eax
    mov al, '-'
    call console_putc
    pop eax
    neg eax
.positive:
    call print_u32
    call console_newline
    ret

calc_print_error:
    cmp byte [calc_err], 2
    je .div_zero
    cmp byte [calc_err], 3
    je .overflow
    mov byte [vga_attr], 0x0C
    mov esi, calc_syntax_text
    call console_write
    mov eax, [calc_ptr]
    sub eax, [calc_start]
    inc eax
    call print_u32
    mov esi, calc_column_text
    call console_write
    mov byte [vga_attr], 0x07
    ret
.div_zero:
    mov byte [vga_attr], 0x0C
    mov esi, calc_divzero_text
    call console_write
    mov byte [vga_attr], 0x07
    ret
.overflow:
    mov byte [vga_attr], 0x0C
    mov esi, calc_overflow_text
    call console_write
    mov byte [vga_attr], 0x07
    ret

calc_help_word db 'help',0
calc_host_word db 'host',0
calc_q_word db 'q',0
calc_quit_word db 'quit',0
calc_exit_word db 'exit',0
calc_help_text db 'Usage: calc [expression | host | help]',13,10
                db 'Built-in signed 32-bit calculator: + - * / % and parentheses.',13,10
                db 'calc              try the Windows helper, else open calc>',13,10
                db 'calc 12 + 8       prints 20',13,10
                db 'calc (12 + 8) * 3 prints 60',13,10
                db 'calc host         ask the helper to open Windows Calculator',13,10
                db 'In calc>, type q to return to the shell.',13,10,0
calc_banner db 'Aarambh calc - integer expressions; type q to return.',13,10,0
calc_prompt db 'calc> ',0
calc_bye_text db 'calc: returned to shell.',13,10,0
calc_fallback_text db 'calc: host helper unavailable; starting built-in calculator.',13,10,0
calc_launched_text db 'calc: Windows Calculator launch confirmed by the host helper.',13,10,0
calc_host_fail_text db 'calc: host helper unavailable or refused the launch. Use calc 12 + 8.',13,10,0
calc_syntax_text db 'calc: syntax error at column ',0
calc_column_text db '.',13,10,0
calc_divzero_text db 'calc: division by zero.',13,10,0
calc_overflow_text db 'calc: integer overflow (signed 32-bit).',13,10,0

calc_start dd 0
calc_ptr dd 0
calc_err db 0
