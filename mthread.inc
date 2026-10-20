;=============================================================================
; MTHREAD.INC - MASM 6.11 Macros, Model Abstractions & TCB Layout
;=============================================================================

; --- 1. Processor Feature Detection (@Cpu bit 1 = 80186+) ---
HAS_186 EQU (@Cpu AND 0002h)

SHL_IMM MACRO dest, count
    IF HAS_186
        shl dest, count
    ELSE
        IF (count) LE 3
            REPEAT count
                shl dest, 1
            ENDM
        ELSE
            mov cl, count
            shl dest, cl
        ENDIF
    ENDIF
ENDM

SHR_IMM MACRO dest, count
    IF HAS_186
        shr dest, count
    ELSE
        IF (count) LE 3
            REPEAT count
                shr dest, 1
            ENDM
        ELSE
            mov cl, count
            shr dest, cl
        ENDIF
    ENDIF
ENDM

; --- 2. Memory Model Pointer Sizes (@CodeSize, @DataSize) ---
IF @CodeSize
    CODE_PTR_SIZE   EQU 4
     far_or_near_code TEXTEQU <FAR>
ELSE
    CODE_PTR_SIZE   EQU 2
    far_or_near_code TEXTEQU <NEAR>
ENDIF

IF @DataSize
    DATA_PTR_SIZE   EQU 4
    far_or_near_data TEXTEQU <FAR>
ELSE
    DATA_PTR_SIZE   EQU 2
    far_or_near_data TEXTEQU <NEAR>
ENDIF

CODE_PTR TYPEDEF far_or_near_code PTR
DATA_PTR TYPEDEF far_or_near_data PTR

; --- 3. Scheduler & DOS Constants ---
MAX_THREADS         EQU 8
NO_THREAD           EQU 0FFh
DEFAULT_STACK_SIZE  EQU 1024
ISR_STACK_SIZE      EQU 512
STACK_GUARD_MARGIN  EQU 64          ; Margin above allocation base for STKHQQ

SDA_HEADER_SIZE     EQU 24          ; DX=0018h bytes at SDA+00h..17h
SDA_CON_VARS_SIZE   EQU 16          ; Console state vars at SDA+18h..27h
IOSTACK_SIZE        EQU 384         ; 180h bytes for DOS IOStack
IOSWAP_BUF_SIZE     EQU (SDA_CON_VARS_SIZE + IOSTACK_SIZE) ; 400 bytes total

; Thread States (tcb_state)
STATE_UNUSED        EQU 0
STATE_READY         EQU 1
STATE_RUNNING       EQU 2
STATE_SLEEPING      EQU 3
STATE_WAIT_DISK     EQU 4           ; Waiting for DiskStack owner to exit DOS
STATE_WAIT_AUX      EQU 5           ; Waiting for AuxStack owner to exit DOS
STATE_ZOMBIE        EQU 6           ; Exited; awaiting lazy stack free()
STATE_WAIT_IO       EQU 7           ; Three-way IOStack contention
STATE_WAIT_SYNC     EQU 8           ; Blocked on Event, Mutex, Sem, or CondVar


; Thread Flags (tcb_flags)
TCB_FLG_OWN_STACK   EQU 01h         ; Stack allocated via malloc()
TCB_FLG_IN_DOS      EQU 02h         ; Thread is currently inside INT 21h
TCB_FLG_SWAPPED_IO  EQU 04h         ; Thread's IOStack is saved in io_swap_buf

; DOS Internal Stack IDs (dos_stack_id)
DOS_STK_NONE        EQU 0
DOS_STK_IO          EQU 1           ; AH = 01h..0Ch
DOS_STK_DISK        EQU 2           ; AH >= 0Dh (except 59h)
DOS_STK_AUX         EQU 3           ; AH = 59h or ErrorMode != 0

; Error Codes (matching mthread.h)
MT_ERR_NONE         EQU 00h
MT_ERR_NO_SLOTS     EQU 0FEh
MT_ERR_NO_MEMORY    EQU 0FDh
MT_ERR_INVALID      EQU 0FCh

; --- 4. Thread Control Block (64 Bytes = 40h -> Indexed via SHL_IMM reg, 6) ---
TCB STRUC
    ; Saved 8086 Register Context (28 bytes)
    reg_ax          DW  ?           ; +00h
    reg_bx          DW  ?           ; +02h
    reg_cx          DW  ?           ; +04h
    reg_dx          DW  ?           ; +06h
    reg_si          DW  ?           ; +08h
    reg_di          DW  ?           ; +0Ah
    reg_bp          DW  ?           ; +0Ch
    reg_sp          DW  ?           ; +0Eh
    reg_ds          DW  ?           ; +10h
    reg_es          DW  ?           ; +12h
    reg_ss          DW  ?           ; +14h
    reg_ip          DW  ?           ; +16h
    reg_cs          DW  ?           ; +18h
    reg_flags       DW  ?           ; +1Ah

    ; Scheduler, Priority Inheritance & Stack Metadata (12 bytes)
    tcb_state       DB  ?           ; +1Ch: STATE_*
    tcb_prio        DB  ?           ; +1Dh: Effective priority (0..7)
    tcb_base_prio   DB  ?           ; +1Eh: Base priority (restored after boost)
    tcb_flags       DB  ?           ; +1Fh: TCB_FLG_* bitmask
    sleep_ticks     DW  ?           ; +20h: Tick countdown for thread_sleep()
    stk_limit       DW  ?           ; +22h: Written to DGROUP:STKHQQ on switch
    stk_alloc_off   DW  ?           ; +24h: Heap offset for free()
    stk_alloc_seg   DW  ?           ; +26h: Heap segment (or dos_stack_id in low byte)

    ; Per-Thread 24-Byte DOS SDA Header Snapshot (24 bytes)
    sda_hdr         DB  SDA_HEADER_SIZE DUP (?) ; +28h..3Fh
TCB ENDS

; --- Load Near or Far DATA_PTR into ES:reg_off (DS stays DGROUP) ---
LOAD_DATA_PTR MACRO reg_off, ptr_param
    IF @DataSize
        les  reg_off, ptr_param
    ELSE
        push ds
        pop  es
        mov  reg_off, ptr_param
    ENDIF
ENDM

; --- Synchronization Object Structures (matching mthread.h) ---
MT_EVENT STRUC
    ev_signaled     DB  ?           ; +00h: 0 or 1
    ev_manual       DB  ?           ; +01h: 0 = Auto-reset, 1 = Manual-reset
    ev_wait_mask    DB  ?           ; +02h: Bitmask of waiting TIDs
    ev_reserved     DB  ?           ; +03h
MT_EVENT ENDS

MT_MUTEX STRUC
    mtx_owner       DB  ?           ; +00h: TID (0..7) or NO_THREAD (0FFh)
    mtx_count       DB  ?           ; +01h: Recursive lock count
    mtx_wait_mask   DB  ?           ; +02h: Bitmask of waiting TIDs
    mtx_reserved    DB  ?           ; +03h
MT_MUTEX ENDS

MT_SEM STRUC
    sem_count       DW  ?           ; +00h: 16-bit unsigned resource count
    sem_wait_mask   DB  ?           ; +02h: Bitmask of waiting TIDs
    sem_reserved    DB  ?           ; +03h
MT_SEM ENDS

MT_COND STRUC
    cv_wait_mask    DB  ?           ; +00h: Bitmask of waiting TIDs
    cv_reserved     DB  ?           ; +01h
MT_COND ENDS
