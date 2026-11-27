; ============================================================================
; raster.inc — Pure x86-64 Software ASCII Pixel Rasterizer v3.0.1
;
; Features:
;   - Non-blinking fault flags (!TMP / !VRM / !RAM) on the taskbar strip
;   - HUD_MAX_W = 944 (58 chars) for FULL mode + fault flag
; ============================================================================

HUD_MAX_W       = 944
HUD_MAX_H       = 28
HUD_1X_H        = 16

; ----------------------------------------------------------------------------
; fmt_pct3 / fmt_num3 / fmt_temp4 / fmt_io4 (all preserve rbx!)
; ----------------------------------------------------------------------------
fmt_pct3:
    cmp     eax, 100
    jb      .lt100
    mov     byte [rdi + 0], '1'
    mov     byte [rdi + 1], '0'
    mov     byte [rdi + 2], '0'
    mov     byte [rdi + 3], '%'
    add     rdi, 4
    ret
.lt100:
    mov     byte [rdi + 0], ' '
    cmp     eax, 10
    jb      .lt10
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    add     al, '0'
    add     dl, '0'
    mov     byte [rdi + 1], al
    mov     byte [rdi + 2], dl
    mov     byte [rdi + 3], '%'
    add     rdi, 4
    ret
.lt10:
    mov     byte [rdi + 1], ' '
    add     al, '0'
    mov     byte [rdi + 2], al
    mov     byte [rdi + 3], '%'
    add     rdi, 4
    ret

fmt_num3:
    cmp     eax, 999
    jbe     .ok
    mov     eax, 999
.ok:
    mov     ecx, 10
    xor     edx, edx
    div     ecx
    add     dl, '0'
    mov     byte [rdi + 2], dl

    test    eax, eax
    jz      .pad_two
    xor     edx, edx
    div     ecx
    add     dl, '0'
    mov     byte [rdi + 1], dl
    test    eax, eax
    jz      .pad_one
    add     al, '0'
    mov     byte [rdi + 0], al
    jmp     .put_unit
.pad_two:
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], ' '
    jmp     .put_unit
.pad_one:
    mov     byte [rdi + 0], ' '
.put_unit:
    mov     byte [rdi + 3], bl
    add     rdi, 4
    ret

fmt_temp4:
    test    eax, eax
    jnz     .live
    mov     dword [rdi], ' --C'
    add     rdi, 4
    ret
.live:
    push    rbx
    mov     bl, 'C'
    call    fmt_num3
    pop     rbx
    ret

fmt_io4:
    push    rbx
    mov     rax, qword [g_io_bps]
    mov     rcx, 1048576
    cmp     rax, rcx
    jae     .check_mb
    shr     rax, 10
    cmp     eax, 999
    jbe     .do_kb
    mov     eax, 999
.do_kb:
    mov     bl, 'K'
    call    fmt_num3
    pop     rbx
    ret

.check_mb:
    mov     rcx, 1048576000
    cmp     rax, rcx
    jae     .do_gb
    shr     rax, 20
    mov     bl, 'M'
    call    fmt_num3
    pop     rbx
    ret

.do_gb:
    mov     rcx, 10
    mul     rcx
    add     rax, 536870912
    adc     rdx, 0
    shrd    rax, rdx, 30
    cmp     rax, 99
    jbe     .gb_fmt
    mov     byte [rdi + 0], '9'
    mov     byte [rdi + 1], '.'
    mov     byte [rdi + 2], '9'
    mov     byte [rdi + 3], 'G'
    add     rdi, 4
    pop     rbx
    ret
.gb_fmt:
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    add     al, '0'
    add     dl, '0'
    mov     byte [rdi + 0], al
    mov     byte [rdi + 1], '.'
    mov     byte [rdi + 2], dl
    mov     byte [rdi + 3], 'G'
    add     rdi, 4
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; Branchless 6-Theme Color Lookups (all preserve ecx!)
; ----------------------------------------------------------------------------
pick_metric_color:
    mov     edx, dword [g_palette]
    cmp     eax, dword [g_heat_hot]
    jae     .hot
    cmp     eax, dword [g_heat_warn]
    jae     .warm
    mov     eax, dword [theme_cool_col + rdx*4]
    ret
.warm:
    mov     eax, dword [theme_warm_col + rdx*4]
    ret
.hot:
    mov     eax, dword [theme_hot_col + rdx*4]
    ret

pick_label_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_label_col + rax*4]
    ret

pick_outline_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_out_col + rax*4]
    ret

