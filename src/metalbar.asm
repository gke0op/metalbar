; ============================================================================
; metalbar.asm — Pure x86-64 FASM Windows 11 Telemetry Bar & HUD v3.0.1
;
; Zero CRT, zero COM, zero UIA, zero PDH, zero WMI, zero DirectX, zero fonts.
; Strictly 4 static OS DLLs: ntdll, kernel32, user32, gdi32.
;
; Features:
;   1. Per-process dedicated VRAM via D3DKMT Type 9 (PROCESS_SEGMENT_GROUP)
;   2. 5-minute 1 Hz static ring-buffer history (300 frames, 9.4 KiB .bss)
;   3. Direct process control: F (NtSuspendProcess/NtResumeProcess),
;      L (IDLE/NORMAL SetPriorityClass), K (TerminateProcess w/ denylist & hash)
;   4. Fault flags (!TMP / !VRM / !RAM) + QPC/DwmFlush frame jitter readout
; ============================================================================

format PE64 GUI 5.0
entry start

include 'win64a.inc'
include 'metal_defs.inc'

; --- Configuration Constants ---
TIMER_ID         = 1
TRAY_MARGIN_R    = 340
EDGE_MARGIN      = 16
NUDGE_STEP       = 32
NUDGE_LIMIT      = 3840

; Hotkey IDs
HK_QUIT          = 1
HK_HIDE          = 2
HK_BACKDROP      = 3
HK_PALETTE       = 4
HK_SCALE         = 5
HK_ANCHOR        = 6
HK_LEFT          = 7
HK_RIGHT         = 8
HK_HUD           = 9

; ============================================================================
; .rdata — Strings, 6-Theme Color Tables & Embedded 768-byte 8x8 ASCII Font
; ============================================================================
section '.rdata' data readable

include 'font8x8.inc'

align 16
crt_scan_mask   dd 0x003F3F3F, 0x003F3F3F, 0x003F3F3F, 0x003F3F3F

szClassName     db 'MetalBarHUD', 0
szPanelClass    db 'MetalBarPanel', 0
szTrayClass     db 'Shell_TrayWnd', 0
szTaskbarMsg    db 'TaskbarCreated', 0
szMutexName     db 'Local\MetalBarHUD_v1', 0
szCfgName       db 'metalbar.cfg', 0
szRunKey        db 'Software\Microsoft\Windows\CurrentVersion\Run', 0
szRunValName    db 'MetalBar', 0
szShotFile      db 'metalbar_shot.bmp', 0
szTxtFile       db 'metalbar_shot.txt', 0
szHudShotFile   db 'metalbar_hud.bmp', 0
szBbShotFile    db 'metalbar_hud_bb.bmp', 0
szSetShotFile   db 'metalbar_hud_set.bmp', 0
szThemeFile0    db 'theme_0_heat.bmp', 0
szThemeFile1    db 'theme_1_crt.bmp', 0
szThemeFile2    db 'theme_2_forge.bmp', 0
szThemeFile3    db 'theme_3_synth.bmp', 0
szThemeFile4    db 'theme_4_blueprint.bmp', 0
szThemeFile5    db 'theme_5_nerv.bmp', 0

align 8
theme_shot_files dq szThemeFile0, szThemeFile1, szThemeFile2, szThemeFile3, szThemeFile4, szThemeFile5

; Quick-Kill / Freeze / Low-Gear Critical OS Process Denylist
PROT_PROC_COUNT = 13
szProt0  db 'csrss.exe', 0
szProt1  db 'smss.exe', 0
szProt2  db 'wininit.exe', 0
szProt3  db 'winlogon.exe', 0
szProt4  db 'lsass.exe', 0
szProt5  db 'services.exe', 0
szProt6  db 'svchost.exe', 0
szProt7  db 'dwm.exe', 0
szProt8  db 'explorer.exe', 0
szProt9  db 'fontdrvhost.exe', 0
szProt10 db 'Registry', 0
szProt11 db 'Memory Compression', 0
szProt12 db 'System', 0

align 8
prot_proc_tbl:
    dq szProt0, szProt1, szProt2, szProt3, szProt4, szProt5, szProt6
    dq szProt7, szProt8, szProt9, szProt10, szProt11, szProt12

; Dynamic DLL & export names
szNvmlDll       db 'nvml.dll', 0
szNvmlInit      db 'nvmlInit_v2', 0
szNvmlGetHandle db 'nvmlDeviceGetHandleByIndex_v2', 0
szNvmlGetUtil   db 'nvmlDeviceGetUtilizationRates', 0
szNvmlGetTemp   db 'nvmlDeviceGetTemperature', 0
szNvmlGetPower  db 'nvmlDeviceGetPowerUsage', 0
szNvmlGetLimit  db 'nvmlDeviceGetEnforcedPowerLimit', 0
szNvmlGetClock  db 'nvmlDeviceGetClockInfo', 0
szNvmlGetThrottle db 'nvmlDeviceGetCurrentClocksThrottleReasons', 0
szNvmlShutdown  db 'nvmlShutdown', 0

szIphlpapiDll   db 'iphlpapi.dll', 0
szGetIfTable2   db 'GetIfTable2', 0
szFreeMibTable  db 'FreeMibTable', 0

szPowrprofDll   db 'powrprof.dll', 0
szCallNtPower   db 'CallNtPowerInformation', 0

szAdvapiDll     db 'advapi32.dll', 0
szRegOpenKeyExA db 'RegOpenKeyExA', 0
szRegQueryValA  db 'RegQueryValueExA', 0
szRegSetValA    db 'RegSetValueExA', 0
szRegDelValA    db 'RegDeleteValueA', 0
szRegCloseKey   db 'RegCloseKey', 0

szDwmapiDll     db 'dwmapi.dll', 0
szDwmFlush      db 'DwmFlush', 0

; --- 6 Signature Theme Color Tables ---
align 4
theme_bg_main_rgb dd 0x00090C12, 0x00031006, 0x00130E0A, 0x0009061B, 0x000A1F3D, 0x000E0406
theme_bg_col_rgb  dd 0x000D121B, 0x00041709, 0x001A130D, 0x00100A28, 0x000D264A, 0x00150609

theme_well_bg     dd 0xF006090E, 0xF2020B04, 0xF00D0805, 0xF0070416, 0xF0081A34, 0xF20A0204
theme_accent_col  dd 0xFF38D4FF, 0xFF44FF88, 0xFFFFA820, 0xFFFF2888, 0xFFF2F8FF, 0xFFFF3038
theme_sel_bg      dd 0xEE142A3D, 0xEE0B381B, 0xEE36210A, 0xEE2B1045, 0xEE1C4880, 0xEE3C0C12
theme_tab_bg      dd 0xF0152232, 0xF0092C14, 0xF02A1B0C, 0xF0220D38, 0xF0163B6C, 0xF02E090E
theme_bdr_out     dd 0xF4263042, 0xF824A850, 0xF8B87220, 0xF69828D8, 0xF874ACE8, 0xF8D81E28
theme_bdr_dim     dd 0xEC1F2838, 0xEC0E4B22, 0xEC46331E, 0xEC3B1E68, 0xEC32629C, 0xEC5A1218
theme_divider     dd 0xE81C2433, 0xE8104820, 0xE8583816, 0xE8462078, 0xE82A5A94, 0xE868141A
theme_txt_dim     dd 0xFF6B7888, 0xFF1D6E38, 0xFF7D5A34, 0xFF7852A8, 0xFF5A8AC2, 0xFF8C363C

theme_label_col   dd 0xFF96A4B4, 0xFF1F9E4A, 0xFFB87830, 0xFFC058FF, 0xFF7CB4E8, 0xFFD93038
theme_cool_col    dd 0xFFEAF2F8, 0xFF42FF82, 0xFFFFB428, 0xFF28F4FF, 0xFFF2F8FF, 0xFFFFAA28
theme_warm_col    dd 0xFFFFB422, 0xFF42FF82, 0xFFFF7A18, 0xFF28F4FF, 0xFFF2F8FF, 0xFFFF5A1F
theme_hot_col     dd 0xFFFF3B30, 0xFFAEFFCA, 0xFFFFEEAA, 0xFFFF2A88, 0xFFFF4B4B, 0xFFFFE8D8
theme_out_col     dd 0xE0000000, 0xE600280A, 0xE6220D00, 0xE618052E, 0xEA051228, 0xE8280306
theme_pill_bg     dd 0xBC080B10, 0xC8020D05, 0xC8140D08, 0xC40A061C, 0xD4081B36, 0xCC120305
theme_pill_bdr    dd 0xD8242C38, 0xE01D8842, 0xE4B86B14, 0xE09C28D8, 0xE46098D8, 0xE8D81E28

; HUD Tab, History & Process Status Strings
szTabTelem      db 'TELEM', 0
szTabCtrl       db 'CTRL', 0
szTabSet        db 'SET', 0
szFootGpu       db 'GPU ', 0
szFootVram      db 'DEDIC VRAM  ', 0
szFootHint      db 'SPACE:HISTORY  F:SUSPEND  L:IDLE  K:KILL', 0
szKillPfx       db '> KILL ', 0
szKillAsk       db ']? PRESS [Y/N]', 0
szBbTopPfx      db 'HISTORY   T-', 0
szBbFootHint    db '[L/R E]', 0
szCtrlFoot      db 'UP/DN:SELECT  ENTER:RUN  TAB:NEXT  ESC:CLOSE', 0
szSetFoot       db 'UP/DN:SEL  L/R:EDIT  ENTER:APPLY  AUTO-SAVED', 0
szRateVsyncPfx  db '< VSYNC ', 0

szStFrozen      db '> SUSPENDED (0% CPU): ', 0
szStThawed      db '> RESUMED: ', 0
szStLowOn       db '> IDLE PRIORITY: ', 0
szStLowOff      db '> NORMAL PRIORITY: ', 0
szStBlocked     db '> BLOCKED: PROTECTED OS/SELF PROCESS', 0
szStDenied      db '> ACCESS DENIED (NEEDS ELEVATION): ', 0
szStNoFault     db '> HISTORY: NO FAULTS IN 5M RING', 0

; Controls Tab Rows (12 rows)
szCk0  db 'CTRL+SHF+SPACE', 0
szCk1  db 'SPACE (IN HUD)', 0
szCk2  db 'E (IN HISTORY)', 0
szCk3  db 'F / L (ON PROC)', 0
szCk4  db 'K / DELETE', 0
szCk5  db 'ARROWS / CLICK', 0
szCk6  db 'CTRL+SHF+C', 0
szCk7  db 'CTRL+SHF+B', 0
szCk8  db 'CTRL+SHF+S', 0
szCk9  db 'CTRL+SHF+A', 0
szCk10 db 'CTRL+SHF+Q', 0
szCk11 db 'ESC / ALT+F4', 0

szCd0  db 'TOGGLE TELEMETRY HUD', 0
szCd1  db 'PAUSE & SCRUB 5M RING', 0
szCd2  db 'JUMP TO LAST FAULT', 0
szCd3  db 'SUSPEND / IDLE PRIORITY', 0
szCd4  db 'TERMINATE SELECTED PROC', 0
szCd5  db 'NAV COLUMNS & SELECT', 0
szCd6  db 'CYCLE THEME   [ENTER]', 0
szCd7  db 'TOGGLE BAR BG [ENTER]', 0
szCd8  db 'TOGGLE SCALE  [ENTER]', 0
szCd9  db 'CYCLE ANCHOR  [ENTER]', 0
szCd10 db 'EXIT METALBAR [ENTER]', 0
szCd11 db 'HIDE HUD      [ENTER]', 0

align 8
ctrl_keys_tbl:
    dq szCk0, szCk1, szCk2, szCk3, szCk4, szCk5, szCk6, szCk7, szCk8, szCk9, szCk10, szCk11
ctrl_desc_tbl:
    dq szCd0, szCd1, szCd2, szCd3, szCd4, szCd5, szCd6, szCd7, szCd8, szCd9, szCd10, szCd11

; Settings Tab Row Labels (15 rows!)
szSl0  db 'THEME', 0
szSl1  db 'HUD OPACITY', 0
szSl2  db 'BAR MODE', 0
szSl3  db 'BAR BACKDROP', 0
szSl4  db 'BAR SCALE', 0
szSl5  db 'DOCK ANCHOR', 0
szSl6  db 'NUDGE X', 0
szSl7  db 'POLL RATE', 0
szSl8  db 'GROUP PIDS', 0
szSl9  db 'HEAT THRESH', 0
szSl10 db 'GPU D3 GUARD', 0
szSl11 db 'FS AUTO-HIDE', 0
szSl12 db 'FAULT FLAGS', 0
szSl13 db 'WIN STARTUP', 0
szSl14 db 'RESET CONFIG', 0

align 8
set_label_tbl:
    dq szSl0, szSl1, szSl2, szSl3, szSl4, szSl5, szSl6
    dq szSl7, szSl8, szSl9, szSl10, szSl11, szSl12, szSl13, szSl14

; Setting Option Names
szPal0      db 'OBSIDIAN HEAT', 0
szPal1      db 'PHOSPHOR CRT', 0
szPal2      db 'MOLTEN FORGE', 0
szPal3      db 'SYNTHGRID', 0
szPal4      db 'BLUEPRINT CAD', 0
szPal5      db 'NERV CRIMSON', 0

szOpa75     db '75% GLASS', 0
szOpa85     db '85% GLASS', 0
szOpa92     db '92% OBSIDIAN', 0
szOpa100    db '100% SOLID', 0

szBmDef     db 'DEFAULT (5)', 0
szBmThm     db 'THERMAL (+C)', 0
szBmMin     db 'MINIMAL (3)', 0
szBmFull    db 'FULL (+C +VRM)', 0

szBgGhost   db 'FLOATING GHOST', 0
szBgStrip   db 'DARK STRIP', 0
szSetSc2x   db '2X CHUNKY RETRO', 0
szSetSc1x   db '1X MICRO PIXEL', 0
szAncBR     db 'BOTTOM-RIGHT', 0
szAncBL     db 'BOTTOM-LEFT', 0
szAncTR     db 'TOP-RIGHT', 0
szAncTL     db 'TOP-LEFT', 0

szRate100   db '100MS (10HZ)', 0
szRate250   db '250MS (4HZ)', 0
szRate500   db '500MS (2HZ)', 0
szRate1000  db '1000MS (1HZ)', 0
szRate2000  db '2000MS (ECO)', 0

szGrpOff    db 'OFF (PER-PID)', 0
szGrpOn     db 'ON (BY APP *)', 0

szThrBal    db 'BALANCED 65/85', 0
szThrChill  db 'CHILL 75/90', 0
szThrStrict db 'STRICT 50/75', 0

szD3Always  db 'ALWAYS POLL', 0
szD3Auto    db 'AUTO SLEEP', 0

szOff       db 'OFF', 0
szOn        db 'ON', 0
szSetResetVal db '[ENTER] RESTORE DEFAULTS', 0

