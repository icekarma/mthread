;=============================================================================
; MTHREAD.ASM: Preemptive Multithreading Kernel for MS-DOS
;=============================================================================

ifndef MODEL_NAME
    MODEL_NAME TEXTEQU <SMALL>
endif

.MODEL MODEL_NAME, C

IFDEF USE_186
    .186
ELSE
    .8086
ENDIF

INCLUDE mthread.inc

; External MSC 7.00 / CRT symbols
EXTERNDEF C STKHQQ: WORD            ; MSC 7.00 stack-check lower bound in DGROUP
EXTERNDEF C malloc: PROC
EXTERNDEF C free:   PROC

;=============================================================================
; DATA SEGMENT (DGROUP)
;=============================================================================

.DATA

ALIGN 2
tcb_table       TCB MAX_THREADS DUP (<>)

current_tcb     DW  OFFSET tcb_table    ; Cached pointer to TCB[current_tid]
current_tid     DB  0                   ; 0..7, or NO_THREAD (0FFh) when idle
active_mask     DB  01h                 ; Bitmask of in-use TCB slots
join_all_mask   DB  00h                 ; Bitmask of threads in thread_join_all()
join_wait_mask  DB  MAX_THREADS DUP (0) ; Per-TID bitmask of thread_join() waiters

; PIT & timer chaining state
pit_shift_val   DB  0                   ; 0..3 (1x, 2x, 4x, 8x)
pit_sub_reload  DB  1                   ; (1 SHL pit_shift_val)
pit_sub_count   DB  1                   ; Countdown to BIOS INT 08h chain

; DOS SDA & stack ownership tracking
sda_off         DW  0                   ; Offset of DOS SDA (from AX=5D06h)
sda_seg         DW  0                   ; Segment of DOS SDA
iostack_off     DW  0                   ; Offset of 384-byte IOStack inside SDA
io_stack_owner  DB  NO_THREAD           ; Thread currently inside AH=01h..0Ch
disk_stack_owner DB NO_THREAD           ; Thread currently inside AH>=0Dh
aux_stack_owner DB  NO_THREAD           ; Thread currently inside AH=59h/Err
io_swap_owner   DB  NO_THREAD           ; Thread whose IOStack is in io_swap_buf

; 400-byte buffer for IOStack collision swapping (16B con vars + 384B IOStack)
ALIGN 2
io_swap_buf     DB  IOSWAP_BUF_SIZE DUP (0)

; Dedicated 512-byte scheduler & ISR stack (keeps user thread stacks tiny)
ALIGN 2
isr_stack_base  DB  ISR_STACK_SIZE DUP (0)
isr_stack_top   LABEL WORD

tid_bit_lut     DB  01h, 02h, 04h, 08h, 10h, 20h, 40h, 80h

;=============================================================================
; CODE SEGMENT
;=============================================================================

.CODE

; CS-reachable variables for interrupt entry stubs before DS is loaded
dgroup_seg      DW  0                   ; Initialized to SEG DGROUP in thread_init
scratch_ds      DW  0                   ; Temp storage during context save
scratch_bx      DW  0                   ; Temp storage during context save
bios_lock       DB  0                   ; >0 when executing inside wrapped BIOS int
in_scheduler    DB  0                   ; Reentrancy guard for scheduler
calib_iostack   DB  0                   ; 1 during thread_init IOStack probe

old_int08       DD  0                   ; Saved BIOS INT 08h vector
old_int10       DD  0                   ; Saved BIOS Video vector
old_int13       DD  0                   ; Saved BIOS Disk vector
old_int15       DD  0                   ; Saved BIOS System Services vector
old_int16       DD  0                   ; Saved BIOS Keyboard vector
old_int17       DD  0                   ; Saved BIOS Printer vector
old_int21       DD  0                   ; Saved DOS INT 21h vector

;-----------------------------------------------------------------------------
; isr_timer (INT 08h hardware timer handler)
;-----------------------------------------------------------------------------
isr_timer PROC FAR
    ; 1. Did this interrupt wake us from the built-in HLT idle loop?
    cmp  cs:[dgroup_seg], 0
    je   chain_only                 ; Failsafe if fired before init finishes

    mov  cs:[scratch_ds], ds
    mov  cs:[scratch_bx], bx
    mov  ds, cs:[dgroup_seg]

    cmp  current_tid, NO_THREAD
    je   from_idle_loop

    ; 2. Save interrupted thread's 14 registers directly into its TCB
    mov  bx, current_tcb
    mov  [bx].TCB.reg_ax, ax
    mov  ax, cs:[scratch_bx]
    mov  [bx].TCB.reg_bx, ax
    mov  [bx].TCB.reg_cx, cx
    mov  [bx].TCB.reg_dx, dx
    mov  [bx].TCB.reg_si, si
    mov  [bx].TCB.reg_di, di
    mov  [bx].TCB.reg_bp, bp
    mov  ax, cs:[scratch_ds]
    mov  [bx].TCB.reg_ds, ax
    mov  [bx].TCB.reg_es, es

    ; Pop hardware interrupt frame (IP, CS, FLAGS) off thread stack into TCB
    pop  WORD PTR [bx].TCB.reg_ip
    pop  WORD PTR [bx].TCB.reg_cs
    pop  WORD PTR [bx].TCB.reg_flags

    ; Thread stack is now completely clean; save SS:SP
    mov  [bx].TCB.reg_sp, sp
    mov  [bx].TCB.reg_ss, ss

    ; If thread was RUNNING, mark it READY so the round-robin scan considers it
    cmp  [bx].TCB.tcb_state, STATE_RUNNING
    jne   switch_to_isr_stk
    mov  [bx].TCB.tcb_state, STATE_READY
    jmp  SHORT switch_to_isr_stk

from_idle_loop:
    ; CPU was in HLT on isr_stack; discard the 6-byte IRET frame by resetting SP
switch_to_isr_stk:
    mov  ax, ds
    mov  ss, ax
    mov  sp, OFFSET isr_stack_top

    ; 3. Handle PIT sub-tick divider and either chain to BIOS INT 08h or send EOI
    dec  pit_sub_count
    jz   do_bios_chain

    ; Sub-tick: send Specific EOI for IRQ0 (60h) to 8259 PIC
    mov  al, 60h
    out  20h, al
    jmp  SHORT tick_sleepers

do_bios_chain:
    mov  al, pit_sub_reload
    mov  pit_sub_count, al
    ; Chain to original BIOS INT 08h handler on isr_stack (BIOS sends EOI)
    pushf
    call DWORD PTR cs:[old_int08]
    cli                             ; Ensure interrupts are off again for scheduler

tick_sleepers:
    ; 4. Decrement sleep_ticks for any STATE_SLEEPING threads
    mov  bx, OFFSET tcb_table
    mov  cx, MAX_THREADS
sleep_scan_loop:
    cmp  [bx].TCB.tcb_state, STATE_SLEEPING
    jne   sleep_next
    dec  [bx].TCB.sleep_ticks
    jnz  sleep_next
    mov  [bx].TCB.tcb_state, STATE_READY
sleep_next:
    add  bx, SIZEOF TCB
    loop sleep_scan_loop

    ; 5. Check if preemption is temporarily locked (e.g., inside BIOS call)
    cmp  cs:[bios_lock], 0
    jne  resume_current_thread

    ; Fall through into the priority-based round-robin scheduler!
    jmp  SHORT schedule_next_thread

chain_only:
    jmp  DWORD PTR cs:[old_int08]
isr_timer ENDP

;-----------------------------------------------------------------------------
; schedule_next_thread - Priority-based round-robin dispatcher
; Expects: DS = SS = DGROUP, SP = OFFSET isr_stack_top, IF = 0
;-----------------------------------------------------------------------------
schedule_next_thread LABEL NEAR
    ; Start scanning at (current_tid + 1) AND 07h for round-robin fairness
    mov  al, current_tid
    inc  al
    and  al, (MAX_THREADS - 1)      ; AL = candidate TID (0..7)

    mov  dl, NO_THREAD              ; DL = best_tid found so far
    mov  dh, -1                     ; DH = best_prio found so far (-1 = none)
    mov  ch, MAX_THREADS            ; CH = loop counter (8 threads)