pick_pill_bg:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_pill_bg + rax*4]
    ret

pick_pill_border:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_pill_bdr + rax*4]
    ret

fill_col_run:
    lea     rdx, [g_char_colors + r8*4]
.lp:
    mov     dword [rdx], eax
    add     rdx, 4
    inc     r8
    dec     ecx
    jnz     .lp
    ret

map_temp_heat:
    cmp     eax, 85
    jae     .hot
    cmp     eax, 72
    jae     .warm
    xor     eax, eax
    ret
.hot:
    mov     eax, 95
    ret
.warm:
    mov     eax, 75
    ret

; ----------------------------------------------------------------------------
; raster_build_text (with Master Caution !TMP / !VRM / !RAM annunciator!)
; ----------------------------------------------------------------------------
raster_build_text:
    push    rbx
    push    rdi
    push    r12

    lea     rdi, [g_text_buf]
    xor     r8d, r8d
    mov     r12d, dword [g_bar_mode]

    ; CPU
    mov     dword [rdi], 'CPU:'
    add     rdi, 4
    mov     eax, dword [g_cpu_pct]
    call    fmt_pct3
    mov     byte [rdi], ' '
    inc     rdi
    call    pick_label_color
    mov     ecx, 4
    call    fill_col_run
    mov     eax, dword [g_cpu_pct]
    call    pick_metric_color
    mov     ecx, 5
    call    fill_col_run

    ; RAM
    mov     dword [rdi], 'RAM:'
    add     rdi, 4
    mov     eax, dword [g_ram_pct]
    call    fmt_pct3
    mov     byte [rdi], ' '
    inc     rdi
    call    pick_label_color
    mov     ecx, 4
    call    fill_col_run
    mov     eax, dword [g_ram_pct]
    call    pick_metric_color
    mov     ecx, 5
    call    fill_col_run

    ; GPU
    mov     dword [rdi], 'GPU:'
    add     rdi, 4
    mov     eax, dword [g_gpu_pct]
    call    fmt_pct3
    call    pick_label_color
    mov     ecx, 4
    call    fill_col_run
    mov     eax, dword [g_gpu_pct]
    call    pick_metric_color
    mov     ecx, 4
    call    fill_col_run

    cmp     r12d, 2
    je      .chk_caution

    mov     byte [rdi], ' '
    inc     rdi
    mov     ecx, 1
    call    fill_col_run

    test    r12d, 1
    jz      .chk_vram
    mov     dword [rdi], 'TMP:'
    add     rdi, 4
    mov     eax, dword [g_gpu_temp]
    call    fmt_temp4
    mov     byte [rdi], ' '
    inc     rdi
    call    pick_label_color
    mov     ecx, 4
    call    fill_col_run
    mov     eax, dword [g_gpu_temp]
    call    map_temp_heat
    call    pick_metric_color
    mov     ecx, 5
    call    fill_col_run

.chk_vram:
    cmp     r12d, 1
    je      .append_io
    mov     dword [rdi], 'VRM:'
    add     rdi, 4
    mov     eax, dword [g_vram_pct]
    call    fmt_pct3
    mov     byte [rdi], ' '
    inc     rdi
    call    pick_label_color
    mov     ecx, 4
    call    fill_col_run
    mov     eax, dword [g_vram_pct]
    call    pick_metric_color
    mov     ecx, 5
    call    fill_col_run

.append_io:
    mov     byte [rdi + 0], 'I'
    mov     byte [rdi + 1], 'O'
    mov     byte [rdi + 2], ':'
    add     rdi, 3
    call    fmt_io4
    call    pick_label_color
    mov     ecx, 3
    call    fill_col_run
    mov     rdx, qword [g_io_bps]
    xor     eax, eax
    cmp     rdx, 262144000
    jae     .io_hot
    cmp     rdx, 52428800
    jae     .io_warm
    jmp     .io_col
.io_hot:
    mov     eax, 95
    jmp     .io_col
.io_warm:
    mov     eax, 75
.io_col:
    call    pick_metric_color
    mov     ecx, 4
    call    fill_col_run

.chk_caution:
    ; Master Caution Annunciator: ONLY appears when a real hardware fault trips!
    cmp     dword [g_caution_en], 0
    je      .finish
    mov     eax, dword [g_caution_flags]
    test    eax, eax
    jz      .finish
    mov     byte [rdi], ' '
    inc     rdi
    test    eax, FAULT_TMP
    jnz     .c_tmp
    test    eax, FAULT_VRM
    jnz     .c_vrm
    mov     dword [rdi], '!RAM'
    jmp     .c_col