align 8
set_pal_names   dq szPal0, szPal1, szPal2, szPal3, szPal4, szPal5
set_opa_names   dq szOpa75, szOpa85, szOpa92, szOpa100
set_bmode_names dq szBmDef, szBmThm, szBmMin, szBmFull
set_bg_names    dq szBgGhost, szBgStrip
set_anc_names   dq szAncBR, szAncBL, szAncTR, szAncTL
set_rate_names  dq szRate100, szRate250, szRate500, szRate1000, szRate2000
set_grp_names   dq szGrpOff, szGrpOn
set_thr_names   dq szThrBal, szThrChill, szThrStrict
set_d3_names    dq szD3Always, szD3Auto
set_onoff_names dq szOff, szOn

align 4
rate_ms_tbl     dd 100, 100, 250, 500, 1000, 2000
thresh_warn_tbl dd 65, 75, 50
thresh_hot_tbl  dd 85, 90, 75

; ============================================================================
; .data — Initialized runtime defaults (overridden by metalbar.cfg v5)
; ============================================================================
section '.data' data readable writeable

g_scale         dd 2
g_backdrop      dd 0
g_palette       dd 0
g_anchor        dd 1
g_visible       dd 1
g_fs_hidden     dd 0
g_nudge_x       dd 0
g_opacity_idx   dd 2
g_bar_mode      dd 0
g_rate_idx      dd 3
g_group_pids    dd 0
g_thresh_idx    dd 0
g_heat_warn     dd 65
g_heat_hot      dd 85
g_d3_guard      dd 1
g_fs_autohide   dd 1
g_caution_en    dd 1            ; Master Caution taskbar annunciator ON by default!
g_startup_on    dd 0
g_display_hz    dd 60

g_bar_chars     dd 43
g_hud_w         dd 704
g_hud_h         dd HUD_MAX_H
g_pos_dirty     dd 1

g_panel_open    dd 0
g_panel_tab     dd 0
g_col_active    dd 0
g_ctrl_sel      dd 0
g_set_sel       dd 0
g_set_scroll    dd 0
g_kill_confirm  dd 0
g_kill_pid      dd 0
g_kill_count    dd 0
g_anim_frame    dd 0

g_bb_paused     dd 0
g_bb_offset     dd 0

; ============================================================================
; .bss — 100% static zero-initialized state (0 bytes on disk, ZERO heap!)
; ============================================================================
section '.bss' data readable writeable

align 16
g_hInst         dq ?
g_hwnd          dq ?
g_hwnd_panel    dq ?
g_prev_fg       dq ?
g_hMutex        dq ?
g_hook_fg       dq ?
g_msg_tb_created dd ?
g_self_pid      dd ?

; VSync / QPC Jitter / Caching state
align 8
g_pDwmFlush     dq ?
g_last_poll_ms  dq ?
g_last_deep_ms  dq ?
g_last_fs_ms    dq ?
g_last_bar_sig  dq ?
g_qpc_freq      dq ?
g_qpc_prev      dq ?
g_qpc_cur       dq ?
g_jitter_10ms   dd ?            ; peak-decayed compositor/scheduler jitter in 0.1ms units!
g_cached_bg_col dd ?
g_devmode       rb 220

; Cockpit Action Status Flash (2.5s auto-expire)
align 8
g_status_until_ms dq ?
g_status_active dd ?
g_status_color  dd ?
g_status_buf    rb 96

; Bottom bar DIB
align 8
g_mem_dc        dq ?
g_dib_bmp       dq ?
g_old_bmp       dq ?
g_dib_bits      dq ?

; Panel DIB (760 x 388)
g_panel_dc      dq ?
g_panel_bmp     dq ?
g_panel_old     dq ?
g_panel_bits    dq ?

g_col_sel       rd 5
g_col_scroll    rd 5

; Win32 helpers
g_wc            rb 80
g_msg           rb 48
g_bmi           rb 44
g_rc_tray       rb 16
g_rc_work       rb 16
g_rc_fg         rb 16
g_fg_cls        rb 32
g_pt_dst        dd ?, ?
g_pt_panel      dd ?, ?
g_pt_src        dd ?, ?
g_sz_hud        dd ?, ?
g_blend         dd ?

; Paths, Quoted Startup Path, Config & Target Process Descriptor
g_exe_path      rb 300
g_exe_path_q    rb 304
g_exe_path_q_len dd ?
g_cfg_path      rb 300
align 8
g_cfg_buf       rd 16
g_kill_hash     dq ?
g_kill_name     rb 24

; Telemetry state
align 8
g_prev_ft       dq ?
g_cur_ft        dq ?
g_deep_prev_ft  dq ?
g_deep_cur_ft   dq ?
g_prev_io_bytes dq ?
g_io_bps        dq ?
g_vram_committed dq ?
g_phys_pages    dd ?
g_ram_tot_mb    dd ?
g_ram_used_mb   dd ?
g_vram_tot_mb   dd ?
g_vram_used_mb  dd ?
g_num_cores     dd ?
g_cpu_pct       dd ?
g_ram_pct       dd ?
g_gpu_pct       dd ?
g_igpu_pct      dd ?
g_vram_pct      dd ?
g_ret_len       dd ?
g_gpu_count     dd ?
g_vram_gpu_idx  dd ?
g_qai_flags     dd ?
g_close_handle  dd ?
g_hist_head     dd ?
g_deep_primed   dd ?
g_nvml_tried    dd ?
g_temp_polled   dd ?
g_caution_flags dd ?

; Avionics 5-Minute Black-Box Flight Data Recorder (9.4 KiB static .bss!)
align 8
g_bb_last_ms    dq ?
g_bb_head       dd ?
g_bb_count      dd ?
align 16
g_bb_ring       rb BB_LEN * BB_STRIDE
g_bb_wave_tmp   rb 32
g_bb_fault_tmp  rb 32

align 16
g_core_pct      rb MAX_LOGICAL_CPUS
g_disp_core     rw MAX_LOGICAL_CPUS
g_hist_cpu      rb HIST_LEN
g_hist_ram      rb HIST_LEN
g_hist_gpu      rb HIST_LEN
g_hist_vram     rb HIST_LEN
g_hist_io       rb HIST_LEN

; Dynamic DLL function pointers
align 8
g_nvml_dev      dq ?
g_pNvmlGetUtil  dq ?
g_pNvmlGetTemp  dq ?
g_pNvmlGetPower dq ?
g_pNvmlGetLimit dq ?
g_pNvmlGetClock dq ?
g_pNvmlGetThrottle dq ?
g_pNvmlShutdown dq ?
g_nvml_util     dd ?, ?
g_nv_tmp32      dd ?
g_gpu_temp      dd ?
g_gpu_pwr_w     dd ?
g_gpu_pwr_lim   dd ?
g_gpu_clk       dd ?
g_gpu_mclk      dd ?
g_gpu_throttle  dq ?

g_pGetIfTable2  dq ?
g_pFreeMibTable dq ?
g_if_tbl_ptr    dq ?
g_prev_net_rx   dq ?
g_prev_net_tx   dq ?
g_net_rx_bps    dq ?
g_net_tx_bps    dq ?

g_pCallNtPower  dq ?
g_bat_buf       rb 40
g_bat_pct       dd ?
g_bat_rate_mw   dd ?
g_bat_ac        db ?
g_bat_present   db ?

align 8
g_pRegOpenKeyExA dq ?
g_pRegQueryValA  dq ?
g_pRegSetValA    dq ?
g_pRegDelValA    dq ?
g_pRegCloseKey   dq ?
g_hRegKey        dq ?
g_reg_tmp_len    dd ?

; Process table + Per-Process VRAM + Frozen/Low-Gear Trackers
align 8
g_tmp_cpu_t     dq ?
g_tmp_io_b      dq ?
g_tmp_io_bps    dq ?
g_tmp_phash     dq ?
g_tmp_mem_mb    dd ?
g_tmp_cpu_pct   dd ?
g_tmp_vram_mb   dd ?
g_tmp_pflags    dd ?
g_tmp_pname     rb 24
g_proc_count    dd ?
g_prev_proc_cnt dd ?
g_top_count     dd ?
g_top_vram_count dd ?
g_top_cpu       rd MAX_TOP_PROCS
g_top_ram       rd MAX_TOP_PROCS
g_top_io        rd MAX_TOP_PROCS
g_top_vram      rd MAX_TOP_PROCS
align 8
g_frozen_hashes rq MAX_HASH_TRACK
g_low_hashes    rq MAX_HASH_TRACK

; Scratch buffers
align 16
g_sbi_buf       rb SBI_SIZE
g_spi_buf       rb SPI_BUF_SIZE
g_cpu_prev      rb MAX_LOGICAL_CPUS * SPPI_STRIDE
g_cpu_cur       rb MAX_LOGICAL_CPUS * SPPI_STRIDE
g_ea2_hdr       rb 16
g_ea2_arr       rb MAX_GPU_ADAPTERS * D3DKMT_AI_STRIDE
g_qai_hdr       rb 24
g_d3d_qs        rb D3DKMT_QS_SIZE
g_gpus          rb MAX_HW_GPUS * HWGPU_STRIDE
g_gpu_prev_rt   rq MAX_HW_GPUS * MAX_NODES_GPU

g_procs         rb MAX_PROCS * PROC_STRIDE
g_prev_procs    rb MAX_PREV_PROCS * PPROC_STRIDE
g_next_prev_procs rb MAX_PREV_PROCS * PPROC_STRIDE
g_proc_raw      rb SPI5_BUF_SIZE

align 16
g_text_buf      rb 64
g_char_colors   rd 64
g_cell_buf      rb 64
g_foot_buf      rb 96
g_dec_tmp       rb 24

; 3.7 KiB streaming BMP scanline buffer
align 16
g_bmp_hdr       rb 56
g_bmp_row       rd HUD_MAX_W
g_bytes_written dd ?

; ============================================================================
; .text — Pure x86-64 Code
; ============================================================================
section '.text' code readable executable

include 'telemetry.inc'
include 'raster.inc'
include 'hud_raster.inc'

; ----------------------------------------------------------------------------
; detect_display_hz & load_dwm_flush
; ----------------------------------------------------------------------------
detect_display_hz:
    sub     rsp, 40
    mov     word [g_devmode + DEVMODEA_dmSize], DEVMODEA_SIZE
    xor     ecx, ecx
    mov     edx, ENUM_CURRENT_SETTINGS_
    lea     r8, [g_devmode]
    call    [EnumDisplaySettingsA]
    test    eax, eax
    jz      .done
    mov     eax, dword [g_devmode + DEVMODEA_dmFrequency]
    cmp     eax, 30
    jb      .done
    cmp     eax, 360
    ja      .done
    mov     dword [g_display_hz], eax
.done:
    add     rsp, 40
    ret

load_dwm_flush:
    sub     rsp, 40
    mov     qword [g_pDwmFlush], 0
    lea     rcx, [szDwmapiDll]
    call    [LoadLibraryA]
    test    rax, rax
    jz      .done
    mov     rcx, rax
    lea     rdx, [szDwmFlush]
    call    [GetProcAddress]
    mov     qword [g_pDwmFlush], rax
.done:
    add     rsp, 40
    ret

; ----------------------------------------------------------------------------
; sample_qpc_jitter: sub-ms RDTSC/QPC compositor & scheduler jitter radar!
;   in: ecx = expected_period_us (e.g. 6060us @ 165Hz, or timer_ms * 1000)
;   updates g_jitter_10ms (tenths of ms, peak-hold with smooth decay!)
; ----------------------------------------------------------------------------
sample_qpc_jitter:
    push    rbx
    sub     rsp, 32
    mov     ebx, ecx

    cmp     qword [g_qpc_freq], 0
    je      .done
    lea     rcx, [g_qpc_cur]
    call    [QueryPerformanceCounter]
    mov     rax, qword [g_qpc_cur]
    mov     rcx, qword [g_qpc_prev]
    mov     qword [g_qpc_prev], rax
    test    rcx, rcx
    jz      .done

    sub     rax, rcx
    jle     .done
    ; dt_10ms = (dt_ticks * 10000) / qpc_freq (tenths of milliseconds!)
    mov     r8, 10000
    mul     r8
    mov     r9, qword [g_qpc_freq]
    cmp     rdx, r9
    jae     .done
    div     r9                  ; rax = actual dt in 0.1ms

    ; expected_10ms = expected_period_us / 100
    mov     r8d, eax            ; actual_10ms
    mov     eax, ebx
    xor     edx, edx
    mov     ecx, 100
    div     ecx                 ; eax = expected_10ms

    sub     r8d, eax
    jns     .abs_ok
    neg     r8d
.abs_ok:
    cmp     r8d, 999
    jbe     .cap_ok
    mov     r8d, 999
.cap_ok:
    ; Smooth peak-hold: jump up immediately on micro-stutter, decay gently by 1/16!
    mov     eax, dword [g_jitter_10ms]
    cmp     r8d, eax
    jbe     .decay
    mov     dword [g_jitter_10ms], r8d
    jmp     .done
.decay:
    mov     edx, eax
    sub     edx, r8d
    shr     edx, 4
    jnz     .dec_go
    cmp     eax, r8d
    jbe     .done
    dec     eax
    mov     dword [g_jitter_10ms], eax
    jmp     .done
.dec_go:
    sub     eax, edx
    mov     dword [g_jitter_10ms], eax
.done:
    add     rsp, 32
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; sync_disp_cores: snap or spring-lerp g_disp_core[0..N-1] toward g_core_pct
;   out: eax = 1 if any core bar moved, 0 if all bars settled
; ----------------------------------------------------------------------------
sync_disp_cores:
    xor     r10d, r10d
    mov     edx, dword [g_num_cores]
    test    edx, edx
    jz      .done
    xor     r8d, r8d
.lp:
    cmp     r8d, edx
    jae     .done
    movzx   eax, byte [g_core_pct + r8]
    shl     eax, 8
    movzx   r9d, word [g_disp_core + r8*2]
    cmp     eax, r9d
    je      .next
    mov     r10d, 1
    test    ecx, ecx
    jz      .snap
    sub     eax, r9d
    sar     eax, 3
    jnz     .step_ok
    movzx   eax, byte [g_core_pct + r8]
    shl     eax, 8
    cmp     eax, r9d
    jl      .dec1
    inc     r9d
    mov     word [g_disp_core + r8*2], r9w
    jmp     .next
.dec1:
    dec     r9d
    mov     word [g_disp_core + r8*2], r9w
    jmp     .next
.step_ok:
    add     r9d, eax
    mov     word [g_disp_core + r8*2], r9w
    jmp     .next
.snap:
    mov     word [g_disp_core + r8*2], ax
.next:
    inc     r8d
    jmp     .lp
.done:
    mov     eax, r10d
    ret

; ----------------------------------------------------------------------------
; check_cli_flags: NUL-safe CLI parser
; ----------------------------------------------------------------------------
check_cli_flags:
    push    rsi
    sub     rsp, 32
    call    [GetCommandLineA]
    mov     rsi, rax