sched_loop:
    ; Compute BX = OFFSET tcb_table + (AL * 64)
    mov  bl, al
    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table

    cmp  [bx].TCB.tcb_state, STATE_READY
    jne  sched_advance

    ; Strictly greater '>' preserves round-robin order within each priority tier
    mov  ah, [bx].TCB.tcb_prio
    cmp  ah, dh
    jle  sched_advance

    mov  dh, ah                     ; New highest priority
    mov  dl, al                     ; Record winning TID
    mov  si, bx                     ; Record winning TCB pointer in SI

sched_advance:
    inc  al
    and  al, (MAX_THREADS - 1)
    dec  ch
    jnz  sched_loop

    ; Did we find a runnable thread?
    cmp  dl, NO_THREAD
    je   enter_idle_hlt

    ; Check if we are switching between different threads that need SDA header swap
    mov  bl, current_tid
    cmp  bl, dl
    je   activate_winner            ; Same thread selected -> no SDA swap needed

    ; Perform 24-byte SDA header micro-swap if leaving/entering an IN_DOS thread
    call swap_sda_headers

activate_winner:
    mov  current_tid, dl
    mov  current_tcb, si
    mov  bx, si

resume_current_thread:
    mov  bx, current_tcb
    mov  [bx].TCB.tcb_state, STATE_RUNNING

    ; Update MSC 7.00 stack probe watermark (DGROUP:STKHQQ)
    mov  ax, [bx].TCB.stk_limit
    mov  STKHQQ, ax

    ; Restore target thread's SS:SP and build IRET frame
    mov  ss, [bx].TCB.reg_ss
    mov  sp, [bx].TCB.reg_sp
    push WORD PTR [bx].TCB.reg_flags
    push WORD PTR [bx].TCB.reg_cs
    push WORD PTR [bx].TCB.reg_ip

    ; Restore registers and dispatch
    mov  ax, [bx].TCB.reg_ax
    mov  cx, [bx].TCB.reg_cx
    mov  dx, [bx].TCB.reg_dx
    mov  si, [bx].TCB.reg_si
    mov  di, [bx].TCB.reg_di
    mov  bp, [bx].TCB.reg_bp
    mov  es, [bx].TCB.reg_es
    push WORD PTR [bx].TCB.reg_ds
    mov  bx, [bx].TCB.reg_bx
    pop  ds
    iret

;-----------------------------------------------------------------------------
; Built-in HLT idle loop (runs on isr_stack when no thread is STATE_READY)
;-----------------------------------------------------------------------------
enter_idle_hlt:
    ; If outgoing thread was in DOS, snapshot its 24-byte SDA header first
    mov  dl, NO_THREAD
    call swap_sda_headers
    mov  current_tid, NO_THREAD
idle_hlt_spin:
    sti
    hlt
    jmp  SHORT idle_hlt_spin

;-----------------------------------------------------------------------------
; swap_sda_headers - 24-byte SDA header micro-swap & 400-byte IOStack exchange
; Entry: DS = DGROUP, BL = outgoing TID (or NO_THREAD), DL = incoming TID
;        SI = incoming TCB pointer (preserved)
;-----------------------------------------------------------------------------
swap_sda_headers PROC NEAR
    push si
    push di
    push es

    ; 1. Save outgoing thread's 24-byte SDA header if it is inside INT 21h
    cmp  bl, NO_THREAD
    je   check_incoming_sda

    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table
    test [bx].TCB.tcb_flags, TCB_FLG_IN_DOS
    jz   check_incoming_sda

    ; Copy 24 bytes from live SDA (sda_seg:sda_off) -> outgoing TCB.sda_hdr
    push ds
    pop  es
    lea  di, [bx].TCB.sda_hdr
    mov  cx, (SDA_HEADER_SIZE / 2)
    push ds
    mov  si, sda_off
    mov  ds, sda_seg
    cld
    rep  movsw
    pop  ds

check_incoming_sda:
    ; 2. Restore incoming thread's 24-byte SDA header if it is inside INT 21h
    cmp  dl, NO_THREAD
    je   swap_sda_done

    mov  bl, dl
    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table
    test [bx].TCB.tcb_flags, TCB_FLG_IN_DOS
    jz   swap_sda_done

    ; Copy 24 bytes from incoming TCB.sda_hdr -> live SDA (sda_seg:sda_off)
    mov  es, sda_seg
    mov  di, sda_off
    lea  si, [bx].TCB.sda_hdr
    mov  cx, (SDA_HEADER_SIZE / 2)
    cld
    rep  movsw

    ; 3. Did incoming thread have its 400-byte IOStack swapped out?
    test [bx].TCB.tcb_flags, TCB_FLG_SWAPPED_IO
    jz   swap_sda_done

    ; Exchange io_swap_buf (400B) with live SDA console vars + IOStack
    call exchange_iostack_slice

    ; Update ownership flags between previous live IOStack owner and incoming
    and  [bx].TCB.tcb_flags, NOT TCB_FLG_SWAPPED_IO
    mov  al, io_stack_owner
    mov  io_swap_owner, al
    mov  io_stack_owner, dl

    cmp  al, NO_THREAD
    je   swap_sda_done
    mov  bl, al
    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table
    or   [bx].TCB.tcb_flags, TCB_FLG_SWAPPED_IO

swap_sda_done:
    pop  es
    pop  di
    pop  si
    ret
swap_sda_headers ENDP

;-----------------------------------------------------------------------------
; exchange_iostack_slice - In-place 400B XCHG between io_swap_buf & live SDA
; Expects: DS = DGROUP, ES = sda_seg
;-----------------------------------------------------------------------------
exchange_iostack_slice PROC NEAR
    push ax
    push cx
    push si
    push di

    mov  es, sda_seg
    mov  si, OFFSET io_swap_buf

    ; Part A: 16 bytes of console state variables at SDA + 18h
    mov  di, sda_off
    add  di, SDA_HEADER_SIZE
    mov  cx, (SDA_CON_VARS_SIZE / 2)
xchg_con_loop:
    mov  ax, es:[di]
    xchg ax, [si]
    mov  es:[di], ax
    add  si, 2
    add  di, 2
    loop xchg_con_loop

    ; Part B: 384 bytes of IOStack at SDA + iostack_off
    mov  di, iostack_off
    mov  cx, (IOSTACK_SIZE / 2)
xchg_iostk_loop:
    mov  ax, es:[di]
    xchg ax, [si]
    mov  es:[di], ax
    add  si, 2
    add  di, 2
    loop xchg_iostk_loop

    pop  di
    pop  si
    pop  cx
    pop  ax
    ret
exchange_iostack_slice ENDP

;-----------------------------------------------------------------------------
; soft_context_switch - Voluntary/gatekeeper context save & reschedule
; Invoked via: PUSHF / CLI / PUSH CS / CALL NEAR PTR soft_context_switch
; Expects: DS = DGROUP, target tcb_state already set in current_tcb
;-----------------------------------------------------------------------------
soft_context_switch PROC NEAR
    mov  cs:[scratch_bx], bx
    mov  bx, current_tcb

    mov  [bx].TCB.reg_ax, ax
    mov  ax, cs:[scratch_bx]
    mov  [bx].TCB.reg_bx, ax
    mov  [bx].TCB.reg_cx, cx
    mov  [bx].TCB.reg_dx, dx
    mov  [bx].TCB.reg_si, si
    mov  [bx].TCB.reg_di, di
    mov  [bx].TCB.reg_bp, bp
    mov  ax, cs:[scratch_ds]
    mov  [bx].TCB.reg_ds, ax
    mov  [bx].TCB.reg_es, es

    ; Pop the synthetic IRET frame (IP, CS, FLAGS) into the TCB
    pop  WORD PTR [bx].TCB.reg_ip
    pop  WORD PTR [bx].TCB.reg_cs
    pop  WORD PTR [bx].TCB.reg_flags

    mov  [bx].TCB.reg_sp, sp
    mov  [bx].TCB.reg_ss, ss

    ; Switch onto dedicated scheduler stack and pick next thread
    mov  ax, ds
    mov  ss, ax
    mov  sp, OFFSET isr_stack_top
    jmp  schedule_next_thread
soft_context_switch ENDP

