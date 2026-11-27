; ============================================================================
; hud_raster.inc — Pure x86-64 6-Theme Oscilloscope HUD Rasterizer v3.0.1
;
; Features:
;   - Per-process dedicated VRAM column (D3DKMT Type 9)
;   - 5-minute 1 Hz ring-buffer rewind overlay (HISTORY T-MM:SS, fault ticks)
;   - Suspended (F) & idle-priority (L) process indicators
;   - QPC/DwmFlush frame jitter readout (JIT X.Xm) in GPU column Row 7
;   - 2.5s process action status flash in footer
; ============================================================================

PANEL_W         = 760
PANEL_H         = 388

COL_X0          = 10
COL_W           = 144
COL_STRIDE      = 149
COL_TOP         = 42
COL_BOT         = 346
COL_H           = COL_BOT - COL_TOP
VISIBLE_ROWS    = 8
ROW_Y0          = 150
ROW_PITCH       = 24
CTRL_ROW_COUNT  = 12
CTRL_ROW_PITCH  = 25
SET_ROW_COUNT   = 15
SET_VIS_ROWS    = 10
SET_ROW_Y0      = 48
SET_ROW_PITCH   = 29

P_BG_KILL       = 0xEE3A0E12
P_KILL_RED      = 0xFFFF453A
P_FRZ_CYAN      = 0xFF48F4FF
P_CAUT_AMBER    = 0xFFFFCC22

; ----------------------------------------------------------------------------
; hud_get_bb_ptr: return ptr to Black-Box frame at age eax (0 = newest)
;   out: rax = ptr to 32-byte BB frame; preserves rbx, rsi, rdi, r12-r15, ecx
; ----------------------------------------------------------------------------
hud_get_bb_ptr:
    mov     edx, dword [g_bb_count]
    test    edx, edx
    jz      .zero
    dec     edx
    cmp     eax, edx
    jbe     .age_ok
    mov     eax, edx
.age_ok:
    mov     edx, dword [g_bb_head]
    dec     edx
    sub     edx, eax
.wrap:
    cmp     edx, 0
    jge     .idx_ok
    add     edx, BB_LEN
    jmp     .wrap
.idx_ok:
    shl     edx, 5              ; BB_STRIDE = 32
    lea     rax, [g_bb_ring + rdx]
    ret
.zero:
    lea     rax, [g_bb_ring]
    ret

; ----------------------------------------------------------------------------
; Theme Color Accessors (all preserve ecx!)
; ----------------------------------------------------------------------------
scale_premul_alpha:
    mov     r8d, 235
    cmp     dword [g_opacity_idx], 0
    je      .a75
    cmp     dword [g_opacity_idx], 1
    je      .a85
    cmp     dword [g_opacity_idx], 3
    je      .a100
    jmp     .go
.a75:
    mov     r8d, 192
    jmp     .go
.a85:
    mov     r8d, 217
    jmp     .go
.a100:
    or      eax, 0xFF000000
    ret
.go:
    mov     edx, eax
    and     edx, 0xFF
    imul    edx, r8d
    shr     edx, 8
    mov     r9d, edx

    mov     edx, eax
    shr     edx, 8
    and     edx, 0xFF
    imul    edx, r8d
    shr     edx, 8
    shl     edx, 8
    or      r9d, edx

    mov     edx, eax
    shr     edx, 16
    and     edx, 0xFF
    imul    edx, r8d
    shr     edx, 8
    shl     edx, 16
    or      r9d, edx

    mov     eax, r8d
    shl     eax, 24
    or      eax, r9d
    ret

hud_bg_well_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_well_bg + rax*4]
    ret

hud_accent_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_accent_col + rax*4]
    ret

hud_sel_bg_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_sel_bg + rax*4]
    ret

hud_tab_bg_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_tab_bg + rax*4]
    ret

hud_border_out_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_bdr_out + rax*4]
    ret

hud_border_dim_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_bdr_dim + rax*4]
    ret

hud_divider_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_divider + rax*4]
    ret

hud_dim_txt_color:
    mov     eax, dword [g_palette]
    mov     eax, dword [theme_txt_dim + rax*4]
    ret

hud_dim_color:
    mov     edx, eax
    shr     edx, 1
    and     edx, 0x007F7F7F
    or      edx, 0xFF000000
    mov     eax, edx
    ret

; ----------------------------------------------------------------------------
; hud_fill_rect / hud_draw_box / hud_draw_brackets / hud_draw_str_2x
; ----------------------------------------------------------------------------
hud_fill_rect:
    push    rbx
    push    rdi
    push    r12

    mov     r12d, eax
    cmp     r10d, 0
    jge     .x_ok
    add     r8d, r10d
    xor     r10d, r10d
.x_ok:
    cmp     r11d, 0
    jge     .y_ok
    add     r9d, r11d
    xor     r11d, r11d
.y_ok:
    cmp     r8d, 0
    jle     .done
    cmp     r9d, 0
    jle     .done
    mov     eax, r10d
    add     eax, r8d
    cmp     eax, PANEL_W
    jle     .w_ok
    mov     r8d, PANEL_W
    sub     r8d, r10d
    jle     .done
.w_ok:
    mov     eax, r11d
    add     eax, r9d
    cmp     eax, PANEL_H
    jle     .h_ok
    mov     r9d, PANEL_H
    sub     r9d, r11d
    jle     .done
.h_ok:
    mov     rbx, qword [g_panel_bits]
    mov     eax, r11d
    imul    eax, PANEL_W
    add     eax, r10d
    lea     rbx, [rbx + rax*4]

.row_lp:
    mov     rdi, rbx
    mov     ecx, r8d
    mov     eax, r12d
    rep     stosd
    add     rbx, PANEL_W * 4
    dec     r9d
    jnz     .row_lp

.done:
    pop     r12
    pop     rdi
    pop     rbx
    ret

hud_draw_box:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r12d, r10d
    mov     r13d, r11d
    mov     r14d, r8d
    mov     r15d, r9d
    mov     ebx, eax

    mov     r10d, r12d
    mov     r11d, r13d
    mov     r8d, r14d
    mov     r9d, 1
    mov     eax, ebx
    call    hud_fill_rect

    mov     r10d, r12d
    lea     r11d, [r13 + r15 - 1]
    mov     r8d, r14d
    mov     r9d, 1
    mov     eax, ebx
    call    hud_fill_rect

    mov     r10d, r12d
    mov     r11d, r13d
    mov     r8d, 1
    mov     r9d, r15d
    mov     eax, ebx
    call    hud_fill_rect

    lea     r10d, [r12 + r14 - 1]
    mov     r11d, r13d
    mov     r8d, 1
    mov     r9d, r15d
    mov     eax, ebx
    call    hud_fill_rect

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

hud_draw_brackets:
    push    rbx
    push    rbp
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r12d, r10d
    mov     r13d, r11d
    mov     r14d, r8d
    mov     r15d, r9d
    mov     ebx, eax
    mov     ebp, edx

    mov     r10d, r12d
    mov     r11d, r13d
    mov     r8d, ebp
    mov     r9d, 2
    mov     eax, ebx
    call    hud_fill_rect
    mov     r10d, r12d
    mov     r11d, r13d
    mov     r8d, 2
    mov     r9d, ebp
    mov     eax, ebx
    call    hud_fill_rect

    lea     r10d, [r12 + r14]
    sub     r10d, ebp
    mov     r11d, r13d
    mov     r8d, ebp
    mov     r9d, 2
    mov     eax, ebx
    call    hud_fill_rect
    lea     r10d, [r12 + r14 - 2]
    mov     r11d, r13d
    mov     r8d, 2
    mov     r9d, ebp
    mov     eax, ebx
    call    hud_fill_rect

    mov     r10d, r12d
    lea     r11d, [r13 + r15 - 2]
    mov     r8d, ebp
    mov     r9d, 2
    mov     eax, ebx
    call    hud_fill_rect
    mov     r10d, r12d
    lea     r11d, [r13 + r15]
    sub     r11d, ebp
    mov     r8d, 2
    mov     r9d, ebp
    mov     eax, ebx
    call    hud_fill_rect

    lea     r10d, [r12 + r14]
    sub     r10d, ebp
    lea     r11d, [r13 + r15 - 2]
    mov     r8d, ebp
    mov     r9d, 2
    mov     eax, ebx
    call    hud_fill_rect
    lea     r10d, [r12 + r14 - 2]
    lea     r11d, [r13 + r15]
    sub     r11d, ebp
    mov     r8d, 2
    mov     r9d, ebp
    mov     eax, ebx
    call    hud_fill_rect

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbp
    pop     rbx
    ret

hud_draw_str_2x:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r12d, r10d
    mov     r13d, r11d
    mov     r14d, ecx
    mov     r15d, eax
    mov     rbx, qword [g_panel_bits]

.ch_lp:
    test    r14d, r14d
    jz      .done
    movzx   eax, byte [rsi]
    test    al, al
    jz      .done
    inc     rsi
    dec     r14d

    cmp     eax, 0x20
    jbe     .adv
    cmp     eax, 0x7F
    ja      .adv
    lea     edx, [r12 + 16]
    cmp     edx, PANEL_W
    ja      .done
    lea     edx, [r13 + 16]
    cmp     edx, PANEL_H
    ja      .done

    sub     eax, 0x20
    shl     eax, 3
    lea     rdi, [font8x8_table + rax]

    xor     r8d, r8d
.r_lp:
    cmp     r8d, 8
    jae     .adv
    movzx   edx, byte [rdi + r8]
    test    edx, edx
    jz      .r_next

    lea     eax, [r13 + r8*2]
    imul    eax, PANEL_W
    add     eax, r12d
    lea     r10, [rbx + rax*4]

    xor     r9d, r9d
.c_lp:
    cmp     r9d, 8
    jae     .r_next
    mov     ecx, 7
    sub     ecx, r9d
    bt      edx, ecx
    jnc     .c_next
    lea     r11, [r10 + r9*8]
    mov     dword [r11 + 0], r15d
    mov     dword [r11 + 4], r15d
    mov     dword [r11 + PANEL_W*4 + 0], r15d
    mov     dword [r11 + PANEL_W*4 + 4], r15d
.c_next:
    inc     r9d
    jmp     .c_lp
.r_next:
    inc     r8d
    jmp     .r_lp

.adv:
    add     r12d, 16
    jmp     .ch_lp

.done:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; hud_draw_wave: 6 Signature Oscilloscope Styles + Black-Box Time-Travel Overlay!
;   in: r10d = x, r11d = y, r8d = h (48), rsi = live 64B ring ptr, ecx = BB field offset
; ----------------------------------------------------------------------------
hud_draw_wave:
    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r12d, r10d          ; base x
    mov     r13d, r11d          ; base y
    mov     r14d, r8d           ; height (48)
    mov     rbx, rsi            ; live ring ptr
    mov     ebp, ecx            ; BB byte offset (0..4)

    ; 1. Fill theme-specific scope well
    mov     r8d, 128
    mov     r9d, r14d
    call    hud_bg_well_color
    call    hud_fill_rect

    ; 2. Theme-specific well background grid
    mov     eax, dword [g_palette]
    cmp     eax, 3
    je      .grid_synth
    cmp     eax, 4
    je      .grid_blueprint
    cmp     eax, 5
    je      .grid_nerv
    cmp     eax, 1
    je      .grid_crt

    mov     eax, r14d
    shr     eax, 1
    add     eax, r13d
    imul    eax, PANEL_W
    add     eax, r12d
    mov     rcx, qword [g_panel_bits]
    lea     rdx, [rcx + rax*4]
    xor     ecx, ecx