.scan:
    mov     al, byte [rsi]
    test    al, al
    jz      .none
    cmp     al, '-'
    jne     .next
    cmp     byte [rsi + 1], '-'
    jne     .next
    cmp     byte [rsi + 2], 's'
    jne     .next
    cmp     byte [rsi + 3], 'h'
    jne     .next
    cmp     byte [rsi + 4], 'o'
    jne     .next
    cmp     byte [rsi + 5], 't'
    jne     .next
    cmp     byte [rsi + 6], '-'
    jne     .ret_shot
    cmp     byte [rsi + 7], 'h'
    jne     .chk_thm
    cmp     byte [rsi + 8], 'u'
    jne     .next
    cmp     byte [rsi + 9], 'd'
    jne     .next
    mov     eax, 2
    jmp     .ret_ok
.chk_thm:
    cmp     byte [rsi + 7], 't'
    jne     .next
    cmp     byte [rsi + 8], 'h'
    jne     .next
    mov     eax, 3
    jmp     .ret_ok
.ret_shot:
    mov     eax, 1
    jmp     .ret_ok
.next:
    inc     rsi
    jmp     .scan
.none:
    xor     eax, eax
.ret_ok:
    add     rsp, 32
    pop     rsi
    ret

; ----------------------------------------------------------------------------
; apply_thresholds & apply_poll_rate
; ----------------------------------------------------------------------------
apply_thresholds:
    mov     eax, dword [g_thresh_idx]
    cmp     eax, 3
    jb      .ok
    xor     eax, eax
    mov     dword [g_thresh_idx], eax
.ok:
    mov     ecx, dword [thresh_warn_tbl + rax*4]
    mov     dword [g_heat_warn], ecx
    mov     ecx, dword [thresh_hot_tbl + rax*4]
    mov     dword [g_heat_hot], ecx
    ret

apply_poll_rate:
    sub     rsp, 40
    mov     qword [g_qpc_prev], 0
    mov     dword [g_jitter_10ms], 0
    cmp     qword [g_hwnd], 0
    je      .done
    mov     rcx, qword [g_hwnd]
    mov     edx, TIMER_ID
    call    [KillTimer]
    mov     eax, dword [g_rate_idx]
    cmp     eax, NUM_RATES
    jb      .ok
    mov     eax, 3
    mov     dword [g_rate_idx], eax
.ok:
    mov     r8d, dword [rate_ms_tbl + rax*4]
    mov     rcx, qword [g_hwnd]
    mov     edx, TIMER_ID
    xor     r9d, r9d
    call    [SetTimer]
.done:
    add     rsp, 40
    ret

; ----------------------------------------------------------------------------
; startup_query & startup_set
; ----------------------------------------------------------------------------
startup_query:
    sub     rsp, 56
    mov     dword [g_startup_on], 0
    cmp     qword [g_pRegOpenKeyExA], 0
    je      .done
    cmp     qword [g_pRegQueryValA], 0
    je      .done
    cmp     qword [g_pRegCloseKey], 0
    je      .done

    mov     rcx, HKEY_CURRENT_USER_
    lea     rdx, [szRunKey]
    xor     r8d, r8d
    mov     r9d, KEY_READ_
    lea     rax, [g_hRegKey]
    mov     qword [rsp + 0x20], rax
    mov     rax, qword [g_pRegOpenKeyExA]
    call    rax
    test    eax, eax
    jnz     .done

    mov     dword [g_reg_tmp_len], 0
    mov     rcx, qword [g_hRegKey]
    lea     rdx, [szRunValName]
    xor     r8d, r8d
    xor     r9d, r9d
    mov     qword [rsp + 0x20], 0
    lea     rax, [g_reg_tmp_len]
    mov     qword [rsp + 0x28], rax
    mov     rax, qword [g_pRegQueryValA]
    call    rax
    test    eax, eax
    jnz     .close
    mov     dword [g_startup_on], 1
.close:
    mov     rcx, qword [g_hRegKey]
    mov     rax, qword [g_pRegCloseKey]
    call    rax
.done:
    add     rsp, 56
    ret

startup_set:
    sub     rsp, 56
    cmp     qword [g_pRegOpenKeyExA], 0
    je      .done
    cmp     qword [g_pRegCloseKey], 0
    je      .done

    mov     rcx, HKEY_CURRENT_USER_
    lea     rdx, [szRunKey]
    xor     r8d, r8d
    mov     r9d, KEY_SET_VALUE_
    lea     rax, [g_hRegKey]
    mov     qword [rsp + 0x20], rax
    mov     rax, qword [g_pRegOpenKeyExA]
    call    rax
    test    eax, eax
    jnz     .done

    cmp     dword [g_startup_on], 0
    je      .del_val

    cmp     qword [g_pRegSetValA], 0
    je      .close
    mov     rcx, qword [g_hRegKey]
    lea     rdx, [szRunValName]
    xor     r8d, r8d
    mov     r9d, REG_SZ_
    lea     rax, [g_exe_path_q]
    mov     qword [rsp + 0x20], rax
    mov     eax, dword [g_exe_path_q_len]
    inc     eax
    mov     dword [rsp + 0x28], eax
    mov     rax, qword [g_pRegSetValA]
    call    rax
    jmp     .close

.del_val:
    cmp     qword [g_pRegDelValA], 0
    je      .close
    mov     rcx, qword [g_hRegKey]
    lea     rdx, [szRunValName]
    mov     rax, qword [g_pRegDelValA]
    call    rax

.close:
    mov     rcx, qword [g_hRegKey]
    mov     rax, qword [g_pRegCloseKey]
    call    rax
.done:
    call    startup_query
    add     rsp, 56
    ret

; ----------------------------------------------------------------------------
; cfg_build_path / cfg_load / cfg_save (64-byte config v5, backward-safe!)
; ----------------------------------------------------------------------------
cfg_build_path:
    push    rsi
    push    rdi
    sub     rsp, 40

    xor     ecx, ecx
    lea     rdx, [g_exe_path]
    mov     r8d, 260
    call    [GetModuleFileNameA]
    test    eax, eax
    jz      .done

    lea     rsi, [g_exe_path]
    lea     rdi, [g_exe_path_q]
    mov     byte [rdi], '"'
    inc     rdi
    mov     ecx, eax
    rep     movsb
    mov     byte [rdi], '"'
    mov     byte [rdi + 1], 0
    lea     ecx, [eax + 2]
    mov     dword [g_exe_path_q_len], ecx

    lea     rsi, [g_exe_path]
    lea     rdi, [g_cfg_path]
    mov     ecx, eax
    inc     ecx
    rep     movsb

    lea     rdi, [g_cfg_path + rax]
.find_slash:
    cmp     rdi, g_cfg_path
    jbe     .copy_name
    dec     rdi
    cmp     byte [rdi], '\'
    jne     .find_slash
    inc     rdi
.copy_name:
    lea     rsi, [szCfgName]
.cp:
    mov     al, byte [rsi]
    mov     byte [rdi], al
    inc     rsi
    inc     rdi
    test    al, al
    jnz     .cp
.done:
    add     rsp, 40
    pop     rdi
    pop     rsi
    ret

cfg_load:
    push    rbx
    push    rsi
    sub     rsp, 56

    lea     rcx, [g_cfg_path]
    mov     edx, 0x80000000
    mov     r8d, 1
    xor     r9d, r9d
    mov     dword [rsp + 0x20], 3
    mov     dword [rsp + 0x28], 0x80
    mov     qword [rsp + 0x30], 0
    call    [CreateFileA]
    cmp     rax, -1
    je      .done
    mov     rbx, rax

    mov     rcx, rbx
    lea     rdx, [g_cfg_buf]
    mov     r8d, CFG_SIZE
    lea     r9, [g_bytes_written]
    mov     qword [rsp + 0x20], 0
    call    [ReadFile]
    mov     esi, eax

    mov     rcx, rbx
    call    [CloseHandle]

    test    esi, esi
    jz      .done
    cmp     dword [g_bytes_written], CFG_SIZE
    jne     .done
    cmp     dword [g_cfg_buf + 0], CFG_MAGIC
    jne     .done

    mov     eax, dword [g_cfg_buf + 0x08]
    cmp     eax, 1
    je      .sc_ok
    mov     eax, 2
.sc_ok:
    mov     dword [g_scale], eax

    mov     eax, dword [g_cfg_buf + 0x0C]
    and     eax, 1
    mov     dword [g_backdrop], eax

    mov     eax, dword [g_cfg_buf + 0x10]
    cmp     eax, NUM_PALETTES
    jb      .pal_ok
    xor     eax, eax
.pal_ok:
    mov     dword [g_palette], eax

    mov     eax, dword [g_cfg_buf + 0x14]
    and     eax, 3
    mov     dword [g_anchor], eax

    mov     eax, dword [g_cfg_buf + 0x18]
    cmp     eax, -NUDGE_LIMIT
    jge     .n_hi
    mov     eax, -NUDGE_LIMIT
.n_hi:
    cmp     eax, NUDGE_LIMIT
    jle     .n_ok
    mov     eax, NUDGE_LIMIT
.n_ok:
    mov     dword [g_nudge_x], eax

    mov     eax, dword [g_cfg_buf + 0x1C]
    cmp     eax, 5
    jb      .col_ok
    xor     eax, eax
.col_ok:
    mov     dword [g_col_active], eax

    mov     eax, dword [g_cfg_buf + 0x20]
    and     eax, 3
    mov     dword [g_opacity_idx], eax

    mov     eax, dword [g_cfg_buf + 0x24]
    and     eax, 3
    mov     dword [g_bar_mode], eax

    mov     eax, dword [g_cfg_buf + 0x28]
    cmp     dword [g_cfg_buf + 0x04], 3
    jne     .rate_v4
    add     eax, 2
.rate_v4:
    cmp     eax, NUM_RATES
    jb      .rate_ok
    mov     eax, 3
.rate_ok:
    mov     dword [g_rate_idx], eax

    mov     eax, dword [g_cfg_buf + 0x2C]
    and     eax, 1
    mov     dword [g_group_pids], eax

    mov     eax, dword [g_cfg_buf + 0x30]
    cmp     eax, 3
    jb      .thr_ok
    xor     eax, eax
.thr_ok:
    mov     dword [g_thresh_idx], eax

    mov     eax, dword [g_cfg_buf + 0x34]
    and     eax, 1
    mov     dword [g_d3_guard], eax

    mov     eax, dword [g_cfg_buf + 0x38]
    and     eax, 1
    mov     dword [g_fs_autohide], eax

    ; v5 adds g_caution_en at 0x3C (migrates v3/v4 cleanly defaulting ON!)
    cmp     dword [g_cfg_buf + 0x04], 5
    jb      .caut_def
    mov     eax, dword [g_cfg_buf + 0x3C]
    and     eax, 1
    mov     dword [g_caution_en], eax
    jmp     .apply_t
.caut_def:
    mov     dword [g_caution_en], 1
.apply_t:
    call    apply_thresholds

.done:
    add     rsp, 56
    pop     rsi
    pop     rbx
    ret

cfg_save:
    push    rbx
    sub     rsp, 64

    mov     dword [g_cfg_buf + 0x00], CFG_MAGIC
    mov     dword [g_cfg_buf + 0x04], 5
    mov     eax, dword [g_scale]
    mov     dword [g_cfg_buf + 0x08], eax
    mov     eax, dword [g_backdrop]
    mov     dword [g_cfg_buf + 0x0C], eax
    mov     eax, dword [g_palette]
    mov     dword [g_cfg_buf + 0x10], eax
    mov     eax, dword [g_anchor]
    mov     dword [g_cfg_buf + 0x14], eax
    mov     eax, dword [g_nudge_x]
    mov     dword [g_cfg_buf + 0x18], eax
    mov     eax, dword [g_col_active]
    mov     dword [g_cfg_buf + 0x1C], eax
    mov     eax, dword [g_opacity_idx]
    mov     dword [g_cfg_buf + 0x20], eax
    mov     eax, dword [g_bar_mode]
    mov     dword [g_cfg_buf + 0x24], eax
    mov     eax, dword [g_rate_idx]
    mov     dword [g_cfg_buf + 0x28], eax
    mov     eax, dword [g_group_pids]
    mov     dword [g_cfg_buf + 0x2C], eax
    mov     eax, dword [g_thresh_idx]
    mov     dword [g_cfg_buf + 0x30], eax
    mov     eax, dword [g_d3_guard]
    mov     dword [g_cfg_buf + 0x34], eax
    mov     eax, dword [g_fs_autohide]
    mov     dword [g_cfg_buf + 0x38], eax
    mov     eax, dword [g_caution_en]
    mov     dword [g_cfg_buf + 0x3C], eax

    lea     rcx, [g_cfg_path]
    mov     edx, 0x40000000
    xor     r8d, r8d
    xor     r9d, r9d
    mov     dword [rsp + 0x20], 2
    mov     dword [rsp + 0x28], 0x80
    mov     qword [rsp + 0x30], 0
    call    [CreateFileA]
    cmp     rax, -1
    je      .done
    mov     rbx, rax

    mov     rcx, rbx
    lea     rdx, [g_cfg_buf]
    mov     r8d, CFG_SIZE
    lea     r9, [g_bytes_written]
    mov     qword [rsp + 0x20], 0
    call    [WriteFile]

    mov     rcx, rbx
    call    [CloseHandle]
.done:
    add     rsp, 64
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; create_dib_surfaces / compute_position / present_hud / present_panel
; ----------------------------------------------------------------------------
create_dib_surfaces:
    push    rbx
    sub     rsp, 48

    xor     ecx, ecx
    call    [GetDC]
    test    rax, rax
    jz      .fail_no_dc
    mov     rbx, rax

    mov     rcx, rbx
    call    [CreateCompatibleDC]
    test    rax, rax
    jz      .fail
    mov     qword [g_mem_dc], rax

    mov     dword [g_bmi + 0], 40
    mov     dword [g_bmi + 4], HUD_MAX_W
    mov     dword [g_bmi + 8], -HUD_MAX_H
    mov     word  [g_bmi + 12], 1
    mov     word  [g_bmi + 14], 32
    mov     dword [g_bmi + 16], BI_RGB_

    mov     rcx, qword [g_mem_dc]
    lea     rdx, [g_bmi]
    mov     r8d, DIB_RGB_COLORS_
    lea     r9, [g_dib_bits]
    mov     qword [rsp + 0x20], 0
    mov     dword [rsp + 0x28], 0
    call    [CreateDIBSection]
    test    rax, rax
    jz      .fail
    cmp     qword [g_dib_bits], 0
    je      .fail
    mov     qword [g_dib_bmp], rax
    mov     rcx, qword [g_mem_dc]
    mov     rdx, rax
    call    [SelectObject]
    mov     qword [g_old_bmp], rax

    mov     rcx, rbx
    call    [CreateCompatibleDC]
    test    rax, rax
    jz      .fail
    mov     qword [g_panel_dc], rax

    mov     dword [g_bmi + 4], PANEL_W
    mov     dword [g_bmi + 8], -PANEL_H

    mov     rcx, qword [g_panel_dc]
    lea     rdx, [g_bmi]
    mov     r8d, DIB_RGB_COLORS_
    lea     r9, [g_panel_bits]
    mov     qword [rsp + 0x20], 0
    mov     dword [rsp + 0x28], 0
    call    [CreateDIBSection]
    test    rax, rax
    jz      .fail
    cmp     qword [g_panel_bits], 0
    je      .fail
    mov     qword [g_panel_bmp], rax
    mov     rcx, qword [g_panel_dc]
    mov     rdx, rax
    call    [SelectObject]
    mov     qword [g_panel_old], rax

    xor     ecx, ecx
    mov     rdx, rbx
    call    [ReleaseDC]

    mov     eax, 1
    add     rsp, 48
    pop     rbx
    ret