;-----------------------------------------------------------------------------
; isr_int21 - Hybrid MS-DOS INT 21h gatekeeper & stack-tier manager
;-----------------------------------------------------------------------------
isr_int21 PROC FAR
    ; 1. Fast-path Tier 0 (Stackless calls) & Program Termination (AH=4Ch/00h)
    cmp  ah, 33h
    je   dos_pass_through
    cmp  ah, 4Ch
    je   dos_pass_through
    cmp  ah, 50h
    je   dos_pass_through
    cmp  ah, 51h
    je   dos_pass_through
    cmp  ah, 62h
    je   dos_pass_through
    cmp  ah, 64h
    je   dos_pass_through
    or   ah, ah
    jz   dos_pass_through

    ; Save caller's DS, AX, BX, ES on caller's user stack while classifying
    push ds
    push ax
    push bx
    push es

    mov  ds, cs:[dgroup_seg]
    cmp  current_tid, NO_THREAD
    je   dos_pop_and_pass

    ; 2. Determine which internal DOS stack this call will use
    ;    Check ErrorMode byte at SDA+00h first: if non-zero, DOS uses AuxStack
    mov  es, sda_seg
    mov  bx, sda_off
    cmp  BYTE PTR es:[bx], 0
    jne  tier_aux_stack
    cmp  ah, 59h
    je   tier_aux_stack
    cmp  ah, 0Ch
    jbe  tier_io_stack

    ; --- TIER 2: DiskStack (AH >= 0Dh except 59h) ---
tier_disk_retry:
    cli
    mov  al, disk_stack_owner
    cmp  al, NO_THREAD
    je   claim_disk_stack
    cmp  al, current_tid
    je   dos_pop_and_pass           ; Recursive INT 21h from same thread

    ; Collision on DiskStack: boost owner's priority & block current thread
    mov  bl, al
    mov  dl, STATE_WAIT_DISK
    call boost_and_block_on_dos
    jmp  SHORT tier_disk_retry

claim_disk_stack:
    mov  al, current_tid
    mov  disk_stack_owner, al
    jmp  SHORT mark_in_dos_and_call

    ; --- TIER 3: AuxStack (AH = 59h or ErrorMode != 0) ---
tier_aux_retry:
tier_aux_stack:
    cli
    mov  al, aux_stack_owner
    cmp  al, NO_THREAD
    je   claim_aux_stack
    cmp  al, current_tid
    je   dos_pop_and_pass

    mov  bl, al
    mov  dl, STATE_WAIT_AUX
    call boost_and_block_on_dos
    jmp  SHORT tier_aux_retry

claim_aux_stack:
    mov  al, current_tid
    mov  aux_stack_owner, al
    jmp  SHORT mark_in_dos_and_call

    ; --- TIER 1: IOStack (AH = 01h..0Ch) ---
tier_io_retry:
tier_io_stack:
    cli
    mov  al, io_stack_owner
    cmp  al, NO_THREAD
    je   claim_io_stack
    cmp  al, current_tid
    je   dos_pop_and_pass

    ; Another thread owns IOStack! Is io_swap_buf free for a 400B slice swap?
    cmp  io_swap_owner, NO_THREAD
    jne  wait_for_io_buf

    ; Snapshot owner's 24-byte SDA header + 400-byte IOStack slice into buffer
    mov  bl, al
    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table
    or   [bx].TCB.tcb_flags, TCB_FLG_SWAPPED_IO
    mov  io_swap_owner, al

    ; Copy live 24-byte header into displaced owner's TCB.sda_hdr
    push si
    push di
    push cx
    push ds
    pop  es
    lea  di, [bx].TCB.sda_hdr
    mov  cx, (SDA_HEADER_SIZE / 2)
    push ds
    mov  si, sda_off
    mov  ds, sda_seg
    cld
    rep  movsw
    pop  ds
    pop  cx
    pop  di
    pop  si

    ; Exchange live 400B IOStack slice into io_swap_buf
    call exchange_iostack_slice
    jmp  SHORT claim_io_stack

wait_for_io_buf:
    mov  bl, al
    mov  dl, STATE_WAIT_IO
    call boost_and_block_on_dos
    jmp  SHORT tier_io_retry

claim_io_stack:
    mov  al, current_tid
    mov  io_stack_owner, al

mark_in_dos_and_call:
    mov  bx, current_tcb
    or   [bx].TCB.tcb_flags, TCB_FLG_IN_DOS

    ; Restore caller's registers before invoking the real DOS INT 21h handler
    pop  es
    pop  bx
    pop  ax
    pop  ds

    ; Call real DOS INT 21h
    pushf
    call DWORD PTR cs:[old_int21]

    ; --- POST-CALL: Preserve DOS return registers & flags, release tier ---
    pushf                           ; Save DOS return flags (CF, ZF, etc.)
    push ds
    push ax
    push bx
    push cx

    mov  ds, cs:[dgroup_seg]
    cli
    mov  bx, current_tcb
    and  [bx].TCB.tcb_flags, NOT TCB_FLG_IN_DOS

    ; Release whichever DOS stack this thread owned and wake waiters
    mov  al, current_tid
    cmp  disk_stack_owner, al
    jne  chk_rel_aux
    mov  disk_stack_owner, NO_THREAD
    mov  dl, STATE_WAIT_DISK
    call restore_prio_and_wake
    jmp  SHORT finish_int21_exit

chk_rel_aux:
    cmp  aux_stack_owner, al
    jne  chk_rel_io
    mov  aux_stack_owner, NO_THREAD
    mov  dl, STATE_WAIT_AUX
    call restore_prio_and_wake
    jmp  SHORT finish_int21_exit

chk_rel_io:
    cmp  io_stack_owner, al
    jne  finish_int21_exit

    ; Did we displace another thread's IOStack into io_swap_buf?
    mov  ah, io_swap_owner
    cmp  ah, NO_THREAD
    je   clear_io_owner

    ; Restore displaced thread's 400B IOStack slice & 24B SDA header
    push es
    call exchange_iostack_slice
    mov  bl, ah
    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table
    and  [bx].TCB.tcb_flags, NOT TCB_FLG_SWAPPED_IO

    push si
    push di
    mov  es, sda_seg
    mov  di, sda_off
    lea  si, [bx].TCB.sda_hdr
    mov  cx, (SDA_HEADER_SIZE / 2)
    cld
    rep  movsw
    pop  di
    pop  si
    pop  es

    mov  io_stack_owner, ah
    mov  io_swap_owner, NO_THREAD
    jmp  SHORT wake_io_waiters

clear_io_owner:
    mov  io_stack_owner, NO_THREAD
wake_io_waiters:
    mov  dl, STATE_WAIT_IO
    call restore_prio_and_wake

finish_int21_exit:
    ; Copy DOS return flags into caller's IRET frame on stack:
    ; Stack layout: [SP+0]=CX, [SP+2]=BX, [SP+4]=AX, [SP+6]=DS, [SP+8]=DOS_FLAGS,
    ;               [SP+10]=IP, [SP+12]=CS, [SP+14]=CALLER_FLAGS
    mov  bx, sp
    mov  ax, ss:[bx+8]              ; AX = flags returned by DOS
    and  ax, 0FD5h                  ; Mask status flags (CF,PF,AF,ZF,SF,OF,DF)
    and  WORD PTR ss:[bx+14], NOT 0FD5h
    or   ss:[bx+14], ax             ; Merge into caller's IRET FLAGS word

    pop  cx
    pop  bx
    pop  ax
    pop  ds
    add  sp, 2                      ; Discard saved DOS_FLAGS word
    iret

dos_pop_and_pass:
    pop  es
    pop  bx
    pop  ax
    pop  ds
dos_pass_through:
    jmp  DWORD PTR cs:[old_int21]
isr_int21 ENDP

;-----------------------------------------------------------------------------
; boost_and_block_on_dos - Priority inheritance & wait helper for INT 21h
; Entry: DS = DGROUP, BL = owner TID, DL = wait state (STATE_WAIT_*)
;        Stack has [SP+2]=ES, [SP+4]=BX, [SP+6]=AX, [SP+8]=DS
;-----------------------------------------------------------------------------
boost_and_block_on_dos PROC NEAR
    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table       ; BX = TCB of lock owner

    mov  si, current_tcb            ; SI = TCB of current (blocking) thread
    mov  al, [si].TCB.tcb_prio
    cmp  al, [bx].TCB.tcb_prio
    jle  no_prio_boost
    mov  [bx].TCB.tcb_prio, al      ; Boost owner to match waiter's priority