.gd_lp:
    cmp     ecx, 128
    jae     .wave_prep
    mov     dword [rdx + rcx*4], 0xFF202A3A
    add     ecx, 4
    jmp     .gd_lp

.grid_crt:
    mov     r10d, r12d
    mov     eax, r14d
    shr     eax, 1
    lea     r11d, [r13 + rax]
    mov     r8d, 128
    mov     r9d, 1
    mov     eax, 0xFF0A3818
    call    hud_fill_rect
    lea     r10d, [r12 + 64]
    mov     r11d, r13d
    mov     r8d, 1
    mov     r9d, r14d
    mov     eax, 0xFF0A3818
    call    hud_fill_rect
    jmp     .wave_prep

.grid_synth:
    mov     rdi, qword [g_panel_bits]
    xor     r9d, r9d
.gs_y:
    cmp     r9d, r14d
    jae     .wave_prep
    lea     eax, [r13 + r9]
    imul    eax, PANEL_W
    add     eax, r12d
    lea     rdx, [rdi + rax*4]
    xor     ecx, ecx
.gs_x:
    cmp     ecx, 128
    jae     .gs_yn
    mov     dword [rdx + rcx*4], 0xFF3E2270
    add     ecx, 8
    jmp     .gs_x
.gs_yn:
    add     r9d, 8
    jmp     .gs_y

.grid_blueprint:
    xor     r9d, r9d
.gb_y:
    cmp     r9d, r14d
    jae     .gb_v
    push    r9
    mov     r10d, r12d
    lea     r11d, [r13 + r9]
    mov     r8d, 128
    mov     r9d, 1
    mov     eax, 0xFF143560
    call    hud_fill_rect
    pop     r9
    add     r9d, 12
    jmp     .gb_y
.gb_v:
    xor     esi, esi
.gb_x:
    cmp     esi, 128
    jae     .wave_prep
    lea     r10d, [r12 + rsi]
    mov     r11d, r13d
    mov     r8d, 1
    mov     r9d, r14d
    mov     eax, 0xFF143560
    call    hud_fill_rect
    add     esi, 16
    jmp     .gb_x

.grid_nerv:
    mov     r10d, r12d
    lea     r11d, [r13 + 7]
    mov     r8d, 128
    mov     r9d, 1
    mov     eax, 0xFF5A1016
    call    hud_fill_rect
    mov     r10d, r12d
    mov     eax, r14d
    shr     eax, 1
    lea     r11d, [r13 + rax]
    mov     r8d, 128
    mov     r9d, 1
    mov     eax, 0xFF2E0B10
    call    hud_fill_rect

.wave_prep:
    ; Select Live 64B ring OR Black-Box 32-second Rewind Window!
    cmp     dword [g_bb_paused], 0
    je      .use_live_ring

    ; Fill g_bb_wave_tmp[0..31] & g_bb_fault_tmp[0..31] for ages (offset+31 .. offset)
    xor     esi, esi
.bb_fill:
    cmp     esi, 32
    jae     .bb_ready
    mov     eax, 31
    sub     eax, esi
    add     eax, dword [g_bb_offset]
    cmp     eax, dword [g_bb_count]
    jae     .bb_empty
    call    hud_get_bb_ptr
    mov     dl, byte [rax + rbp]
    mov     byte [g_bb_wave_tmp + rsi], dl
    mov     dl, byte [rax + BB_Faults]
    mov     byte [g_bb_fault_tmp + rsi], dl
    jmp     .bb_fn
.bb_empty:
    mov     byte [g_bb_wave_tmp + rsi], 0
    mov     byte [g_bb_fault_tmp + rsi], 0
.bb_fn:
    inc     esi
    jmp     .bb_fill

.bb_ready:
    lea     rbx, [g_bb_wave_tmp]
    xor     r15d, r15d          ; 0..31 linear (never exceeds 31, safe with & 63!)
    jmp     .wave_dispatch

.use_live_ring:
    mov     r15d, dword [g_hist_head]
    sub     r15d, 32
    and     r15d, HIST_LEN - 1

.wave_dispatch:
    mov     eax, dword [g_palette]
    cmp     eax, 1
    je      .wave_crt_trace
    cmp     eax, 2
    je      .wave_forge_vu
    cmp     eax, 3
    je      .wave_synth_grad
    cmp     eax, 4
    je      .wave_blueprint_step

    ; --- Style 0 (Obsidian Heat) & Style 5 (NERV Crimson) ---
    xor     esi, esi
.bar_lp:
    cmp     esi, 32
    jae     .bb_overlay
    movzx   eax, byte [rbx + r15]
    cmp     eax, 100
    jbe     .v_ok
    mov     eax, 100
.v_ok:
    push    rax
    lea     ecx, [r14 - 2]
    imul    eax, ecx
    xor     edx, edx
    mov     ecx, 100
    div     ecx
    pop     rcx

    test    eax, eax
    jnz     .draw_bar
    lea     r10d, [r12 + rsi*4]
    lea     r11d, [r13 + r14 - 1]
    mov     r8d, 3
    mov     r9d, 1
    mov     eax, 0xFF202A3A
    cmp     dword [g_palette], 5
    jne     .fl_go
    mov     eax, 0xFF421016
.fl_go:
    call    hud_fill_rect
    jmp     .bar_next

.draw_bar:
    push    rax
    mov     eax, ecx
    call    pick_metric_color
    mov     ecx, eax
    call    hud_dim_color
    cmp     dword [g_palette], 5
    jne     .bcol_ok
    mov     eax, 0xFF7A121A
.bcol_ok:
    pop     r9
    lea     r10d, [r12 + rsi*4]
    mov     r11d, r13d
    add     r11d, r14d
    sub     r11d, r9d
    mov     r8d, 3
    push    rcx
    push    r9
    push    r10
    push    r11
    call    hud_fill_rect
    pop     r11
    pop     r10
    pop     r9
    pop     rax
    cmp     r9d, 2
    jbe     .cap_h_ok
    mov     r9d, 2
.cap_h_ok:
    mov     r8d, 3
    call    hud_fill_rect

.bar_next:
    inc     r15d
    and     r15d, HIST_LEN - 1
    inc     esi
    jmp     .bar_lp

    ; --- Style 1 (PHOSPHOR CRT) ---
.wave_crt_trace:
    mov     edi, -1
    xor     esi, esi
.crt_lp:
    cmp     esi, 32
    jae     .crt_beam
    movzx   eax, byte [rbx + r15]
    cmp     eax, 100
    jbe     .crt_v
    mov     eax, 100
.crt_v:
    lea     ecx, [r14 - 4]
    imul    eax, ecx
    xor     edx, edx
    mov     ecx, 100
    div     ecx
    mov     r9d, r13d
    add     r9d, r14d
    sub     r9d, 3
    sub     r9d, eax

    lea     r11d, [r9 + 2]
    lea     eax, [r13 + r14]
    sub     eax, r11d
    jle     .crt_trace
    cmp     eax, 6
    jbe     .crt_ag_ok
    mov     eax, 6
.crt_ag_ok:
    push    r9
    lea     r10d, [r12 + rsi*4]
    mov     r8d, 4
    mov     r9d, eax
    mov     eax, 0xFF0A3A1A
    call    hud_fill_rect
    pop     r9

.crt_trace:
    cmp     edi, -1
    je      .crt_dot
    mov     ecx, edi
    mov     edx, r9d
    cmp     ecx, edx
    jle     .crt_vok
    xchg    ecx, edx
.crt_vok:
    sub     edx, ecx
    inc     edx
    push    r9
    lea     r10d, [r12 + rsi*4]
    mov     r11d, ecx
    mov     r8d, 2
    mov     r9d, edx
    mov     eax, 0xFF32E870
    call    hud_fill_rect
    pop     r9
.crt_dot:
    push    r9
    lea     r10d, [r12 + rsi*4]
    mov     r11d, r9d
    mov     r8d, 4
    mov     r9d, 2
    mov     eax, 0xFF7EFFAA
    call    hud_fill_rect
    pop     r9

    mov     edi, r9d
    inc     r15d
    and     r15d, HIST_LEN - 1
    inc     esi
    jmp     .crt_lp

.crt_beam:
    cmp     dword [g_bb_paused], 0
    jne     .bb_overlay
    mov     eax, dword [g_anim_frame]
    shr     eax, 1
    and     eax, 127
    lea     r10d, [r12 + rax]
    mov     r11d, r13d
    mov     r8d, 2
    mov     r9d, r14d
    mov     eax, 0xFF1D7A3C
    call    hud_fill_rect
    jmp     .bb_overlay

    ; --- Style 2 (MOLTEN FORGE) ---
.wave_forge_vu:
    xor     esi, esi
.fg_lp:
    cmp     esi, 32
    jae     .bb_overlay
    movzx   eax, byte [rbx + r15]
    cmp     eax, 100
    jbe     .fg_v
    mov     eax, 100
.fg_v:
    imul    eax, 9
    add     eax, 50
    xor     edx, edx
    mov     ecx, 100
    div     ecx
    mov     edi, eax
    xor     ecx, ecx
.fg_seg:
    cmp     ecx, 9
    jae     .fg_next
    lea     r10d, [r12 + rsi*4]
    mov     eax, ecx
    imul    eax, 5
    mov     r11d, r13d
    add     r11d, r14d
    sub     r11d, 4
    sub     r11d, eax
    cmp     ecx, edi
    jae     .fg_off
    mov     eax, 0xFFC85410
    cmp     ecx, 4
    jb      .fg_draw
    mov     eax, 0xFFFFAA20
    cmp     ecx, 7
    jb      .fg_draw
    mov     eax, 0xFFFFEEAA
    jmp     .fg_draw
.fg_off:
    mov     eax, 0xFF1C130C
.fg_draw:
    push    rcx
    mov     r8d, 3
    mov     r9d, 3
    call    hud_fill_rect
    pop     rcx
    inc     ecx
    jmp     .fg_seg
.fg_next:
    inc     r15d
    and     r15d, HIST_LEN - 1
    inc     esi
    jmp     .fg_lp

    ; --- Style 3 (SYNTHGRID 3-Band Fast) ---
.wave_synth_grad:
    xor     esi, esi
.sy_lp:
    cmp     esi, 32
    jae     .bb_overlay
    movzx   eax, byte [rbx + r15]
    cmp     eax, 100
    jbe     .sy_v
    mov     eax, 100