.fail:
    xor     ecx, ecx
    mov     rdx, rbx
    call    [ReleaseDC]
.fail_no_dc:
    xor     eax, eax
    add     rsp, 48
    pop     rbx
    ret

compute_position:
    push    rbx
    push    rsi
    push    rdi
    sub     rsp, 32

    xor     ecx, ecx
    call    [GetSystemMetrics]
    mov     esi, eax
    mov     ecx, 1
    call    [GetSystemMetrics]
    mov     edi, eax

    lea     rcx, [szTrayClass]
    xor     edx, edx
    call    [FindWindowA]
    test    rax, rax
    jz      .fallback_workarea

    mov     rcx, rax
    lea     rdx, [g_rc_tray]
    call    [GetWindowRect]
    test    eax, eax
    jz      .fallback_workarea

    mov     eax, dword [g_rc_tray + 12]
    sub     eax, dword [g_rc_tray + 4]
    cmp     eax, 8
    jge     .tray_ready

.fallback_workarea:
    mov     ecx, SPI_GETWORKAREA_
    xor     edx, edx
    lea     r8, [g_rc_work]
    xor     r9d, r9d
    call    [SystemParametersInfoA]

    mov     dword [g_rc_tray + 0], 0
    mov     eax, dword [g_rc_work + 12]
    cmp     eax, edi
    jl      .wa_has_bar
    lea     eax, [edi - 48]
.wa_has_bar:
    mov     dword [g_rc_tray + 4], eax
    mov     dword [g_rc_tray + 8], esi
    mov     dword [g_rc_tray + 12], edi

.tray_ready:
    mov     ebx, dword [g_anchor]
    cmp     ebx, 2
    jae     .anchor_top

    mov     eax, dword [g_rc_tray + 12]
    sub     eax, dword [g_rc_tray + 4]
    sub     eax, dword [g_hud_h]
    sar     eax, 1
    add     eax, dword [g_rc_tray + 4]
    mov     dword [g_pt_dst + 4], eax
    jmp     .calc_x

.anchor_top:
    mov     dword [g_pt_dst + 4], EDGE_MARGIN

.calc_x:
    test    ebx, 1
    jnz     .anchor_left

    mov     eax, dword [g_rc_tray + 8]
    sub     eax, dword [g_hud_w]
    test    ebx, ebx
    jnz     .top_right_margin
    sub     eax, TRAY_MARGIN_R
    jmp     .apply_nudge
.top_right_margin:
    sub     eax, EDGE_MARGIN
    jmp     .apply_nudge

.anchor_left:
    mov     eax, dword [g_rc_tray + 0]
    add     eax, EDGE_MARGIN

.apply_nudge:
    add     eax, dword [g_nudge_x]

    mov     edx, esi
    sub     edx, dword [g_hud_w]
    sub     edx, 4
    cmp     eax, edx
    jle     .x_max_ok
    mov     eax, edx
.x_max_ok:
    cmp     eax, 4
    jge     .x_min_ok
    mov     eax, 4
.x_min_ok:
    mov     dword [g_pt_dst + 0], eax

    mov     edx, esi
    sub     edx, PANEL_W + 8
    cmp     eax, edx
    jle     .px_max
    mov     eax, edx
.px_max:
    cmp     eax, 8
    jge     .px_min
    mov     eax, 8
.px_min:
    mov     dword [g_pt_panel + 0], eax

    cmp     ebx, 2
    jae     .py_below
    mov     eax, dword [g_rc_tray + 4]
    sub     eax, PANEL_H + 8
    cmp     eax, 8
    jge     .py_ok
    mov     eax, 8
    jmp     .py_ok
.py_below:
    mov     eax, dword [g_pt_dst + 4]
    add     eax, dword [g_hud_h]
    add     eax, 10
.py_ok:
    mov     dword [g_pt_panel + 4], eax
    mov     dword [g_pos_dirty], 0

    add     rsp, 32
    pop     rdi
    pop     rsi
    pop     rbx
    ret

present_hud:
    sub     rsp, 88

    cmp     dword [g_visible], 0
    je      .skip
    cmp     dword [g_fs_hidden], 0
    jne     .skip

    mov     ecx, dword [g_pos_dirty]
    call    raster_render
    test    eax, eax
    jnz     .do_upload
    cmp     dword [g_pos_dirty], 0
    je      .skip

.do_upload:
    cmp     dword [g_pos_dirty], 0
    je      .pos_ok
    call    compute_position
.pos_ok:
    mov     eax, dword [g_hud_w]
    mov     dword [g_sz_hud + 0], eax
    mov     eax, dword [g_hud_h]
    mov     dword [g_sz_hud + 4], eax
    mov     dword [g_pt_src + 0], 0
    mov     dword [g_pt_src + 4], 0
    mov     dword [g_blend], (AC_SRC_ALPHA_ shl 24) or (255 shl 16) or AC_SRC_OVER_

    mov     rcx, qword [g_hwnd]
    xor     edx, edx
    lea     r8, [g_pt_dst]
    lea     r9, [g_sz_hud]
    mov     rax, qword [g_mem_dc]
    mov     qword [rsp + 0x20], rax
    lea     rax, [g_pt_src]
    mov     qword [rsp + 0x28], rax
    mov     dword [rsp + 0x30], 0
    lea     rax, [g_blend]
    mov     qword [rsp + 0x38], rax
    mov     dword [rsp + 0x40], ULW_ALPHA_
    call    [UpdateLayeredWindow]

.skip:
    add     rsp, 88
    ret

present_panel:
    sub     rsp, 88

    cmp     dword [g_panel_open], 0
    je      .skip
    cmp     dword [g_visible], 0
    je      .skip
    cmp     dword [g_fs_hidden], 0
    jne     .skip

    ; Auto-expire 2.5s cockpit action status flash cleanly
    cmp     dword [g_status_active], 0
    je      .do_ren
    call    [GetTickCount64]
    cmp     rax, qword [g_status_until_ms]
    jb      .do_ren
    mov     dword [g_status_active], 0

.do_ren:
    call    hud_render_panel
    cmp     dword [g_pos_dirty], 0
    je      .pos_ok
    call    compute_position
.pos_ok:
    mov     dword [g_sz_hud + 0], PANEL_W
    mov     dword [g_sz_hud + 4], PANEL_H
    mov     dword [g_pt_src + 0], 0
    mov     dword [g_pt_src + 4], 0
    mov     dword [g_blend], (AC_SRC_ALPHA_ shl 24) or (255 shl 16) or AC_SRC_OVER_

    mov     rcx, qword [g_hwnd_panel]
    xor     edx, edx
    lea     r8, [g_pt_panel]
    lea     r9, [g_sz_hud]
    mov     rax, qword [g_panel_dc]
    mov     qword [rsp + 0x20], rax
    lea     rax, [g_pt_src]
    mov     qword [rsp + 0x28], rax
    mov     dword [rsp + 0x30], 0
    lea     rax, [g_blend]
    mov     qword [rsp + 0x38], rax
    mov     dword [rsp + 0x40], ULW_ALPHA_
    call    [UpdateLayeredWindow]

.skip:
    add     rsp, 88
    ret

assert_zorder:
    sub     rsp, 56
    cmp     dword [g_visible], 0
    je      .done
    cmp     dword [g_fs_hidden], 0
    jne     .done

    mov     rcx, qword [g_hwnd]
    mov     rdx, HWND_TOPMOST_
    xor     r8d, r8d
    xor     r9d, r9d
    mov     dword [rsp + 0x20], 0
    mov     dword [rsp + 0x28], 0
    mov     dword [rsp + 0x30], SWP_NOMOVE_ or SWP_NOSIZE_ or SWP_NOACTIVATE_
    call    [SetWindowPos]

    cmp     dword [g_panel_open], 0
    je      .done
    mov     rcx, qword [g_hwnd_panel]
    mov     rdx, HWND_TOPMOST_
    xor     r8d, r8d
    xor     r9d, r9d
    mov     dword [rsp + 0x20], 0
    mov     dword [rsp + 0x28], 0
    mov     dword [rsp + 0x30], SWP_NOMOVE_ or SWP_NOSIZE_ or SWP_NOACTIVATE_
    call    [SetWindowPos]
.done:
    add     rsp, 56
    ret

close_panel:
    sub     rsp, 40
    mov     dword [g_kill_confirm], 0
    mov     dword [g_bb_paused], 0
    mov     dword [g_bb_offset], 0
    mov     dword [g_status_active], 0
    mov     dword [g_deep_primed], 0
    mov     qword [g_qpc_prev], 0
    cmp     dword [g_panel_open], 0
    je      .done
    mov     dword [g_panel_open], 0
    mov     rcx, qword [g_hwnd_panel]
    xor     edx, edx
    call    [ShowWindow]
    mov     rcx, qword [g_prev_fg]
    test    rcx, rcx
    jz      .done
    call    [SetForegroundWindow]
    mov     qword [g_prev_fg], 0
.done:
    add     rsp, 40
    ret

toggle_panel:
    sub     rsp, 40
    cmp     dword [g_panel_open], 0
    jne     .do_close

    call    [GetForegroundWindow]
    cmp     rax, qword [g_hwnd_panel]
    je      .fg_ok
    mov     qword [g_prev_fg], rax
.fg_ok:
    mov     dword [g_panel_open], 1
    mov     dword [g_visible], 1
    mov     dword [g_fs_hidden], 0
    mov     dword [g_kill_confirm], 0
    mov     dword [g_bb_paused], 0
    mov     dword [g_bb_offset], 0
    mov     dword [g_pos_dirty], 1
    mov     qword [g_qpc_prev], 0

    xor     ecx, ecx
    call    sync_disp_cores
    call    telemetry_poll_deep
    call    [GetTickCount64]
    mov     qword [g_last_poll_ms], rax
    mov     qword [g_last_deep_ms], rax
    mov     qword [g_last_fs_ms], rax
    call    present_panel

    mov     rcx, qword [g_hwnd_panel]
    mov     edx, SW_SHOW_
    call    [ShowWindow]
    call    assert_zorder
    mov     rcx, qword [g_hwnd_panel]
    call    [SetForegroundWindow]
    add     rsp, 40
    ret

.do_close:
    call    close_panel
    add     rsp, 40
    ret

; ----------------------------------------------------------------------------
; set_status_flash: show 2.5s cockpit feedback banner in footer
;   in: rsi = prefix cstr, rdx = optional app name cstr (or 0), eax = BGRA color
; ----------------------------------------------------------------------------
set_status_flash:
    push    rbx
    push    rsi
    push    rdi
    sub     rsp, 32

    mov     dword [g_status_color], eax
    mov     rbx, rdx
    lea     rdi, [g_status_buf]
    mov     ecx, 45
    call    copy_cstr_lim
    test    rbx, rbx
    jz      .nul
    mov     rsi, rbx
    mov     ecx, 18
    call    copy_cstr_lim
.nul:
    mov     byte [rdi], 0

    call    [GetTickCount64]
    add     rax, 2500
    mov     qword [g_status_until_ms], rax
    mov     dword [g_status_active], 1

    add     rsp, 32
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; write_bmp_generic & write_shot_txt (3.7 KiB streaming row writer!)
; ----------------------------------------------------------------------------
write_bmp_generic:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 64

    mov     rsi, rcx
    mov     r13d, edx
    mov     r14d, r8d
    mov     r15, r9

    mov     eax, r13d
    imul    eax, r14d
    shl     eax, 2
    mov     r12d, eax

    mov     word  [g_bmp_hdr + 0], 'BM'
    lea     edx, [r12 + 54]
    mov     dword [g_bmp_hdr + 2], edx
    mov     dword [g_bmp_hdr + 6], 0
    mov     dword [g_bmp_hdr + 10], 54
    mov     dword [g_bmp_hdr + 14], 40
    mov     dword [g_bmp_hdr + 18], r13d
    mov     dword [g_bmp_hdr + 22], r14d
    mov     word  [g_bmp_hdr + 26], 1
    mov     word  [g_bmp_hdr + 28], 32
    mov     dword [g_bmp_hdr + 30], 0
    mov     dword [g_bmp_hdr + 34], r12d
    mov     dword [g_bmp_hdr + 38], 2835
    mov     dword [g_bmp_hdr + 42], 2835
    mov     dword [g_bmp_hdr + 46], 0
    mov     dword [g_bmp_hdr + 50], 0

    mov     rcx, r15
    mov     edx, 0x40000000
    xor     r8d, r8d
    xor     r9d, r9d
    mov     dword [rsp + 0x20], 2
    mov     dword [rsp + 0x28], 0x80
    mov     qword [rsp + 0x30], 0
    call    [CreateFileA]
    cmp     rax, -1
    je      .done
    mov     r15, rax

    mov     rcx, r15
    lea     rdx, [g_bmp_hdr]
    mov     r8d, 54
    lea     r9, [g_bytes_written]
    mov     qword [rsp + 0x20], 0
    call    [WriteFile]

    lea     r12d, [r14 - 1]
.row_lp:
    cmp     r12d, 0
    jl      .close_file
    mov     eax, r12d
    imul    eax, r13d
    lea     rbx, [rsi + rax*4]
    lea     rdi, [g_bmp_row]
    mov     ecx, r13d
.px_lp:
    mov     eax, dword [rbx]
    mov     edx, eax
    shr     edx, 24
    mov     r8d, 255
    sub     r8d, edx
    imul    r8d, 0x20
    shr     r8d, 8

    mov     r9d, eax
    and     r9d, 0xFF
    add     r9d, r8d
    cmp     r9d, 255
    jbe     .b_ok
    mov     r9d, 255
.b_ok:
    mov     r10d, eax
    shr     r10d, 8
    and     r10d, 0xFF
    add     r10d, r8d
    cmp     r10d, 255
    jbe     .g_ok
    mov     r10d, 255
.g_ok:
    mov     r11d, eax
    shr     r11d, 16
    and     r11d, 0xFF
    add     r11d, r8d
    cmp     r11d, 255
    jbe     .r_ok
    mov     r11d, 255