no_prio_boost:
    mov  [si].TCB.tcb_state, dl     ; Mark current thread waiting

    ; Save caller's original DS into scratch_ds for soft_context_switch
    push bp
    mov  bp, sp
    mov  ax, ss:[bp+10]             ; Original DS pushed at entry of isr_int21
    mov  cs:[scratch_ds], ax
    pop  bp

    pushf
    cli
    push cs
    call NEAR PTR soft_context_switch
    ret
boost_and_block_on_dos ENDP

;-----------------------------------------------------------------------------
; restore_prio_and_wake - Restore base priority & wake threads in state DL
;-----------------------------------------------------------------------------
restore_prio_and_wake PROC NEAR
    mov  bx, current_tcb
    mov  al, [bx].TCB.tcb_base_prio
    mov  [bx].TCB.tcb_prio, al

    mov  bx, OFFSET tcb_table
    mov  cx, MAX_THREADS
wake_dos_loop:
    cmp  [bx].TCB.tcb_state, dl
    jne  wake_dos_next
    mov  [bx].TCB.tcb_state, STATE_READY
wake_dos_next:
    add  bx, SIZEOF TCB
    loop wake_dos_loop
    ret
restore_prio_and_wake ENDP

;-----------------------------------------------------------------------------
; BIOS Interrupt Wrappers (Guard non-reentrant ROM BIOS routines via bios_lock)
;-----------------------------------------------------------------------------
DEFINE_BIOS_WRAPPER MACRO func_name, old_vec
func_name PROC FAR
    inc  cs:[bios_lock]
    pushf
    call DWORD PTR cs:[old_vec]
    pushf
    dec  cs:[bios_lock]
    popf
    retf 2                          ; Return preserving BIOS output flags!
func_name ENDP
ENDM

DEFINE_BIOS_WRAPPER isr_int10, old_int10
DEFINE_BIOS_WRAPPER isr_int13, old_int13
DEFINE_BIOS_WRAPPER isr_int15, old_int15
DEFINE_BIOS_WRAPPER isr_int17, old_int17

; INT 16h wrapper also performs one-time IOStack offset calibration at init
isr_int16 PROC FAR
    cmp  cs:[calib_iostack], 0
    je   normal_int16

    ; Record exact IOStack base offset inside SDA during AH=0Bh calibration call
    push ds
    push ax
    mov  ds, cs:[dgroup_seg]
    mov  ax, sp
    sub  ax, (IOSTACK_SIZE - 32)    ; Account for DOS CON entry stack frame
    mov  iostack_off, ax
    mov  cs:[calib_iostack], 0
    pop  ax
    pop  ds

normal_int16:
    inc  cs:[bios_lock]
    pushf
    call DWORD PTR cs:[old_int16]
    pushf
    dec  cs:[bios_lock]
    popf
    retf 2
isr_int16 ENDP

;=============================================================================
; PUBLIC C-CALLABLE API FUNCTIONS (compatible with small/medium/compact/large memory models)
;=============================================================================

;-----------------------------------------------------------------------------
; int MT_API thread_init(unsigned char pit_shift, unsigned char main_prio)
;-----------------------------------------------------------------------------
thread_init PROC PUBLIC USES si di ds, pit_shift: BYTE, main_prio: BYTE
    mov  ax, @data
    mov  ds, ax
    mov  cs:[dgroup_seg], ax

    ; 1. Discover DOS Swappable Data Area via INT 21h AX=5D06h
    push ds
    mov  ax, 5D06h
    int  21h                        ; Returns DS:SI = SDA pointer
    mov  ax, ds
    pop  ds
    mov  sda_seg, ax
    mov  sda_off, si

    ; Default fallback IOStack offset (SDA + 438h + 180h - 180h = SDA + 458h)
    add  si, 0458h
    mov  iostack_off, si

    ; 3. Clear TCB table and initialize Thread 0 (main C thread)
    push ds
    pop  es
    mov  di, OFFSET tcb_table
    mov  cx, (( SIZEOF TCB * MAX_THREADS ) / 2)
    xor  ax, ax
    cld
    rep  stosw

    mov  bx, OFFSET tcb_table
    mov  [bx].TCB.tcb_state, STATE_RUNNING
    mov  al, main_prio
    and  al, 07h
    mov  [bx].TCB.tcb_prio, al
    mov  [bx].TCB.tcb_base_prio, al
    mov  ax, STKHQQ                 ; Capture MSC 7.00 main stack lower bound
    mov  [bx].TCB.stk_limit, ax

    mov  current_tid, 0
    mov  current_tcb, OFFSET tcb_table
    mov  active_mask, 01h
    mov  join_all_mask, 00h

    mov  di, OFFSET join_wait_mask
    mov  cx, (MAX_THREADS / 2)
    xor  ax, ax
    rep  stosw

    ; 3. Save original interrupt vectors
    mov  ax, 3508h
    int  21h
    mov  WORD PTR cs:[old_int08], bx
    mov  WORD PTR cs:[old_int08+2], es

    mov  ax, 3510h
    int  21h
    mov  WORD PTR cs:[old_int10], bx
    mov  WORD PTR cs:[old_int10+2], es

    mov  ax, 3513h
    int  21h
    mov  WORD PTR cs:[old_int13], bx
    mov  WORD PTR cs:[old_int13+2], es

    mov  ax, 3515h
    int  21h
    mov  WORD PTR cs:[old_int15], bx
    mov  WORD PTR cs:[old_int15+2], es

    mov  ax, 3516h
    int  21h
    mov  WORD PTR cs:[old_int16], bx
    mov  WORD PTR cs:[old_int16+2], es

    mov  ax, 3517h
    int  21h
    mov  WORD PTR cs:[old_int17], bx
    mov  WORD PTR cs:[old_int17+2], es

    mov  ax, 3521h
    int  21h
    mov  WORD PTR cs:[old_int21], bx
    mov  WORD PTR cs:[old_int21+2], es

    ; 4. Install BIOS wrappers and calibrate IOStack offset via AH=0Bh
    push ds
    push cs
    pop  ds
    mov  dx, OFFSET isr_int10
    mov  ax, 2510h
    int  21h
    mov  dx, OFFSET isr_int13
    mov  ax, 2513h
    int  21h
    mov  dx, OFFSET isr_int15
    mov  ax, 2515h
    int  21h
    mov  dx, OFFSET isr_int16
    mov  ax, 2516h
    int  21h
    mov  dx, OFFSET isr_int17
    mov  ax, 2517h
    int  21h
    pop  ds

    mov  cs:[calib_iostack], 1
    mov  ah, 0Bh                    ; Triggers INT 16h on DOS IOStack
    int  21h
    mov  cs:[calib_iostack], 0

    ; 5. Configure 8253 PIT channel 0 rate and install INT 21h & INT 08h hooks
    mov  cl, pit_shift
    and  cl, 03h
    mov  pit_shift_val, cl
    mov  al, 1
    shl  al, cl
    mov  pit_sub_reload, al
    mov  pit_sub_count, al

    cli
    push ds
    push cs
    pop  ds
    mov  dx, OFFSET isr_int21
    mov  ax, 2521h
    int  21h
    mov  dx, OFFSET isr_timer
    mov  ax, 2508h
    int  21h
    pop  ds

    ; Reprogram PIT channel 0 divisor = (65536 SHR pit_shift)
    mov  al, 36h                    ; Channel 0, LSB+MSB, mode 3
    out  43h, al
    xor  ax, ax
    or   cl, cl
    jz   set_pit_div
    mov  ax, 8000h
    dec  cl
    shr  ax, cl
set_pit_div:
    out  40h, al
    mov  al, ah
    out  40h, al
    sti

    xor  ax, ax                     ; Return MT_ERR_NONE (0)
    ret
thread_init ENDP