.sy_v:
    push    rax
    lea     ecx, [r14 - 2]
    imul    eax, ecx
    xor     edx, edx
    mov     ecx, 100
    div     ecx
    pop     rcx
    mov     edi, eax
    test    edi, edi
    jz      .sy_next

    mov     eax, edi
    cmp     eax, 12
    jbe     .sy_b0
    mov     eax, 12
.sy_b0:
    lea     r10d, [r12 + rsi*4]
    mov     r11d, r13d
    add     r11d, r14d
    sub     r11d, eax
    mov     r8d, 3
    mov     r9d, eax
    mov     eax, 0xFFC02898
    push    rcx
    call    hud_fill_rect
    pop     rcx

    cmp     edi, 12
    jbe     .sy_next
    mov     eax, edi
    cmp     eax, 24
    jbe     .sy_b1
    mov     eax, 24
.sy_b1:
    sub     eax, 12
    lea     r10d, [r12 + rsi*4]
    mov     r11d, r13d
    add     r11d, r14d
    sub     r11d, 12
    sub     r11d, eax
    mov     r8d, 3
    mov     r9d, eax
    mov     eax, 0xFF8838F8
    push    rcx
    call    hud_fill_rect
    pop     rcx

    cmp     edi, 24
    jbe     .sy_next
    lea     eax, [rdi - 24]
    lea     r10d, [r12 + rsi*4]
    mov     r11d, r13d
    add     r11d, r14d
    sub     r11d, edi
    mov     r8d, 3
    mov     r9d, eax
    mov     eax, 0xFF18F4FF
    cmp     ecx, dword [g_heat_hot]
    jb      .sy_b2_go
    mov     eax, 0xFFFF2A88
.sy_b2_go:
    call    hud_fill_rect

.sy_next:
    inc     r15d
    and     r15d, HIST_LEN - 1
    inc     esi
    jmp     .sy_lp

    ; --- Style 4 (BLUEPRINT CAD) ---
.wave_blueprint_step:
    mov     edi, -1
    xor     esi, esi
.bp_lp:
    cmp     esi, 32
    jae     .bb_overlay
    movzx   eax, byte [rbx + r15]
    cmp     eax, 100
    jbe     .bp_v
    mov     eax, 100
.bp_v:
    push    rax
    lea     ecx, [r14 - 4]
    imul    eax, ecx
    xor     edx, edx
    mov     ecx, 100
    div     ecx
    mov     r9d, r13d
    add     r9d, r14d
    sub     r9d, 3
    sub     r9d, eax
    pop     rcx

    mov     eax, r13d
    add     eax, r14d
    sub     eax, r9d
    lea     r10d, [r12 + rsi*4]
    mov     r11d, r9d
    push    rcx
    push    r9
    mov     r8d, 2
    mov     r9d, eax
    mov     eax, 0xFF153E72
    call    hud_fill_rect
    pop     r9
    pop     rcx

    mov     eax, 0xFFF2F8FF
    cmp     ecx, dword [g_heat_hot]
    jb      .bp_col_ok
    mov     eax, 0xFFFF4B4B
.bp_col_ok:
    push    rax
    cmp     edi, -1
    je      .bp_ledge
    mov     ecx, edi
    mov     edx, r9d
    cmp     ecx, edx
    jle     .bp_vok
    xchg    ecx, edx
.bp_vok:
    sub     edx, ecx
    inc     edx
    lea     r10d, [r12 + rsi*4]
    mov     r11d, ecx
    mov     r8d, 2
    push    r9
    mov     r9d, edx
    mov     eax, dword [rsp + 8]
    call    hud_fill_rect
    pop     r9
.bp_ledge:
    pop     rax
    lea     r10d, [r12 + rsi*4]
    mov     r11d, r9d
    mov     r8d, 4
    push    r9
    mov     r9d, 2
    call    hud_fill_rect
    pop     r9

    mov     edi, r9d
    inc     r15d
    and     r15d, HIST_LEN - 1
    inc     esi
    jmp     .bp_lp

.bb_overlay:
    ; In Black-Box Rewind mode: draw red/amber anomaly tick marks + right playhead cursor!
    cmp     dword [g_bb_paused], 0
    je      .done
    xor     esi, esi
.bb_tick_lp:
    cmp     esi, 32
    jae     .bb_cursor
    movzx   eax, byte [g_bb_fault_tmp + rsi]
    test    eax, eax
    jz      .bb_tn
    lea     r10d, [r12 + rsi*4]
    lea     r11d, [r13 + r14 - 3]
    mov     r8d, 3
    mov     r9d, 3
    mov     eax, P_KILL_RED
    call    hud_fill_rect
.bb_tn:
    inc     esi
    jmp     .bb_tick_lp

.bb_cursor:
    lea     r10d, [r12 + 126]
    mov     r11d, r13d
    mov     r8d, 2
    mov     r9d, r14d
    mov     eax, P_CAUT_AMBER
    call    hud_fill_rect

.done:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; hud_draw_core_eq: 165-FPS smoothly damped per-core CPU equalizer
; ----------------------------------------------------------------------------
hud_draw_core_eq:
    push    rbx
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r12d, r10d
    mov     r13d, r11d
    mov     r14d, dword [g_num_cores]
    test    r14d, r14d
    jz      .done
    cmp     r14d, 32
    jbe     .nc_ok
    mov     r14d, 32
.nc_ok:
    mov     r8d, 128
    mov     r9d, 16
    call    hud_bg_well_color
    call    hud_fill_rect

    mov     eax, 128
    xor     edx, edx
    div     r14d
    mov     r15d, eax
    cmp     r15d, 2
    jae     .step_ok
    mov     r15d, 2
.step_ok:
    xor     ebx, ebx
.c_lp:
    cmp     ebx, r14d
    jae     .done
    movzx   eax, word [g_disp_core + rbx*2]
    shr     eax, 8
    cmp     eax, 100
    jbe     .cv_ok
    mov     eax, 100
.cv_ok:
    push    rax
    imul    eax, 15
    xor     edx, edx
    mov     ecx, 100
    div     ecx
    inc     eax
    pop     rcx

    push    rax
    mov     eax, ecx
    call    pick_metric_color
    mov     edx, eax
    pop     r9

    mov     eax, ebx
    imul    eax, r15d
    add     eax, r12d
    mov     r10d, eax
    mov     r11d, r13d
    add     r11d, 16
    sub     r11d, r9d
    lea     r8d, [r15 - 1]
    mov     eax, edx
    call    hud_fill_rect

    inc     ebx
    jmp     .c_lp

.done:
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; Formatting helpers (preserve rbx!)
; ----------------------------------------------------------------------------
fmt_mem4:
    push    rbx
    cmp     eax, 1000
    jae     .as_gb
    mov     bl, 'M'
    call    fmt_num3
    pop     rbx
    ret
.as_gb:
    imul    eax, 10
    add     eax, 512
    shr     eax, 10
    cmp     eax, 100
    jae     .whole_gb
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
.whole_gb:
    add     eax, 5
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    mov     bl, 'G'
    call    fmt_num3
    pop     rbx
    ret

; fmt_jit4: format tenths of ms (eax = 0..999) into 4 chars: "0.2m", "9.9m", " 12m"
fmt_jit4:
    push    rbx
    cmp     eax, 100
    jae     .whole_ms
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    add     al, '0'
    add     dl, '0'
    mov     byte [rdi + 0], al
    mov     byte [rdi + 1], '.'
    mov     byte [rdi + 2], dl
    mov     byte [rdi + 3], 'm'
    add     rdi, 4
    pop     rbx
    ret
.whole_ms:
    add     eax, 5
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    mov     bl, 'm'
    call    fmt_num3
    pop     rbx
    ret

; fmt_mmss: format seconds in eax (0..599) as "MM:SS" (5 chars)
fmt_mmss:
    xor     edx, edx
    mov     ecx, 60
    div     ecx                 ; eax = min, edx = sec
    push    rdx
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    add     al, '0'
    add     dl, '0'
    mov     byte [rdi + 0], al
    mov     byte [rdi + 1], dl
    mov     byte [rdi + 2], ':'
    pop     rax                 ; sec
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    add     al, '0'
    add     dl, '0'
    mov     byte [rdi + 3], al
    mov     byte [rdi + 4], dl
    add     rdi, 5
    ret

fmt_sub_gb_pair:
    push    rbx
    push    r12
    mov     r12d, edx
    add     r12d, 512
    shr     r12d, 10
    cmp     r12d, 999
    jbe     .tot_ok
    mov     r12d, 999
.tot_ok:
    call    fmt_mem4
    cmp     r12d, 100
    jb      .two_digit_tot
    cmp     byte [rdi - 4], ' '
    jne     .put_slash_3d
    mov     al, byte [rdi - 3]
    mov     byte [rdi - 4], al
    mov     al, byte [rdi - 2]
    mov     byte [rdi - 3], al
    mov     al, byte [rdi - 1]
    mov     byte [rdi - 2], al
    dec     rdi
.put_slash_3d:
    mov     byte [rdi], '/'
    inc     rdi
    mov     eax, r12d
    mov     bl, 'G'
    call    fmt_num3
    mov     byte [rdi], 0
    jmp     .done

.two_digit_tot:
    mov     byte [rdi], '/'
    inc     rdi
    mov     eax, r12d
    cmp     eax, 10
    jae     .t_2d
    mov     byte [rdi + 0], ' '
    add     al, '0'
    mov     byte [rdi + 1], al
    jmp     .t_g
.t_2d:
    xor     edx, edx
    mov     ecx, 10
    div     ecx
    add     al, '0'
    add     dl, '0'
    mov     byte [rdi + 0], al
    mov     byte [rdi + 1], dl
.t_g:
    mov     byte [rdi + 2], 'G'
    mov     byte [rdi + 3], 0
.done:
    pop     r12
    pop     rbx
    ret

fmt_io4_val:
    push    qword [g_io_bps]
    mov     qword [g_io_bps], rax
    call    fmt_io4
    pop     qword [g_io_bps]
    ret

fmt_3char_name:
    xor     ecx, ecx
.lp:
    cmp     ecx, 3
    jae     .done
    mov     al, byte [rsi + rcx]
    test    al, al
    jz      .pad
    cmp     al, '.'
    je      .pad
    mov     byte [rdi + rcx], al
    inc     ecx
    jmp     .lp
.pad:
    cmp     ecx, 3
    jae     .done
    mov     byte [rdi + rcx], ' '
    inc     ecx
    jmp     .pad
.done:
    add     rdi, 3
    ret

; pick_proc_sep: choose 1-char marker ('F'=Frozen, '*'=Grouped, 'L'=Low-Gear, ' '=Normal)
;   in: rbx = PROC ptr -> al = char
pick_proc_sep:
    test    dword [rbx + PROC_Flags], PFLAG_FROZEN
    jnz     .frz
    cmp     dword [rbx + PROC_Count], 1
    ja      .grp
    test    dword [rbx + PROC_Flags], PFLAG_LOWGEAR
    jnz     .low
    mov     al, ' '
    ret
.frz:
    mov     al, 'F'
    ret
.grp:
    mov     al, '*'
    ret
.low:
    mov     al, 'L'
    ret

