; ============================================================================
; telemetry.inc — Pure Ring-3 System Telemetry & Process Control Engine v3.0.1
; Zero CRT, zero heap, zero float, zero PDH, zero WMI, zero COM, zero drivers.
;
; Capabilities:
;   1. Per-process dedicated VRAM via D3DKMT Type 9 (PROCESS_SEGMENT_GROUP)
;   2. 5-minute 1 Hz static ring buffer (300 frames, 9.4 KiB .bss, zero disk)
;   3. Process actions: TerminateProcess, NtSuspendProcess/NtResumeProcess,
;      SetPriorityClass(IDLE/NORMAL) — self-PID, OS denylist & 64-bit hash safe
;   4. Threshold fault flags (!TMP / !VRM / !RAM)
; ============================================================================

MAX_HW_GPUS     = 4
MAX_NODES_GPU   = 32

HWGPU_STRIDE    = 32
HWGPU_LuidLow   = 0x00
HWGPU_LuidHigh  = 0x04
HWGPU_NbSegs    = 0x08
HWGPU_NodeCnt   = 0x0C
HWGPU_DedMask   = 0x10
HWGPU_DedLimit  = 0x18

; ----------------------------------------------------------------------------
; u64_pct_128: compute clamp((num * 100) / den, 0, 100) safely with 128-bit mul
; ----------------------------------------------------------------------------
u64_pct_128:
    test    rdx, rdx
    jz      .zero
    mov     rax, rcx
    mov     r8, rdx
    mov     r9, 100
    mul     r9
    cmp     rdx, r8
    jae     .clamp100
    div     r8
    cmp     rax, 100
    jbe     .done
.clamp100:
    mov     eax, 100
    ret
.zero:
    xor     eax, eax
.done:
    ret

; ----------------------------------------------------------------------------
; zero_d3d_qs: zero the 0x328-byte D3DKMT_QUERYSTATISTICS scratch struct
; ----------------------------------------------------------------------------
zero_d3d_qs:
    push    rdi
    lea     rdi, [g_d3d_qs]
    xor     eax, eax
    mov     ecx, D3DKMT_QS_SIZE / 8
    rep     stosq
    pop     rdi
    ret

; ----------------------------------------------------------------------------
; ascii_ieq: case-insensitive ASCII string equality (rcx = s1, rdx = s2) -> eax 0/1
; ----------------------------------------------------------------------------
ascii_ieq:
.lp:
    movzx   eax, byte [rcx]
    movzx   r8d, byte [rdx]
    cmp     al, 'A'
    jb      .c1
    cmp     al, 'Z'
    ja      .c1
    or      al, 0x20
.c1:
    cmp     r8b, 'A'
    jb      .c2
    cmp     r8b, 'Z'
    ja      .c2
    or      r8b, 0x20
.c2:
    cmp     al, r8b
    jne     .no
    test    al, al
    jz      .yes
    inc     rcx
    inc     rdx
    jmp     .lp
.yes:
    mov     eax, 1
    ret
.no:
    xor     eax, eax
    ret

; ----------------------------------------------------------------------------
; is_protected_proc: check if ASCII name in rcx is a critical OS/shell process
; ----------------------------------------------------------------------------
is_protected_proc:
    push    rbx
    push    rsi
    mov     rsi, rcx
    xor     ebx, ebx
.lp:
    cmp     ebx, PROT_PROC_COUNT
    jae     .no
    mov     rcx, rsi
    mov     rdx, qword [prot_proc_tbl + rbx*8]
    call    ascii_ieq
    test    eax, eax
    jnz     .yes
    inc     ebx
    jmp     .lp
.yes:
    mov     eax, 1
    pop     rsi
    pop     rbx
    ret
.no:
    xor     eax, eax
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; Hash set helpers for tracking frozen & low-gear apps (MAX_HASH_TRACK = 32)
;   in: rcx = table ptr (qword[32]), rdx = 64-bit hash
; ----------------------------------------------------------------------------
hash_tbl_has:
    xor     eax, eax
.lp:
    cmp     eax, MAX_HASH_TRACK
    jae     .no
    cmp     qword [rcx + rax*8], rdx
    je      .yes
    inc     eax
    jmp     .lp
.yes:
    mov     eax, 1
    ret
.no:
    xor     eax, eax
    ret

hash_tbl_add:
    call    hash_tbl_has
    test    eax, eax
    jnz     .done
    xor     eax, eax
.lp:
    cmp     eax, MAX_HASH_TRACK
    jae     .done
    cmp     qword [rcx + rax*8], 0
    je      .put
    inc     eax
    jmp     .lp
.put:
    mov     qword [rcx + rax*8], rdx
.done:
    ret

hash_tbl_rem:
    xor     eax, eax
.lp:
    cmp     eax, MAX_HASH_TRACK
    jae     .done
    cmp     qword [rcx + rax*8], rdx
    jne     .nx
    mov     qword [rcx + rax*8], 0
.nx:
    inc     eax
    jmp     .lp
.done:
    ret

; ----------------------------------------------------------------------------
; extract_utf16_pname_hash: parse FULL UTF-16LE ImageName from SPI5 entry
;   in:  r8 = UTF-16LE ptr, ecx = char count, r9 = dst ASCII buf (24B)
;   out: rax = 64-bit case-insensitive FNV-1a hash of FULL name; [r9] = NUL-term (<=23 chars)
; ----------------------------------------------------------------------------
extract_utf16_pname_hash:
    push    rbx
    push    rsi
    mov     rax, 0xCBF29CE484222325
    mov     r10, 0x00000100000001B3
    xor     ebx, ebx
    test    r8, r8
    jz      .done
    test    ecx, ecx
    jz      .done
.lp:
    movzx   edx, word [r8]
    add     r8, 2

    mov     esi, edx
    cmp     esi, 'A'
    jb      .h_lo
    cmp     esi, 'Z'
    ja      .h_lo
    or      esi, 0x20
.h_lo:
    movzx   r11d, sil
    xor     rax, r11
    imul    rax, r10
    shr     esi, 8
    jz      .disp_put
    xor     rax, rsi
    imul    rax, r10

.disp_put:
    cmp     ebx, 23
    jae     .next
    cmp     edx, 0x20
    jb      .qmark
    cmp     edx, 0x7E
    jbe     .ascii_ok
.qmark:
    mov     dl, '?'
.ascii_ok:
    mov     byte [r9 + rbx], dl
    inc     ebx
.next:
    dec     ecx
    jnz     .lp

.done:
    mov     byte [r9 + rbx], 0
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; ensure_nvml_ready: lazy D3cold-safe NVML loader!
; ----------------------------------------------------------------------------
ensure_nvml_ready:
    cmp     dword [g_nvml_tried], 0
    jne     .already_tried

    push    r12
    sub     rsp, 32
    mov     dword [g_nvml_tried], 1

    lea     rcx, [szNvmlDll]
    call    [LoadLibraryA]
    test    rax, rax
    jz      .fin
    mov     r12, rax

    mov     rcx, r12
    lea     rdx, [szNvmlInit]
    call    [GetProcAddress]
    test    rax, rax
    jz      .fin
    call    rax
    test    eax, eax
    jnz     .fin

    mov     rcx, r12
    lea     rdx, [szNvmlShutdown]
    call    [GetProcAddress]
    mov     qword [g_pNvmlShutdown], rax

    mov     rcx, r12
    lea     rdx, [szNvmlGetHandle]
    call    [GetProcAddress]
    test    rax, rax
    jz      .fin
    xor     ecx, ecx
    lea     rdx, [g_nvml_dev]
    call    rax
    test    eax, eax
    jnz     .fail

    mov     rcx, r12
    lea     rdx, [szNvmlGetUtil]
    call    [GetProcAddress]
    mov     qword [g_pNvmlGetUtil], rax

    mov     rcx, r12
    lea     rdx, [szNvmlGetTemp]
    call    [GetProcAddress]
    mov     qword [g_pNvmlGetTemp], rax

    mov     rcx, r12
    lea     rdx, [szNvmlGetPower]
    call    [GetProcAddress]
    mov     qword [g_pNvmlGetPower], rax

    mov     rcx, r12
    lea     rdx, [szNvmlGetLimit]
    call    [GetProcAddress]
    mov     qword [g_pNvmlGetLimit], rax

    mov     rcx, r12
    lea     rdx, [szNvmlGetClock]
    call    [GetProcAddress]
    mov     qword [g_pNvmlGetClock], rax

    mov     rcx, r12
    lea     rdx, [szNvmlGetThrottle]
    call    [GetProcAddress]
    mov     qword [g_pNvmlGetThrottle], rax
    jmp     .fin

.fail:
    mov     qword [g_nvml_dev], 0