;-----------------------------------------------------------------------------
; void MT_API thread_shutdown(void)
;-----------------------------------------------------------------------------
thread_shutdown PROC PUBLIC USES si di ds
    mov  ax, @data
    mov  ds, ax

    cli
    ; Restore standard 18.2 Hz PIT divisor (0000h)
    mov  al, 36h
    out  43h, al
    xor  al, al
    out  40h, al
    out  40h, al

    ; Restore original interrupt vectors
    push ds
    lds  dx, cs:[old_int08]
    mov  ax, 2508h
    int  21h
    lds  dx, cs:[old_int21]
    mov  ax, 2521h
    int  21h
    lds  dx, cs:[old_int10]
    mov  ax, 2510h
    int  21h
    lds  dx, cs:[old_int13]
    mov  ax, 2513h
    int  21h
    lds  dx, cs:[old_int15]
    mov  ax, 2515h
    int  21h
    lds  dx, cs:[old_int16]
    mov  ax, 2516h
    int  21h
    lds  dx, cs:[old_int17]
    mov  ax, 2517h
    int  21h
    pop  ds

    ; Restore main thread's STKHQQ watermark
    mov  bx, OFFSET tcb_table
    mov  ax, [bx].TCB.stk_limit
    mov  STKHQQ, ax
    sti

    ; Reclaim any heap-allocated stacks
    call reap_zombie_stacks
    ret
thread_shutdown ENDP

;-----------------------------------------------------------------------------
; reap_zombie_stacks - Lazily free() auto-allocated stacks of exited threads
;-----------------------------------------------------------------------------
reap_zombie_stacks PROC NEAR
    mov  si, OFFSET tcb_table
    mov  di, MAX_THREADS

reap_loop:
    pushf
    cli
    cmp  [si].TCB.tcb_state, STATE_ZOMBIE
    jne  reap_next_popf
    test [si].TCB.tcb_flags, TCB_FLG_OWN_STACK
    jz   reap_mark_unused

    ; Clear flag and latch pointer under CLI before calling C runtime free()
    and  [si].TCB.tcb_flags, NOT TCB_FLG_OWN_STACK
    mov  ax, [si].TCB.stk_alloc_off
    mov  dx, [si].TCB.stk_alloc_seg
    popf

    IF @DataSize
        push dx
        push ax
        call free
        add  sp, 4
    ELSE
        push ax
        call free
        add  sp, 2
    ENDIF

    pushf
    cli
reap_mark_unused:
    mov  [si].TCB.tcb_flags, 0
    mov  [si].TCB.tcb_state, STATE_UNUSED
reap_next_popf:
    popf
    add  si, SIZEOF TCB
    dec  di
    jnz  reap_loop
    ret
reap_zombie_stacks ENDP

;-----------------------------------------------------------------------------
; tid_t MT_API thread_create(thread_func_t func, void *arg,
;                            void *stack_ptr, unsigned int stack_size,
;                            unsigned char priority)
;-----------------------------------------------------------------------------
thread_create PROC PUBLIC USES si di ds,
    func:CODE_PTR, arg:DATA_PTR, stack_ptr:DATA_PTR,
    stack_size:WORD, priority:BYTE

    mov  ax, @data
    mov  ds, ax

    ; 1. Reclaim any exited STATE_ZOMBIE stacks first
    call reap_zombie_stacks

    ; 2. Find an unused TCB slot (1..7)
    mov  si, OFFSET tcb_table + SIZEOF TCB
    mov  cx, (MAX_THREADS - 1)
    mov  di, 1                      ; DI = candidate TID
find_slot_loop:
    cmp  [si].TCB.tcb_state, STATE_UNUSED
    je   found_slot
    add  si, SIZEOF TCB
    inc  di
    loop find_slot_loop

    mov  al, MT_ERR_NO_SLOTS
    jmp  create_exit

found_slot:
    ; 3. Resolve stack_size (0 -> DEFAULT_STACK_SIZE)
    mov  bx, stack_size
    or   bx, bx
    jnz  chk_min_stack
    mov  bx, DEFAULT_STACK_SIZE
chk_min_stack:
    cmp  bx, 128
    jae   resolve_stack_ptr
    mov  bx, 128
resolve_stack_ptr:
    mov  stack_size, bx

    ; 4. Resolve stack_ptr (NULL -> allocate via C runtime malloc)
    mov  [si].TCB.tcb_flags, 0
    IF @DataSize
        mov  ax, WORD PTR stack_ptr
        or   ax, WORD PTR stack_ptr+2
        jnz  have_caller_stack
        push stack_size
        call malloc
        add  sp, 2
        mov  cx, ax
        or   cx, dx
        jnz  save_far_alloc
        mov  al, MT_ERR_NO_MEMORY
        jmp  create_exit
    save_far_alloc:
        mov  WORD PTR stack_ptr, ax
        mov  WORD PTR stack_ptr+2, dx
        mov  [si].TCB.stk_alloc_off, ax
        mov  [si].TCB.stk_alloc_seg, dx
        mov  [si].TCB.tcb_flags, TCB_FLG_OWN_STACK
    have_caller_stack:
        mov  ax, WORD PTR stack_ptr
        mov  dx, WORD PTR stack_ptr+2
    ELSE
        mov  ax, stack_ptr
        or   ax, ax
        jnz  have_near_stack
        push stack_size
        call malloc
        add  sp, 2
        or   ax, ax
        jnz  save_near_alloc
        mov  al, MT_ERR_NO_MEMORY
        jmp  create_exit
    save_near_alloc:
        mov  stack_ptr, ax
        mov  [si].TCB.stk_alloc_off, ax
        mov  [si].TCB.stk_alloc_seg, ds
        mov  [si].TCB.tcb_flags, TCB_FLG_OWN_STACK
    have_near_stack:
        mov  dx, ds                 ; In near data models, SS == DS (DGROUP)
    ENDIF

    ; 5. Compute stack bottom guard (STKHQQ value) & aligned stack top
    ;    AX = base offset, DX = stack segment
    mov  cx, ax
    add  cx, STACK_GUARD_MARGIN
    mov  [si].TCB.stk_limit, cx

    add  ax, stack_size
    and  ax, 0FFFEh                 ; Word-align top of stack
    mov  bx, ax                     ; DX:BX = top of new thread's stack

    ; 6. Seed initial stack frame for func(arg) so returning enters thread_exit
    mov  es, dx
    IF @DataSize
        sub  bx, 4
        mov  ax, WORD PTR arg
        mov  es:[bx], ax
        mov  ax, WORD PTR arg+2
        mov  es:[bx+2], ax
    ELSE
        sub  bx, 2
        mov  ax, arg
        mov  es:[bx], ax
    ENDIF

    IF @CodeSize
        sub  bx, 4
        mov  WORD PTR es:[bx], OFFSET thread_exit
        mov  WORD PTR es:[bx+2], SEG thread_exit
    ELSE
        sub  bx, 2
        mov  WORD PTR es:[bx], OFFSET thread_exit
    ENDIF

    ; 7. Populate TCB registers & scheduler metadata
    cli
    mov  [si].TCB.reg_sp, bx
    mov  [si].TCB.reg_ss, dx
    mov  [si].TCB.reg_bp, bx
    mov  [si].TCB.reg_ds, ds
    mov  [si].TCB.reg_es, ds
    mov  [si].TCB.reg_flags, 0202h  ; IF = 1

    IF @CodeSize
        mov  ax, WORD PTR func
        mov  [si].TCB.reg_ip, ax
        mov  ax, WORD PTR func+2
        mov  [si].TCB.reg_cs, ax
    ELSE
        mov  ax, func
        mov  [si].TCB.reg_ip, ax
        mov  [si].TCB.reg_cs, cs
    ENDIF

    mov  al, priority
    and  al, 07h
    mov  [si].TCB.tcb_prio, al
    mov  [si].TCB.tcb_base_prio, al
    mov  [si].TCB.sleep_ticks, 0
    mov  [si].TCB.tcb_state, STATE_READY

    mov  al, tid_bit_lut[di]
    or   active_mask, al
    mov  join_wait_mask[di], 0
    sti

    mov  ax, di                     ; Return new TID (1..7) in AL
create_exit:
    xor  ah, ah
    ret
thread_create ENDP

;-----------------------------------------------------------------------------
; void MT_API thread_yield(void)
;-----------------------------------------------------------------------------
thread_yield PROC PUBLIC
    mov  cs:[scratch_ds], ds
    mov  ds, cs:[dgroup_seg]
    pushf
    cli
    mov  bx, current_tcb
    mov  [bx].TCB.tcb_state, STATE_READY
    push cs
    call NEAR PTR soft_context_switch
    ret
thread_yield ENDP