fmt_u32_dec:
    push    rbx
    lea     rbx, [g_dec_tmp + 15]
    mov     byte [rbx], 0
    mov     ecx, 10
.div_lp:
    xor     edx, edx
    div     ecx
    add     dl, '0'
    dec     rbx
    mov     byte [rbx], dl
    test    eax, eax
    jnz     .div_lp
.cp_lp:
    mov     al, byte [rbx]
    test    al, al
    jz      .done
    mov     byte [rdi], al
    inc     rbx
    inc     rdi
    jmp     .cp_lp
.done:
    pop     rbx
    ret

fmt_i32_dec:
    test    eax, eax
    jns     .pos
    mov     byte [rdi], '-'
    inc     rdi
    neg     eax
    jmp     fmt_u32_dec
.pos:
    jz      fmt_u32_dec
    mov     byte [rdi], '+'
    inc     rdi
    jmp     fmt_u32_dec

copy_cstr_lim:
.lp:
    test    ecx, ecx
    jz      .done
    mov     al, byte [rsi]
    test    al, al
    jz      .done
    mov     byte [rdi], al
    inc     rsi
    inc     rdi
    dec     ecx
    jmp     .lp
.done:
    ret

fmt_mhz5:
    push    rbx
    cmp     eax, 9999
    jbe     .ok
    mov     eax, 9999
.ok:
    cmp     eax, 1000
    jae     .ge1000
    mov     byte [rdi], ' '
    inc     rdi
    mov     bl, 'M'
    call    fmt_num3
    pop     rbx
    ret
.ge1000:
    mov     ecx, 1000
    xor     edx, edx
    div     ecx
    add     al, '0'
    mov     byte [rdi + 0], al
    mov     eax, edx
    mov     ecx, 100
    xor     edx, edx
    div     ecx
    add     al, '0'
    mov     byte [rdi + 1], al
    mov     eax, edx
    mov     ecx, 10
    xor     edx, edx
    div     ecx
    add     al, '0'
    add     dl, '0'
    mov     byte [rdi + 2], al
    mov     byte [rdi + 3], dl
    mov     byte [rdi + 4], 'M'
    add     rdi, 5
    pop     rbx
    ret

hud_col_max_rows:
    cmp     ecx, 2
    je      .gpu_rows
    cmp     ecx, 3
    je      .vram_rows
    mov     eax, dword [g_top_count]
    test    eax, eax
    jnz     .ok
    mov     eax, 1
.ok:
    ret
.gpu_rows:
    mov     eax, 8
    ret
.vram_rows:
    mov     eax, dword [g_top_vram_count]
    test    eax, eax
    jnz     .ok
    mov     eax, 1
    ret

; ----------------------------------------------------------------------------
; hud_render_theme_chrome & SSE2 hud_post_crt_scanlines
; ----------------------------------------------------------------------------
hud_render_theme_chrome:
    push    rbx
    push    rdi
    push    r12
    push    r13

    mov     eax, dword [g_palette]
    cmp     eax, 1
    je      .chr_crt
    cmp     eax, 2
    je      .chr_forge
    cmp     eax, 3
    je      .chr_synth
    cmp     eax, 4
    je      .chr_blueprint
    cmp     eax, 5
    je      .chr_nerv
    jmp     .done

.chr_crt:
    mov     r10d, 3
    mov     r11d, 3
    mov     r8d, PANEL_W - 6
    mov     r9d, PANEL_H - 6
    mov     eax, 0xFF145C2B
    call    hud_draw_box
    jmp     .done

.chr_forge:
    mov     r10d, 1
    mov     r11d, 1
    mov     r8d, PANEL_W - 2
    mov     r9d, PANEL_H - 2
    mov     eax, 0xFF4C433A
    call    hud_draw_box
    mov     r10d, 3
    mov     r11d, 3
    mov     r8d, PANEL_W - 6
    mov     r9d, PANEL_H - 6
    mov     eax, 0xFF9E5A14
    call    hud_draw_box
    mov     r12d, 20
.fg_tk:
    cmp     r12d, PANEL_W - 20
    jae     .done
    mov     r10d, r12d
    mov     r11d, 34
    mov     r8d, 2
    mov     r9d, 4
    mov     eax, 0xFFD88418
    call    hud_fill_rect
    add     r12d, 24
    jmp     .fg_tk

.chr_synth:
    mov     r10d, 6
    mov     r11d, 0
    mov     r8d, PANEL_W - 12
    mov     r9d, 2
    mov     eax, 0xFFFF2888
    call    hud_fill_rect
    mov     r10d, 6
    mov     r11d, PANEL_H - 2
    mov     r8d, PANEL_W - 12
    mov     r9d, 2
    mov     eax, 0xFF18F4FF
    call    hud_fill_rect
    mov     rdi, qword [g_panel_bits]
    xor     r12d, r12d
.sy_cut:
    cmp     r12d, 5
    jae     .done
    mov     ecx, 5
    sub     ecx, r12d
    mov     eax, r12d
    imul    eax, PANEL_W
    xor     edx, edx
.sy_cx:
    cmp     edx, ecx
    jae     .sy_cn
    lea     r8d, [rax + rdx]
    mov     dword [rdi + r8*4], 0
    lea     r8d, [rax + PANEL_W - 1]
    sub     r8d, edx
    mov     dword [rdi + r8*4], 0
    mov     r9d, PANEL_H - 1
    sub     r9d, r12d
    imul    r9d, PANEL_W
    lea     r8d, [r9 + rdx]
    mov     dword [rdi + r8*4], 0
    lea     r8d, [r9 + PANEL_W - 1]
    sub     r8d, edx
    mov     dword [rdi + r8*4], 0
    inc     edx
    jmp     .sy_cx
.sy_cn:
    inc     r12d
    jmp     .sy_cut

.chr_blueprint:
    mov     r12d, 16
.bp_gy:
    cmp     r12d, PANEL_H - 4
    jae     .bp_gx_start
    mov     r10d, 2
    mov     r11d, r12d
    mov     r8d, PANEL_W - 4
    mov     r9d, 1
    mov     eax, 0xE8102C52
    call    hud_fill_rect
    add     r12d, 16
    jmp     .bp_gy
.bp_gx_start:
    mov     r12d, 16
.bp_gx:
    cmp     r12d, PANEL_W - 4
    jae     .bp_cross
    mov     r10d, r12d
    mov     r11d, 2
    mov     r8d, 1
    mov     r9d, PANEL_H - 4
    mov     eax, 0xE8102C52
    call    hud_fill_rect
    add     r12d, 16
    jmp     .bp_gx
.bp_cross:
    mov     r10d, 3
    mov     r11d, 3
    mov     r8d, PANEL_W - 6
    mov     r9d, PANEL_H - 6
    mov     eax, 0xFF3A72B2
    call    hud_draw_box
    jmp     .done

.chr_nerv:
    mov     r10d, 1
    mov     r11d, 1
    mov     r8d, PANEL_W - 2
    mov     r9d, PANEL_H - 2
    mov     eax, 0xFFD81E28
    call    hud_draw_box
    mov     rdi, qword [g_panel_bits]
    mov     r12d, 10
.nv_y:
    cmp     r12d, 26
    jae     .done
    mov     eax, r12d
    imul    eax, PANEL_W
    mov     r13d, 258
.nv_x:
    cmp     r13d, 412
    jae     .nv_yn
    lea     edx, [r13 + r12]
    and     edx, 15
    cmp     edx, 6
    jae     .nv_xn
    lea     r8d, [rax + r13]
    mov     dword [rdi + r8*4], 0xFF6E1218
.nv_xn:
    inc     r13d
    jmp     .nv_x
.nv_yn:
    inc     r12d
    jmp     .nv_y

.done:
    pop     r13
    pop     r12
    pop     rdi
    pop     rbx
    ret

hud_post_crt_scanlines:
    cmp     dword [g_palette], 1
    jne     .done
    push    rbx
    push    rdi
    mov     rbx, qword [g_panel_bits]
    movdqa  xmm2, dqword [crt_scan_mask]
    mov     r9d, 1
.r_lp:
    cmp     r9d, PANEL_H - 1
    jae     .fin
    mov     eax, r9d
    imul    eax, PANEL_W
    lea     rdi, [rbx + rax*4]
    mov     ecx, PANEL_W / 4
.px_lp:
    movdqu  xmm0, dqword [rdi]
    movdqa  xmm1, xmm0
    psrld   xmm1, 2
    pand    xmm1, xmm2
    psubb   xmm0, xmm1
    movdqu  dqword [rdi], xmm0
    add     rdi, 16
    dec     ecx
    jnz     .px_lp
    add     r9d, 2
    jmp     .r_lp
.fin:
    pop     rdi
    pop     rbx
.done:
    ret

; ----------------------------------------------------------------------------
; hud_render_top_bar (with Black-Box REC T-MM:SS indicator!)
; ----------------------------------------------------------------------------
hud_render_top_bar:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13

    call    hud_accent_color
    mov     r12d, eax
    call    hud_tab_bg_color
    mov     r13d, eax

    ; Tab 0: TELEM
    cmp     dword [g_panel_tab], 0
    jne     .t0_dim
    mov     r10d, 10
    mov     r11d, 6
    mov     r8d, 90
    mov     r9d, 24
    mov     eax, r13d
    call    hud_fill_rect
    mov     r10d, 10
    mov     r11d, 28
    mov     r8d, 90
    mov     r9d, 2
    mov     eax, r12d
    call    hud_fill_rect
    mov     eax, r12d
    jmp     .t0_txt
.t0_dim:
    call    hud_dim_txt_color
.t0_txt:
    mov     r10d, 15
    mov     r11d, 10
    lea     rsi, [szTabTelem]
    mov     ecx, 5
    call    hud_draw_str_2x

    ; Tab 1: CTRL
    cmp     dword [g_panel_tab], 1
    jne     .t1_dim
    mov     r10d, 106
    mov     r11d, 6
    mov     r8d, 74
    mov     r9d, 24
    mov     eax, r13d
    call    hud_fill_rect
    mov     r10d, 106
    mov     r11d, 28
    mov     r8d, 74
    mov     r9d, 2
    mov     eax, r12d
    call    hud_fill_rect
    mov     eax, r12d
    jmp     .t1_txt
.t1_dim:
    call    hud_dim_txt_color
.t1_txt:
    mov     r10d, 111
    mov     r11d, 10
    lea     rsi, [szTabCtrl]
    mov     ecx, 4
    call    hud_draw_str_2x

    ; Tab 2: SET
    cmp     dword [g_panel_tab], 2
    jne     .t2_dim
    mov     r10d, 186
    mov     r11d, 6
    mov     r8d, 58
    mov     r9d, 24
    mov     eax, r13d
    call    hud_fill_rect
    mov     r10d, 186
    mov     r11d, 28
    mov     r8d, 58
    mov     r9d, 2
    mov     eax, r12d
    call    hud_fill_rect
    mov     eax, r12d
    jmp     .t2_txt
.t2_dim:
    call    hud_dim_txt_color