.fin:
    add     rsp, 32
    pop     r12
.already_tried:
    ret

; ----------------------------------------------------------------------------
; compute_io_log: map g_io_bps -> 0..100 log-scaled scope height in eax
; ----------------------------------------------------------------------------
compute_io_log:
    mov     rax, qword [g_io_bps]
    test    rax, rax
    jz      .zero
    bsr     rdx, rax
    cmp     edx, 12
    jbe     .min
    sub     edx, 12
    imul    edx, 5
    cmp     edx, 100
    jbe     .ok
    mov     edx, 100
.ok:
    mov     eax, edx
    ret
.min:
    mov     eax, 5
    ret
.zero:
    xor     eax, eax
    ret

; ----------------------------------------------------------------------------
; push_history_rings & update_caution_and_blackbox
; ----------------------------------------------------------------------------
push_history_rings:
    mov     ecx, dword [g_hist_head]
    and     ecx, HIST_LEN - 1

    mov     al, byte [g_cpu_pct]
    mov     byte [g_hist_cpu + rcx], al
    mov     al, byte [g_ram_pct]
    mov     byte [g_hist_ram + rcx], al
    mov     al, byte [g_gpu_pct]
    mov     byte [g_hist_gpu + rcx], al
    mov     al, byte [g_vram_pct]
    mov     byte [g_hist_vram + rcx], al

    call    compute_io_log
    mov     byte [g_hist_io + rcx], al

    inc     ecx
    and     ecx, HIST_LEN - 1
    mov     dword [g_hist_head], ecx
    ret

; update_caution_and_blackbox: computes Master Caution bits & appends 1 Hz Black-Box frame!
update_caution_and_blackbox:
    push    rbx
    push    rdi
    sub     rsp, 40

    ; 1. Compute live hardware fault bitmask
    xor     ebx, ebx
    cmp     dword [g_gpu_temp], 90
    jae     .f_tmp
    test    qword [g_gpu_throttle], 0x60
    jz      .chk_pwr
.f_tmp:
    or      ebx, FAULT_TMP
.chk_pwr:
    test    qword [g_gpu_throttle], 0x0C
    jz      .chk_vrm
    or      ebx, FAULT_PWR
.chk_vrm:
    cmp     dword [g_vram_pct], 95
    jb      .chk_ram
    or      ebx, FAULT_VRM
.chk_ram:
    cmp     dword [g_ram_pct], 92
    jb      .chk_cpu
    or      ebx, FAULT_RAM
.chk_cpu:
    cmp     dword [g_cpu_pct], 95
    jb      .save_caut
    or      ebx, FAULT_CPU
.save_caut:
    ; Master Caution bar annunciator triggers on TMP / VRM / RAM faults!
    mov     eax, ebx
    and     eax, FAULT_TMP or FAULT_VRM or FAULT_RAM
    mov     dword [g_caution_flags], eax

    ; 2. Append 1 Hz Black-Box Flight Data Recorder frame (zero disk, even when HUD closed!)
    call    [GetTickCount64]
    mov     rcx, rax
    sub     rcx, qword [g_bb_last_ms]
    cmp     dword [g_bb_count], 0
    je      .do_rec
    cmp     rcx, 1000
    jb      .done
.do_rec:
    mov     qword [g_bb_last_ms], rax

    mov     eax, dword [g_bb_head]
    shl     eax, 5              ; BB_STRIDE = 32
    lea     rdi, [g_bb_ring + rax]

    mov     al, byte [g_cpu_pct]
    mov     byte [rdi + BB_Cpu], al
    mov     al, byte [g_ram_pct]
    mov     byte [rdi + BB_Ram], al
    mov     al, byte [g_gpu_pct]
    mov     byte [rdi + BB_Gpu], al
    mov     al, byte [g_vram_pct]
    mov     byte [rdi + BB_Vram], al
    call    compute_io_log
    mov     byte [rdi + BB_IoLog], al
    mov     al, byte [g_gpu_temp]
    mov     byte [rdi + BB_Temp], al
    mov     al, byte [g_igpu_pct]
    mov     byte [rdi + BB_Igpu], al
    mov     byte [rdi + BB_Faults], bl

    mov     ax, word [g_gpu_pwr_w]
    mov     word [rdi + BB_PwrW], ax
    mov     ax, word [g_gpu_clk]
    mov     word [rdi + BB_Clk], ax
    mov     eax, dword [g_ram_used_mb]
    mov     dword [rdi + BB_RamMB], eax
    mov     eax, dword [g_vram_used_mb]
    mov     dword [rdi + BB_VramMB], eax
    mov     rax, qword [g_io_bps]
    shr     rax, 10
    mov     dword [rdi + BB_IoKB], eax

    ; Snapshot Top-1 CPU process 8-byte prefix (if process table populated!)
    mov     qword [rdi + BB_TopName], 0
    cmp     dword [g_top_count], 0
    je      .bb_adv
    mov     eax, dword [g_top_cpu + 0]
    shl     eax, 6              ; PROC_STRIDE = 64
    mov     rax, qword [g_procs + rax + PROC_Name]
    mov     qword [rdi + BB_TopName], rax

.bb_adv:
    mov     eax, dword [g_bb_head]
    inc     eax
    cmp     eax, BB_LEN
    jb      .bb_h_ok
    xor     eax, eax
.bb_h_ok:
    mov     dword [g_bb_head], eax

    mov     eax, dword [g_bb_count]
    cmp     eax, BB_LEN
    jae     .done
    inc     eax
    mov     dword [g_bb_count], eax

.done:
    add     rsp, 40
    pop     rdi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; telemetry_init: discover memory, cores, GPUs, QPC freq, dynamic DLLs, baseline
; ----------------------------------------------------------------------------
telemetry_init:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 32

    call    [GetCurrentProcessId]
    mov     dword [g_self_pid], eax

    lea     rcx, [g_qpc_freq]
    call    [QueryPerformanceFrequency]

    ; 1. SystemBasicInformation (class 0, EXACT 64 bytes)
    xor     ecx, ecx
    lea     rdx, [g_sbi_buf]
    mov     r8d, SBI_SIZE
    lea     r9, [g_ret_len]
    call    [NtQuerySystemInformation]
    test    eax, eax
    jnz     .phys_fallback
    mov     eax, dword [g_sbi_buf + SBI_NumberOfPhysicalPages]
    test    eax, eax
    jnz     .phys_ok
.phys_fallback:
    mov     eax, 1
.phys_ok:
    mov     dword [g_phys_pages], eax
    shr     eax, 8
    mov     dword [g_ram_tot_mb], eax

    ; 2. Discover hardware GPUs via D3DKMTEnumAdapters2
    mov     dword [g_gpu_count], 0
    mov     dword [g_vram_gpu_idx], 0

    mov     dword [g_ea2_hdr + D3DKMT_EA2_NumAdapters], 0
    mov     qword [g_ea2_hdr + D3DKMT_EA2_pAdapters], 0
    lea     rcx, [g_ea2_hdr]
    call    [D3DKMTEnumAdapters2]
    test    eax, eax
    jnz     .gpu_enum_done

    mov     eax, dword [g_ea2_hdr + D3DKMT_EA2_NumAdapters]
    test    eax, eax
    jz      .gpu_enum_done
    cmp     eax, MAX_GPU_ADAPTERS
    jbe     .ea2_cap_ok
    mov     eax, MAX_GPU_ADAPTERS
.ea2_cap_ok:
    mov     dword [g_ea2_hdr + D3DKMT_EA2_NumAdapters], eax
    lea     rdx, [g_ea2_arr]
    mov     qword [g_ea2_hdr + D3DKMT_EA2_pAdapters], rdx
    lea     rcx, [g_ea2_hdr]
    call    [D3DKMTEnumAdapters2]
    test    eax, eax
    jnz     .gpu_enum_done

    mov     r12d, dword [g_ea2_hdr + D3DKMT_EA2_NumAdapters]
    cmp     r12d, MAX_GPU_ADAPTERS
    jbe     .ea2_actual_ok
    mov     r12d, MAX_GPU_ADAPTERS
.ea2_actual_ok:
    xor     r13d, r13d
    xor     r15, r15

