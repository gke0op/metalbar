; ============================================================================
; metal_defs.inc — Live-verified Windows 11 x64 (Build 26200) bare-metal defs
; MetalBar v3.0 "FLIGHT DECK" — Zero CRT, COM, PDH, WMI, DirectX, Drivers!
; ============================================================================

; --- DPI Awareness ---
DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4

; --- Window Styles / ExStyles ---
WS_POPUP_STYLE      = 0x80000000
WS_EX_TOPMOST_      = 0x00000008
WS_EX_TRANSPARENT_  = 0x00000020
WS_EX_TOOLWINDOW_   = 0x00000080
WS_EX_LAYERED_      = 0x00080000
WS_EX_NOACTIVATE_   = 0x08000000

OVERLAY_EXSTYLE     = WS_EX_TOPMOST_ or WS_EX_TOOLWINDOW_ or WS_EX_NOACTIVATE_ or WS_EX_LAYERED_ or WS_EX_TRANSPARENT_
PANEL_EXSTYLE       = WS_EX_TOPMOST_ or WS_EX_TOOLWINDOW_ or WS_EX_LAYERED_

; --- SetWindowPos / UpdateLayeredWindow / SystemParametersInfo ---
HWND_TOPMOST_       = -1
SWP_NOSIZE_         = 0x0001
SWP_NOMOVE_         = 0x0002
SWP_NOACTIVATE_     = 0x0010
ULW_ALPHA_          = 0x00000002
AC_SRC_OVER_        = 0x00
AC_SRC_ALPHA_       = 0x01
SPI_GETWORKAREA_    = 0x0030
DIB_RGB_COLORS_     = 0
BI_RGB_             = 0
SW_SHOWNOACTIVATE_  = 4
SW_SHOW_            = 5

; --- Messages & Hotkeys ---
MOD_CONTROL_        = 0x0002
MOD_SHIFT_          = 0x0004
MOD_NOREPEAT_       = 0x4000
WM_DESTROY_         = 0x0002
WM_CLOSE_           = 0x0010
WM_QUIT_            = 0x0012
WM_SETTINGCHANGE_   = 0x001A
WM_MOUSEACTIVATE_   = 0x0021
WM_DISPLAYCHANGE_   = 0x007E
WM_KEYDOWN_         = 0x0100
WM_TIMER_           = 0x0113
WM_LBUTTONDOWN_     = 0x0201
WM_MOUSEWHEEL_      = 0x020A
WM_HOTKEY_          = 0x0312
MA_NOACTIVATE_      = 3

; Virtual keys
VK_TAB_             = 0x09
VK_RETURN_          = 0x0D
VK_SHIFT_           = 0x10
VK_CONTROL_         = 0x11
VK_MENU_            = 0x12
VK_ESCAPE_          = 0x1B
VK_SPACE_           = 0x20
VK_PRIOR_           = 0x21          ; Page Up
VK_NEXT_            = 0x22          ; Page Down
VK_LEFT_            = 0x25
VK_UP_              = 0x26
VK_RIGHT_           = 0x27
VK_DOWN_            = 0x28
VK_DELETE_          = 0x2E
VK_1_               = '1'
VK_2_               = '2'
VK_3_               = '3'
VK_A_               = 'A'
VK_B_               = 'B'
VK_C_               = 'C'
VK_E_               = 'E'
VK_F_               = 'F'
VK_H_               = 'H'
VK_K_               = 'K'
VK_L_               = 'L'
VK_N_               = 'N'
VK_Q_               = 'Q'
VK_S_               = 'S'
VK_Y_               = 'Y'

; --- SetWinEventHook (Instant Z-order lock) ---
EVENT_SYSTEM_FOREGROUND_  = 0x0003
WINEVENT_OUTOFCONTEXT_    = 0x0000
WINEVENT_SKIPOWNPROCESS_  = 0x0002

; --- Registry (HKCU\...\Run for Start With Windows) ---
HKEY_CURRENT_USER_        = -0x7FFFFFFF
KEY_READ_                 = 0x20019
KEY_SET_VALUE_            = 0x0002
REG_SZ_                   = 1