.t2_txt:
    mov     r10d, 191
    mov     r11d, 10
    lea     rsi, [szTabSet]
    mov     ecx, 3
    call    hud_draw_str_2x

    ; Right status: Black-Box Rewind badge OR Live Battery + Net!
    cmp     dword [g_bb_paused], 0
    je      .live_status

    ; Draw Black-Box Rewind pill + "BB REWIND  T-MM:SS"
    mov     r10d, 424
    mov     r11d, 6
    mov     r8d, 326
    mov     r9d, 24
    mov     eax, P_BG_KILL
    call    hud_fill_rect

    lea     rdi, [g_cell_buf]
    lea     rsi, [szBbTopPfx]
    mov     ecx, 13
    call    copy_cstr_lim
    mov     eax, dword [g_bb_offset]
    call    fmt_mmss
    mov     byte [rdi], 0

    mov     eax, dword [g_bb_offset]
    call    hud_get_bb_ptr
    movzx   edx, byte [rax + BB_Faults]
    mov     eax, P_CAUT_AMBER
    test    edx, edx
    jz      .bb_hdr_draw
    mov     eax, P_KILL_RED
.bb_hdr_draw:
    mov     r10d, 436
    mov     r11d, 10
    lea     rsi, [g_cell_buf]
    mov     ecx, 20
    call    hud_draw_str_2x
    jmp     .top_div

.live_status:
    lea     rdi, [g_cell_buf]
    cmp     byte [g_bat_present], 0
    je      .bat_none
    cmp     byte [g_bat_ac], 0
    je      .bat_dc
    mov     byte [rdi + 0], 'A'
    mov     byte [rdi + 1], 'C'
    mov     byte [rdi + 2], ':'
    add     rdi, 3
    mov     eax, dword [g_bat_pct]
    call    fmt_pct3
    jmp     .bat_done
.bat_dc:
    mov     eax, dword [g_bat_rate_mw]
    cmp     eax, BATTERY_UNKNOWN_RATE_
    je      .bat_pct_only
    test    eax, eax
    jns     .bat_pct_only
    neg     eax
    add     eax, 500
    xor     edx, edx
    mov     ecx, 1000
    div     ecx
    mov     byte [rdi], '-'
    inc     rdi
    mov     bl, 'W'
    call    fmt_num3
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], ' '
    add     rdi, 2
    jmp     .bat_done
.bat_pct_only:
    mov     byte [rdi + 0], 'B'
    mov     byte [rdi + 1], 'T'
    mov     byte [rdi + 2], ':'
    add     rdi, 3
    mov     eax, dword [g_bat_pct]
    call    fmt_pct3
    jmp     .bat_done
.bat_none:
    mov     dword [rdi + 0], 'DESK'
    mov     byte  [rdi + 4], 'T'
    mov     byte  [rdi + 5], 'O'
    mov     byte  [rdi + 6], 'P'
    add     rdi, 7
.bat_done:
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], ' '
    mov     byte [rdi + 2], 'v'
    add     rdi, 3
    mov     rax, qword [g_net_rx_bps]
    call    fmt_io4_val
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], '^'
    add     rdi, 2
    mov     rax, qword [g_net_tx_bps]
    call    fmt_io4_val
    mov     byte [rdi], 0

    mov     r10d, 428
    mov     r11d, 10
    lea     rsi, [g_cell_buf]
    mov     ecx, 20
    call    pick_label_color
    call    hud_draw_str_2x

.top_div:
    mov     r10d, 10
    mov     r11d, 35
    mov     r8d, PANEL_W - 20
    mov     r9d, 1
    call    hud_divider_color
    call    hud_fill_rect

    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; hud_render_col_row: CPU, RAM, GPU (+JIT!), LIVE PER-PROCESS VRM, IO!
; ----------------------------------------------------------------------------
hud_render_col_row:
    push    rbx
    push    rsi
    push    rdi

    lea     rdi, [g_cell_buf]
    mov     dword [rdi + 0], '    '
    mov     dword [rdi + 4], '    '
    mov     byte  [rdi + 8], 0

    cmp     r12d, 0
    je      .col_cpu
    cmp     r12d, 1
    je      .col_ram
    cmp     r12d, 2
    je      .col_gpu
    cmp     r12d, 3
    je      .col_vram
    jmp     .col_io

.col_cpu:
    cmp     r13d, dword [g_top_count]
    jae     .empty_row
    mov     eax, dword [g_top_cpu + r13*4]
    shl     eax, 6
    lea     rbx, [g_procs + rax]
    lea     rsi, [rbx + PROC_Name]
    call    fmt_3char_name
    call    pick_proc_sep
    mov     byte [rdi], al
    inc     rdi
    mov     eax, dword [rbx + PROC_CpuPct]
    push    rax
    call    fmt_pct3
    pop     rax
    test    dword [rbx + PROC_Flags], PFLAG_FROZEN
    jnz     .ret_frz_col
    call    pick_metric_color
    jmp     .done

.col_ram:
    cmp     r13d, dword [g_top_count]
    jae     .empty_row
    mov     eax, dword [g_top_ram + r13*4]
    shl     eax, 6
    lea     rbx, [g_procs + rax]
    lea     rsi, [rbx + PROC_Name]
    call    fmt_3char_name
    call    pick_proc_sep
    mov     byte [rdi], al
    inc     rdi
    mov     eax, dword [rbx + PROC_MemMB]
    push    rax
    call    fmt_mem4
    pop     rax
    test    dword [rbx + PROC_Flags], PFLAG_FROZEN
    jnz     .ret_frz_col
    xor     edx, edx
    cmp     eax, 6144
    jae     .ram_hot
    cmp     eax, 2048
    jae     .ram_warm
    jmp     .ram_col
.ram_hot:
    mov     edx, 95
    jmp     .ram_col
.ram_warm:
    mov     edx, 75
.ram_col:
    mov     eax, edx
    call    pick_metric_color
    jmp     .done

.col_gpu:
    ; When Black-Box paused, rewind GPU silicon telemetry rows to T - g_bb_offset!
    xor     ebx, ebx            ; rbx = BB frame ptr (0 if live)
    cmp     dword [g_bb_paused], 0
    je      .gpu_disp
    mov     eax, dword [g_bb_offset]
    call    hud_get_bb_ptr
    mov     rbx, rax
.gpu_disp:
    cmp     r13d, 0
    je      .gpu_r0
    cmp     r13d, 1
    je      .gpu_r1
    cmp     r13d, 2
    je      .gpu_r2
    cmp     r13d, 3
    je      .gpu_r3
    cmp     r13d, 4
    je      .gpu_r4
    cmp     r13d, 5
    je      .gpu_r5
    cmp     r13d, 6
    je      .gpu_r6
    cmp     r13d, 7
    je      .gpu_r7
    jmp     .empty_row

.gpu_r0:
    mov     dword [rdi], 'TMP '
    add     rdi, 4
    mov     eax, dword [g_gpu_temp]
    test    rbx, rbx
    jz      .gr0_go
    movzx   eax, byte [rbx + BB_Temp]
.gr0_go:
    push    rax
    call    fmt_temp4
    pop     rax
    call    map_temp_heat
    call    pick_metric_color
    jmp     .done

.gpu_r1:
    mov     dword [rdi], 'PWR '
    add     rdi, 4
    mov     eax, dword [g_gpu_pwr_w]
    test    rbx, rbx
    jz      .gr1_go
    movzx   eax, word [rbx + BB_PwrW]
.gr1_go:
    push    rbx
    mov     bl, 'W'
    call    fmt_num3
    pop     rbx
    xor     eax, eax
    call    pick_metric_color
    jmp     .done

.gpu_r2:
    mov     dword [rdi], 'LIM '
    add     rdi, 4
    mov     eax, dword [g_gpu_pwr_lim]
    push    rbx
    mov     bl, 'W'
    call    fmt_num3
    pop     rbx
    call    pick_label_color
    jmp     .done

.gpu_r3:
    mov     byte [rdi+0], 'C'
    mov     byte [rdi+1], 'L'
    mov     byte [rdi+2], 'K'
    add     rdi, 3
    mov     eax, dword [g_gpu_clk]
    test    rbx, rbx
    jz      .gr3_go
    movzx   eax, word [rbx + BB_Clk]
.gr3_go:
    call    fmt_mhz5
    xor     eax, eax
    call    pick_metric_color
    jmp     .done

.gpu_r4:
    mov     byte [rdi+0], 'M'
    mov     byte [rdi+1], 'E'
    mov     byte [rdi+2], 'M'
    add     rdi, 3
    mov     eax, dword [g_gpu_mclk]
    call    fmt_mhz5
    call    pick_label_color
    jmp     .done

.gpu_r5:
    mov     dword [rdi], 'THR '
    test    rbx, rbx
    jz      .gr5_live
    movzx   eax, byte [rbx + BB_Faults]
    test    eax, FAULT_TMP
    jnz     .thr_tmp
    test    eax, FAULT_PWR
    jnz     .thr_pwr
    jmp     .thr_ok
.gr5_live:
    mov     rax, qword [g_gpu_throttle]
    test    rax, 0x60
    jnz     .thr_tmp
    test    rax, 0x0C
    jnz     .thr_pwr
.thr_ok:
    mov     dword [rdi + 4], '  OK'
    xor     eax, eax
    call    pick_metric_color
    jmp     .done
.thr_tmp:
    mov     dword [rdi + 4], 'TMP!'
    mov     eax, 95
    call    pick_metric_color
    jmp     .done
.thr_pwr:
    mov     dword [rdi + 4], 'PWR!'
    mov     eax, 75
    call    pick_metric_color
    jmp     .done

.gpu_r6:
    mov     dword [rdi], 'iGP '
    add     rdi, 4
    mov     eax, dword [g_igpu_pct]
    test    rbx, rbx
    jz      .gr6_go
    movzx   eax, byte [rbx + BB_Igpu]
.gr6_go:
    push    rax
    call    fmt_pct3
    pop     rax
    call    pick_metric_color
    jmp     .done

.gpu_r7:
    ; Live Sub-ms Compositor/Scheduler Jitter Radar!
    mov     dword [rdi], 'JIT '
    add     rdi, 4
    mov     eax, dword [g_jitter_10ms]
    push    rax
    call    fmt_jit4
    pop     rdx
    xor     eax, eax
    cmp     edx, 60             ; >= 6.0ms micro-stutter -> hot!
    jae     .jit_hot
    cmp     edx, 20             ; >= 2.0ms -> warm!
    jae     .jit_warm
    jmp     .jit_col
.jit_hot:
    mov     eax, 95
    jmp     .jit_col
.jit_warm:
    mov     eax, 75
.jit_col:
    call    pick_metric_color
    jmp     .done

    ; --- Column 3 (VRM): LIVE PER-PROCESS DEDICATED VRAM RADAR! ---
.col_vram:
    cmp     dword [g_top_vram_count], 0
    jne     .vram_has_procs
    cmp     r13d, 0
    jne     .empty_row
    cmp     qword [g_vram_committed], 0
    je      .vr_slp
    mov     dword [rdi + 0], 'NO A'
    mov     dword [rdi + 4], 'LLOC'
    call    hud_dim_txt_color
    jmp     .done