.r_ok:
    shl     r11d, 16
    shl     r10d, 8
    or      r9d, r10d
    or      r9d, r11d
    or      r9d, 0xFF000000
    mov     dword [rdi], r9d
    add     rbx, 4
    add     rdi, 4
    dec     ecx
    jnz     .px_lp

    mov     rcx, r15
    lea     rdx, [g_bmp_row]
    lea     r8d, [r13*4]
    lea     r9, [g_bytes_written]
    mov     qword [rsp + 0x20], 0
    call    [WriteFile]

    dec     r12d
    jmp     .row_lp

.close_file:
    mov     rcx, r15
    call    [CloseHandle]

.done:
    add     rsp, 64
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

write_shot_txt:
    push    rbx
    sub     rsp, 64
    lea     rcx, [szTxtFile]
    mov     edx, 0x40000000
    xor     r8d, r8d
    xor     r9d, r9d
    mov     dword [rsp + 0x20], 2
    mov     dword [rsp + 0x28], 0x80
    mov     qword [rsp + 0x30], 0
    call    [CreateFileA]
    cmp     rax, -1
    je      .done
    mov     rbx, rax
    mov     rcx, rbx
    lea     rdx, [g_text_buf]
    mov     r8d, dword [g_bar_chars]
    lea     r9, [g_bytes_written]
    mov     qword [rsp + 0x20], 0
    call    [WriteFile]
    mov     rcx, rbx
    call    [CloseHandle]
.done:
    add     rsp, 64
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; is_fullscreen_active & check_fs_transition & WinEventProc
; ----------------------------------------------------------------------------
is_fullscreen_active:
    push    rbx
    push    rsi
    push    rdi
    sub     rsp, 32

    cmp     dword [g_fs_autohide], 0
    je      .not_fs

    call    [GetForegroundWindow]
    test    rax, rax
    jz      .not_fs
    cmp     rax, qword [g_hwnd]
    je      .not_fs
    cmp     rax, qword [g_hwnd_panel]
    je      .not_fs
    mov     rbx, rax

    mov     rcx, rbx
    lea     rdx, [g_fg_cls]
    mov     r8d, 31
    call    [GetClassNameA]
    test    eax, eax
    jz      .not_fs

    mov     eax, dword [g_fg_cls]
    cmp     eax, 'Prog'
    je      .not_fs
    cmp     eax, 'Work'
    je      .not_fs
    cmp     eax, 'Shel'
    je      .not_fs

    mov     rcx, rbx
    lea     rdx, [g_rc_fg]
    call    [GetWindowRect]
    test    eax, eax
    jz      .not_fs

    xor     ecx, ecx
    call    [GetSystemMetrics]
    mov     esi, eax
    mov     ecx, 1
    call    [GetSystemMetrics]
    mov     edi, eax

    cmp     dword [g_rc_fg + 0], 0
    jg      .not_fs
    cmp     dword [g_rc_fg + 4], 0
    jg      .not_fs
    cmp     dword [g_rc_fg + 8], esi
    jl      .not_fs
    cmp     dword [g_rc_fg + 12], edi
    jl      .not_fs

    mov     eax, 1
    add     rsp, 32
    pop     rdi
    pop     rsi
    pop     rbx
    ret

.not_fs:
    xor     eax, eax
    add     rsp, 32
    pop     rdi
    pop     rsi
    pop     rbx
    ret

check_fs_transition:
    sub     rsp, 40
    call    is_fullscreen_active
    test    eax, eax
    jz      .not_fs

    cmp     dword [g_fs_hidden], 0
    jne     .ret_hid
    mov     dword [g_fs_hidden], 1
    mov     rcx, qword [g_hwnd]
    xor     edx, edx
    call    [ShowWindow]
    cmp     dword [g_panel_open], 0
    je      .ret_hid
    mov     rcx, qword [g_hwnd_panel]
    xor     edx, edx
    call    [ShowWindow]
.ret_hid:
    mov     eax, 1
    add     rsp, 40
    ret

.not_fs:
    cmp     dword [g_fs_hidden], 0
    je      .ret_vis
    mov     dword [g_fs_hidden], 0
    mov     dword [g_pos_dirty], 1
    mov     rcx, qword [g_hwnd]
    mov     edx, SW_SHOWNOACTIVATE_
    call    [ShowWindow]
    cmp     dword [g_panel_open], 0
    je      .ret_vis
    mov     rcx, qword [g_hwnd_panel]
    mov     edx, SW_SHOWNOACTIVATE_
    call    [ShowWindow]
.ret_vis:
    xor     eax, eax
    add     rsp, 40
    ret

WinEventProc:
    sub     rsp, 40
    cmp     dword [g_visible], 0
    je      .done
    call    check_fs_transition
    test    eax, eax
    jnz     .done
    call    assert_zorder
.done:
    add     rsp, 40
    ret

; ----------------------------------------------------------------------------
; Action & Settings Adjustment Helpers
; ----------------------------------------------------------------------------
act_cycle_palette:
    sub     rsp, 40
    mov     eax, dword [g_palette]
    inc     eax
    cmp     eax, NUM_PALETTES
    jb      .ok
    xor     eax, eax
.ok:
    mov     dword [g_palette], eax
    call    cfg_save
    call    present_hud
    call    present_panel
    add     rsp, 40
    ret

act_toggle_backdrop:
    sub     rsp, 40
    xor     dword [g_backdrop], 1
    call    cfg_save
    call    present_hud
    call    present_panel
    add     rsp, 40
    ret

act_toggle_scale:
    sub     rsp, 40
    xor     dword [g_scale], 3
    mov     dword [g_pos_dirty], 1
    call    cfg_save
    call    present_hud
    call    present_panel
    add     rsp, 40
    ret

act_cycle_anchor:
    sub     rsp, 40
    mov     eax, dword [g_anchor]
    inc     eax
    and     eax, 3
    mov     dword [g_anchor], eax
    mov     dword [g_nudge_x], 0
    mov     dword [g_pos_dirty], 1
    call    cfg_save
    call    present_hud
    call    present_panel
    add     rsp, 40
    ret

act_reset_defaults:
    sub     rsp, 40
    mov     dword [g_scale], 2
    mov     dword [g_backdrop], 0
    mov     dword [g_palette], 0
    mov     dword [g_anchor], 1
    mov     dword [g_nudge_x], 0
    mov     dword [g_opacity_idx], 2
    mov     dword [g_bar_mode], 0
    mov     dword [g_rate_idx], 3
    mov     dword [g_group_pids], 0
    mov     dword [g_thresh_idx], 0
    mov     dword [g_d3_guard], 1
    mov     dword [g_fs_autohide], 1
    mov     dword [g_caution_en], 1
    mov     dword [g_pos_dirty], 1
    call    apply_thresholds
    call    apply_poll_rate
    call    cfg_save
    call    telemetry_poll_deep
    call    present_hud
    call    present_panel
    add     rsp, 40
    ret

adjust_nudge_clamped:
    add     eax, dword [g_nudge_x]
    cmp     eax, -NUDGE_LIMIT
    jge     .hi
    mov     eax, -NUDGE_LIMIT
.hi:
    cmp     eax, NUDGE_LIMIT
    jle     .ok
    mov     eax, NUDGE_LIMIT
.ok:
    mov     dword [g_nudge_x], eax
    mov     dword [g_pos_dirty], 1
    ret

setting_adjust:
    sub     rsp, 40
    mov     edx, dword [g_set_sel]

    cmp     edx, 0
    je      .s_pal
    cmp     edx, 1
    je      .s_opa
    cmp     edx, 2
    je      .s_bmode
    cmp     edx, 3
    je      .s_bg
    cmp     edx, 4
    je      .s_scale
    cmp     edx, 5
    je      .s_anchor
    cmp     edx, 6
    je      .s_nudge
    cmp     edx, 7
    je      .s_rate
    cmp     edx, 8
    je      .s_group
    cmp     edx, 9
    je      .s_thresh
    cmp     edx, 10
    je      .s_d3
    cmp     edx, 11
    je      .s_fs
    cmp     edx, 12
    je      .s_caut
    cmp     edx, 13
    je      .s_startup
    cmp     edx, 14
    je      .s_reset
    jmp     .done

.s_pal:
    mov     eax, dword [g_palette]
    add     eax, ecx
    cmp     eax, 0
    jge     .pal_hi
    mov     eax, NUM_PALETTES - 1
    jmp     .pal_st
.pal_hi:
    cmp     eax, NUM_PALETTES
    jb      .pal_st
    xor     eax, eax
.pal_st:
    mov     dword [g_palette], eax
    jmp     .save_redraw

.s_opa:
    mov     eax, dword [g_opacity_idx]
    add     eax, ecx
    and     eax, 3
    mov     dword [g_opacity_idx], eax
    jmp     .save_redraw

.s_bmode:
    mov     eax, dword [g_bar_mode]
    add     eax, ecx
    and     eax, 3
    mov     dword [g_bar_mode], eax
    mov     dword [g_pos_dirty], 1
    jmp     .save_redraw

.s_bg:
    xor     dword [g_backdrop], 1
    jmp     .save_redraw

.s_scale:
    xor     dword [g_scale], 3
    mov     dword [g_pos_dirty], 1
    jmp     .save_redraw

.s_anchor:
    mov     eax, dword [g_anchor]
    add     eax, ecx
    and     eax, 3
    mov     dword [g_anchor], eax
    mov     dword [g_nudge_x], 0
    mov     dword [g_pos_dirty], 1
    jmp     .save_redraw

.s_nudge:
    mov     eax, ecx
    imul    eax, NUDGE_STEP
    call    adjust_nudge_clamped
    jmp     .save_redraw

.s_rate:
    mov     eax, dword [g_rate_idx]
    add     eax, ecx
    cmp     eax, 0
    jge     .rt_hi
    mov     eax, NUM_RATES - 1
    jmp     .rt_st
.rt_hi:
    cmp     eax, NUM_RATES
    jb      .rt_st
    xor     eax, eax
.rt_st:
    mov     dword [g_rate_idx], eax
    call    apply_poll_rate
    jmp     .save_redraw

.s_group:
    xor     dword [g_group_pids], 1
    mov     dword [g_col_sel + 0], 0
    mov     dword [g_col_sel + 4], 0
    mov     dword [g_col_sel + 12], 0
    mov     dword [g_col_sel + 16], 0
    mov     dword [g_col_scroll + 0], 0
    mov     dword [g_col_scroll + 4], 0
    mov     dword [g_col_scroll + 12], 0
    mov     dword [g_col_scroll + 16], 0
    jmp     .save_redraw

.s_thresh:
    mov     eax, dword [g_thresh_idx]
    add     eax, ecx
    cmp     eax, 0
    jge     .th_hi
    mov     eax, 2
    jmp     .th_st
.th_hi:
    cmp     eax, 3
    jb      .th_st
    xor     eax, eax
.th_st:
    mov     dword [g_thresh_idx], eax
    call    apply_thresholds
    jmp     .save_redraw

.s_d3:
    xor     dword [g_d3_guard], 1
    jmp     .save_redraw

.s_fs:
    xor     dword [g_fs_autohide], 1
    jmp     .save_redraw

.s_caut:
    xor     dword [g_caution_en], 1
    mov     dword [g_pos_dirty], 1
    jmp     .save_redraw

.s_startup:
    xor     dword [g_startup_on], 1
    call    startup_set
    call    present_panel
    jmp     .done

.s_reset:
    call    act_reset_defaults
    jmp     .done

.save_redraw:
    call    cfg_save
    call    present_hud
    call    present_panel
.done:
    add     rsp, 40
    ret

; ----------------------------------------------------------------------------
; Cockpit Process Target Selection & Unified [K] / [F] / [L] Trigger!
;   in: ecx = ACT_KILL (0), ACT_FREEZE (1), ACT_LOWGEAR (2)
;   Works across CPU (0), RAM (1), VRM (3), and IO (4) columns!
; ----------------------------------------------------------------------------
trigger_proc_action:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    sub     rsp, 40             ; 8+32+40 = 80 -> aligned

    mov     r12d, ecx           ; requested action (ACT_*)
    mov     eax, dword [g_col_active]
    cmp     eax, 2
    je      .done               ; GPU silicon stats col has no target PID

    mov     edx, dword [g_col_sel + rax*4]
    cmp     eax, 3
    je      .pick_vram
    cmp     edx, dword [g_top_count]
    jae     .done
    cmp     eax, 0
    je      .p_cpu
    cmp     eax, 1
    je      .p_ram
    mov     ecx, dword [g_top_io + rdx*4]
    jmp     .got
.p_cpu:
    mov     ecx, dword [g_top_cpu + rdx*4]
    jmp     .got
.p_ram:
    mov     ecx, dword [g_top_ram + rdx*4]
    jmp     .got
.pick_vram:
    cmp     edx, dword [g_top_vram_count]
    jae     .done
    mov     ecx, dword [g_top_vram + rdx*4]

.got:
    shl     ecx, 6
    lea     rbx, [g_procs + rcx]

    ; Block PID <= 4, self-PID, and critical OS processes with crisp flash!
    mov     eax, dword [rbx + PROC_Pid]
    cmp     eax, 4
    jbe     .blocked
    cmp     eax, dword [g_self_pid]
    je      .blocked
    lea     rcx, [rbx + PROC_Name]
    call    is_protected_proc
    test    eax, eax
    jnz     .blocked

    ; Populate target descriptor
    mov     eax, dword [rbx + PROC_Pid]
    mov     dword [g_kill_pid], eax
    mov     eax, dword [rbx + PROC_Count]
    mov     dword [g_kill_count], eax
    mov     rax, qword [rbx + PROC_NameHash]
    mov     qword [g_kill_hash], rax

    mov     rax, qword [rbx + PROC_Name + 0]
    mov     qword [g_kill_name + 0], rax
    mov     rax, qword [rbx + PROC_Name + 8]
    mov     qword [g_kill_name + 8], rax
    mov     rax, qword [rbx + PROC_Name + 16]
    mov     qword [g_kill_name + 16], rax
    mov     byte [g_kill_name + 23], 0

    cmp     r12d, ACT_KILL
    je      .do_kill_prompt
    cmp     r12d, ACT_FREEZE
    je      .do_freeze_toggle
    jmp     .do_lowgear_toggle

.do_kill_prompt:
    mov     dword [g_kill_confirm], 1
    call    present_panel
    jmp     .done

.do_freeze_toggle:
    ; Check if currently frozen -> thaw (2), else freeze (1)!
    lea     rcx, [g_frozen_hashes]
    mov     rdx, qword [g_kill_hash]
    call    hash_tbl_has
    test    eax, eax
    jnz     .run_thaw

    mov     ecx, 1              ; subaction = FREEZE
    call    execute_cockpit_action
    test    eax, eax
    jz      .denied
    lea     rcx, [g_frozen_hashes]
    mov     rdx, qword [g_kill_hash]
    call    hash_tbl_add
    lea     rsi, [szStFrozen]
    lea     rdx, [g_kill_name]
    mov     eax, P_FRZ_CYAN
    call    set_status_flash
    jmp     .refresh_now