.adapter_loop:
    cmp     r13d, r12d
    jae     .gpu_enum_done

    mov     eax, r13d
    imul    eax, D3DKMT_AI_STRIDE
    lea     rsi, [g_ea2_arr + rax]

    mov     ebx, dword [rsi + D3DKMT_AI_hAdapter]
    test    ebx, ebx
    jz      .next_adapter

    mov     dword [g_qai_flags], 0
    mov     dword [g_qai_hdr + D3DKMT_QAI_hAdapter], ebx
    mov     dword [g_qai_hdr + D3DKMT_QAI_Type], KMTQAITYPE_ADAPTERTYPE
    lea     rax, [g_qai_flags]
    mov     qword [g_qai_hdr + D3DKMT_QAI_pData], rax
    mov     dword [g_qai_hdr + D3DKMT_QAI_DataSize], 4
    lea     rcx, [g_qai_hdr]
    call    [D3DKMTQueryAdapterInfo]
    test    eax, eax
    jnz     .close_and_next

    mov     eax, dword [g_qai_flags]
    test    eax, ADAPTERTYPE_SOFTWARE_BIT
    jnz     .close_and_next

    mov     edi, dword [g_gpu_count]
    cmp     edi, MAX_HW_GPUS
    jae     .close_and_next

    call    zero_d3d_qs
    mov     dword [g_d3d_qs + D3DKMT_QS_Type], D3DKMT_QS_TYPE_ADAPTER
    mov     eax, dword [rsi + D3DKMT_AI_LuidLow]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidLow], eax
    mov     eax, dword [rsi + D3DKMT_AI_LuidHigh]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidHigh], eax
    lea     rcx, [g_d3d_qs]
    call    [D3DKMTQueryStatistics]
    test    eax, eax
    jnz     .close_and_next

    mov     eax, edi
    shl     eax, 5
    lea     rdi, [g_gpus + rax]

    mov     eax, dword [rsi + D3DKMT_AI_LuidLow]
    mov     dword [rdi + HWGPU_LuidLow], eax
    mov     eax, dword [rsi + D3DKMT_AI_LuidHigh]
    mov     dword [rdi + HWGPU_LuidHigh], eax

    mov     eax, dword [g_d3d_qs + D3DKMT_QSR_NbSegments]
    cmp     eax, 31
    jbe     .seg_ok
    mov     eax, 31
.seg_ok:
    mov     dword [rdi + HWGPU_NbSegs], eax

    mov     eax, dword [g_d3d_qs + D3DKMT_QSR_NodeCount]
    cmp     eax, MAX_NODES_GPU
    jbe     .node_ok
    mov     eax, MAX_NODES_GPU
.node_ok:
    mov     dword [rdi + HWGPU_NodeCnt], eax
    mov     dword [rdi + HWGPU_DedMask], 0
    mov     qword [rdi + HWGPU_DedLimit], 0

    xor     r14d, r14d
.seg_scan:
    cmp     r14d, dword [rdi + HWGPU_NbSegs]
    jae     .seg_scan_done

    call    zero_d3d_qs
    mov     dword [g_d3d_qs + D3DKMT_QS_Type], D3DKMT_QS_TYPE_SEGMENT
    mov     eax, dword [rdi + HWGPU_LuidLow]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidLow], eax
    mov     eax, dword [rdi + HWGPU_LuidHigh]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidHigh], eax
    mov     dword [g_d3d_qs + D3DKMT_QS_InputId], r14d
    lea     rcx, [g_d3d_qs]
    call    [D3DKMTQueryStatistics]
    test    eax, eax
    jnz     .seg_next

    mov     eax, dword [g_d3d_qs + D3DKMT_QSR_SegAperture]
    test    eax, eax
    jnz     .seg_next

    mov     rax, qword [g_d3d_qs + D3DKMT_QSR_SegCommitLimit]
    test    rax, rax
    jz      .seg_next

    mov     ecx, r14d
    mov     edx, 1
    shl     edx, cl
    or      dword [rdi + HWGPU_DedMask], edx
    add     qword [rdi + HWGPU_DedLimit], rax

.seg_next:
    inc     r14d
    jmp     .seg_scan

.seg_scan_done:
    mov     rax, qword [rdi + HWGPU_DedLimit]
    cmp     rax, r15
    jbe     .not_best_vram
    mov     r15, rax
    mov     eax, dword [g_gpu_count]
    mov     dword [g_vram_gpu_idx], eax
.not_best_vram:
    inc     dword [g_gpu_count]

.close_and_next:
    mov     dword [g_close_handle], ebx
    lea     rcx, [g_close_handle]
    call    [D3DKMTCloseAdapter]
.next_adapter:
    inc     r13d
    jmp     .adapter_loop

.gpu_enum_done:
    lea     rcx, [g_prev_ft]
    call    [GetSystemTimeAsFileTime]
    mov     rax, qword [g_prev_ft]
    mov     qword [g_deep_prev_ft], rax

    ; 4. Baseline CPU ticks (class 8)
    mov     dword [g_num_cores], 0
    mov     ecx, SystemProcessorPerformanceInformation
    lea     rdx, [g_cpu_prev]
    mov     r8d, MAX_LOGICAL_CPUS * SPPI_STRIDE
    lea     r9, [g_ret_len]
    call    [NtQuerySystemInformation]
    test    eax, eax
    jnz     .perf_base
    xor     edx, edx
    mov     eax, dword [g_ret_len]
    mov     ecx, SPPI_STRIDE
    div     ecx
    cmp     eax, MAX_LOGICAL_CPUS
    jbe     .cores_ok
    mov     eax, MAX_LOGICAL_CPUS
.cores_ok:
    mov     dword [g_num_cores], eax

.perf_base:
    mov     ecx, SystemPerformanceInformation
    lea     rdx, [g_spi_buf]
    mov     r8d, SPI_BUF_SIZE
    lea     r9, [g_ret_len]
    call    [NtQuerySystemInformation]
    test    eax, eax
    jnz     .init_ram_zero
    mov     rax, qword [g_spi_buf + SPI_IoReadTransferCount]
    add     rax, qword [g_spi_buf + SPI_IoWriteTransferCount]
    mov     qword [g_prev_io_bytes], rax

    mov     eax, dword [g_phys_pages]
    mov     ecx, dword [g_spi_buf + SPI_AvailablePages]
    cmp     ecx, eax
    jae     .init_ram_zero
    sub     eax, ecx
    mov     edx, eax
    shr     edx, 8
    mov     dword [g_ram_used_mb], edx
    mov     rcx, rax
    mov     edx, dword [g_phys_pages]
    call    u64_pct_128
    mov     dword [g_ram_pct], eax
    jmp     .init_dyn_misc
.init_ram_zero:
    mov     dword [g_ram_pct], 0
    mov     dword [g_ram_used_mb], 0

.init_dyn_misc:
    mov     dword [g_nvml_tried], 0
    mov     qword [g_nvml_dev], 0
    mov     qword [g_pNvmlGetUtil], 0
    mov     qword [g_pNvmlShutdown], 0
    mov     qword [g_pNvmlGetTemp], 0
    mov     qword [g_pNvmlGetPower], 0
    mov     qword [g_pNvmlGetLimit], 0
    mov     qword [g_pNvmlGetClock], 0
    mov     qword [g_pNvmlGetThrottle], 0

    mov     qword [g_pGetIfTable2], 0
    mov     qword [g_pFreeMibTable], 0
    mov     qword [g_pCallNtPower], 0
    mov     qword [g_pRegOpenKeyExA], 0
    mov     qword [g_pRegQueryValA], 0
    mov     qword [g_pRegSetValA], 0
    mov     qword [g_pRegDelValA], 0
    mov     qword [g_pRegCloseKey], 0

    lea     rcx, [szIphlpapiDll]
    call    [LoadLibraryA]
    test    rax, rax
    jz      .load_powr
    mov     r12, rax
    mov     rcx, r12
    lea     rdx, [szGetIfTable2]
    call    [GetProcAddress]
    mov     qword [g_pGetIfTable2], rax
    mov     rcx, r12
    lea     rdx, [szFreeMibTable]
    call    [GetProcAddress]
    mov     qword [g_pFreeMibTable], rax

.load_powr:
    lea     rcx, [szPowrprofDll]
    call    [LoadLibraryA]
    test    rax, rax
    jz      .load_advapi
    mov     rcx, rax
    lea     rdx, [szCallNtPower]
    call    [GetProcAddress]
    mov     qword [g_pCallNtPower], rax

.load_advapi:
    lea     rcx, [szAdvapiDll]
    call    [LoadLibraryA]
    test    rax, rax
    jz      .init_gpu_base
    mov     r12, rax
    mov     rcx, r12
    lea     rdx, [szRegOpenKeyExA]
    call    [GetProcAddress]
    mov     qword [g_pRegOpenKeyExA], rax
    mov     rcx, r12
    lea     rdx, [szRegQueryValA]
    call    [GetProcAddress]
    mov     qword [g_pRegQueryValA], rax
    mov     rcx, r12
    lea     rdx, [szRegSetValA]
    call    [GetProcAddress]
    mov     qword [g_pRegSetValA], rax
    mov     rcx, r12
    lea     rdx, [szRegDelValA]
    call    [GetProcAddress]
    mov     qword [g_pRegDelValA], rax
    mov     rcx, r12
    lea     rdx, [szRegCloseKey]
    call    [GetProcAddress]
    mov     qword [g_pRegCloseKey], rax