;-----------------------------------------------------------------------------
; void MT_API thread_sleep(unsigned int ticks)
;-----------------------------------------------------------------------------
thread_sleep PROC PUBLIC, ticks: WORD
    mov  ax, ticks
    or   ax, ax
    jz   sleep_yield_only

    mov  cs:[scratch_ds], ds
    mov  ds, cs:[dgroup_seg]
    pushf
    cli
    mov  bx, current_tcb
    mov  [bx].TCB.sleep_ticks, ax
    mov  [bx].TCB.tcb_state, STATE_SLEEPING
    push cs
    call NEAR PTR soft_context_switch
    ret

sleep_yield_only:
    call thread_yield
    ret
thread_sleep ENDP

;-----------------------------------------------------------------------------
; void MT_API thread_exit(void)
;-----------------------------------------------------------------------------
thread_exit PROC PUBLIC
    mov  ds, cs:[dgroup_seg]
    cli

    ; 1. Clear exiting thread's bit from active_mask
    mov  bl, current_tid
    xor  bh, bh
    mov  al, tid_bit_lut[bx]
    not  al
    and  active_mask, al

    ; 2. Wake any threads waiting in thread_join(current_tid)
    xor  al, al
    xchg al, join_wait_mask[bx]
    or   al, al
    jz   chk_join_all_wake
    call wake_all_waiters

chk_join_all_wake:
    ; 3. If threads are waiting in thread_join_all() and no other active
    ;    threads remain ((active_mask & ~join_all_mask) == 0), wake them!
    mov  al, join_all_mask
    or   al, al
    jz   mark_exit_state
    mov  ah, al
    not  ah
    test active_mask, ah
    jnz  mark_exit_state

    xor  al, al
    xchg al, join_all_mask
    call wake_all_waiters

mark_exit_state:
    ; 4. Mark TCB slot STATE_ZOMBIE (if malloc stack) or STATE_UNUSED
    mov  bx, current_tcb
    test [bx].TCB.tcb_flags, TCB_FLG_OWN_STACK
    jnz  mark_zombie
    mov  [bx].TCB.tcb_state, STATE_UNUSED
    jmp  SHORT exit_switch
mark_zombie:
    mov  [bx].TCB.tcb_state, STATE_ZOMBIE

exit_switch:
    mov  ax, ds
    mov  ss, ax
    mov  sp, OFFSET isr_stack_top
    jmp  schedule_next_thread
thread_exit ENDP

;-----------------------------------------------------------------------------
; int MT_API thread_join(tid_t tid)
; Blocks until thread `tid` exits and reclaims its stack if auto-allocated.
;-----------------------------------------------------------------------------
thread_join PROC PUBLIC USES si di ds, tid: BYTE
    mov  dx, ds                     ; Save caller's DS in DX
    mov  ds, cs:[dgroup_seg]

    mov  bl, tid
    cmp  bl, MAX_THREADS
    jae  join_err_invalid
    cmp  bl, current_tid
    je   join_err_invalid           ; Cannot join self

    xor  bh, bh
    mov  di, bx                     ; DI = target tid (0..7)
    mov  ah, tid_bit_lut[di]        ; AH = (1 << tid)

    pushf
    cli
    test active_mask, ah            ; Is target thread still running?
    jz   join_already_exited

    ; Add current_tid to join_wait_mask[tid] and block
    mov  bl, current_tid
    xor  bh, bh
    mov  al, tid_bit_lut[bx]
    or   join_wait_mask[di], al
    mov  ax, dx
    call block_on_sync

join_already_exited:
    popf
    call reap_zombie_stacks
    xor  ax, ax                     ; Return MT_ERR_NONE (0)
    ret

join_err_invalid:
    mov  ax, MT_ERR_INVALID
    ret
thread_join ENDP

;-----------------------------------------------------------------------------
; void MT_API thread_join_all(void)
; Blocks until all other active threads have exited, then reaps zombie stacks.
;-----------------------------------------------------------------------------
thread_join_all PROC PUBLIC USES si di ds
    mov  dx, ds                     ; Save caller's DS in DX
    mov  ds, cs:[dgroup_seg]

    mov  bl, current_tid
    xor  bh, bh
    mov  al, tid_bit_lut[bx]        ; AL = (1 << current_tid)
    mov  ah, al
    not  ah                         ; AH = ~(1 << current_tid)

    pushf
    cli
    test active_mask, ah            ; Are any OTHER threads still active?
    jz   join_all_already_done

    or   join_all_mask, al          ; Register current_tid in join_all_mask
    mov  ax, dx
    call block_on_sync

join_all_already_done:
    popf
    call reap_zombie_stacks
    ret
thread_join_all ENDP

;-----------------------------------------------------------------------------
; tid_t MT_API thread_self(void)
;-----------------------------------------------------------------------------
thread_self PROC PUBLIC
    push ds
    mov  ds, cs:[dgroup_seg]
    mov  al, current_tid
    xor  ah, ah
    pop  ds
    ret
thread_self ENDP

;-----------------------------------------------------------------------------
; unsigned char MT_API thread_get_priority(tid_t tid)
;-----------------------------------------------------------------------------
thread_get_priority PROC PUBLIC, tid: BYTE
    push ds
    mov  ds, cs:[dgroup_seg]

    mov  bl, tid
    cmp  bl, MAX_THREADS
    jae  get_prio_err

    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table

    cmp  [bx].TCB.tcb_state, STATE_UNUSED
    je   get_prio_err

    mov  al, [bx].TCB.tcb_prio
    xor  ah, ah
    pop  ds
    ret

get_prio_err:
    mov  ax, MT_ERR_INVALID
    pop  ds
    ret
thread_get_priority ENDP

;-----------------------------------------------------------------------------
; int MT_API thread_set_priority(tid_t tid, unsigned char new_prio)
;-----------------------------------------------------------------------------
thread_set_priority PROC PUBLIC, tid: BYTE, new_prio: BYTE
    push ds
    mov  ds, cs:[dgroup_seg]

    mov  bl, tid
    cmp  bl, MAX_THREADS
    jae  set_prio_err

    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table

    cmp  [bx].TCB.tcb_state, STATE_UNUSED
    je   set_prio_err

    mov  al, new_prio
    and  al, 07h
    cli
    mov  [bx].TCB.tcb_base_prio, al
    mov  [bx].TCB.tcb_prio, al
    sti
    xor  ax, ax
    pop  ds
    ret

set_prio_err:
    mov  ax, MT_ERR_INVALID
    pop  ds
    ret
thread_set_priority ENDP

;-----------------------------------------------------------------------------
; wake_highest_waiter - Finds & wakes the highest-priority thread in a bitmask
; Entry: DS = DGROUP, AL = wait_mask (must be non-zero), IF = 0
; Exit:  DL = woken TID (0..7)
;        SI = woken TCB pointer
;        DH = woken thread's priority (tcb_prio)
;        AL = bitmask with woken thread's bit cleared (updated wait_mask)
;-----------------------------------------------------------------------------
wake_highest_waiter PROC NEAR
    push bx
    push cx

    mov  cl, al                     ; CL = original wait_mask
    mov  al, current_tid
    inc  al
    and  al, (MAX_THREADS - 1)      ; Start scan at (current_tid + 1) & 7

    mov  dl, NO_THREAD              ; DL = winning TID
    mov  dh, -1                     ; DH = winning priority
    mov  ch, MAX_THREADS            ; CH = loop count (8)

wake_scan_loop:
    mov  bl, al
    xor  bh, bh
    mov  ah, tid_bit_lut[bx]        ; AH = (1 << candidate_tid)
    test cl, ah
    jz   wake_scan_next

    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table
    mov  ah, [bx].TCB.tcb_prio
    cmp  ah, dh
    jle  wake_scan_next             ; '>' preserves round-robin within priority

    mov  dh, ah                     ; New best priority
    mov  dl, al                     ; New best TID
    mov  si, bx                     ; New best TCB pointer

wake_scan_next:
    inc  al
    and  al, (MAX_THREADS - 1)
    dec  ch
    jnz  wake_scan_loop

    ; Wake the winning thread and clear its bit from CL
    mov  [si].TCB.tcb_state, STATE_READY
    mov  bl, dl
    xor  bh, bh
    mov  al, tid_bit_lut[bx]
    not  al
    and  al, cl                     ; AL = updated wait_mask

    pop  cx
    pop  bx
    ret