.c_tmp:
    mov     dword [rdi], '!TMP'
    jmp     .c_col
.c_vrm:
    mov     dword [rdi], '!VRM'
.c_col:
    add     rdi, 4
    mov     eax, 0xFFFF453A     ; steady solid aviation caution red (ZERO blinking!)
    mov     ecx, 5
    call    fill_col_run

.finish:
    mov     byte [rdi], 0
    lea     rax, [g_text_buf]
    sub     rdi, rax
    mov     dword [g_bar_chars], edi

    pop     r12
    pop     rdi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; raster_bar_sig: 64-bit signature (includes caution pulse bit when faulting!)
; ----------------------------------------------------------------------------
raster_bar_sig:
    mov     rax, 0xCBF29CE484222325
    mov     r10, 0x00000100000001B3

    mov     edx, dword [g_scale]
    shl     edx, 4
    or      edx, dword [g_backdrop]
    shl     edx, 4
    or      edx, dword [g_palette]
    shl     edx, 8
    or      edx, dword [g_bar_chars]
    xor     rax, rdx
    imul    rax, r10

    cmp     dword [g_caution_en], 0
    je      .loop_go
    mov     edx, dword [g_caution_flags]
    test    edx, edx
    jz      .loop_go
    xor     rax, rdx
    imul    rax, r10

.loop_go:
    xor     ecx, ecx
    mov     r8d, dword [g_bar_chars]
.lp:
    cmp     ecx, r8d
    jae     .done
    movzx   edx, byte [g_text_buf + rcx]
    xor     rax, rdx
    imul    rax, r10
    mov     edx, dword [g_char_colors + rcx*4]
    xor     rax, rdx
    imul    rax, r10
    inc     ecx
    jmp     .lp
.done:
    ret

; ----------------------------------------------------------------------------
; raster_draw_pass
; ----------------------------------------------------------------------------
raster_draw_pass:
    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14

    mov     ebx, dword [g_scale]
    cmp     ebx, 2
    je      .pad2x
    mov     r12d, 6
    mov     r13d, 4
    jmp     .pad_ok
.pad2x:
    mov     r12d, 8
    mov     r13d, 6
.pad_ok:

    test    r15d, r15d
    jnz     .no_out_hoist
    call    pick_outline_color
    mov     ebp, eax
.no_out_hoist:

    mov     rsi, qword [g_dib_bits]
    xor     r14d, r14d

.char_loop:
    cmp     r14d, dword [g_bar_chars]
    jae     .done

    movzx   eax, byte [g_text_buf + r14]
    cmp     eax, 0x20
    jbe     .next_char
    cmp     eax, 0x7F
    ja      .next_char
    sub     eax, 0x20
    shl     eax, 3
    lea     rdi, [font8x8_table + rax]

    test    r15d, r15d
    jz      .col_ready
    mov     ebp, dword [g_char_colors + r14*4]
.col_ready:

    mov     eax, r14d
    shl     eax, 3
    imul    eax, ebx
    add     eax, r12d
    mov     r8d, eax

    xor     r9d, r9d
.row_loop:
    cmp     r9d, 8
    jae     .next_char
    movzx   edx, byte [rdi + r9]
    test    edx, edx
    jz      .next_row

    xor     r10d, r10d
.col_loop:
    cmp     r10d, 8
    jae     .next_row
    mov     ecx, 7
    sub     ecx, r10d
    bt      edx, ecx
    jnc     .next_col

    mov     eax, r10d
    imul    eax, ebx
    add     eax, r8d
    mov     ecx, r9d
    imul    ecx, ebx
    add     ecx, r13d

    imul    ecx, HUD_MAX_W
    add     ecx, eax
    lea     r11, [rsi + rcx*4]

    test    r15d, r15d
    jz      .stamp_outline

    cmp     ebx, 2
    je      .fg_2x
    mov     dword [r11], ebp
    jmp     .next_col
.fg_2x:
    mov     dword [r11], ebp
    mov     dword [r11 + 4], ebp
    mov     dword [r11 + HUD_MAX_W*4], ebp
    mov     dword [r11 + HUD_MAX_W*4 + 4], ebp
    jmp     .next_col