.init_gpu_base:
    xor     r12d, r12d
.init_gpu_loop:
    cmp     r12d, dword [g_gpu_count]
    jae     .init_done
    cmp     r12d, dword [g_vram_gpu_idx]
    jne     .init_gpu_nodes
    mov     eax, r12d
    shl     eax, 5
    cmp     dword [g_gpus + rax + HWGPU_DedMask], 0
    jne     .init_gpu_next
.init_gpu_nodes:
    mov     eax, r12d
    shl     eax, 5
    lea     rsi, [g_gpus + rax]
    xor     r13d, r13d
.init_node_loop:
    cmp     r13d, dword [rsi + HWGPU_NodeCnt]
    jae     .init_gpu_next
    call    zero_d3d_qs
    mov     dword [g_d3d_qs + D3DKMT_QS_Type], D3DKMT_QS_TYPE_NODE
    mov     eax, dword [rsi + HWGPU_LuidLow]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidLow], eax
    mov     eax, dword [rsi + HWGPU_LuidHigh]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidHigh], eax
    mov     dword [g_d3d_qs + D3DKMT_QS_InputId], r13d
    lea     rcx, [g_d3d_qs]
    call    [D3DKMTQueryStatistics]

    mov     eax, r12d
    shl     eax, 5
    add     eax, r13d
    mov     rdx, qword [g_d3d_qs + D3DKMT_QSR_NodeRunningTime]
    mov     qword [g_gpu_prev_rt + rax*8], rdx

    inc     r13d
    jmp     .init_node_loop
.init_gpu_next:
    inc     r12d
    jmp     .init_gpu_loop

.init_done:
    mov     dword [g_cpu_pct], 0
    mov     dword [g_gpu_pct], 0
    mov     dword [g_igpu_pct], 0
    mov     dword [g_vram_pct], 0
    mov     dword [g_gpu_temp], 0
    mov     qword [g_io_bps], 0
    mov     dword [g_hist_head], 0
    mov     dword [g_deep_primed], 0
    mov     dword [g_proc_count], 0
    mov     dword [g_prev_proc_cnt], 0
    mov     dword [g_top_count], 0
    mov     dword [g_top_vram_count], 0
    mov     dword [g_bb_head], 0
    mov     dword [g_bb_count], 0
    mov     dword [g_caution_flags], 0

    add     rsp, 32
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; telemetry_poll: always-on core telemetry + Master Caution + 1 Hz Black-Box!
; ----------------------------------------------------------------------------
telemetry_poll:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 32

    lea     rcx, [g_cur_ft]
    call    [GetSystemTimeAsFileTime]
    mov     rax, qword [g_cur_ft]
    mov     r15, rax
    sub     r15, qword [g_prev_ft]
    mov     qword [g_prev_ft], rax
    cmp     r15, 100000
    jge     .dt_ok
    mov     r15, 5000000
.dt_ok:

    ; 2. CPU % (total + per-core)
    cmp     dword [g_num_cores], 0
    je      .cpu_done
    mov     ecx, SystemProcessorPerformanceInformation
    lea     rdx, [g_cpu_cur]
    mov     r8d, MAX_LOGICAL_CPUS * SPPI_STRIDE
    lea     r9, [g_ret_len]
    call    [NtQuerySystemInformation]
    test    eax, eax
    jnz     .cpu_done

    xor     r12, r12
    xor     r13, r13
    xor     r14, r14
    xor     ebx, ebx
    lea     rsi, [g_cpu_cur]
    lea     rdi, [g_cpu_prev]
.cpu_loop:
    cmp     ebx, dword [g_num_cores]
    jae     .cpu_total

    mov     rax, qword [rsi + SPPI_IdleTime]
    mov     r8, rax
    sub     r8, qword [rdi + SPPI_IdleTime]
    mov     qword [rdi + SPPI_IdleTime], rax
    add     r12, r8

    mov     rax, qword [rsi + SPPI_KernelTime]
    mov     r9, rax
    sub     r9, qword [rdi + SPPI_KernelTime]
    mov     qword [rdi + SPPI_KernelTime], rax
    add     r13, r9

    mov     rax, qword [rsi + SPPI_UserTime]
    mov     r10, rax
    sub     r10, qword [rdi + SPPI_UserTime]
    mov     qword [rdi + SPPI_UserTime], rax
    add     r14, r10

    mov     rdx, r9
    add     rdx, r10
    cmp     rdx, r8
    jbe     .core_zero
    mov     rcx, rdx
    sub     rcx, r8
    call    u64_pct_128
    mov     byte [g_core_pct + rbx], al
    jmp     .core_next
.core_zero:
    mov     byte [g_core_pct + rbx], 0
.core_next:
    add     rsi, SPPI_STRIDE
    add     rdi, SPPI_STRIDE
    inc     ebx
    jmp     .cpu_loop

.cpu_total:
    mov     rdx, r13
    add     rdx, r14
    cmp     rdx, r12
    jbe     .cpu_zero
    mov     rcx, rdx
    sub     rcx, r12
    call    u64_pct_128
    mov     dword [g_cpu_pct], eax
    jmp     .cpu_done
.cpu_zero:
    mov     dword [g_cpu_pct], 0
.cpu_done:

    ; 3. RAM % & Kernel I/O B/s
    mov     ecx, SystemPerformanceInformation
    lea     rdx, [g_spi_buf]
    mov     r8d, SPI_BUF_SIZE
    lea     r9, [g_ret_len]
    call    [NtQuerySystemInformation]
    test    eax, eax
    jnz     .perf_done

    mov     eax, dword [g_phys_pages]
    mov     ecx, dword [g_spi_buf + SPI_AvailablePages]
    cmp     ecx, eax
    jae     .ram_zero
    sub     eax, ecx
    mov     edx, eax
    shr     edx, 8
    mov     dword [g_ram_used_mb], edx
    mov     rcx, rax
    mov     edx, dword [g_phys_pages]
    call    u64_pct_128
    mov     dword [g_ram_pct], eax
    jmp     .io_calc
.ram_zero:
    mov     dword [g_ram_pct], 0
    mov     dword [g_ram_used_mb], 0

.io_calc:
    mov     rax, qword [g_spi_buf + SPI_IoReadTransferCount]
    add     rax, qword [g_spi_buf + SPI_IoWriteTransferCount]
    mov     rcx, qword [g_prev_io_bytes]
    mov     qword [g_prev_io_bytes], rax
    cmp     rax, rcx
    jbe     .io_zero
    sub     rax, rcx
    mov     r8, 10000000
    mul     r8
    cmp     rdx, r15
    jae     .io_zero
    div     r15
    mov     qword [g_io_bps], rax
    jmp     .perf_done
.io_zero:
    mov     qword [g_io_bps], 0
.perf_done:

    ; 4. Dedicated VRAM % & MiB (0.004ms, never wakes D3cold!)
    xor     r12, r12
    xor     r13, r13
    mov     qword [g_vram_committed], 0
    mov     dword [g_vram_used_mb], 0
    mov     dword [g_vram_tot_mb], 0
    mov     eax, dword [g_gpu_count]
    test    eax, eax
    jz      .vram_zero
    mov     eax, dword [g_vram_gpu_idx]
    shl     eax, 5
    lea     rsi, [g_gpus + rax]
    mov     ebx, dword [rsi + HWGPU_DedMask]
    test    ebx, ebx
    jz      .vram_zero

    xor     r14d, r14d
.vram_loop:
    cmp     r14d, dword [rsi + HWGPU_NbSegs]
    jae     .vram_calc
    bt      ebx, r14d
    jnc     .vram_next

    call    zero_d3d_qs
    mov     dword [g_d3d_qs + D3DKMT_QS_Type], D3DKMT_QS_TYPE_SEGMENT
    mov     eax, dword [rsi + HWGPU_LuidLow]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidLow], eax
    mov     eax, dword [rsi + HWGPU_LuidHigh]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidHigh], eax
    mov     dword [g_d3d_qs + D3DKMT_QS_InputId], r14d
    lea     rcx, [g_d3d_qs]
    call    [D3DKMTQueryStatistics]
    test    eax, eax
    jnz     .vram_next

    add     r13, qword [g_d3d_qs + D3DKMT_QSR_SegCommitLimit]
    add     r12, qword [g_d3d_qs + D3DKMT_QSR_SegBytesResid]
    mov     rax, qword [g_d3d_qs + D3DKMT_QSR_SegBytesCommit]
    add     qword [g_vram_committed], rax