.run_thaw:
    mov     ecx, 2              ; subaction = THAW
    call    execute_cockpit_action
    lea     rcx, [g_frozen_hashes]
    mov     rdx, qword [g_kill_hash]
    call    hash_tbl_rem
    lea     rsi, [szStThawed]
    lea     rdx, [g_kill_name]
    call    hud_accent_color
    call    set_status_flash
    jmp     .refresh_now

.do_lowgear_toggle:
    ; Query live priority via PROCESS_QUERY_LIMITED_ (or tracked hash) -> toggle IDLE/NORMAL!
    mov     ecx, PROCESS_QUERY_LIMITED_
    xor     edx, edx
    mov     r8d, dword [g_kill_pid]
    call    [OpenProcess]
    xor     esi, esi            ; is_idle = 0
    test    rax, rax
    jz      .chk_low_hash
    mov     rdi, rax
    mov     rcx, rdi
    call    [GetPriorityClass]
    cmp     eax, IDLE_PRIORITY_CLASS_
    sete    sil
    mov     rcx, rdi
    call    [CloseHandle]
    jmp     .low_decide
.chk_low_hash:
    lea     rcx, [g_low_hashes]
    mov     rdx, qword [g_kill_hash]
    call    hash_tbl_has
    mov     esi, eax
.low_decide:
    test    esi, esi
    jnz     .run_normal

    mov     ecx, 3              ; subaction = SET_IDLE
    call    execute_cockpit_action
    test    eax, eax
    jz      .denied
    lea     rcx, [g_low_hashes]
    mov     rdx, qword [g_kill_hash]
    call    hash_tbl_add
    lea     rsi, [szStLowOn]
    lea     rdx, [g_kill_name]
    mov     eax, P_CAUT_AMBER
    call    set_status_flash
    jmp     .refresh_now

.run_normal:
    mov     ecx, 4              ; subaction = SET_NORMAL
    call    execute_cockpit_action
    lea     rcx, [g_low_hashes]
    mov     rdx, qword [g_kill_hash]
    call    hash_tbl_rem
    lea     rsi, [szStLowOff]
    lea     rdx, [g_kill_name]
    call    hud_accent_color
    call    set_status_flash
    jmp     .refresh_now

.blocked:
    lea     rsi, [szStBlocked]
    xor     edx, edx
    mov     eax, P_KILL_RED
    call    set_status_flash
    call    present_panel
    jmp     .done

.denied:
    lea     rsi, [szStDenied]
    lea     rdx, [g_kill_name]
    mov     eax, P_KILL_RED
    call    set_status_flash
    call    present_panel
    jmp     .done

.refresh_now:
    call    telemetry_poll_deep
    call    present_panel

.done:
    add     rsp, 40
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; Black-Box Scrubber Helpers: scrub_bb_delta & jump_prev_bb_fault
; ----------------------------------------------------------------------------
scrub_bb_delta:
    ; in: ecx = signed delta in seconds (+ = deeper into past, - = newer)
    sub     rsp, 40
    mov     edx, dword [g_bb_count]
    test    edx, edx
    jz      .done
    dec     edx
    mov     eax, dword [g_bb_offset]
    add     eax, ecx
    cmp     eax, 0
    jge     .hi
    xor     eax, eax
.hi:
    cmp     eax, edx
    jle     .ok
    mov     eax, edx
.ok:
    mov     dword [g_bb_offset], eax
    call    present_panel
.done:
    add     rsp, 40
    ret

jump_prev_bb_fault:
    push    rbx
    sub     rsp, 32
    mov     ebx, dword [g_bb_count]
    test    ebx, ebx
    jz      .none

    ; Scan from g_bb_offset + 1 .. g_bb_count - 1, wrapping once through 0 .. g_bb_offset!
    mov     ecx, dword [g_bb_offset]
    inc     ecx
    xor     edx, edx            ; steps tried
.lp:
    cmp     edx, ebx
    jae     .none
    cmp     ecx, ebx
    jb      .c_ok
    xor     ecx, ecx
.c_ok:
    push    rcx
    push    rdx
    mov     eax, ecx
    call    hud_get_bb_ptr
    pop     rdx
    pop     rcx
    cmp     byte [rax + BB_Faults], 0
    jne     .found
    inc     ecx
    inc     edx
    jmp     .lp

.found:
    mov     dword [g_bb_offset], ecx
    call    present_panel
    jmp     .done

.none:
    lea     rsi, [szStNoFault]
    xor     edx, edx
    mov     eax, P_CAUT_AMBER
    call    set_status_flash
    call    present_panel

.done:
    add     rsp, 32
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; switch_panel_tab / panel_scroll_delta / PanelWndProc
; ----------------------------------------------------------------------------
switch_panel_tab:
    sub     rsp, 40
    mov     dword [g_kill_confirm], 0
    mov     dword [g_bb_paused], 0
    mov     dword [g_bb_offset], 0
    mov     dword [g_panel_tab], ecx
    test    ecx, ecx
    jnz     .draw
    call    telemetry_poll_deep
.draw:
    call    present_panel
    add     rsp, 40
    ret

panel_scroll_delta:
    push    rbx
    sub     rsp, 32

    mov     dword [g_kill_confirm], 0
    mov     eax, dword [g_panel_tab]
    test    eax, eax
    jz      .scroll_telem
    cmp     eax, 1
    je      .scroll_ctrl
    jmp     .scroll_set

.scroll_ctrl:
    mov     eax, dword [g_ctrl_sel]
    add     eax, ecx
    cmp     eax, 0
    jge     .c_min
    xor     eax, eax
.c_min:
    cmp     eax, CTRL_ROW_COUNT - 1
    jle     .c_max
    mov     eax, CTRL_ROW_COUNT - 1
.c_max:
    mov     dword [g_ctrl_sel], eax
    call    present_panel
    jmp     .done

.scroll_set:
    mov     eax, dword [g_set_sel]
    add     eax, ecx
    cmp     eax, 0
    jge     .st_min
    xor     eax, eax
.st_min:
    cmp     eax, SET_ROW_COUNT - 1
    jle     .st_max
    mov     eax, SET_ROW_COUNT - 1
.st_max:
    mov     dword [g_set_sel], eax
    mov     r8d, dword [g_set_scroll]
    cmp     eax, r8d
    jge     .st_bot
    mov     dword [g_set_scroll], eax
    jmp     .st_draw
.st_bot:
    lea     r9d, [r8 + SET_VIS_ROWS - 1]
    cmp     eax, r9d
    jle     .st_draw
    lea     r8d, [eax - (SET_VIS_ROWS - 1)]
    mov     dword [g_set_scroll], r8d
.st_draw:
    call    present_panel
    jmp     .done

.scroll_telem:
    mov     ebx, dword [g_col_active]
    push    rcx
    mov     ecx, ebx
    call    hud_col_max_rows
    pop     rcx
    mov     edx, eax
    dec     edx
    jns     .max_ok
    xor     edx, edx
.max_ok:
    mov     eax, dword [g_col_sel + rbx*4]
    add     eax, ecx
    cmp     eax, 0
    jge     .s_min
    xor     eax, eax
.s_min:
    cmp     eax, edx
    jle     .s_max
    mov     eax, edx
.s_max:
    mov     dword [g_col_sel + rbx*4], eax

    mov     r8d, dword [g_col_scroll + rbx*4]
    cmp     eax, r8d
    jge     .chk_bot
    mov     dword [g_col_scroll + rbx*4], eax
    jmp     .do_redraw
.chk_bot:
    lea     r9d, [r8 + VISIBLE_ROWS - 1]
    cmp     eax, r9d
    jle     .do_redraw
    lea     r8d, [eax - (VISIBLE_ROWS - 1)]
    mov     dword [g_col_scroll + rbx*4], r8d
.do_redraw:
    call    present_panel
.done:
    add     rsp, 32
    pop     rbx
    ret

PanelWndProc:
    sub     rsp, 40

    cmp     edx, WM_KEYDOWN_
    je      .on_key
    cmp     edx, WM_MOUSEWHEEL_
    je      .on_wheel
    cmp     edx, WM_LBUTTONDOWN_
    je      .on_click
    cmp     edx, WM_CLOSE_
    je      .k_esc

    call    [DefWindowProcA]
    add     rsp, 40
    ret

.on_wheel:
    mov     eax, r8d
    sar     eax, 16
    ; In Black-Box Rewind mode, mouse wheel scrubs 3 seconds at a time!
    cmp     dword [g_bb_paused], 0
    je      .wh_normal
    mov     ecx, 3
    test    eax, eax
    jle     .wh_bb
    mov     ecx, -3
.wh_bb:
    call    scrub_bb_delta
    jmp     .ret0
.wh_normal:
    mov     ecx, 1
    test    eax, eax
    jle     .wh_go
    mov     ecx, -1
.wh_go:
    call    panel_scroll_delta
    jmp     .ret0

.on_click:
    mov     r10d, dword [g_kill_confirm]
    mov     dword [g_kill_confirm], 0

    mov     eax, r9d
    shr     eax, 16
    mov     ecx, r9d
    and     ecx, 0xFFFF
    cmp     eax, 35
    jae     .chk_body_click
    cmp     ecx, 102
    jb      .click_t0
    cmp     ecx, 182
    jb      .click_t1
    cmp     ecx, 248
    jb      .click_t2
    ; Clicking Black-Box pill on top-right toggles Live/Rewind!
    cmp     ecx, 424
    jb      .chk_kill_redraw
    cmp     dword [g_panel_tab], 0
    jne     .chk_kill_redraw
    jmp     .k_space
.click_t0:
    xor     ecx, ecx
    call    switch_panel_tab
    jmp     .ret0
.click_t1:
    mov     ecx, 1
    call    switch_panel_tab
    jmp     .ret0
.click_t2:
    mov     ecx, 2
    call    switch_panel_tab
    jmp     .ret0

.chk_body_click:
    cmp     dword [g_panel_tab], 0
    je      .click_telem
    cmp     dword [g_panel_tab], 1
    je      .click_ctrl
    jmp     .click_set

.click_telem:
    cmp     ecx, COL_X0
    jb      .chk_kill_redraw
    mov     r11d, eax
    sub     ecx, COL_X0
    mov     eax, ecx
    xor     edx, edx
    mov     r8d, COL_STRIDE
    div     r8d
    cmp     eax, 5
    jae     .chk_kill_redraw
    mov     dword [g_col_active], eax

    cmp     r11d, ROW_Y0 - 4
    jb      .ct_save
    cmp     r11d, COL_BOT
    jae     .ct_save
    mov     r9d, eax
    mov     eax, r11d
    sub     eax, ROW_Y0 - 4
    xor     edx, edx
    mov     r8d, ROW_PITCH
    div     r8d
    cmp     eax, VISIBLE_ROWS
    jae     .ct_save
    add     eax, dword [g_col_scroll + r9*4]
    push    rax
    push    r9
    mov     ecx, r9d
    call    hud_col_max_rows
    pop     r9
    pop     rdx
    cmp     edx, eax
    jae     .ct_save
    mov     dword [g_col_sel + r9*4], edx
.ct_save:
    call    cfg_save
    call    present_panel
    jmp     .ret0

.click_ctrl:
    cmp     eax, 42
    jb      .chk_kill_redraw
    sub     eax, 42
    xor     edx, edx
    mov     r8d, CTRL_ROW_PITCH
    div     r8d
    cmp     eax, CTRL_ROW_COUNT
    jae     .chk_kill_redraw
    mov     dword [g_ctrl_sel], eax
    call    present_panel
    jmp     .ret0

.click_set:
    cmp     eax, SET_ROW_Y0 - 4
    jb      .chk_kill_redraw
    sub     eax, SET_ROW_Y0 - 4
    xor     edx, edx
    mov     r8d, SET_ROW_PITCH
    div     r8d
    cmp     eax, SET_VIS_ROWS
    jae     .chk_kill_redraw
    add     eax, dword [g_set_scroll]
    cmp     eax, SET_ROW_COUNT
    jae     .chk_kill_redraw
    mov     dword [g_set_sel], eax
    cmp     ecx, 270
    jb      .cs_just_sel
    mov     ecx, 1
    call    setting_adjust
    jmp     .ret0
.cs_just_sel:
    call    present_panel
    jmp     .ret0

.chk_kill_redraw:
    test    r10d, r10d
    jz      .ret0
    call    present_panel
    jmp     .ret0

.on_key:
    cmp     dword [g_kill_confirm], 0
    je      .key_normal
    cmp     r8d, VK_SHIFT_
    je      .ret0
    cmp     r8d, VK_CONTROL_
    je      .ret0
    cmp     r8d, VK_MENU_
    je      .ret0
    cmp     r8d, VK_Y_
    je      .k_kill_yes
    cmp     r8d, VK_N_
    je      .k_kill_no
    cmp     r8d, VK_ESCAPE_
    je      .k_kill_no
    mov     dword [g_kill_confirm], 0
    call    present_panel
    jmp     .ret0

.k_kill_yes:
    mov     dword [g_kill_confirm], 0
    xor     ecx, ecx            ; subaction 0 = KILL
    call    execute_cockpit_action
    lea     rcx, [g_frozen_hashes]
    mov     rdx, qword [g_kill_hash]
    call    hash_tbl_rem
    lea     rcx, [g_low_hashes]
    mov     rdx, qword [g_kill_hash]
    call    hash_tbl_rem
    call    telemetry_poll_deep
    call    present_panel
    jmp     .ret0

.k_kill_no:
    mov     dword [g_kill_confirm], 0
    call    present_panel
    jmp     .ret0

.key_normal:
    cmp     r8d, VK_ESCAPE_
    je      .k_esc
    cmp     r8d, VK_TAB_
    je      .k_tab
    cmp     r8d, VK_1_
    je      .k_t0
    cmp     r8d, VK_2_
    je      .k_t1
    cmp     r8d, VK_3_
    je      .k_t2
    cmp     r8d, VK_SPACE_
    je      .k_space
    cmp     r8d, VK_LEFT_
    je      .k_left
    cmp     r8d, VK_RIGHT_
    je      .k_right
    cmp     r8d, VK_PRIOR_
    je      .k_pgup
    cmp     r8d, VK_NEXT_
    je      .k_pgdn
    cmp     r8d, VK_E_
    je      .k_fault_jump
    cmp     r8d, VK_UP_
    je      .k_up
    cmp     r8d, VK_DOWN_
    je      .k_down
    cmp     r8d, VK_RETURN_
    je      .k_enter
    cmp     r8d, VK_K_
    je      .k_kill
    cmp     r8d, VK_DELETE_
    je      .k_kill
    cmp     r8d, VK_F_
    je      .k_freeze
    cmp     r8d, VK_L_
    je      .k_lowgear
    jmp     .ret0

.k_space:
    cmp     dword [g_panel_tab], 0
    jne     .ret0
    xor     dword [g_bb_paused], 1
    mov     dword [g_bb_offset], 0
    call    present_panel
    jmp     .ret0