.vr_slp:
    mov     dword [rdi + 0], 'dGPU'
    mov     dword [rdi + 4], ' SLP'
    call    hud_dim_txt_color
    jmp     .done

.vram_has_procs:
    cmp     r13d, dword [g_top_vram_count]
    jae     .empty_row
    mov     eax, dword [g_top_vram + r13*4]
    shl     eax, 6
    lea     rbx, [g_procs + rax]
    lea     rsi, [rbx + PROC_Name]
    call    fmt_3char_name
    call    pick_proc_sep
    mov     byte [rdi], al
    inc     rdi
    mov     eax, dword [rbx + PROC_VramMB]
    push    rax
    call    fmt_mem4
    pop     rax
    test    dword [rbx + PROC_Flags], PFLAG_FROZEN
    jnz     .ret_frz_col
    xor     edx, edx
    cmp     eax, 3072           ; >= 3 GB VRAM -> hot!
    jae     .vr_hot
    cmp     eax, 1024           ; >= 1 GB VRAM -> warm!
    jae     .vr_warm
    jmp     .vr_col
.vr_hot:
    mov     edx, 95
    jmp     .vr_col
.vr_warm:
    mov     edx, 75
.vr_col:
    mov     eax, edx
    call    pick_metric_color
    jmp     .done

.col_io:
    cmp     r13d, dword [g_top_count]
    jae     .empty_row
    mov     eax, dword [g_top_io + r13*4]
    shl     eax, 6
    lea     rbx, [g_procs + rax]
    lea     rsi, [rbx + PROC_Name]
    call    fmt_3char_name
    call    pick_proc_sep
    mov     byte [rdi], al
    inc     rdi
    mov     rax, qword [rbx + PROC_IoBps]
    push    rax
    call    fmt_io4_val
    pop     rdx
    test    dword [rbx + PROC_Flags], PFLAG_FROZEN
    jnz     .ret_frz_col
    xor     eax, eax
    cmp     rdx, 104857600
    jae     .pio_hot
    cmp     rdx, 10485760
    jae     .pio_warm
    jmp     .pio_col
.pio_hot:
    mov     eax, 95
    jmp     .pio_col
.pio_warm:
    mov     eax, 75
.pio_col:
    call    pick_metric_color
    jmp     .done

.ret_frz_col:
    mov     eax, P_FRZ_CYAN
    jmp     .done

.empty_row:
    call    hud_dim_txt_color
.done:
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; hud_render_telem_footer: Kill Confirm -> Black-Box Log -> Action Flash -> Detail!
; ----------------------------------------------------------------------------
hud_render_telem_footer:
    push    rbx
    push    rsi
    push    rdi
    push    r12

    ; 1. Highest priority: Crimson Quick-Kill Confirmation Modal
    cmp     dword [g_kill_confirm], 0
    je      .chk_bb_foot

    mov     r10d, 10
    mov     r11d, 353
    mov     r8d, PANEL_W - 20
    mov     r9d, 26
    mov     eax, P_BG_KILL
    call    hud_fill_rect

    lea     rdi, [g_foot_buf]
    lea     rsi, [szKillPfx]
    mov     ecx, 7
    call    copy_cstr_lim
    lea     rsi, [g_kill_name]
    mov     ecx, 15
    call    copy_cstr_lim
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], '['
    add     rdi, 2
    cmp     dword [g_kill_count], 1
    ja      .kill_grp
    mov     eax, dword [g_kill_pid]
    call    fmt_u32_dec
    jmp     .kill_tail
.kill_grp:
    mov     byte [rdi], 'x'
    inc     rdi
    mov     eax, dword [g_kill_count]
    call    fmt_u32_dec
.kill_tail:
    lea     rsi, [szKillAsk]
    mov     ecx, 16
    call    copy_cstr_lim
    mov     byte [rdi], 0

    mov     r10d, 14
    mov     r11d, 358
    lea     rsi, [g_foot_buf]
    mov     ecx, 46
    mov     eax, P_KILL_RED
    call    hud_draw_str_2x
    jmp     .done

    ; 2. Black-Box Rewind Flight Log!
.chk_bb_foot:
    cmp     dword [g_bb_paused], 0
    je      .chk_status_flash

    mov     eax, dword [g_bb_offset]
    call    hud_get_bb_ptr
    mov     rbx, rax

    lea     rdi, [g_foot_buf]
    mov     dword [rdi], '> T-'
    add     rdi, 4
    mov     eax, dword [g_bb_offset]
    call    fmt_mmss
    mov     byte [rdi], ' '
    inc     rdi

    ; Top CPU process snapshot at that second (max 6 chars so footer NEVER clips!)
    cmp     byte [rbx + BB_TopName], 0
    je      .bb_f_clk
    mov     dword [rdi], 'TOP:'
    add     rdi, 4
    lea     rsi, [rbx + BB_TopName]
    mov     ecx, 6
    call    copy_cstr_lim
    mov     byte [rdi], ' '
    inc     rdi

.bb_f_clk:
    movzx   eax, byte [rbx + BB_Temp]
    call    fmt_temp4
    mov     byte [rdi], ' '
    inc     rdi
    movzx   eax, word [rbx + BB_Clk]
    test    eax, eax
    jz      .bb_f_fault
    call    fmt_mhz5
    mov     byte [rdi], ' '
    inc     rdi
.bb_f_fault:

    movzx   eax, byte [rbx + BB_Faults]
    test    eax, eax
    jz      .bb_f_hint
    test    eax, FAULT_TMP
    jnz     .bbf_tmp
    test    eax, FAULT_VRM
    jnz     .bbf_vrm
    test    eax, FAULT_PWR
    jnz     .bbf_pwr
    test    eax, FAULT_RAM
    jnz     .bbf_ram
    mov     dword [rdi], '!CPU'
    jmp     .bbf_end
.bbf_tmp:
    mov     dword [rdi], '!TMP'
    jmp     .bbf_end
.bbf_vrm:
    mov     dword [rdi], '!VRM'
    jmp     .bbf_end
.bbf_pwr:
    mov     dword [rdi], '!PWR'
    jmp     .bbf_end
.bbf_ram:
    mov     dword [rdi], '!RAM'
.bbf_end:
    add     rdi, 4
    mov     byte [rdi], ' '
    inc     rdi
.bb_f_hint:
    lea     rsi, [szBbFootHint]
    mov     ecx, 18
    call    copy_cstr_lim
    mov     byte [rdi], 0

    mov     r10d, 12
    mov     r11d, 358
    lea     rsi, [g_foot_buf]
    mov     ecx, 46
    mov     eax, P_CAUT_AMBER
    cmp     byte [rbx + BB_Faults], 0
    je      .bb_f_draw
    mov     eax, P_KILL_RED
.bb_f_draw:
    call    hud_draw_str_2x
    jmp     .done

    ; 3. 2.5-Second Cockpit Action Flash (Freeze / Thaw / Low-Gear / Protected!)
.chk_status_flash:
    cmp     dword [g_status_active], 0
    je      .normal_foot
    mov     r10d, 12
    mov     r11d, 358
    lea     rsi, [g_status_buf]
    mov     ecx, 46
    mov     eax, dword [g_status_color]
    call    hud_draw_str_2x
    jmp     .done

    ; 4. Normal Detail Readout
.normal_foot:
    lea     rdi, [g_foot_buf]
    mov     byte [rdi + 0], '>'
    mov     byte [rdi + 1], ' '
    add     rdi, 2

    mov     r12d, dword [g_col_active]
    mov     eax, dword [g_col_sel + r12*4]

    cmp     r12d, 2
    je      .foot_gpu
    cmp     r12d, 3
    je      .foot_vram_col

    cmp     eax, dword [g_top_count]
    jae     .foot_hint
    cmp     r12d, 0
    je      .pick_cpu_p
    cmp     r12d, 1
    je      .pick_ram_p
    mov     ecx, dword [g_top_io + rax*4]
    jmp     .got_proc
.pick_cpu_p:
    mov     ecx, dword [g_top_cpu + rax*4]
    jmp     .got_proc
.pick_ram_p:
    mov     ecx, dword [g_top_ram + rax*4]
    jmp     .got_proc

.foot_vram_col:
    cmp     eax, dword [g_top_vram_count]
    jae     .foot_vram_empty
    mov     ecx, dword [g_top_vram + rax*4]

.got_proc:
    shl     ecx, 6
    lea     rbx, [g_procs + rcx]

    lea     rsi, [rbx + PROC_Name]
    mov     ecx, 12
    call    copy_cstr_lim

    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], '['
    add     rdi, 2
    cmp     dword [rbx + PROC_Count], 1
    ja      .foot_grp_cnt
    mov     eax, dword [rbx + PROC_Pid]
    call    fmt_u32_dec
    jmp     .foot_metrics
.foot_grp_cnt:
    mov     byte [rdi], 'x'
    inc     rdi
    mov     eax, dword [rbx + PROC_Count]
    call    fmt_u32_dec
.foot_metrics:
    mov     byte [rdi + 0], ']'
    mov     byte [rdi + 1], ' '
    add     rdi, 2

    ; Show VRAM first if this process allocates VRAM!
    cmp     dword [rbx + PROC_VramMB], 0
    je      .foot_c_m_i
    mov     byte [rdi + 0], 'V'
    mov     byte [rdi + 1], ':'
    add     rdi, 2
    mov     eax, dword [rbx + PROC_VramMB]
    call    fmt_mem4
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], 'C'
    mov     byte [rdi + 2], ':'
    add     rdi, 3
    mov     eax, dword [rbx + PROC_CpuPct]
    call    fmt_pct3
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], 'M'
    mov     byte [rdi + 2], ':'
    add     rdi, 3
    mov     eax, dword [rbx + PROC_MemMB]
    call    fmt_mem4
    jmp     .foot_tags

.foot_c_m_i:
    mov     byte [rdi + 0], 'C'
    mov     byte [rdi + 1], ':'
    add     rdi, 2
    mov     eax, dword [rbx + PROC_CpuPct]
    call    fmt_pct3
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], 'M'
    mov     byte [rdi + 2], ':'
    add     rdi, 3
    mov     eax, dword [rbx + PROC_MemMB]
    call    fmt_mem4
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], 'I'
    mov     byte [rdi + 2], ':'
    add     rdi, 3
    mov     rax, qword [rbx + PROC_IoBps]
    call    fmt_io4_val

.foot_tags:
    test    dword [rbx + PROC_Flags], PFLAG_FROZEN
    jz      .chk_low_tag
    mov     byte [rdi], ' '
    mov     dword [rdi + 1], 'FRZ!'
    add     rdi, 5
    jmp     .foot_nul
.chk_low_tag:
    test    dword [rbx + PROC_Flags], PFLAG_LOWGEAR
    jz      .foot_nul
    mov     byte [rdi], ' '
    mov     dword [rdi + 1], 'LOW '
    add     rdi, 4
.foot_nul:
    mov     byte [rdi], 0
    jmp     .draw_foot