.vram_next:
    inc     r14d
    jmp     .vram_loop

.vram_calc:
    mov     rax, r12
    shr     rax, 20
    mov     dword [g_vram_used_mb], eax
    mov     rax, r13
    shr     rax, 20
    mov     dword [g_vram_tot_mb], eax
    mov     rcx, r12
    mov     rdx, r13
    call    u64_pct_128
    mov     dword [g_vram_pct], eax
    jmp     .gpu_util_start
.vram_zero:
    mov     dword [g_vram_pct], 0

.gpu_util_start:
    ; 5. GPU % + Temp (when bar displays temp OR Master Caution is active on active dGPU!)
    xor     r14d, r14d
    mov     dword [g_igpu_pct], 0
    mov     dword [g_temp_polled], 0

    cmp     dword [g_d3_guard], 0
    je      .nvml_allow
    cmp     qword [g_vram_committed], 0
    jne     .nvml_allow
    mov     dword [g_gpu_temp], 0
    mov     qword [g_gpu_throttle], 0
    jmp     .d3d_nodes_start

.nvml_allow:
    call    ensure_nvml_ready
    mov     rax, qword [g_pNvmlGetUtil]
    test    rax, rax
    jz      .d3d_nodes_start
    mov     rcx, qword [g_nvml_dev]
    lea     rdx, [g_nvml_util]
    call    rax
    test    eax, eax
    jnz     .nvml_bar_temp
    mov     eax, dword [g_nvml_util]
    cmp     eax, 100
    jbe     .nvml_pct_ok
    mov     eax, 100
.nvml_pct_ok:
    mov     r14d, eax

.nvml_bar_temp:
    ; Poll temp (0.05ms!) if bar shows temp (mode 1/3) OR Master Caution is ON!
    mov     eax, dword [g_bar_mode]
    test    eax, 1
    jnz     .do_nv_temp
    cmp     dword [g_caution_en], 0
    je      .d3d_nodes_start
.do_nv_temp:
    mov     rax, qword [g_pNvmlGetTemp]
    test    rax, rax
    jz      .d3d_nodes_start
    mov     rcx, qword [g_nvml_dev]
    xor     edx, edx
    lea     r8, [g_nv_tmp32]
    call    rax
    test    eax, eax
    jnz     .nvml_temp_err
    mov     eax, dword [g_nv_tmp32]
    mov     dword [g_gpu_temp], eax
    mov     dword [g_temp_polled], 1
    jmp     .d3d_nodes_start
.nvml_temp_err:
    mov     dword [g_gpu_temp], 0

.d3d_nodes_start:
    xor     r12d, r12d
.gpu_loop:
    cmp     r12d, dword [g_gpu_count]
    jae     .gpu_util_done
    cmp     qword [g_pNvmlGetUtil], 0
    je      .gpu_chk_sleep
    cmp     r12d, dword [g_vram_gpu_idx]
    je      .gpu_next
    jmp     .gpu_poll_nodes
.gpu_chk_sleep:
    cmp     r12d, dword [g_vram_gpu_idx]
    jne     .gpu_poll_nodes
    cmp     dword [g_d3_guard], 0
    je      .gpu_poll_nodes
    cmp     qword [g_vram_committed], 0
    je      .gpu_next
.gpu_poll_nodes:
    mov     eax, r12d
    shl     eax, 5
    lea     rsi, [g_gpus + rax]
    xor     r13d, r13d
.node_loop:
    cmp     r13d, dword [rsi + HWGPU_NodeCnt]
    jae     .gpu_next

    call    zero_d3d_qs
    mov     dword [g_d3d_qs + D3DKMT_QS_Type], D3DKMT_QS_TYPE_NODE
    mov     eax, dword [rsi + HWGPU_LuidLow]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidLow], eax
    mov     eax, dword [rsi + HWGPU_LuidHigh]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidHigh], eax
    mov     dword [g_d3d_qs + D3DKMT_QS_InputId], r13d
    lea     rcx, [g_d3d_qs]
    call    [D3DKMTQueryStatistics]
    test    eax, eax
    jnz     .node_next

    mov     ebx, r12d
    shl     ebx, 5
    add     ebx, r13d
    mov     rax, qword [g_d3d_qs + D3DKMT_QSR_NodeRunningTime]
    mov     rcx, qword [g_gpu_prev_rt + rbx*8]
    mov     qword [g_gpu_prev_rt + rbx*8], rax
    cmp     rax, rcx
    jbe     .node_next
    sub     rax, rcx
    mov     rcx, rax
    mov     rdx, r15
    call    u64_pct_128
    cmp     r12d, dword [g_vram_gpu_idx]
    je      .node_not_igpu
    cmp     eax, dword [g_igpu_pct]
    jbe     .node_not_igpu
    mov     dword [g_igpu_pct], eax
.node_not_igpu:
    cmp     eax, r14d
    jbe     .node_next
    mov     r14d, eax
.node_next:
    inc     r13d
    jmp     .node_loop
.gpu_next:
    inc     r12d
    jmp     .gpu_loop

.gpu_util_done:
    mov     dword [g_gpu_pct], r14d
    call    push_history_rings
    call    update_caution_and_blackbox

    add     rsp, 32
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; find_prev_proc & find_grouped_proc (O(1) 64-bit hash match!)
; ----------------------------------------------------------------------------
find_prev_proc:
    mov     edx, dword [g_prev_proc_cnt]
    test    edx, edx
    jz      .not_found
    lea     rax, [g_prev_procs]
.lp:
    cmp     qword [rax + PPROC_Pid], rcx
    je      .found
    add     rax, PPROC_STRIDE
    dec     edx
    jnz     .lp
.not_found:
    xor     eax, eax
.found:
    ret

find_grouped_proc:
    test    edx, edx
    jz      .none
    lea     rax, [g_procs]
.lp:
    cmp     qword [rax + PROC_NameHash], rcx
    je      .found
    add     rax, PROC_STRIDE
    dec     edx
    jnz     .lp
.none:
    xor     eax, eax
.found:
    ret

; ----------------------------------------------------------------------------
; sort_top_list: fill g_top_*[0..MAX_TOP_PROCS-1] with indices into g_procs
;   in: rcx = dst array, edx = key (0=CPU, 1=RAM, 2=IO, 3=VRAM > 0 only!)
;   out: eax = entries written
; ----------------------------------------------------------------------------
sort_top_list:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15

    mov     r12, rcx
    mov     r13d, edx
    mov     r14d, dword [g_proc_count]
    xor     r15d, r15d

    xor     ebx, ebx
.outer:
    cmp     ebx, r14d
    jae     .done

    mov     eax, ebx
    shl     eax, 6
    lea     rsi, [g_procs + rax]
    cmp     r13d, 3
    je      .key_vram
    cmp     r13d, 2
    je      .key_io
    cmp     r13d, 1
    je      .key_mem
    mov     r8d, dword [rsi + PROC_CpuPct]
    shl     r8, 32
    mov     eax, dword [rsi + PROC_MemMB]
    or      r8, rax
    jmp     .insert
.key_mem:
    mov     r8d, dword [rsi + PROC_MemMB]
    jmp     .insert
.key_io:
    mov     r8, qword [rsi + PROC_IoBps]
    jmp     .insert
.key_vram:
    mov     r8d, dword [rsi + PROC_VramMB]
    test    r8d, r8d
    jz      .next_proc          ; VRAM leaderboard strictly shows active GPU consumers!

.insert:
    xor     ecx, ecx
.find_slot:
    cmp     ecx, r15d
    jae     .at_end
    mov     eax, dword [r12 + rcx*4]
    shl     eax, 6
    lea     rdi, [g_procs + rax]
    cmp     r13d, 3
    je      .cmp_vram
    cmp     r13d, 2
    je      .cmp_io
    cmp     r13d, 1
    je      .cmp_mem
    mov     r9d, dword [rdi + PROC_CpuPct]
    shl     r9, 32
    mov     eax, dword [rdi + PROC_MemMB]
    or      r9, rax
    jmp     .do_cmp
.cmp_mem:
    mov     r9d, dword [rdi + PROC_MemMB]
    jmp     .do_cmp
.cmp_io:
    mov     r9, qword [rdi + PROC_IoBps]
    jmp     .do_cmp
.cmp_vram:
    mov     r9d, dword [rdi + PROC_VramMB]
.do_cmp:
    cmp     r8, r9
    ja      .shift_and_put
    inc     ecx
    jmp     .find_slot

.at_end:
    cmp     r15d, MAX_TOP_PROCS
    jae     .next_proc
    mov     dword [r12 + r15*4], ebx
    inc     r15d
    jmp     .next_proc