.k_pgup:
    cmp     dword [g_bb_paused], 0
    je      .ret0
    mov     ecx, 15
    call    scrub_bb_delta
    jmp     .ret0

.k_pgdn:
    cmp     dword [g_bb_paused], 0
    je      .ret0
    mov     ecx, -15
    call    scrub_bb_delta
    jmp     .ret0

.k_fault_jump:
    cmp     dword [g_panel_tab], 0
    jne     .ret0
    ; Pressing E even from Live automatically freezes & jumps straight to last fault!
    mov     dword [g_bb_paused], 1
    call    jump_prev_bb_fault
    jmp     .ret0

.k_kill:
    cmp     dword [g_panel_tab], 0
    jne     .ret0
    mov     ecx, ACT_KILL
    call    trigger_proc_action
    jmp     .ret0

.k_freeze:
    cmp     dword [g_panel_tab], 0
    jne     .ret0
    mov     ecx, ACT_FREEZE
    call    trigger_proc_action
    jmp     .ret0

.k_lowgear:
    cmp     dword [g_panel_tab], 0
    jne     .ret0
    mov     ecx, ACT_LOWGEAR
    call    trigger_proc_action
    jmp     .ret0

.k_esc:
    call    close_panel
    jmp     .ret0
.k_tab:
    mov     ecx, dword [g_panel_tab]
    inc     ecx
    cmp     ecx, 3
    jb      .tab_ok
    xor     ecx, ecx
.tab_ok:
    call    switch_panel_tab
    jmp     .ret0
.k_t0:
    xor     ecx, ecx
    call    switch_panel_tab
    jmp     .ret0
.k_t1:
    mov     ecx, 1
    call    switch_panel_tab
    jmp     .ret0
.k_t2:
    mov     ecx, 2
    call    switch_panel_tab
    jmp     .ret0

.k_left:
    cmp     dword [g_panel_tab], 2
    je      .k_set_dec
    cmp     dword [g_panel_tab], 0
    jne     .ret0
    cmp     dword [g_bb_paused], 0
    je      .kl_col
    mov     ecx, 1              ; scrub 1 second deeper into Black-Box!
    call    scrub_bb_delta
    jmp     .ret0
.kl_col:
    mov     eax, dword [g_col_active]
    dec     eax
    jns     .kl_ok
    mov     eax, 4
.kl_ok:
    mov     dword [g_col_active], eax
    call    cfg_save
    call    present_panel
    jmp     .ret0

.k_right:
    cmp     dword [g_panel_tab], 2
    je      .k_set_inc
    cmp     dword [g_panel_tab], 0
    jne     .ret0
    cmp     dword [g_bb_paused], 0
    je      .kr_col
    mov     ecx, -1             ; scrub 1 second closer to Live!
    call    scrub_bb_delta
    jmp     .ret0
.kr_col:
    mov     eax, dword [g_col_active]
    inc     eax
    cmp     eax, 5
    jb      .kr_ok
    xor     eax, eax
.kr_ok:
    mov     dword [g_col_active], eax
    call    cfg_save
    call    present_panel
    jmp     .ret0

.k_set_dec:
    mov     ecx, -1
    call    setting_adjust
    jmp     .ret0
.k_set_inc:
    mov     ecx, 1
    call    setting_adjust
    jmp     .ret0

.k_up:
    mov     ecx, -1
    call    panel_scroll_delta
    jmp     .ret0
.k_down:
    mov     ecx, 1
    call    panel_scroll_delta
    jmp     .ret0

.k_enter:
    cmp     dword [g_panel_tab], 2
    je      .k_set_inc
    cmp     dword [g_panel_tab], 1
    jne     .ret0
    mov     eax, dword [g_ctrl_sel]
    cmp     eax, 6
    je      .ent_pal
    cmp     eax, 7
    je      .ent_bg
    cmp     eax, 8
    je      .ent_sc
    cmp     eax, 9
    je      .ent_anc
    cmp     eax, 10
    je      .ent_quit
    cmp     eax, 11
    je      .k_esc
    jmp     .ret0
.ent_pal:
    call    act_cycle_palette
    jmp     .ret0
.ent_bg:
    call    act_toggle_backdrop
    jmp     .ret0
.ent_sc:
    call    act_toggle_scale
    jmp     .ret0
.ent_anc:
    call    act_cycle_anchor
    jmp     .ret0
.ent_quit:
    xor     ecx, ecx
    call    [PostQuitMessage]
    jmp     .ret0

.ret0:
    xor     eax, eax
    add     rsp, 40
    ret

; ----------------------------------------------------------------------------
; WndProc: Bottom Bar + Smart Decoupled Timer + QPC Jitter + Hotkeys
; ----------------------------------------------------------------------------
WndProc:
    sub     rsp, 40

    cmp     edx, WM_TIMER_
    je      .on_timer
    cmp     edx, WM_HOTKEY_
    je      .on_hotkey
    cmp     edx, WM_MOUSEACTIVATE_
    je      .on_mouseactivate
    cmp     edx, WM_DISPLAYCHANGE_
    je      .on_display_chg
    cmp     edx, WM_SETTINGCHANGE_
    je      .on_reposition
    cmp     edx, WM_DESTROY_
    je      .on_destroy

    mov     eax, dword [g_msg_tb_created]
    test    eax, eax
    jz      .def
    cmp     edx, eax
    je      .on_reposition

.def:
    call    [DefWindowProcA]
    add     rsp, 40
    ret

.on_mouseactivate:
    mov     eax, MA_NOACTIVATE_
    add     rsp, 40
    ret

.on_display_chg:
    call    detect_display_hz
.on_reposition:
    mov     dword [g_pos_dirty], 1
    call    present_hud
    call    present_panel
    call    assert_zorder
    xor     eax, eax
    add     rsp, 40
    ret

.on_timer:
    cmp     dword [g_rate_idx], 0
    jne     .timer_go
    cmp     dword [g_panel_open], 0
    jne     .timer_done

.timer_go:
    cmp     dword [g_visible], 0
    je      .timer_done

    call    check_fs_transition
    test    eax, eax
    jnz     .timer_done

    ; Measure scheduler wake jitter on fixed-rate timer when HUD is open!
    cmp     dword [g_panel_open], 0
    je      .t_no_jit
    mov     eax, dword [g_rate_idx]
    mov     ecx, dword [rate_ms_tbl + rax*4]
    imul    ecx, 1000
    call    sample_qpc_jitter
.t_no_jit:

    inc     dword [g_anim_frame]
    call    telemetry_poll
    xor     ecx, ecx
    call    sync_disp_cores
    call    present_hud

    cmp     dword [g_panel_open], 0
    je      .timer_done

    call    [GetTickCount64]
    mov     rcx, rax
    sub     rcx, qword [g_last_deep_ms]
    cmp     rcx, 400
    jb      .timer_redraw_panel
    mov     qword [g_last_deep_ms], rax
    call    telemetry_poll_deep
.timer_redraw_panel:
    ; Keep Black-Box frozen cleanly when paused (still updates bar in background!)
    cmp     dword [g_bb_paused], 0
    jne     .timer_done
    call    present_panel
.timer_done:
    xor     eax, eax
    add     rsp, 40
    ret

.on_hotkey:
    cmp     r8d, HK_HUD
    je      .hk_hud
    cmp     r8d, HK_QUIT
    je      .hk_quit
    cmp     r8d, HK_HIDE
    je      .hk_hide
    cmp     r8d, HK_BACKDROP
    je      .hk_backdrop
    cmp     r8d, HK_PALETTE
    je      .hk_palette
    cmp     r8d, HK_SCALE
    je      .hk_scale
    cmp     r8d, HK_ANCHOR
    je      .hk_anchor
    cmp     r8d, HK_LEFT
    je      .hk_left
    cmp     r8d, HK_RIGHT
    je      .hk_right
    xor     eax, eax
    add     rsp, 40
    ret

.hk_hud:
    call    toggle_panel
    xor     eax, eax
    add     rsp, 40
    ret

.hk_quit:
    xor     ecx, ecx
    call    [PostQuitMessage]
    xor     eax, eax
    add     rsp, 40
    ret

.hk_hide:
    xor     dword [g_visible], 1
    mov     dword [g_fs_hidden], 0
    cmp     dword [g_visible], 0
    je      .do_hide
    mov     dword [g_pos_dirty], 1
    mov     rcx, qword [g_hwnd]
    mov     edx, SW_SHOWNOACTIVATE_
    call    [ShowWindow]
    call    present_hud
    call    assert_zorder
    xor     eax, eax
    add     rsp, 40
    ret
.do_hide:
    call    close_panel
    mov     rcx, qword [g_hwnd]
    xor     edx, edx
    call    [ShowWindow]
    xor     eax, eax
    add     rsp, 40
    ret

.hk_backdrop:
    call    act_toggle_backdrop
    xor     eax, eax
    add     rsp, 40
    ret

.hk_palette:
    call    act_cycle_palette
    xor     eax, eax
    add     rsp, 40
    ret

.hk_scale:
    call    act_toggle_scale
    xor     eax, eax
    add     rsp, 40
    ret

.hk_anchor:
    call    act_cycle_anchor
    xor     eax, eax
    add     rsp, 40
    ret

.hk_left:
    mov     eax, -NUDGE_STEP
    call    adjust_nudge_clamped
    call    cfg_save
    call    present_hud
    call    present_panel
    xor     eax, eax
    add     rsp, 40
    ret

.hk_right:
    mov     eax, NUDGE_STEP
    call    adjust_nudge_clamped
    call    cfg_save
    call    present_hud
    call    present_panel
    xor     eax, eax
    add     rsp, 40
    ret

.on_destroy:
    xor     ecx, ecx
    call    [PostQuitMessage]
    xor     eax, eax
    add     rsp, 40
    ret

; ----------------------------------------------------------------------------
; register_hotkeys
; ----------------------------------------------------------------------------
register_hotkeys:
    push    rbx
    sub     rsp, 32

    mov     rbx, qword [g_hwnd]

    mov     rcx, rbx
    mov     edx, HK_QUIT
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_ or MOD_NOREPEAT_
    mov     r9d, VK_Q_
    call    [RegisterHotKey]

    mov     rcx, rbx
    mov     edx, HK_HIDE
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_ or MOD_NOREPEAT_
    mov     r9d, VK_H_
    call    [RegisterHotKey]

    mov     rcx, rbx
    mov     edx, HK_BACKDROP
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_ or MOD_NOREPEAT_
    mov     r9d, VK_B_
    call    [RegisterHotKey]

    mov     rcx, rbx
    mov     edx, HK_PALETTE
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_ or MOD_NOREPEAT_
    mov     r9d, VK_C_
    call    [RegisterHotKey]

    mov     rcx, rbx
    mov     edx, HK_SCALE
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_ or MOD_NOREPEAT_
    mov     r9d, VK_S_
    call    [RegisterHotKey]

    mov     rcx, rbx
    mov     edx, HK_ANCHOR
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_ or MOD_NOREPEAT_
    mov     r9d, VK_A_
    call    [RegisterHotKey]

    mov     rcx, rbx
    mov     edx, HK_LEFT
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_
    mov     r9d, VK_LEFT_
    call    [RegisterHotKey]

    mov     rcx, rbx
    mov     edx, HK_RIGHT
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_
    mov     r9d, VK_RIGHT_
    call    [RegisterHotKey]

    mov     rcx, rbx
    mov     edx, HK_HUD
    mov     r8d, MOD_CONTROL_ or MOD_SHIFT_ or MOD_NOREPEAT_
    mov     r9d, VK_SPACE_
    call    [RegisterHotKey]

    add     rsp, 32
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; start: PE64 Entry Point
; ----------------------------------------------------------------------------
start:
    sub     rsp, 104
    xor     r13d, r13d

    mov     rcx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    call    [SetProcessDpiAwarenessContext]

    call    detect_display_hz
    call    load_dwm_flush

    call    check_cli_flags
    cmp     eax, 1
    je      .mode_shot
    cmp     eax, 2
    je      .mode_shot_hud
    cmp     eax, 3
    je      .mode_shot_themes
    jmp     .normal_mode

.mode_shot:
    call    create_dib_surfaces
    test    eax, eax
    jz      .exit_fail
    call    telemetry_init
    mov     ecx, 200
    call    [Sleep]
    call    telemetry_poll
    mov     ecx, 1
    call    raster_render
    mov     rcx, qword [g_dib_bits]
    mov     edx, HUD_MAX_W
    mov     r8d, HUD_MAX_H
    lea     r9, [szShotFile]
    call    write_bmp_generic
    call    write_shot_txt
    jmp     .cleanup

.mode_shot_hud:
    call    create_dib_surfaces
    test    eax, eax
    jz      .exit_fail
    call    telemetry_init
    call    startup_query
    mov     dword [g_group_pids], 1
    mov     dword [g_palette], 4 ; Blueprint CAD showcase!
    call    telemetry_poll_deep
    mov     ebx, 16
.sh_lp:
    mov     ecx, 50
    call    [Sleep]
    ; Force a Black-Box sample each step so we get a rich 16-frame rewind shot!
    mov     qword [g_bb_last_ms], 0
    call    telemetry_poll
    dec     ebx
    jnz     .sh_lp
    xor     ecx, ecx
    call    sync_disp_cores
    mov     dword [g_jitter_10ms], 3 ; 0.3ms sample
    call    telemetry_poll_deep

    ; Shot 1: Live TELEM with Per-Process VRAM column selected!
    mov     dword [g_panel_tab], 0
    mov     dword [g_col_active], 3
    mov     dword [g_bb_paused], 0
    call    hud_render_panel
    mov     rcx, qword [g_panel_bits]
    mov     edx, PANEL_W
    mov     r8d, PANEL_H
    lea     r9, [szHudShotFile]
    call    write_bmp_generic

    ; Shot 2: Black-Box Flight Data Rewind Mode (T-00:04)!
    mov     dword [g_bb_paused], 1
    mov     dword [g_bb_offset], 4
    call    hud_render_panel
    mov     rcx, qword [g_panel_bits]
    mov     edx, PANEL_W
    mov     r8d, PANEL_H
    lea     r9, [szBbShotFile]
    call    write_bmp_generic

    ; Shot 3: SET tab scrolled to showcase VSYNC 165HZ + MASTER CAUT!
    mov     dword [g_bb_paused], 0
    mov     dword [g_palette], 0
    mov     dword [g_panel_tab], 2
    mov     dword [g_set_scroll], 5
    mov     dword [g_set_sel], 12
    mov     dword [g_rate_idx], 0
    call    hud_render_panel
    mov     rcx, qword [g_panel_bits]
    mov     edx, PANEL_W
    mov     r8d, PANEL_H
    lea     r9, [szSetShotFile]
    call    write_bmp_generic
    jmp     .cleanup

.mode_shot_themes:
    call    create_dib_surfaces
    test    eax, eax
    jz      .exit_fail
    call    telemetry_init
    mov     dword [g_group_pids], 1
    call    telemetry_poll_deep
    mov     ebx, 24