.foot_gpu:
    lea     rsi, [szFootGpu]
    mov     ecx, 4
    call    copy_cstr_lim
    mov     eax, dword [g_gpu_temp]
    call    fmt_temp4
    mov     byte [rdi], ' '
    inc     rdi
    mov     eax, dword [g_gpu_pwr_w]
    mov     bl, 'W'
    call    fmt_num3
    mov     byte [rdi], '/'
    inc     rdi
    mov     eax, dword [g_gpu_pwr_lim]
    mov     bl, 'W'
    call    fmt_num3
    mov     byte [rdi], ' '
    inc     rdi
    mov     eax, dword [g_gpu_clk]
    call    fmt_mhz5
    mov     dword [rdi], ' JIT'
    add     rdi, 4
    mov     byte [rdi], ':'
    inc     rdi
    mov     eax, dword [g_jitter_10ms]
    call    fmt_jit4
    mov     byte [rdi], ' '
    inc     rdi
    cmp     qword [g_vram_committed], 0
    je      .fg_slp
    mov     dword [rdi], 'ACT '
    add     rdi, 3
    jmp     .fg_nul
.fg_slp:
    mov     dword [rdi], 'SLP '
    add     rdi, 3
.fg_nul:
    mov     byte [rdi], 0
    jmp     .draw_foot

.foot_vram_empty:
    lea     rsi, [szFootVram]
    mov     ecx, 12
    call    copy_cstr_lim
    mov     eax, dword [g_vram_used_mb]
    call    fmt_u32_dec
    mov     byte [rdi], '/'
    inc     rdi
    mov     eax, dword [g_vram_tot_mb]
    call    fmt_u32_dec
    mov     dword [rdi], ' MB '
    add     rdi, 4
    mov     eax, dword [g_vram_pct]
    call    fmt_pct3
    mov     byte [rdi], 0
    jmp     .draw_foot

.foot_hint:
    lea     rsi, [szFootHint]
    mov     ecx, 45
    call    copy_cstr_lim
    mov     byte [rdi], 0

.draw_foot:
    mov     r10d, 12
    mov     r11d, 358
    lea     rsi, [g_foot_buf]
    mov     ecx, 46
    call    hud_accent_color
    call    hud_draw_str_2x

.done:
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; hud_render_tab_telem
; ----------------------------------------------------------------------------
hud_render_tab_telem:
    push    rbx
    push    rbp
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15

    xor     r12d, r12d
.col_loop:
    cmp     r12d, 5
    jae     .cols_done

    mov     eax, r12d
    imul    eax, COL_STRIDE
    add     eax, COL_X0
    mov     r14d, eax

    cmp     dword [g_palette], 4
    je      .col_bdr
    mov     r10d, r14d
    mov     r11d, COL_TOP
    mov     r8d, COL_W
    mov     r9d, COL_H
    mov     eax, dword [g_cached_bg_col]
    call    hud_fill_rect

.col_bdr:
    cmp     r12d, dword [g_col_active]
    jne     .col_dim_bdr
    call    hud_accent_color
    mov     ebp, eax
    mov     r10d, r14d
    mov     r11d, COL_TOP
    mov     r8d, COL_W
    mov     r9d, COL_H
    call    hud_draw_box
    mov     r10d, r14d
    mov     r11d, COL_TOP
    mov     r8d, COL_W
    mov     r9d, COL_H
    mov     edx, 10
    mov     eax, ebp
    call    hud_draw_brackets
    jmp     .col_hdr_go
.col_dim_bdr:
    call    hud_border_dim_color
    mov     r10d, r14d
    mov     r11d, COL_TOP
    mov     r8d, COL_W
    mov     r9d, COL_H
    call    hud_draw_box

.col_hdr_go:
    ; Grab Black-Box frame ptr in rbx if rewinding (0 if live)
    xor     ebx, ebx
    cmp     dword [g_bb_paused], 0
    je      .hdr_go2
    mov     eax, dword [g_bb_offset]
    call    hud_get_bb_ptr
    mov     rbx, rax
.hdr_go2:
    lea     rdi, [g_cell_buf]
    cmp     r12d, 0
    je      .hdr_cpu
    cmp     r12d, 1
    je      .hdr_ram
    cmp     r12d, 2
    je      .hdr_gpu
    cmp     r12d, 3
    je      .hdr_vram

    ; IO header
    mov     dword [rdi], 'IO  '
    add     rdi, 4
    mov     rax, qword [g_io_bps]
    test    rbx, rbx
    jz      .hio_fmt
    mov     eax, dword [rbx + BB_IoKB]
    shl     rax, 10
.hio_fmt:
    push    rax
    call    fmt_io4_val
    mov     byte [rdi], 0
    pop     rdx
    xor     eax, eax
    cmp     rdx, 262144000
    jae     .hio_hot
    cmp     rdx, 52428800
    jae     .hio_warm
    jmp     .hdr_col_ok
.hio_hot:
    mov     eax, 95
    jmp     .hdr_col_ok
.hio_warm:
    mov     eax, 75
    jmp     .hdr_col_ok

.hdr_cpu:
    mov     dword [rdi], 'CPU '
    add     rdi, 4
    mov     eax, dword [g_cpu_pct]
    test    rbx, rbx
    jz      .h_pct
    movzx   eax, byte [rbx + BB_Cpu]
    jmp     .h_pct
.hdr_ram:
    mov     dword [rdi], 'RAM '
    add     rdi, 4
    mov     eax, dword [g_ram_pct]
    test    rbx, rbx
    jz      .h_pct
    movzx   eax, byte [rbx + BB_Ram]
    jmp     .h_pct
.hdr_gpu:
    mov     dword [rdi], 'GPU '
    add     rdi, 4
    mov     eax, dword [g_gpu_pct]
    test    rbx, rbx
    jz      .h_pct
    movzx   eax, byte [rbx + BB_Gpu]
    jmp     .h_pct
.hdr_vram:
    mov     eax, dword [g_vram_pct]
    test    rbx, rbx
    jz      .hv_chk
    movzx   eax, byte [rbx + BB_Vram]
.hv_chk:
    cmp     eax, 95
    jae     .hv_spil
    mov     dword [rdi], 'VRM '
    add     rdi, 4
    jmp     .h_pct
.hv_spil:
    mov     dword [rdi + 0], 'VRM '
    mov     dword [rdi + 4], 'SPIL'
    mov     byte  [rdi + 8], 0
    mov     eax, 95
    jmp     .hdr_col_ok

.h_pct:
    push    rax
    call    fmt_pct3
    mov     byte [rdi], 0
    pop     rax
.hdr_col_ok:
    call    pick_metric_color
    lea     r10d, [r14 + 8]
    mov     r11d, 48
    lea     rsi, [g_cell_buf]
    mov     ecx, 8
    call    hud_draw_str_2x

    ; Scope waveform (passes both live ring ptr in rsi AND BB byte offset in ecx!)
    cmp     r12d, 0
    je      .wv_cpu
    cmp     r12d, 1
    je      .wv_ram
    cmp     r12d, 2
    je      .wv_gpu
    cmp     r12d, 3
    je      .wv_vram
    lea     rsi, [g_hist_io]
    mov     ecx, BB_IoLog
    jmp     .wv_go
.wv_cpu:
    lea     rsi, [g_hist_cpu]
    mov     ecx, BB_Cpu
    jmp     .wv_go
.wv_ram:
    lea     rsi, [g_hist_ram]
    mov     ecx, BB_Ram
    jmp     .wv_go
.wv_gpu:
    lea     rsi, [g_hist_gpu]
    mov     ecx, BB_Gpu
    jmp     .wv_go
.wv_vram:
    lea     rsi, [g_hist_vram]
    mov     ecx, BB_Vram
.wv_go:
    lea     r10d, [r14 + 8]
    mov     r11d, 70
    mov     r8d, 48
    call    hud_draw_wave

    ; Sub-header directly below waveform
    cmp     r12d, 0
    je      .sub_cpu_eq
    lea     rdi, [g_cell_buf]
    cmp     r12d, 1
    je      .sub_ram
    cmp     r12d, 2
    je      .sub_gpu
    cmp     r12d, 3
    je      .sub_vram
    mov     dword [rdi + 0], 'TOP '
    mov     dword [rdi + 4], ' I/O'
    mov     byte  [rdi + 8], 0
    call    pick_label_color
    jmp     .sub_draw_txt

.sub_cpu_eq:
    lea     r10d, [r14 + 8]
    mov     r11d, 122
    call    hud_draw_core_eq
    jmp     .sub_done

.sub_ram:
    mov     eax, dword [g_ram_used_mb]
    test    rbx, rbx
    jz      .sr_go
    mov     eax, dword [rbx + BB_RamMB]
.sr_go:
    mov     edx, dword [g_ram_tot_mb]
    call    fmt_sub_gb_pair
    call    pick_label_color
    jmp     .sub_draw_txt

.sub_gpu:
    mov     eax, dword [g_gpu_temp]
    test    rbx, rbx
    jz      .sg_t
    movzx   eax, byte [rbx + BB_Temp]
.sg_t:
    push    rax
    call    fmt_temp4
    mov     eax, dword [g_gpu_pwr_w]
    test    rbx, rbx
    jz      .sg_w
    movzx   eax, word [rbx + BB_PwrW]
.sg_w:
    push    rbx
    mov     bl, 'W'
    call    fmt_num3
    pop     rbx
    mov     byte [rdi], 0
    pop     rax
    call    map_temp_heat
    call    pick_metric_color
    jmp     .sub_draw_txt

.sub_vram:
    mov     eax, dword [g_vram_used_mb]
    test    rbx, rbx
    jz      .sv_go
    mov     eax, dword [rbx + BB_VramMB]
.sv_go:
    mov     edx, dword [g_vram_tot_mb]
    call    fmt_sub_gb_pair
    call    pick_label_color

.sub_draw_txt:
    lea     r10d, [r14 + 8]
    mov     r11d, 122
    lea     rsi, [g_cell_buf]
    mov     ecx, 8
    call    hud_draw_str_2x

.sub_done:
    lea     r10d, [r14 + 4]
    mov     r11d, 144
    mov     r8d, COL_W - 8
    mov     r9d, 1
    call    hud_divider_color
    call    hud_fill_rect

    mov     r15d, dword [g_col_scroll + r12*4]
    xor     ebx, ebx
.vis_loop:
    cmp     ebx, VISIBLE_ROWS
    jae     .col_next

    lea     r13d, [r15 + rbx]
    mov     eax, ebx
    imul    eax, ROW_PITCH
    add     eax, ROW_Y0
    push    rax

    cmp     r12d, dword [g_col_active]
    jne     .no_row_hi
    cmp     r13d, dword [g_col_sel + r12*4]
    jne     .no_row_hi

    lea     r10d, [r14 + 2]
    mov     r11d, eax
    sub     r11d, 2
    mov     r8d, COL_W - 4
    mov     r9d, 20
    call    hud_sel_bg_color
    call    hud_fill_rect