.shift_and_put:
    cmp     r15d, MAX_TOP_PROCS
    jae     .shift_full
    inc     r15d
.shift_full:
    lea     edx, [r15 - 1]
.shift_lp:
    cmp     edx, ecx
    jle     .put_val
    mov     eax, dword [r12 + rdx*4 - 4]
    mov     dword [r12 + rdx*4], eax
    dec     edx
    jmp     .shift_lp
.put_val:
    mov     dword [r12 + rcx*4], ebx

.next_proc:
    inc     ebx
    jmp     .outer

.done:
    mov     eax, r15d
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; clamp_proc_selection: keep g_col_sel & g_col_scroll valid for CPU/RAM/VRM/IO
; ----------------------------------------------------------------------------
clamp_proc_selection:
    xor     r8d, r8d
.lp:
    cmp     r8d, 5
    jae     .done
    cmp     r8d, 2
    je      .next

    mov     ecx, dword [g_top_count]
    cmp     r8d, 3
    jne     .cnt_ok
    mov     ecx, dword [g_top_vram_count]
.cnt_ok:
    test    ecx, ecx
    jnz     .has_rows
    mov     ecx, 1
.has_rows:
    dec     ecx

    mov     eax, dword [g_col_sel + r8*4]
    cmp     eax, ecx
    jle     .sel_ok
    mov     eax, ecx
    mov     dword [g_col_sel + r8*4], eax
.sel_ok:
    mov     edx, dword [g_col_scroll + r8*4]
    cmp     edx, eax
    jle     .scr_bot
    mov     dword [g_col_scroll + r8*4], eax
    jmp     .next
.scr_bot:
    mov     r9d, ecx
    sub     r9d, VISIBLE_ROWS - 1
    jns     .max_scr_ok
    xor     r9d, r9d
.max_scr_ok:
    cmp     edx, r9d
    jle     .next
    mov     dword [g_col_scroll + r8*4], r9d
.next:
    inc     r8d
    jmp     .lp
.done:
    ret

; ----------------------------------------------------------------------------
; query_proc_vram_and_prio: live D3DKMT Type 9 Local VRAM + Priority check!
;   in: ecx = PID; out: eax = dedicated VRAM MiB, edx = PFLAG_LOWGEAR bit (0 or 2)
;   Only called when dGPU is active (g_vram_committed > 0), 0ms when sleeping!
; ----------------------------------------------------------------------------
query_proc_vram_and_prio:
    push    rbx
    push    r12
    push    r13
    sub     rsp, 32             ; 8+24+32 = 64 -> aligned

    xor     r12d, r12d          ; vram_mb = 0
    xor     r13d, r13d          ; prio_flag = 0

    mov     r8d, ecx
    mov     ecx, PROCESS_QUERY_INFO_
    xor     edx, edx
    call    [OpenProcess]
    test    rax, rax
    jz      .done
    mov     rbx, rax

    ; Check IDLE priority class (0x40) on the handle we already opened!
    mov     rcx, rbx
    call    [GetPriorityClass]
    cmp     eax, IDLE_PRIORITY_CLASS_
    jne     .chk_vram
    mov     r13d, PFLAG_LOWGEAR

.chk_vram:
    cmp     dword [g_gpu_count], 0
    je      .close
    cmp     qword [g_vram_committed], 0
    je      .close

    call    zero_d3d_qs
    mov     dword [g_d3d_qs + D3DKMT_QS_Type], D3DKMT_QS_TYPE_PROC_SEG_GRP
    mov     eax, dword [g_vram_gpu_idx]
    shl     eax, 5
    mov     edx, dword [g_gpus + rax + HWGPU_LuidLow]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidLow], edx
    mov     edx, dword [g_gpus + rax + HWGPU_LuidHigh]
    mov     dword [g_d3d_qs + D3DKMT_QS_LuidHigh], edx
    mov     qword [g_d3d_qs + D3DKMT_QS_hProcess], rbx
    mov     dword [g_d3d_qs + D3DKMT_QS_InputId], 0 ; Local dedicated VRAM segment group!
    lea     rcx, [g_d3d_qs]
    call    [D3DKMTQueryStatistics]
    test    eax, eax
    jnz     .close

    mov     rax, qword [g_d3d_qs + D3DKMT_QSR_PsgUsage]
    test    rax, rax
    jz      .close
    shr     rax, 20
    jnz     .v_ok
    mov     eax, 1              ; show >=1M for tiny non-zero GPU allocations
.v_ok:
    mov     r12d, eax

.close:
    mov     rcx, rbx
    call    [CloseHandle]
.done:
    mov     eax, r12d
    mov     edx, r13d
    add     rsp, 32
    pop     r13
    pop     r12
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; telemetry_poll_deep: LAZY-POLLED ONLY WHEN HUD IS OPEN!
;   Now includes Per-Process VRAM Radar (D3DKMT Type 9) + Frozen/Low-Gear tags!
; ----------------------------------------------------------------------------
telemetry_poll_deep:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    push    r14
    push    r15
    sub     rsp, 48

    lea     rcx, [g_deep_cur_ft]
    call    [GetSystemTimeAsFileTime]
    mov     rax, qword [g_deep_cur_ft]
    mov     r15, rax
    sub     r15, qword [g_deep_prev_ft]
    mov     qword [g_deep_prev_ft], rax
    cmp     r15, 100000
    jge     .deep_dt_ok
    mov     r15, 5000000
.deep_dt_ok:

    ; --- A. Deep NVML (only on Tab 0 TELEM!) ---
    cmp     dword [g_panel_tab], 0
    jne     .deep_bat

    mov     dword [g_gpu_pwr_w], 0
    mov     dword [g_gpu_pwr_lim], 0
    mov     dword [g_gpu_clk], 0
    mov     dword [g_gpu_mclk], 0
    mov     qword [g_gpu_throttle], 0

    cmp     dword [g_d3_guard], 0
    je      .nv_deep_allow
    cmp     qword [g_vram_committed], 0
    jne     .nv_deep_allow
    mov     dword [g_gpu_temp], 0
    jmp     .deep_bat

.nv_deep_allow:
    call    ensure_nvml_ready
    cmp     qword [g_nvml_dev], 0
    je      .deep_bat

    cmp     dword [g_temp_polled], 0
    jne     .nv_pwr
    mov     rax, qword [g_pNvmlGetTemp]
    test    rax, rax
    jz      .nv_pwr
    mov     rcx, qword [g_nvml_dev]
    xor     edx, edx
    lea     r8, [g_nv_tmp32]
    call    rax
    test    eax, eax
    jnz     .nv_temp_zero
    mov     eax, dword [g_nv_tmp32]
    mov     dword [g_gpu_temp], eax
    jmp     .nv_pwr
.nv_temp_zero:
    mov     dword [g_gpu_temp], 0

.nv_pwr:
    mov     rax, qword [g_pNvmlGetPower]
    test    rax, rax
    jz      .nv_lim
    mov     rcx, qword [g_nvml_dev]
    lea     rdx, [g_nv_tmp32]
    call    rax
    test    eax, eax
    jnz     .nv_lim
    mov     eax, dword [g_nv_tmp32]
    add     eax, 500
    xor     edx, edx
    mov     ecx, 1000
    div     ecx
    mov     dword [g_gpu_pwr_w], eax

.nv_lim:
    mov     rax, qword [g_pNvmlGetLimit]
    test    rax, rax
    jz      .nv_clk
    mov     rcx, qword [g_nvml_dev]
    lea     rdx, [g_nv_tmp32]
    call    rax
    test    eax, eax
    jnz     .nv_clk
    mov     eax, dword [g_nv_tmp32]
    add     eax, 500
    xor     edx, edx
    mov     ecx, 1000
    div     ecx
    mov     dword [g_gpu_pwr_lim], eax

.nv_clk:
    mov     rax, qword [g_pNvmlGetClock]
    test    rax, rax
    jz      .nv_thr
    mov     rcx, qword [g_nvml_dev]
    xor     edx, edx
    lea     r8, [g_nv_tmp32]
    call    rax
    test    eax, eax
    jnz     .nv_mclk
    mov     eax, dword [g_nv_tmp32]
    mov     dword [g_gpu_clk], eax
.nv_mclk:
    mov     rax, qword [g_pNvmlGetClock]
    mov     rcx, qword [g_nvml_dev]
    mov     edx, 2
    lea     r8, [g_nv_tmp32]
    call    rax
    test    eax, eax
    jnz     .nv_thr
    mov     eax, dword [g_nv_tmp32]
    mov     dword [g_gpu_mclk], eax

.nv_thr:
    mov     rax, qword [g_pNvmlGetThrottle]
    test    rax, rax
    jz      .deep_bat
    mov     rcx, qword [g_nvml_dev]
    lea     rdx, [g_gpu_throttle]
    call    rax

    ; --- B. Laptop Battery State ---