wake_highest_waiter ENDP

;-----------------------------------------------------------------------------
; wake_all_waiters - Wakes all threads present in a wait_mask
; Entry: DS = DGROUP, AL = wait_mask (non-zero), IF = 0
; Exit:  DH = highest priority among all woken threads
;-----------------------------------------------------------------------------
wake_all_waiters PROC NEAR
    push bx
    push cx

    mov  cl, al                     ; CL = wait_mask
    mov  dh, -1                     ; DH = max priority woken
    mov  bx, OFFSET tcb_table
    mov  ch, 1                      ; CH = rolling bitmask (01h..80h)

wake_all_loop:
    test cl, ch
    jz   wake_all_next
    mov  [bx].TCB.tcb_state, STATE_READY
    mov  al, [bx].TCB.tcb_prio
    cmp  al, dh
    jle  wake_all_next
    mov  dh, al
wake_all_next:
    add  bx, SIZEOF TCB
    shl  ch, 1
    jnc  wake_all_loop

    pop  cx
    pop  bx
    ret
wake_all_waiters ENDP

;-----------------------------------------------------------------------------
; block_on_sync - Blocks current thread in STATE_WAIT_SYNC & switches context
; Entry: DS = DGROUP, caller's original DS in AX, IF = 0
;-----------------------------------------------------------------------------
block_on_sync PROC NEAR
    mov  cs:[scratch_ds], ax
    mov  bx, current_tcb
    mov  [bx].TCB.tcb_state, STATE_WAIT_SYNC
    pushf
    cli
    push cs
    call NEAR PTR soft_context_switch
    ret
block_on_sync ENDP

;-----------------------------------------------------------------------------
; preempt_if_higher - Yields immediately if DH > current thread's priority
; Entry: DS = DGROUP, DH = woken thread's priority, caller's DS in AX, IF = 0
;-----------------------------------------------------------------------------
preempt_if_higher PROC NEAR
    mov  bx, current_tcb
    cmp  dh, [bx].TCB.tcb_prio
    jle  no_preempt_needed

    mov  cs:[scratch_ds], ax
    mov  [bx].TCB.tcb_state, STATE_READY
    pushf
    cli
    push cs
    call NEAR PTR soft_context_switch

no_preempt_needed:
    ret
preempt_if_higher ENDP

;=============================================================================
; 1. WIN32 EVENT OBJECT API (event_init, event_set, event_reset, event_pulse, event_wait)
;=============================================================================

thread_event_init LABEL NEAR
event_init PROC PUBLIC USES di, ev: DATA_PTR, manual_reset: BYTE, initial_state: BYTE
    LOAD_DATA_PTR di, ev
    xor  al, al
    cmp  manual_reset, 0
    je   ev_set_man
    mov  al, 1
ev_set_man:
    mov  es:[di].MT_EVENT.ev_manual, al

    xor  al, al
    cmp  initial_state, 0
    je   ev_set_sig
    mov  al, 1
ev_set_sig:
    mov  es:[di].MT_EVENT.ev_signaled, al
    mov  es:[di].MT_EVENT.ev_wait_mask, 0
    mov  es:[di].MT_EVENT.ev_reserved, 0
    ret
event_init ENDP

event_set PROC PUBLIC USES si di ds, ev: DATA_PTR
    mov  dx, ds                     ; Save caller's DS in DX
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, ev

    pushf
    cli
    mov  al, es:[di].MT_EVENT.ev_wait_mask
    or   al, al
    jz   ev_no_waiters

    cmp  es:[di].MT_EVENT.ev_manual, 0
    jne  ev_set_wake_all

    ; Auto-reset event with waiters: wake highest-priority waiter directly,
    ; leave ev_signaled = 0 (direct signal handoff)
    call wake_highest_waiter
    mov  es:[di].MT_EVENT.ev_wait_mask, al
    mov  ax, dx
    call preempt_if_higher
    popf
    ret

ev_set_wake_all:
    ; Manual-reset event: set signaled = 1 and wake ALL waiting threads
    mov  es:[di].MT_EVENT.ev_signaled, 1
    mov  es:[di].MT_EVENT.ev_wait_mask, 0
    call wake_all_waiters
    mov  ax, dx
    call preempt_if_higher
    popf
    ret

ev_no_waiters:
    mov  es:[di].MT_EVENT.ev_signaled, 1
    popf
    ret
event_set ENDP

event_reset PROC PUBLIC USES di, ev: DATA_PTR
    LOAD_DATA_PTR di, ev
    mov  es:[di].MT_EVENT.ev_signaled, 0
    ret
event_reset ENDP

event_pulse PROC PUBLIC USES si di ds, ev: DATA_PTR
    mov  dx, ds
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, ev

    pushf
    cli
    mov  es:[di].MT_EVENT.ev_signaled, 0
    mov  al, es:[di].MT_EVENT.ev_wait_mask
    or   al, al
    jz   ev_pulse_done

    cmp  es:[di].MT_EVENT.ev_manual, 0
    jne  ev_pulse_all

    call wake_highest_waiter
    mov  es:[di].MT_EVENT.ev_wait_mask, al
    mov  ax, dx
    call preempt_if_higher
    popf
    ret

ev_pulse_all:
    mov  es:[di].MT_EVENT.ev_wait_mask, 0
    call wake_all_waiters
    mov  ax, dx
    call preempt_if_higher

ev_pulse_done:
    popf
    ret
event_pulse ENDP

event_wait PROC PUBLIC USES di ds, ev: DATA_PTR
    mov  dx, ds                     ; Save caller's DS in DX
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, ev

    pushf
    cli
    cmp  es:[di].MT_EVENT.ev_signaled, 0
    je   ev_must_block

    ; Event is already signaled! If auto-reset, consume the signal
    cmp  es:[di].MT_EVENT.ev_manual, 0
    jne  ev_wait_done
    mov  es:[di].MT_EVENT.ev_signaled, 0
ev_wait_done:
    popf
    ret

ev_must_block:
    ; Add current_tid bit to ev_wait_mask and sleep (direct handoff on wake)
    mov  bl, current_tid
    xor  bh, bh
    mov  al, tid_bit_lut[bx]
    or   es:[di].MT_EVENT.ev_wait_mask, al
    mov  ax, dx
    call block_on_sync
    popf
    ret
event_wait ENDP

;=============================================================================
; 2. RECURSIVE MUTEX API WITH PRIORITY INHERITANCE (mutex_init/lock/trylock/unlock)
;=============================================================================

mutex_init PROC PUBLIC USES di, mtx: DATA_PTR
    LOAD_DATA_PTR di, mtx
    mov  es:[di].MT_MUTEX.mtx_owner, NO_THREAD
    mov  es:[di].MT_MUTEX.mtx_count, 0
    mov  es:[di].MT_MUTEX.mtx_wait_mask, 0
    mov  es:[di].MT_MUTEX.mtx_reserved, 0
    ret
mutex_init ENDP

mutex_lock PROC PUBLIC USES si di ds, mtx: DATA_PTR
    mov  dx, ds
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, mtx

    pushf
    cli
    mov  al, current_tid
    mov  bl, es:[di].MT_MUTEX.mtx_owner
    cmp  bl, NO_THREAD
    je   mtx_claim_free
    cmp  bl, al
    je   mtx_recursive_inc

    ; Mutex is owned by another thread (BL = owner TID).
    ; Apply Priority Inheritance if current thread outranks owner!
    xor  bh, bh
    SHL_IMM bx, 6
    add  bx, OFFSET tcb_table       ; BX = owner's TCB
    mov  si, current_tcb            ; SI = current thread's TCB
    mov  ah, [si].TCB.tcb_prio
    cmp  ah, [bx].TCB.tcb_prio
    jle  mtx_add_waiter
    mov  [bx].TCB.tcb_prio, ah      ; Boost owner's effective priority

mtx_add_waiter:
    mov  bl, al
    xor  bh, bh
    mov  al, tid_bit_lut[bx]
    or   es:[di].MT_MUTEX.mtx_wait_mask, al

    ; Block until mutex_unlock transfers ownership directly to us
    mov  ax, dx
    call block_on_sync
    popf
    ret

mtx_claim_free:
    mov  es:[di].MT_MUTEX.mtx_owner, al
    mov  es:[di].MT_MUTEX.mtx_count, 1
    popf
    ret