.stamp_outline:
    cmp     ebx, 2
    je      .out_2x
    mov     dword [r11 - HUD_MAX_W*4 - 4], ebp
    mov     dword [r11 - HUD_MAX_W*4 + 0], ebp
    mov     dword [r11 - HUD_MAX_W*4 + 4], ebp
    mov     dword [r11 - 4], ebp
    mov     dword [r11 + 0], ebp
    mov     dword [r11 + 4], ebp
    mov     dword [r11 + HUD_MAX_W*4 - 4], ebp
    mov     dword [r11 + HUD_MAX_W*4 + 0], ebp
    mov     dword [r11 + HUD_MAX_W*4 + 4], ebp
    jmp     .next_col

.out_2x:
    mov     dword [r11 - HUD_MAX_W*4 - 4], ebp
    mov     dword [r11 - HUD_MAX_W*4 + 0], ebp
    mov     dword [r11 - HUD_MAX_W*4 + 4], ebp
    mov     dword [r11 - HUD_MAX_W*4 + 8], ebp
    mov     dword [r11 - 4], ebp
    mov     dword [r11 + 8], ebp
    mov     dword [r11 + HUD_MAX_W*4 - 4], ebp
    mov     dword [r11 + HUD_MAX_W*4 + 8], ebp
    mov     dword [r11 + HUD_MAX_W*8 - 4], ebp
    mov     dword [r11 + HUD_MAX_W*8 + 0], ebp
    mov     dword [r11 + HUD_MAX_W*8 + 4], ebp
    mov     dword [r11 + HUD_MAX_W*8 + 8], ebp

.next_col:
    inc     r10d
    jmp     .col_loop
.next_row:
    inc     r9d
    jmp     .row_loop
.next_char:
    inc     r14d
    jmp     .char_loop

.done:
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; raster_render: returns eax = 1 if pixels updated, 0 if unchanged
; ----------------------------------------------------------------------------
raster_render:
    push    rbx
    push    rbp
    push    rdi
    push    r12
    push    r13
    push    r15

    mov     ebp, ecx
    call    raster_build_text
    call    raster_bar_sig
    test    ebp, ebp
    jnz     .do_raster
    cmp     rax, qword [g_last_bar_sig]
    jne     .do_raster
    xor     eax, eax
    jmp     .ret_done

.do_raster:
    mov     qword [g_last_bar_sig], rax

    mov     eax, dword [g_bar_chars]
    cmp     dword [g_scale], 2
    je      .sz2x
    shl     eax, 3
    add     eax, 12
    cmp     eax, dword [g_hud_w]
    je      .h1_set
    mov     dword [g_pos_dirty], 1
.h1_set:
    mov     dword [g_hud_w], eax
    mov     dword [g_hud_h], HUD_1X_H
    jmp     .sz_done
.sz2x:
    shl     eax, 4
    add     eax, 16
    cmp     eax, HUD_MAX_W
    jbe     .w_ok
    mov     eax, HUD_MAX_W
.w_ok:
    cmp     eax, dword [g_hud_w]
    je      .h2_set
    mov     dword [g_pos_dirty], 1
.h2_set:
    mov     dword [g_hud_w], eax
    mov     dword [g_hud_h], HUD_MAX_H
.sz_done:

    mov     rdi, qword [g_dib_bits]
    xor     eax, eax
    mov     ecx, dword [g_hud_h]
    imul    ecx, HUD_MAX_W
    rep     stosd

    cmp     dword [g_backdrop], 0
    je      .draw_glyphs

    call    pick_pill_bg
    mov     r12d, eax
    call    pick_pill_border
    mov     r13d, eax

    mov     r8d, dword [g_hud_w]
    mov     r9d, dword [g_hud_h]
    mov     rbx, qword [g_dib_bits]
    xor     r10d, r10d
.bg_row:
    cmp     r10d, r9d
    jae     .draw_glyphs

    mov     eax, r10d
    imul    eax, HUD_MAX_W
    lea     rdi, [rbx + rax*4]

    test    r10d, r10d
    jz      .border_row
    lea     eax, [r9 - 1]
    cmp     r10d, eax
    je      .border_row

    mov     dword [rdi], r13d
    add     rdi, 4
    mov     eax, r12d
    lea     ecx, [r8 - 2]
    rep     stosd
    mov     dword [rdi], r13d
    jmp     .bg_next

.border_row:
    add     rdi, 4
    mov     eax, r13d
    lea     ecx, [r8 - 2]
    rep     stosd

.bg_next:
    inc     r10d
    jmp     .bg_row

.draw_glyphs:
    xor     r15d, r15d
    call    raster_draw_pass
    mov     r15d, 1
    call    raster_draw_pass
    mov     eax, 1

.ret_done:
    pop     r15
    pop     r13
    pop     r12
    pop     rdi
    pop     rbp
    pop     rbx
    ret