.deep_bat:
    mov     byte [g_bat_present], 0
    mov     rax, qword [g_pCallNtPower]
    test    rax, rax
    jz      .deep_net
    mov     ecx, SystemBatteryState_
    xor     edx, edx
    xor     r8d, r8d
    lea     r9, [g_bat_buf]
    mov     dword [rsp + 0x20], SBS_SIZE
    call    rax
    test    eax, eax
    jnz     .deep_net

    mov     al, byte [g_bat_buf + SBS_AcOnLine]
    mov     byte [g_bat_ac], al
    mov     al, byte [g_bat_buf + SBS_BatteryPresent]
    mov     byte [g_bat_present], al
    mov     eax, dword [g_bat_buf + SBS_Rate]
    mov     dword [g_bat_rate_mw], eax

    mov     edx, dword [g_bat_buf + SBS_MaxCapacity]
    test    edx, edx
    jz      .bat_pct_zero
    mov     ecx, dword [g_bat_buf + SBS_RemainingCapacity]
    call    u64_pct_128
    mov     dword [g_bat_pct], eax
    jmp     .deep_net
.bat_pct_zero:
    mov     dword [g_bat_pct], 0

    ; --- C. Physical NIC Network Up/Down ---
.deep_net:
    mov     rax, qword [g_pGetIfTable2]
    test    rax, rax
    jz      .deep_procs
    cmp     qword [g_pFreeMibTable], 0
    je      .deep_procs

    mov     qword [g_if_tbl_ptr], 0
    lea     rcx, [g_if_tbl_ptr]
    call    rax
    test    eax, eax
    jnz     .deep_procs
    mov     rsi, qword [g_if_tbl_ptr]
    test    rsi, rsi
    jz      .deep_procs

    mov     ecx, dword [rsi + MIB_IF_TABLE2_NumEntries]
    lea     rdi, [rsi + MIB_IF_TABLE2_TableOffset]
    xor     r12, r12
    xor     r13, r13
.nic_loop:
    test    ecx, ecx
    jz      .nic_done
    cmp     dword [rdi + MIB_IF_ROW2_OperStatus], 1
    jne     .nic_next
    movzx   eax, byte [rdi + MIB_IF_ROW2_Flags]
    and     eax, 3
    cmp     eax, 1
    jne     .nic_next
    add     r12, qword [rdi + MIB_IF_ROW2_InOctets]
    add     r13, qword [rdi + MIB_IF_ROW2_OutOctets]
.nic_next:
    add     rdi, MIB_IF_ROW2_STRIDE
    dec     ecx
    jmp     .nic_loop

.nic_done:
    mov     rcx, rsi
    mov     rax, qword [g_pFreeMibTable]
    call    rax

    cmp     dword [g_deep_primed], 0
    je      .nic_prime
    mov     rax, r12
    sub     rax, qword [g_prev_net_rx]
    jbe     .rx_zero
    mov     r8, 10000000
    mul     r8
    cmp     rdx, r15
    jae     .rx_zero
    div     r15
    mov     qword [g_net_rx_bps], rax
    jmp     .tx_calc
.rx_zero:
    mov     qword [g_net_rx_bps], 0
.tx_calc:
    mov     rax, r13
    sub     rax, qword [g_prev_net_tx]
    jbe     .tx_zero
    mov     r8, 10000000
    mul     r8
    cmp     rdx, r15
    jae     .tx_zero
    div     r15
    mov     qword [g_net_tx_bps], rax
    jmp     .nic_store
.tx_zero:
    mov     qword [g_net_tx_bps], 0
    jmp     .nic_store
.nic_prime:
    mov     qword [g_net_rx_bps], 0
    mov     qword [g_net_tx_bps], 0
.nic_store:
    mov     qword [g_prev_net_rx], r12
    mov     qword [g_prev_net_tx], r13

    ; --- D. Live Top-24 Processes + Per-Process VRAM Radar (Tab 0 only!) ---
.deep_procs:
    cmp     dword [g_panel_tab], 0
    jne     .deep_done

    mov     ecx, SystemProcessInformation
    lea     rdx, [g_proc_raw]
    mov     r8d, SPI5_BUF_SIZE
    lea     r9, [g_ret_len]
    call    [NtQuerySystemInformation]
    test    eax, eax
    jnz     .deep_done

    mov     eax, dword [g_num_cores]
    test    eax, eax
    jnz     .nc_ok
    mov     eax, 1
.nc_ok:
    mov     r14, r15
    imul    r14, rax

    lea     rsi, [g_proc_raw]
    xor     r12d, r12d
    xor     r13d, r13d

.proc_walk:
    mov     rbx, qword [rsi + SPI5_UniqueProcessId]
    cmp     rbx, 4
    jbe     .proc_advance

    mov     rax, qword [rsi + SPI5_UserTime]
    add     rax, qword [rsi + SPI5_KernelTime]
    mov     qword [g_tmp_cpu_t], rax

    mov     rax, qword [rsi + SPI5_ReadTransferCount]
    add     rax, qword [rsi + SPI5_WriteTransferCount]
    mov     qword [g_tmp_io_b], rax

    cmp     r13d, MAX_PREV_PROCS
    jae     .skip_save_prev
    mov     eax, r13d
    imul    eax, PPROC_STRIDE
    lea     rdi, [g_next_prev_procs + rax]
    mov     qword [rdi + PPROC_Pid], rbx
    mov     rax, qword [g_tmp_cpu_t]
    mov     qword [rdi + PPROC_CpuTime], rax
    mov     rax, qword [g_tmp_io_b]
    mov     qword [rdi + PPROC_IoBytes], rax
    inc     r13d
.skip_save_prev:

    mov     rax, qword [rsi + SPI5_WorkingSetPrivateSize]
    shr     rax, 20
    mov     dword [g_tmp_mem_mb], eax
    mov     dword [g_tmp_cpu_pct], 0
    mov     qword [g_tmp_io_bps], 0
    mov     dword [g_tmp_vram_mb], 0
    mov     dword [g_tmp_pflags], 0

    cmp     dword [g_deep_primed], 0
    je      .parse_pname
    mov     rcx, rbx
    call    find_prev_proc
    test    rax, rax
    jz      .parse_pname
    mov     r8, rax

    mov     rcx, qword [g_tmp_cpu_t]
    mov     rdx, qword [r8 + PPROC_CpuTime]
    cmp     rcx, rdx
    jbe     .p_io_d
    sub     rcx, rdx
    mov     rdx, r14
    push    r8
    call    u64_pct_128
    pop     r8
    mov     dword [g_tmp_cpu_pct], eax

.p_io_d:
    mov     rax, qword [g_tmp_io_b]
    mov     rcx, qword [r8 + PPROC_IoBytes]
    cmp     rax, rcx
    jbe     .parse_pname
    sub     rax, rcx
    mov     r9, 10000000
    mul     r9
    cmp     rdx, r15
    jae     .parse_pname
    div     r15
    mov     qword [g_tmp_io_bps], rax

.parse_pname:
    movzx   ecx, word [rsi + SPI5_ImageNameLen]
    shr     ecx, 1
    mov     r8, qword [rsi + SPI5_ImageNameBuf]
    lea     r9, [g_tmp_pname]
    call    extract_utf16_pname_hash
    mov     qword [g_tmp_phash], rax

    ; Tag Frozen / Low-Gear flags from tracked hash tables
    lea     rcx, [g_frozen_hashes]
    mov     rdx, rax
    call    hash_tbl_has
    test    eax, eax
    jz      .chk_low_tbl
    or      dword [g_tmp_pflags], PFLAG_FROZEN
.chk_low_tbl:
    lea     rcx, [g_low_hashes]
    mov     rdx, qword [g_tmp_phash]
    call    hash_tbl_has
    test    eax, eax
    jz      .chk_proc_vram
    or      dword [g_tmp_pflags], PFLAG_LOWGEAR

.chk_proc_vram:
    ; Live Per-Process Dedicated VRAM Radar (D3DKMT Type 9) when dGPU is active!
    cmp     qword [g_vram_committed], 0
    je      .do_group_chk
    mov     ecx, ebx
    call    query_proc_vram_and_prio
    mov     dword [g_tmp_vram_mb], eax
    or      dword [g_tmp_pflags], edx

.do_group_chk:
    cmp     dword [g_group_pids], 0
    je      .new_proc_slot
    mov     rcx, qword [g_tmp_phash]
    mov     edx, r12d
    call    find_grouped_proc
    test    rax, rax
    jz      .new_proc_slot

    mov     edx, dword [g_tmp_cpu_pct]
    add     edx, dword [rax + PROC_CpuPct]
    cmp     edx, 100
    jbe     .mg_cpu_ok
    mov     edx, 100