; --- Process Access Rights & Priority Classes ---
PROCESS_TERMINATE_        = 0x0001
PROCESS_SET_INFORMATION_  = 0x0200
PROCESS_QUERY_INFO_       = 0x0400
PROCESS_SUSPEND_RESUME_   = 0x0800
PROCESS_QUERY_LIMITED_    = 0x1000
IDLE_PRIORITY_CLASS_      = 0x0040
NORMAL_PRIORITY_CLASS_    = 0x0020

; --- PeekMessage / Display Settings ---
PM_REMOVE_                = 0x0001
ENUM_CURRENT_SETTINGS_    = -1
DEVMODEA_SIZE             = 156
DEVMODEA_dmSize           = 36
DEVMODEA_dmFrequency      = 120

; --- Persistent Config (metalbar.cfg, 64 bytes, v5 backward-safe!) ---
CFG_MAGIC           = 'MBV3'
CFG_SIZE            = 64
NUM_PALETTES        = 6
NUM_RATES           = 6

; --- NtQuerySystemInformation classes ---
SystemPerformanceInformation          = 2
SystemProcessInformation              = 5
SystemProcessorPerformanceInformation = 8

SBI_SIZE                   = 64
SBI_NumberOfPhysicalPages  = 0x0C

SPI_BUF_SIZE               = 512
SPI_IoReadTransferCount    = 0x08
SPI_IoWriteTransferCount   = 0x10
SPI_AvailablePages         = 0x2C

; SYSTEM_PROCESS_INFORMATION (class 5 — live-verified on Win11 Build 26200 x64!)
SPI5_BUF_SIZE              = 1572864
SPI5_NextEntryOffset       = 0x00
SPI5_WorkingSetPrivateSize = 0x08
SPI5_UserTime              = 0x28
SPI5_KernelTime            = 0x30
SPI5_ImageNameLen          = 0x38
SPI5_ImageNameBuf          = 0x40
SPI5_UniqueProcessId       = 0x50
SPI5_ReadTransferCount     = 0xE8
SPI5_WriteTransferCount    = 0xF0

SPPI_STRIDE                = 48
SPPI_IdleTime              = 0x00
SPPI_KernelTime            = 0x08
SPPI_UserTime              = 0x10
MAX_LOGICAL_CPUS           = 128

; --- D3DKMT (Live-verified Win11 Build 26200, including Type 9 Per-Process VRAM!) ---
D3DKMT_EA2_NumAdapters     = 0x00
D3DKMT_EA2_pAdapters       = 0x08
D3DKMT_AI_STRIDE           = 20
D3DKMT_AI_hAdapter         = 0x00
D3DKMT_AI_LuidLow          = 0x04
D3DKMT_AI_LuidHigh         = 0x08
MAX_GPU_ADAPTERS           = 64

D3DKMT_QAI_hAdapter        = 0x00
D3DKMT_QAI_Type            = 0x04
D3DKMT_QAI_pData           = 0x08
D3DKMT_QAI_DataSize        = 0x10
KMTQAITYPE_ADAPTERTYPE     = 15
ADAPTERTYPE_SOFTWARE_BIT   = 0x04

D3DKMT_QS_SIZE             = 0x328
D3DKMT_QS_Type             = 0x00
D3DKMT_QS_LuidLow          = 0x04
D3DKMT_QS_LuidHigh         = 0x08
D3DKMT_QS_hProcess         = 0x10
D3DKMT_QS_InputId          = 0x320

D3DKMT_QS_TYPE_ADAPTER     = 0
D3DKMT_QS_TYPE_SEGMENT     = 3
D3DKMT_QS_TYPE_NODE        = 5
D3DKMT_QS_TYPE_PROC_SEG_GRP = 9             ; Per-Process Local VRAM Usage (1 call/proc!)

D3DKMT_QSR_NbSegments      = 0x18
D3DKMT_QSR_NodeCount       = 0x1C
D3DKMT_QSR_SegCommitLimit  = 0x18
D3DKMT_QSR_SegBytesCommit  = 0x20
D3DKMT_QSR_SegBytesResid   = 0x28
D3DKMT_QSR_SegAperture     = 0x40
D3DKMT_QSR_NodeRunningTime = 0x18
D3DKMT_QSR_PsgUsage        = 0x28           ; UINT64 Usage (dedicated local VRAM bytes!)