mtx_recursive_inc:
    inc  es:[di].MT_MUTEX.mtx_count
    popf
    ret
mutex_lock ENDP

mutex_trylock PROC PUBLIC USES di ds, mtx: DATA_PTR
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, mtx

    pushf
    cli
    mov  al, current_tid
    mov  bl, es:[di].MT_MUTEX.mtx_owner
    cmp  bl, NO_THREAD
    je   mtx_try_claim
    cmp  bl, al
    je   mtx_try_rec

    ; Busy -> return 0
    xor  ax, ax
    popf
    ret

mtx_try_claim:
    mov  es:[di].MT_MUTEX.mtx_owner, al
    mov  es:[di].MT_MUTEX.mtx_count, 1
    mov  ax, 1
    popf
    ret

mtx_try_rec:
    inc  es:[di].MT_MUTEX.mtx_count
    mov  ax, 1
    popf
    ret
mutex_trylock ENDP

mutex_unlock PROC PUBLIC USES si di ds, mtx: DATA_PTR
    mov  cx, ds                     ; Save caller's DS in CX
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, mtx

    pushf
    cli
    mov  al, current_tid
    cmp  es:[di].MT_MUTEX.mtx_owner, al
    jne  mtx_unlock_done            ; Ignore if not owner

    dec  es:[di].MT_MUTEX.mtx_count
    jnz  mtx_unlock_done            ; Still recursively held

    ; Restore current thread's base priority (undo any priority inheritance)
    mov  bx, current_tcb
    mov  al, [bx].TCB.tcb_base_prio
    mov  [bx].TCB.tcb_prio, al

    ; Check if any threads are waiting to acquire this mutex
    mov  al, es:[di].MT_MUTEX.mtx_wait_mask
    or   al, al
    jz   mtx_set_unowned

    ; Direct Ownership Handoff to the highest-priority waiting thread!
    call wake_highest_waiter
    mov  es:[di].MT_MUTEX.mtx_wait_mask, al
    mov  es:[di].MT_MUTEX.mtx_owner, dl
    mov  es:[di].MT_MUTEX.mtx_count, 1

    mov  ax, cx
    call preempt_if_higher
    popf
    ret

mtx_set_unowned:
    mov  es:[di].MT_MUTEX.mtx_owner, NO_THREAD
mtx_unlock_done:
    popf
    ret
mutex_unlock ENDP

;=============================================================================
; 3. COUNTING SEMAPHORE API (sem_init, sem_wait, sem_trywait, sem_post)
;=============================================================================

sem_init PROC PUBLIC USES di, sem: DATA_PTR, initial_count: WORD
    LOAD_DATA_PTR di, sem
    mov  ax, initial_count
    mov  es:[di].MT_SEM.sem_count, ax
    mov  es:[di].MT_SEM.sem_wait_mask, 0
    mov  es:[di].MT_SEM.sem_reserved, 0
    ret
sem_init ENDP

sem_wait PROC PUBLIC USES di ds, sem: DATA_PTR
    mov  dx, ds
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, sem

    pushf
    cli
    cmp  es:[di].MT_SEM.sem_count, 0
    je   sem_must_block

    dec  es:[di].MT_SEM.sem_count
    popf
    ret

sem_must_block:
    mov  bl, current_tid
    xor  bh, bh
    mov  al, tid_bit_lut[bx]
    or   es:[di].MT_SEM.sem_wait_mask, al
    mov  ax, dx
    call block_on_sync
    popf
    ret
sem_wait ENDP

sem_trywait PROC PUBLIC USES di, sem: DATA_PTR
    LOAD_DATA_PTR di, sem
    pushf
    cli
    cmp  es:[di].MT_SEM.sem_count, 0
    je   sem_try_fail
    dec  es:[di].MT_SEM.sem_count
    mov  ax, 1
    popf
    ret
sem_try_fail:
    xor  ax, ax
    popf
    ret
sem_trywait ENDP

sem_post PROC PUBLIC USES si di ds, sem: DATA_PTR
    mov  cx, ds
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, sem

    pushf
    cli
    mov  al, es:[di].MT_SEM.sem_wait_mask
    or   al, al
    jz   sem_inc_count

    ; Direct token handoff to highest-priority waiting thread
    call wake_highest_waiter
    mov  es:[di].MT_SEM.sem_wait_mask, al
    mov  ax, cx
    call preempt_if_higher
    popf
    ret

sem_inc_count:
    inc  es:[di].MT_SEM.sem_count
    popf
    ret
sem_post ENDP

;=============================================================================
; 4. CONDITION VARIABLE API (cond_init, cond_wait, cond_signal, cond_broadcast)
;=============================================================================

cond_init PROC PUBLIC USES di, cv: DATA_PTR
    LOAD_DATA_PTR di, cv
    mov  es:[di].MT_COND.cv_wait_mask, 0
    mov  es:[di].MT_COND.cv_reserved, 0
    ret
cond_init ENDP

cond_wait PROC PUBLIC USES si di ds, cv: DATA_PTR, mtx: DATA_PTR
    LOCAL saved_count:BYTE, caller_ds:WORD

    mov  ax, ds
    mov  caller_ds, ax
    mov  ds, cs:[dgroup_seg]

    pushf
    cli

    ; 1. Add current thread to Condition Variable's cv_wait_mask
    LOAD_DATA_PTR di, cv
    mov  bl, current_tid
    xor  bh, bh
    mov  al, tid_bit_lut[bx]
    or   es:[di].MT_COND.cv_wait_mask, al

    ; 2. Save mutex's recursive lock_count and completely release the mutex
    LOAD_DATA_PTR di, mtx
    mov  al, es:[di].MT_MUTEX.mtx_count
    mov  saved_count, al
    mov  es:[di].MT_MUTEX.mtx_count, 1

    ; Restore current thread's base priority & wake next mutex waiter if any
    mov  bx, current_tcb
    mov  al, [bx].TCB.tcb_base_prio
    mov  [bx].TCB.tcb_prio, al

    mov  al, es:[di].MT_MUTEX.mtx_wait_mask
    or   al, al
    jz   cv_mtx_unowned

    call wake_highest_waiter
    mov  es:[di].MT_MUTEX.mtx_wait_mask, al
    mov  es:[di].MT_MUTEX.mtx_owner, dl
    mov  es:[di].MT_MUTEX.mtx_count, 1
    jmp  SHORT cv_enter_sleep

cv_mtx_unowned:
    mov  es:[di].MT_MUTEX.mtx_owner, NO_THREAD

cv_enter_sleep:
    ; 3. Atomically enter STATE_WAIT_SYNC and switch contexts
    mov  ax, caller_ds
    call block_on_sync
    popf

    ; 4. Woken up by cond_signal / cond_broadcast! Re-acquire the mutex
    IF @DataSize
        push WORD PTR mtx+2
        push WORD PTR mtx
        call mutex_lock
        add  sp, 4
    ELSE
        push mtx
        call mutex_lock
        add  sp, 2
    ENDIF

    ; Restore original recursive lock depth
    LOAD_DATA_PTR di, mtx
    mov  al, saved_count
    mov  es:[di].MT_MUTEX.mtx_count, al
    ret
cond_wait ENDP

cond_signal PROC PUBLIC USES si di ds, cv: DATA_PTR
    mov  cx, ds
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, cv

    pushf
    cli
    mov  al, es:[di].MT_COND.cv_wait_mask
    or   al, al
    jz   cv_sig_done

    call wake_highest_waiter
    mov  es:[di].MT_COND.cv_wait_mask, al
    mov  ax, cx
    call preempt_if_higher

cv_sig_done:
    popf
    ret
cond_signal ENDP

cond_broadcast PROC PUBLIC USES si di ds, cv: DATA_PTR
    mov  cx, ds
    mov  ds, cs:[dgroup_seg]
    LOAD_DATA_PTR di, cv

    pushf
    cli
    mov  al, es:[di].MT_COND.cv_wait_mask
    or   al, al
    jz   cv_bcast_done

    mov  es:[di].MT_COND.cv_wait_mask, 0
    call wake_all_waiters
    mov  ax, cx
    call preempt_if_higher

cv_bcast_done:
    popf
    ret
cond_broadcast ENDP

END