.mg_cpu_ok:
    mov     dword [rax + PROC_CpuPct], edx
    mov     edx, dword [g_tmp_mem_mb]
    add     dword [rax + PROC_MemMB], edx
    mov     rdx, qword [g_tmp_io_bps]
    add     qword [rax + PROC_IoBps], rdx
    mov     edx, dword [g_tmp_vram_mb]
    add     dword [rax + PROC_VramMB], edx
    mov     edx, dword [g_tmp_pflags]
    or      dword [rax + PROC_Flags], edx
    inc     dword [rax + PROC_Count]
    jmp     .proc_advance

.new_proc_slot:
    cmp     r12d, MAX_PROCS
    jae     .proc_advance

    mov     eax, r12d
    shl     eax, 6
    lea     rdi, [g_procs + rax]

    mov     dword [rdi + PROC_Pid], ebx
    mov     eax, dword [g_tmp_cpu_pct]
    mov     dword [rdi + PROC_CpuPct], eax
    mov     eax, dword [g_tmp_mem_mb]
    mov     dword [rdi + PROC_MemMB], eax
    mov     dword [rdi + PROC_Count], 1
    mov     rax, qword [g_tmp_io_bps]
    mov     qword [rdi + PROC_IoBps], rax
    mov     rax, qword [g_tmp_phash]
    mov     qword [rdi + PROC_NameHash], rax
    mov     eax, dword [g_tmp_vram_mb]
    mov     dword [rdi + PROC_VramMB], eax
    mov     eax, dword [g_tmp_pflags]
    mov     dword [rdi + PROC_Flags], eax

    ; Copy 24 bytes g_tmp_pname -> [rdi + PROC_Name]
    mov     rax, qword [g_tmp_pname + 0]
    mov     qword [rdi + PROC_Name + 0], rax
    mov     rax, qword [g_tmp_pname + 8]
    mov     qword [rdi + PROC_Name + 8], rax
    mov     rax, qword [g_tmp_pname + 16]
    mov     qword [rdi + PROC_Name + 16], rax

    inc     r12d

.proc_advance:
    mov     eax, dword [rsi + SPI5_NextEntryOffset]
    test    eax, eax
    jz      .proc_walk_done
    add     rsi, rax
    jmp     .proc_walk

.proc_walk_done:
    mov     dword [g_proc_count], r12d
    mov     dword [g_prev_proc_cnt], r13d

    test    r13d, r13d
    jz      .do_sorts
    lea     rsi, [g_next_prev_procs]
    lea     rdi, [g_prev_procs]
    mov     ecx, r13d
    imul    ecx, PPROC_STRIDE / 8
    rep     movsq

.do_sorts:
    lea     rcx, [g_top_cpu]
    xor     edx, edx
    call    sort_top_list
    mov     dword [g_top_count], eax

    lea     rcx, [g_top_ram]
    mov     edx, 1
    call    sort_top_list

    lea     rcx, [g_top_io]
    mov     edx, 2
    call    sort_top_list

    lea     rcx, [g_top_vram]
    mov     edx, 3
    call    sort_top_list
    mov     dword [g_top_vram_count], eax

    call    clamp_proc_selection
    mov     dword [g_deep_primed], 1

.deep_done:
    add     rsp, 48
    pop     r15
    pop     r14
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret

; ----------------------------------------------------------------------------
; apply_action_to_pid: hardened single-PID executive for Kill / Freeze / Low-Gear
;   in: ecx = PID, edx = action (0=KILL, 1=FREEZE, 2=THAW, 3=SET_IDLE, 4=SET_NORMAL)
;   out: eax = 1 if succeeded, 0 if failed/blocked
; ----------------------------------------------------------------------------
apply_action_to_pid:
    cmp     ecx, 4
    jbe     .fail_fast
    cmp     ecx, dword [g_self_pid]
    je      .fail_fast

    push    rbx
    push    r12
    sub     rsp, 40             ; 8+16+40 = 64 -> aligned
    mov     r12d, edx           ; action
    mov     r8d, ecx            ; dwProcessId

    cmp     r12d, 0
    je      .acc_kill
    cmp     r12d, 3
    jae     .acc_prio
    mov     ecx, PROCESS_SUSPEND_RESUME_
    jmp     .do_open
.acc_kill:
    mov     ecx, PROCESS_TERMINATE_
    jmp     .do_open
.acc_prio:
    mov     ecx, PROCESS_SET_INFORMATION_

.do_open:
    xor     edx, edx
    call    [OpenProcess]
    test    rax, rax
    jz      .fail
    mov     rbx, rax

    cmp     r12d, 0
    je      .do_kill
    cmp     r12d, 1
    je      .do_freeze
    cmp     r12d, 2
    je      .do_thaw
    cmp     r12d, 3
    je      .do_idle

    ; Action 4: Restore NORMAL_PRIORITY_CLASS
    mov     rcx, rbx
    mov     edx, NORMAL_PRIORITY_CLASS_
    call    [SetPriorityClass]
    mov     r12d, eax
    jmp     .close_ok

.do_idle:
    mov     rcx, rbx
    mov     edx, IDLE_PRIORITY_CLASS_
    call    [SetPriorityClass]
    mov     r12d, eax
    jmp     .close_ok

.do_kill:
    mov     rcx, rbx
    mov     edx, 1
    call    [TerminateProcess]
    mov     r12d, eax
    jmp     .close_ok

.do_freeze:
    mov     rcx, rbx
    call    [NtSuspendProcess]
    xor     r12d, r12d
    test    eax, eax
    jnz     .close_ok
    mov     r12d, 1
    jmp     .close_ok

.do_thaw:
    mov     rcx, rbx
    call    [NtResumeProcess]
    xor     r12d, r12d
    test    eax, eax
    jnz     .close_ok
    mov     r12d, 1

.close_ok:
    mov     rcx, rbx
    call    [CloseHandle]
    mov     eax, r12d
    add     rsp, 40
    pop     r12
    pop     rbx
    ret

.fail:
    xor     eax, eax
    add     rsp, 40
    pop     r12
    pop     rbx
    ret
.fail_fast:
    xor     eax, eax
    ret

; ----------------------------------------------------------------------------
; execute_cockpit_action: unified TOCTOU-proof Kill / Freeze-Thaw / Low-Gear!
;   Uses g_kill_pid, g_kill_count, g_kill_hash, g_kill_name
;   in: ecx = subaction (0=KILL, 1=FREEZE, 2=THAW, 3=SET_IDLE, 4=SET_NORMAL)
;   out: eax = number of PIDs affected
; ----------------------------------------------------------------------------
execute_cockpit_action:
    push    rbx
    push    rsi
    push    rdi
    push    r12
    push    r13
    sub     rsp, 32             ; 8+40+32 = 80 -> aligned

    mov     r12d, ecx           ; subaction
    xor     r13d, r13d          ; affected count

    lea     rcx, [g_kill_name]
    call    is_protected_proc
    test    eax, eax
    jnz     .done

    mov     ecx, SystemProcessInformation
    lea     rdx, [g_proc_raw]
    mov     r8d, SPI5_BUF_SIZE
    lea     r9, [g_ret_len]
    call    [NtQuerySystemInformation]
    test    eax, eax
    jnz     .done

    lea     rsi, [g_proc_raw]
.lp:
    mov     rbx, qword [rsi + SPI5_UniqueProcessId]
    cmp     rbx, 4
    jbe     .next
    cmp     ebx, dword [g_self_pid]
    je      .next

    cmp     dword [g_kill_count], 1
    ja      .chk_hash
    cmp     ebx, dword [g_kill_pid]
    jne     .next

.chk_hash:
    movzx   ecx, word [rsi + SPI5_ImageNameLen]
    shr     ecx, 1
    mov     r8, qword [rsi + SPI5_ImageNameBuf]
    lea     r9, [g_tmp_pname]
    call    extract_utf16_pname_hash
    cmp     rax, qword [g_kill_hash]
    jne     .next

    lea     rcx, [g_tmp_pname]
    call    is_protected_proc
    test    eax, eax
    jnz     .next

    mov     ecx, ebx
    mov     edx, r12d
    call    apply_action_to_pid
    add     r13d, eax

    cmp     dword [g_kill_count], 1
    jbe     .done

.next:
    mov     eax, dword [rsi + SPI5_NextEntryOffset]
    test    eax, eax
    jz      .done
    add     rsi, rax
    jmp     .lp

.done:
    mov     eax, r13d
    add     rsp, 32
    pop     r13
    pop     r12
    pop     rdi
    pop     rsi
    pop     rbx
    ret