.no_row_hi:

    call    hud_render_col_row
    pop     r11
    lea     r10d, [r14 + 8]
    lea     rsi, [g_cell_buf]
    mov     ecx, 8
    call    hud_draw_str_2x

    inc     ebx
    jmp     .vis_loop

.col_next:
    inc     r12d
    jmp     .col_loop

.cols_done:
    mov     r10d, 10
    mov     r11d, 352
    mov     r8d, PANEL_W - 20
    mov     r9d, 1
    call    hud_divider_color
    call    hud_fill_rect

    call    hud_render_telem_footer

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbp
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; hud_render_tab_ctrl (12 Flight Deck rows @ pitch 25!)
; ----------------------------------------------------------------------------
hud_render_tab_ctrl:
    push    rbx
    push    rsi
    push    r12
    push    r13

    xor     r12d, r12d
.lp:
    cmp     r12d, CTRL_ROW_COUNT
    jae     .foot

    mov     eax, r12d
    imul    eax, CTRL_ROW_PITCH
    add     eax, 45
    mov     r13d, eax

    cmp     r12d, dword [g_ctrl_sel]
    jne     .no_hi
    mov     r10d, 14
    lea     r11d, [r13 - 3]
    mov     r8d, PANEL_W - 28
    mov     r9d, 22
    call    hud_sel_bg_color
    call    hud_fill_rect
    call    hud_accent_color
    mov     ebx, eax
    jmp     .draw_row
.no_hi:
    xor     eax, eax
    call    pick_metric_color
    mov     ebx, eax
.draw_row:
    mov     rsi, qword [ctrl_keys_tbl + r12*8]
    mov     r10d, 24
    mov     r11d, r13d
    mov     ecx, 17
    mov     eax, ebx
    call    hud_draw_str_2x

    mov     rsi, qword [ctrl_desc_tbl + r12*8]
    mov     r10d, 312
    mov     r11d, r13d
    mov     ecx, 26
    cmp     r12d, dword [g_ctrl_sel]
    je      .desc_hi
    call    pick_label_color
    jmp     .desc_go
.desc_hi:
    mov     eax, ebx
.desc_go:
    call    hud_draw_str_2x

    inc     r12d
    jmp     .lp

.foot:
    mov     r10d, 10
    mov     r11d, 352
    mov     r8d, PANEL_W - 20
    mov     r9d, 1
    call    hud_divider_color
    call    hud_fill_rect

    mov     r10d, 14
    mov     r11d, 358
    lea     rsi, [szCtrlFoot]
    mov     ecx, 45
    call    hud_accent_color
    call    hud_draw_str_2x

    pop     r13
    pop     r12
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; hud_fmt_set_val (15 settings incl. MASTER CAUT!)
; ----------------------------------------------------------------------------
hud_fmt_set_val:
    push    rsi
    push    rdi
    lea     rdi, [g_foot_buf]

    cmp     r13d, 0
    je      .v_pal
    cmp     r13d, 1
    je      .v_opa
    cmp     r13d, 2
    je      .v_barmode
    cmp     r13d, 3
    je      .v_bg
    cmp     r13d, 4
    je      .v_scale
    cmp     r13d, 5
    je      .v_anchor
    cmp     r13d, 6
    je      .v_nudge
    cmp     r13d, 7
    je      .v_rate
    cmp     r13d, 8
    je      .v_group
    cmp     r13d, 9
    je      .v_thresh
    cmp     r13d, 10
    je      .v_d3
    cmp     r13d, 11
    je      .v_fs
    cmp     r13d, 12
    je      .v_caut
    cmp     r13d, 13
    je      .v_start
    lea     rsi, [szSetResetVal]
    jmp     .copy_plain

.v_pal:
    mov     eax, dword [g_palette]
    mov     rsi, qword [set_pal_names + rax*8]
    jmp     .wrap_val
.v_opa:
    mov     eax, dword [g_opacity_idx]
    mov     rsi, qword [set_opa_names + rax*8]
    jmp     .wrap_val
.v_barmode:
    mov     eax, dword [g_bar_mode]
    mov     rsi, qword [set_bmode_names + rax*8]
    jmp     .wrap_val
.v_bg:
    mov     eax, dword [g_backdrop]
    mov     rsi, qword [set_bg_names + rax*8]
    jmp     .wrap_val
.v_scale:
    lea     rsi, [szSetSc2x]
    cmp     dword [g_scale], 2
    je      .wrap_val
    lea     rsi, [szSetSc1x]
    jmp     .wrap_val
.v_anchor:
    mov     eax, dword [g_anchor]
    mov     rsi, qword [set_anc_names + rax*8]
    jmp     .wrap_val
.v_nudge:
    mov     byte [rdi + 0], '<'
    mov     byte [rdi + 1], ' '
    add     rdi, 2
    mov     eax, dword [g_nudge_x]
    call    fmt_i32_dec
    mov     dword [rdi], ' PX '
    add     rdi, 4
    mov     byte [rdi + 0], '>'
    mov     byte [rdi + 1], 0
    jmp     .done
.v_rate:
    mov     eax, dword [g_rate_idx]
    test    eax, eax
    jnz     .v_rate_tbl
    lea     rsi, [szRateVsyncPfx]
    mov     ecx, 10
    call    copy_cstr_lim
    mov     eax, dword [g_display_hz]
    call    fmt_u32_dec
    mov     dword [rdi], 'HZ >'
    add     rdi, 4
    mov     byte [rdi], 0
    jmp     .done
.v_rate_tbl:
    mov     rsi, qword [set_rate_names + rax*8 - 8]
    jmp     .wrap_val
.v_group:
    mov     eax, dword [g_group_pids]
    mov     rsi, qword [set_grp_names + rax*8]
    jmp     .wrap_val
.v_thresh:
    mov     eax, dword [g_thresh_idx]
    mov     rsi, qword [set_thr_names + rax*8]
    jmp     .wrap_val
.v_d3:
    mov     eax, dword [g_d3_guard]
    mov     rsi, qword [set_d3_names + rax*8]
    jmp     .wrap_val
.v_fs:
    mov     eax, dword [g_fs_autohide]
    mov     rsi, qword [set_onoff_names + rax*8]
    jmp     .wrap_val
.v_caut:
    mov     eax, dword [g_caution_en]
    mov     rsi, qword [set_onoff_names + rax*8]
    jmp     .wrap_val
.v_start:
    mov     eax, dword [g_startup_on]
    mov     rsi, qword [set_onoff_names + rax*8]

.wrap_val:
    mov     byte [rdi + 0], '<'
    mov     byte [rdi + 1], ' '
    add     rdi, 2
    mov     ecx, 20
    call    copy_cstr_lim
    mov     byte [rdi + 0], ' '
    mov     byte [rdi + 1], '>'
    mov     byte [rdi + 2], 0
    jmp     .done

.copy_plain:
    mov     ecx, 24
    call    copy_cstr_lim
    mov     byte [rdi], 0

.done:
    pop     rdi
    pop     rsi
    ret

; ----------------------------------------------------------------------------
; hud_render_tab_set
; ----------------------------------------------------------------------------
hud_render_tab_set:
    push    rbx
    push    rsi
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r15d, dword [g_set_scroll]
    xor     r12d, r12d
.lp:
    cmp     r12d, SET_VIS_ROWS
    jae     .scroll_ind

    lea     r13d, [r15 + r12]
    cmp     r13d, SET_ROW_COUNT
    jae     .scroll_ind

    mov     eax, r12d
    imul    eax, SET_ROW_PITCH
    add     eax, SET_ROW_Y0
    mov     r14d, eax

    cmp     r13d, dword [g_set_sel]
    jne     .no_hi
    mov     r10d, 14
    lea     r11d, [r14 - 4]
    mov     r8d, PANEL_W - 28
    mov     r9d, 24
    call    hud_sel_bg_color
    call    hud_fill_rect
    call    hud_accent_color
    mov     ebx, eax
    jmp     .draw_lbl
.no_hi:
    call    pick_label_color
    mov     ebx, eax
.draw_lbl:
    mov     rsi, qword [set_label_tbl + r13*8]
    mov     r10d, 24
    mov     r11d, r14d
    mov     ecx, 14
    mov     eax, ebx
    call    hud_draw_str_2x

    call    hud_fmt_set_val
    mov     r10d, 276
    mov     r11d, r14d
    lea     rsi, [g_foot_buf]
    mov     ecx, 26
    cmp     r13d, dword [g_set_sel]
    je      .val_hi
    xor     eax, eax
    call    pick_metric_color
    jmp     .val_go
.val_hi:
    mov     eax, ebx
.val_go:
    call    hud_draw_str_2x

    inc     r12d
    jmp     .lp

.scroll_ind:
    mov     r10d, 742
    mov     r11d, 46
    mov     r8d, 4
    mov     r9d, 290
    call    hud_bg_well_color
    call    hud_fill_rect

    mov     eax, dword [g_set_scroll]
    imul    eax, 20
    add     eax, 46
    mov     r10d, 742
    mov     r11d, eax
    mov     r8d, 4
    mov     r9d, 186
    call    hud_accent_color
    call    hud_fill_rect

    mov     r10d, 10
    mov     r11d, 352
    mov     r8d, PANEL_W - 20
    mov     r9d, 1
    call    hud_divider_color
    call    hud_fill_rect

    mov     r10d, 14
    mov     r11d, 358
    lea     rsi, [szSetFoot]
    mov     ecx, 45
    call    hud_accent_color
    call    hud_draw_str_2x

    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; hud_render_panel: master 6-theme demoscene render pass!
; ----------------------------------------------------------------------------
hud_render_panel:
    push    rbx
    push    rdi

    mov     eax, dword [g_palette]
    mov     eax, dword [theme_bg_col_rgb + rax*4]
    call    scale_premul_alpha
    mov     dword [g_cached_bg_col], eax

    mov     eax, dword [g_palette]
    mov     eax, dword [theme_bg_main_rgb + rax*4]
    call    scale_premul_alpha

    mov     rdi, qword [g_panel_bits]
    mov     ecx, PANEL_W * PANEL_H
    rep     stosd

    xor     r10d, r10d
    xor     r11d, r11d
    mov     r8d, PANEL_W
    mov     r9d, PANEL_H
    call    hud_border_out_color
    call    hud_draw_box

    call    hud_render_theme_chrome

    mov     rdi, qword [g_panel_bits]
    mov     dword [rdi + 0], 0
    mov     dword [rdi + (PANEL_W - 1)*4], 0
    mov     dword [rdi + ((PANEL_H - 1)*PANEL_W)*4], 0
    mov     dword [rdi + ((PANEL_H - 1)*PANEL_W + PANEL_W - 1)*4], 0

    call    hud_render_top_bar

    mov     eax, dword [g_panel_tab]
    test    eax, eax
    jz      .do_telem
    cmp     eax, 1
    je      .do_ctrl
    call    hud_render_tab_set
    jmp     .post_fx
.do_telem:
    call    hud_render_tab_telem
    jmp     .post_fx
.do_ctrl:
    call    hud_render_tab_ctrl

.post_fx:
    call    hud_post_crt_scanlines

    pop     rdi
    pop     rbx
    ret