.thm_poll:
    mov     ecx, 45
    call    [Sleep]
    call    telemetry_poll
    dec     ebx
    jnz     .thm_poll
    xor     ecx, ecx
    call    sync_disp_cores
    call    telemetry_poll_deep

    mov     dword [g_panel_tab], 0
    mov     dword [g_col_active], 3
    mov     dword [g_anim_frame], 22
    xor     ebx, ebx
.thm_lp:
    cmp     ebx, NUM_PALETTES
    jae     .cleanup
    mov     dword [g_palette], ebx
    call    hud_render_panel
    mov     rcx, qword [g_panel_bits]
    mov     edx, PANEL_W
    mov     r8d, PANEL_H
    mov     r9, qword [theme_shot_files + rbx*8]
    call    write_bmp_generic
    inc     ebx
    jmp     .thm_lp

.normal_mode:
    xor     ecx, ecx
    xor     edx, edx
    lea     r8, [szMutexName]
    call    [CreateMutexA]
    mov     qword [g_hMutex], rax
    call    [GetLastError]
    cmp     eax, 183
    je      .cleanup

    call    cfg_build_path
    call    cfg_load

    call    create_dib_surfaces
    test    eax, eax
    jz      .exit_fail
    call    telemetry_init
    call    startup_query

    lea     rcx, [szTaskbarMsg]
    call    [RegisterWindowMessageA]
    mov     dword [g_msg_tb_created], eax

    xor     ecx, ecx
    call    [GetModuleHandleA]
    mov     qword [g_hInst], rax

    xor     ecx, ecx
    mov     edx, 32512
    call    [LoadCursorA]
    mov     qword [g_wc + 0x28], rax

    mov     dword [g_wc + 0x00], 80
    mov     dword [g_wc + 0x04], 0
    lea     rax, [WndProc]
    mov     qword [g_wc + 0x08], rax
    mov     rax, qword [g_hInst]
    mov     qword [g_wc + 0x18], rax
    lea     rax, [szClassName]
    mov     qword [g_wc + 0x40], rax
    lea     rcx, [g_wc]
    call    [RegisterClassExA]

    lea     rax, [PanelWndProc]
    mov     qword [g_wc + 0x08], rax
    lea     rax, [szPanelClass]
    mov     qword [g_wc + 0x40], rax
    lea     rcx, [g_wc]
    call    [RegisterClassExA]

    mov     ecx, OVERLAY_EXSTYLE
    lea     rdx, [szClassName]
    xor     r8d, r8d
    mov     r9d, WS_POPUP_STYLE
    mov     dword [rsp + 0x20], 0
    mov     dword [rsp + 0x28], 0
    mov     dword [rsp + 0x30], HUD_MAX_W
    mov     dword [rsp + 0x38], HUD_MAX_H
    mov     qword [rsp + 0x40], 0
    mov     qword [rsp + 0x48], 0
    mov     rax, qword [g_hInst]
    mov     qword [rsp + 0x50], rax
    mov     qword [rsp + 0x58], 0
    call    [CreateWindowExA]
    test    rax, rax
    jz      .exit_fail
    mov     qword [g_hwnd], rax

    mov     ecx, PANEL_EXSTYLE
    lea     rdx, [szPanelClass]
    xor     r8d, r8d
    mov     r9d, WS_POPUP_STYLE
    mov     dword [rsp + 0x20], 0
    mov     dword [rsp + 0x28], 0
    mov     dword [rsp + 0x30], PANEL_W
    mov     dword [rsp + 0x38], PANEL_H
    mov     qword [rsp + 0x40], 0
    mov     qword [rsp + 0x48], 0
    mov     rax, qword [g_hInst]
    mov     qword [rsp + 0x50], rax
    mov     qword [rsp + 0x58], 0
    call    [CreateWindowExA]
    test    rax, rax
    jz      .exit_fail
    mov     qword [g_hwnd_panel], rax

    mov     rcx, qword [g_hwnd]
    mov     edx, SW_SHOWNOACTIVATE_
    call    [ShowWindow]

    mov     ecx, EVENT_SYSTEM_FOREGROUND_
    mov     edx, EVENT_SYSTEM_FOREGROUND_
    xor     r8d, r8d
    lea     r9, [WinEventProc]
    mov     dword [rsp + 0x20], 0
    mov     dword [rsp + 0x28], 0
    mov     dword [rsp + 0x30], WINEVENT_OUTOFCONTEXT_ or WINEVENT_SKIPOWNPROCESS_
    call    [SetWinEventHook]
    mov     qword [g_hook_fg], rax

    call    register_hotkeys
    call    present_hud
    call    assert_zorder
    call    apply_poll_rate

    ; --- Hybrid Message Loop: Zero-CPU Blocking GetMessage OR Dirty-Frame Hardware VSync! ---
.main_loop:
    cmp     dword [g_rate_idx], 0
    jne     .blocking_msg
    cmp     dword [g_panel_open], 0
    je      .blocking_msg
    cmp     dword [g_visible], 0
    je      .blocking_msg
    cmp     dword [g_fs_hidden], 0
    jne     .blocking_msg

.peek_drain:
    lea     rcx, [g_msg]
    xor     edx, edx
    xor     r8d, r8d
    xor     r9d, r9d
    mov     dword [rsp + 0x20], PM_REMOVE_
    call    [PeekMessageA]
    test    eax, eax
    jz      .vsync_step
    cmp     dword [g_msg + 8], WM_QUIT_
    je      .cleanup
    lea     rcx, [g_msg]
    call    [TranslateMessage]
    lea     rcx, [g_msg]
    call    [DispatchMessageA]
    jmp     .peek_drain

.vsync_step:
    mov     rax, qword [g_pDwmFlush]
    test    rax, rax
    jz      .vsync_fallback
    call    rax
    test    eax, eax
    jz      .vsync_qpc
.vsync_fallback:
    mov     ecx, 16
    call    [Sleep]
    mov     qword [g_qpc_prev], 0
    jmp     .vsync_tick

.vsync_qpc:
    ; Measure exact DWM hardware vblank jitter against 1,000,000 / g_display_hz!
    mov     eax, 1000000
    xor     edx, edx
    mov     ecx, dword [g_display_hz]
    test    ecx, ecx
    jnz     .hz_ok
    mov     ecx, 60
.hz_ok:
    div     ecx
    mov     ecx, eax
    call    sample_qpc_jitter

.vsync_tick:
    cmp     dword [g_panel_open], 0
    je      .main_loop

    inc     dword [g_anim_frame]
    xor     r12d, r12d

    call    [GetTickCount64]
    mov     rbx, rax

    mov     rcx, rbx
    sub     rcx, qword [g_last_fs_ms]
    cmp     rcx, 500
    jb      .vsync_chk_poll
    mov     qword [g_last_fs_ms], rbx
    call    check_fs_transition
    test    eax, eax
    jnz     .main_loop

.vsync_chk_poll:
    mov     rcx, rbx
    sub     rcx, qword [g_last_poll_ms]
    cmp     rcx, 100
    jb      .vsync_anim
    mov     qword [g_last_poll_ms], rbx
    call    telemetry_poll
    call    present_hud
    cmp     dword [g_panel_tab], 0
    jne     .vsync_chk_deep
    mov     r12d, 1

.vsync_chk_deep:
    mov     rcx, rbx
    sub     rcx, qword [g_last_deep_ms]
    cmp     rcx, 400
    jb      .vsync_anim
    mov     qword [g_last_deep_ms], rbx
    call    telemetry_poll_deep
    mov     r12d, 1

.vsync_anim:
    ; When Black-Box paused, keep HUD frozen cleanly (zero overwrites while scrubbing!)
    cmp     dword [g_bb_paused], 0
    jne     .main_loop

    cmp     dword [g_panel_tab], 0
    jne     .vsync_present
    mov     ecx, 1
    call    sync_disp_cores
    or      r12d, eax
    cmp     dword [g_palette], 1
    jne     .vsync_present
    test    dword [g_anim_frame], 1
    jnz     .vsync_present
    mov     r12d, 1

.vsync_present:
    test    r12d, r12d
    jz      .main_loop
    call    present_panel
    jmp     .main_loop

.blocking_msg:
    lea     rcx, [g_msg]
    xor     edx, edx
    xor     r8d, r8d
    xor     r9d, r9d
    call    [GetMessageA]
    test    eax, eax
    jle     .cleanup
    lea     rcx, [g_msg]
    call    [TranslateMessage]
    lea     rcx, [g_msg]
    call    [DispatchMessageA]
    jmp     .main_loop

.exit_fail:
    mov     r13d, 1

.cleanup:
    mov     rcx, qword [g_hook_fg]
    test    rcx, rcx
    jz      .no_hook
    call    [UnhookWinEvent]
.no_hook:
    cmp     qword [g_hwnd], 0
    je      .no_win_clean
    mov     rcx, qword [g_hwnd]
    mov     edx, TIMER_ID
    call    [KillTimer]

    mov     ebx, 1
.unreg_loop:
    mov     rcx, qword [g_hwnd]
    mov     edx, ebx
    call    [UnregisterHotKey]
    inc     ebx
    cmp     ebx, 9
    jle     .unreg_loop

.no_win_clean:
    mov     rax, qword [g_pNvmlShutdown]
    test    rax, rax
    jz      .no_nvml_shut
    call    rax
.no_nvml_shut:

    cmp     qword [g_mem_dc], 0
    je      .no_bar_gdi
    cmp     qword [g_old_bmp], 0
    je      .no_bar_sel
    mov     rcx, qword [g_mem_dc]
    mov     rdx, qword [g_old_bmp]
    call    [SelectObject]
.no_bar_sel:
    cmp     qword [g_dib_bmp], 0
    je      .no_bar_bmp
    mov     rcx, qword [g_dib_bmp]
    call    [DeleteObject]
.no_bar_bmp:
    mov     rcx, qword [g_mem_dc]
    call    [DeleteDC]
.no_bar_gdi:

    cmp     qword [g_panel_dc], 0
    je      .no_pan_gdi
    cmp     qword [g_panel_old], 0
    je      .no_pan_sel
    mov     rcx, qword [g_panel_dc]
    mov     rdx, qword [g_panel_old]
    call    [SelectObject]
.no_pan_sel:
    cmp     qword [g_panel_bmp], 0
    je      .no_pan_bmp
    mov     rcx, qword [g_panel_bmp]
    call    [DeleteObject]
.no_pan_bmp:
    mov     rcx, qword [g_panel_dc]
    call    [DeleteDC]
.no_pan_gdi:

    mov     rcx, qword [g_hMutex]
    test    rcx, rcx
    jz      .do_exit
    call    [CloseHandle]

.do_exit:
    mov     ecx, r13d
    call    [ExitProcess]

; ============================================================================
; .idata — STILL STRICTLY 4 Static OS DLLs (ZERO CRT, COM, PDH, WMI, DirectX!)
; ============================================================================
section '.idata' import data readable writeable

library ntdll,    'NTDLL.DLL', \
        kernel32, 'KERNEL32.DLL', \
        user32,   'USER32.DLL', \
        gdi32,    'GDI32.DLL'

import ntdll, \
       NtQuerySystemInformation, 'NtQuerySystemInformation', \
       NtResumeProcess,          'NtResumeProcess', \
       NtSuspendProcess,         'NtSuspendProcess'

import kernel32, \
       CloseHandle,               'CloseHandle', \
       CreateFileA,               'CreateFileA', \
       CreateMutexA,              'CreateMutexA', \
       ExitProcess,               'ExitProcess', \
       GetCommandLineA,           'GetCommandLineA', \
       GetCurrentProcessId,       'GetCurrentProcessId', \
       GetLastError,              'GetLastError', \
       GetModuleFileNameA,        'GetModuleFileNameA', \
       GetModuleHandleA,          'GetModuleHandleA', \
       GetPriorityClass,          'GetPriorityClass', \
       GetProcAddress,            'GetProcAddress', \
       GetSystemTimeAsFileTime,   'GetSystemTimeAsFileTime', \
       GetTickCount64,            'GetTickCount64', \
       LoadLibraryA,              'LoadLibraryA', \
       OpenProcess,               'OpenProcess', \
       QueryPerformanceCounter,   'QueryPerformanceCounter', \
       QueryPerformanceFrequency, 'QueryPerformanceFrequency', \
       ReadFile,                  'ReadFile', \
       SetPriorityClass,          'SetPriorityClass', \
       Sleep,                     'Sleep', \
       TerminateProcess,          'TerminateProcess', \
       WriteFile,                 'WriteFile'

import user32, \
       CreateWindowExA,               'CreateWindowExA', \
       DefWindowProcA,                'DefWindowProcA', \
       DispatchMessageA,              'DispatchMessageA', \
       EnumDisplaySettingsA,          'EnumDisplaySettingsA', \
       FindWindowA,                   'FindWindowA', \
       GetClassNameA,                 'GetClassNameA', \
       GetDC,                         'GetDC', \
       GetForegroundWindow,           'GetForegroundWindow', \
       GetMessageA,                   'GetMessageA', \
       GetSystemMetrics,              'GetSystemMetrics', \
       GetWindowRect,                 'GetWindowRect', \
       KillTimer,                     'KillTimer', \
       LoadCursorA,                   'LoadCursorA', \
       PeekMessageA,                  'PeekMessageA', \
       PostQuitMessage,               'PostQuitMessage', \
       RegisterClassExA,              'RegisterClassExA', \
       RegisterHotKey,                'RegisterHotKey', \
       RegisterWindowMessageA,        'RegisterWindowMessageA', \
       ReleaseDC,                     'ReleaseDC', \
       SetForegroundWindow,           'SetForegroundWindow', \
       SetProcessDpiAwarenessContext, 'SetProcessDpiAwarenessContext', \
       SetTimer,                      'SetTimer', \
       SetWinEventHook,               'SetWinEventHook', \
       SetWindowPos,                  'SetWindowPos', \
       ShowWindow,                    'ShowWindow', \
       SystemParametersInfoA,         'SystemParametersInfoA', \
       TranslateMessage,              'TranslateMessage', \
       UnhookWinEvent,                'UnhookWinEvent', \
       UnregisterHotKey,              'UnregisterHotKey', \
       UpdateLayeredWindow,           'UpdateLayeredWindow'

import gdi32, \
       CreateCompatibleDC,      'CreateCompatibleDC', \
       CreateDIBSection,        'CreateDIBSection', \
       D3DKMTCloseAdapter,      'D3DKMTCloseAdapter', \
       D3DKMTEnumAdapters2,     'D3DKMTEnumAdapters2', \
       D3DKMTQueryAdapterInfo,  'D3DKMTQueryAdapterInfo', \
       D3DKMTQueryStatistics,   'D3DKMTQueryStatistics', \
       DeleteDC,                'DeleteDC', \
       DeleteObject,            'DeleteObject', \
       SelectObject,            'SelectObject'