; --- MIB_IF_ROW2 (iphlpapi.dll) ---
MIB_IF_TABLE2_NumEntries   = 0x00
MIB_IF_TABLE2_TableOffset  = 0x08
MIB_IF_ROW2_STRIDE         = 1352
MIB_IF_ROW2_Flags          = 0x480
MIB_IF_ROW2_OperStatus     = 0x484
MIB_IF_ROW2_InOctets       = 0x4B8
MIB_IF_ROW2_OutOctets      = 0x500

; --- SYSTEM_BATTERY_STATE (powrprof.dll) ---
SystemBatteryState_        = 5
SBS_SIZE                   = 32
SBS_AcOnLine               = 0x00
SBS_BatteryPresent         = 0x01
SBS_MaxCapacity            = 0x08
SBS_RemainingCapacity      = 0x0C
SBS_Rate                   = 0x10
BATTERY_UNKNOWN_RATE_      = 0x80000000

; --- History Rings, Process Table & Cockpit Control Tables ---
HIST_LEN                   = 64
MAX_PROCS                  = 1024
MAX_PREV_PROCS             = 1024
MAX_TOP_PROCS              = 24
MAX_HASH_TRACK             = 32

; Parsed process entry layout (EXACT 64 bytes — power-of-2 stride!)
PROC_STRIDE                = 64
PROC_Pid                   = 0x00   ; dd (representative PID)
PROC_CpuPct                = 0x04   ; dd (0..100)
PROC_MemMB                 = 0x08   ; dd (RAM MiB)
PROC_Count                 = 0x0C   ; dd (1 = single PID, >1 = grouped app count)
PROC_IoBps                 = 0x10   ; dq (bytes/sec)
PROC_NameHash              = 0x18   ; dq (64-bit FNV-1a hash of FULL UTF-16LE ImageName)
PROC_Name                  = 0x20   ; 24 bytes null-terminated ASCII display name
PROC_VramMB                = 0x38   ; dd (Per-Process Dedicated VRAM MiB via D3DKMT Type 9!)
PROC_Flags                 = 0x3C   ; dd (bit 0 = Frozen [FRZ], bit 1 = Low-Gear [LOW])

PFLAG_FROZEN               = 0x01
PFLAG_LOWGEAR              = 0x02

; Previous PID delta slot (24 bytes)
PPROC_STRIDE               = 24
PPROC_Pid                  = 0x00
PPROC_CpuTime              = 0x08
PPROC_IoBytes              = 0x10

; --- Avionics Flight Data Black-Box Recorder (300 frames = 5 minutes @ 1 Hz, 9.4 KiB!) ---
BB_LEN                     = 300
BB_STRIDE                  = 32
BB_Cpu                     = 0x00   ; db
BB_Ram                     = 0x01   ; db
BB_Gpu                     = 0x02   ; db
BB_Vram                    = 0x03   ; db
BB_IoLog                   = 0x04   ; db (0..100 log-scaled)
BB_Temp                    = 0x05   ; db (GPU C)
BB_Igpu                    = 0x06   ; db (iGPU %)
BB_Faults                  = 0x07   ; db (anomaly bitmask!)
BB_PwrW                    = 0x08   ; dw (GPU watts)
BB_Clk                     = 0x0A   ; dw (GPU MHz)
BB_RamMB                   = 0x0C   ; dd (RAM used MiB)
BB_VramMB                  = 0x10   ; dd (VRAM used MiB)
BB_IoKB                    = 0x14   ; dd (I/O KiB/s)
BB_TopName                 = 0x18   ; 8 bytes top-CPU exe prefix

; Master Caution / Black-Box Anomaly Bits
FAULT_TMP                  = 0x01   ; GPU >= 85C or NVML thermal throttle
FAULT_PWR                  = 0x02   ; NVML power throttle
FAULT_VRM                  = 0x04   ; Dedicated VRAM >= 95% (spill!)
FAULT_RAM                  = 0x08   ; System RAM >= 92%
FAULT_CPU                  = 0x10   ; CPU >= 95% saturation

; Unified Cockpit Process Action Modes
ACT_KILL                   = 0
ACT_FREEZE                 = 1
ACT_LOWGEAR                = 2
