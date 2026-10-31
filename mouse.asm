;=============================================================================
; MOUSE.ASM - Interrupt-Driven Mouse Handler (INT 33h) for MTHREAD
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

EXTERNDEF C event_set_isr: PROC
EXTERNDEF C mt_enter_isr:  PROC
EXTERNDEF C mt_leave_isr:  PROC

MOUSE_QUEUE_SIZE EQU 16             ; Must be power of 2
MOUSE_QUEUE_MASK EQU (MOUSE_QUEUE_SIZE - 1)
MOUSE_EVT_MOVE   EQU 0001h

MOUSE_EVENT STRUC
    mev_flags       DW  ?           ; +00h: MOUSE_EVT_* bitmask
    mev_buttons     DB  ?           ; +02h: MOUSE_BTN_* bitmask
    mev_row         DB  ?           ; +03h: 0-based text row (DX / 8)
    mev_col         DB  ?           ; +04h: 0-based text col (CX / 8)
    mev_reserved    DB  ?           ; +05h: Alignment pad
    mev_raw_x       DW  ?           ; +06h: Virtual pixel X (CX)
    mev_raw_y       DW  ?           ; +08h: Virtual pixel Y (DX)
MOUSE_EVENT ENDS                    ; 10 bytes per entry

;=============================================================================
; DATA SEGMENT (DGROUP)
;=============================================================================

.DATA

ALIGN 2
mouse_queue     MOUSE_EVENT MOUSE_QUEUE_SIZE DUP (<>)
mouse_live      MOUSE_EVENT <>

mouse_ev_off    DW  0
mouse_ev_seg    DW  0
mouse_q_head    DB  0               ; Next write index (0..15)
mouse_q_tail    DB  0               ; Next read index (0..15)
mouse_q_count   DB  0               ; Queued events (0..16)
mouse_active    DB  0

;=============================================================================
; CODE SEGMENT
;=============================================================================

.CODE

mouse_dgroup    DW  0

;-----------------------------------------------------------------------------
; mouse_callback - Invoked via FAR CALL by DOS Mouse Driver at IRQ time
; Entry: AX = event mask, BX = button state, CX = virtual X, DX = virtual Y
;-----------------------------------------------------------------------------
mouse_callback PROC FAR
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es

    call mt_enter_isr

    mov  ds, cs:[mouse_dgroup]
    pushf
    cli

    ; 1. Update live mouse state snapshot
    mov  mouse_live.mev_flags, ax
    mov  mouse_live.mev_buttons, bl
    mov  mouse_live.mev_raw_x, cx
    mov  mouse_live.mev_raw_y, dx

    mov  si, cx
    SHR_IMM si, 3                   ; col = CX / 8
    mov  di, dx
    SHR_IMM di, 3                   ; row = DX / 8
    mov   ax, si
    mov  mouse_live.mev_col, al
    mov  ax, di
    mov  mouse_live.mev_row, al
    mov  mouse_live.mev_reserved, 0

    mov  ax, mouse_live.mev_flags

    ; 2. Coalesce consecutive pure MOUSE_EVT_MOVE events at the queue tail
    cmp  ax, MOUSE_EVT_MOVE
    jne   check_queue_full
    cmp  mouse_q_count, 0
    je   alloc_queue_slot

    ; Check if the most recently written slot (head - 1) was also pure MOVE
    mov  bl, mouse_q_head
    dec  bl
    and  bl, MOUSE_QUEUE_MASK
    xor  bh, bh
    mov  si, bx
    SHL_IMM si, 1                   ; SI = index * 2
    SHL_IMM bx, 3                   ; BX = index * 8
    add  bx, si                     ; BX = index * 10 (SIZEOF MOUSE_EVENT)
    add  bx, OFFSET mouse_queue

    cmp  [bx].MOUSE_EVENT.mev_flags, MOUSE_EVT_MOVE
    je   write_slot_data            ; Overwrite previous unconsumed move event!

check_queue_full:
    cmp  mouse_q_count, MOUSE_QUEUE_SIZE
    jae  mouse_wake_event           ; Queue full: keep live state, skip enqueue

alloc_queue_slot:
    mov  bl, mouse_q_head
    mov  al, bl
    inc  al
    and  al, MOUSE_QUEUE_MASK
    mov  mouse_q_head, al
    inc  mouse_q_count

    xor  bh, bh
    mov  si, bx
    SHL_IMM si, 1
    SHL_IMM bx, 3
    add  bx, si
    add  bx, OFFSET mouse_queue

write_slot_data:
    mov  ax, mouse_live.mev_flags
    mov  [bx].MOUSE_EVENT.mev_flags, ax
    mov  al, mouse_live.mev_buttons
    mov  [bx].MOUSE_EVENT.mev_buttons, al
    mov  al, mouse_live.mev_row
    mov  [bx].MOUSE_EVENT.mev_row, al
    mov  al, mouse_live.mev_col
    mov  [bx].MOUSE_EVENT.mev_col, al
    mov  [bx].MOUSE_EVENT.mev_reserved, 0
    mov  ax, mouse_live.mev_raw_x
    mov  [bx].MOUSE_EVENT.mev_raw_x, ax
    mov  ax, mouse_live.mev_raw_y
    mov  [bx].MOUSE_EVENT.mev_raw_y, ax

mouse_wake_event:
    ; 3. Signal registered mt_event_t via event_set_isr()
    mov  ax, mouse_ev_off
    mov  dx, mouse_ev_seg
    mov  cx, ax
    or   cx, dx
    jz   mouse_cb_done

    IF @DataSize
        push dx
        push ax
        call event_set_isr
        add  sp, 4
    ELSE
        push ax
        call event_set_isr
        add  sp, 2
    ENDIF

mouse_cb_done:
    popf
    call mt_leave_isr

    pop  es
    pop  ds
    pop  di
    pop  si
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    retf
mouse_callback ENDP

;-----------------------------------------------------------------------------
; int MT_API mouse_init(mt_event_t *notify_ev, unsigned int event_mask)
; Returns button count (2 or 3) if initialized, or 0 if no mouse driver.
;-----------------------------------------------------------------------------
mouse_init PROC PUBLIC USES si di ds, notify_ev: DATA_PTR, event_mask: WORD
    mov  ax, @data
    mov  ds, ax
    mov  cs:[mouse_dgroup], ax

    ; 1. Verify INT 33h vector is non-null and not pointing to a bare IRET
    mov  ax, 3533h
    int  21h
    mov  ax, es
    or   ax, bx
    jz   no_mouse_driver
    cmp  BYTE PTR es:[bx], 0CFh     ; 0CFh = IRET
    je   no_mouse_driver

    ; 2. Reset mouse driver and query hardware status (INT 33h, AX=0000h)
    xor  ax, ax
    int  33h
    or   ax, ax
    jz   no_mouse_driver

    ; Preserve button count from BX (normalize 0FFFFh/0 -> 2)
    mov  si, bx
    cmp  si, 2
    jb   default_two_btns
    cmp  si, 3
    jbe  save_notify_ptr
default_two_btns:
    mov  si, 2

save_notify_ptr:
    IF @DataSize
        mov  ax, WORD PTR notify_ev
        mov  dx, WORD PTR notify_ev+2
    ELSE
        mov  ax, notify_ev
        mov  dx, ds
    ENDIF
    mov  mouse_ev_off, ax
    mov  mouse_ev_seg, dx
    mov  mouse_q_head, 0
    mov  mouse_q_tail, 0
    mov  mouse_q_count, 0

    ; 3. Query initial position and button state (INT 33h, AX=0003h)
    mov  ax, 0003h
    int  33h
    mov  mouse_live.mev_flags, 0
    mov  mouse_live.mev_buttons, bl
    mov  mouse_live.mev_raw_x, cx
    mov  mouse_live.mev_raw_y, dx
    SHR_IMM cx, 3
    SHR_IMM dx, 3
    mov  mouse_live.mev_col, cl
    mov  mouse_live.mev_row, dl
    mov  mouse_live.mev_reserved, 0

    ; 4. Register mouse_callback via INT 33h, AX=000Ch
    mov  cx, event_mask
    or   cx, cx
    jnz  install_cb
    mov  cx, 007Fh                  ; Default to MOUSE_EVT_ALL
install_cb:
    mov  mouse_active, 1
    push es
    push cs
    pop  es
    mov  dx, OFFSET mouse_callback
    mov  ax, 000Ch
    int  33h
    pop  es

    mov  ax, si                     ; Return button count
    ret

no_mouse_driver:
    xor  ax, ax
    ret
mouse_init ENDP

;-----------------------------------------------------------------------------
; void MT_API mouse_shutdown(void)
;-----------------------------------------------------------------------------
mouse_shutdown PROC PUBLIC USES ds
    mov  ax, @data
    mov  ds, ax

    cmp  mouse_active, 0
    je   mouse_shut_done
    mov  mouse_active, 0

    ; Hide cursor
    mov  ax, 0002h
    int  33h

    ; Disable callback (CX=0, ES:DX=0000:0000)
    push es
    xor  ax, ax
    mov  es, ax
    xor  cx, cx
    xor  dx, dx
    mov  ax, 000Ch
    int  33h
    pop  es

    mov  mouse_ev_off, 0
    mov  mouse_ev_seg, 0
    mov  mouse_q_head, 0
    mov  mouse_q_tail, 0
    mov  mouse_q_count, 0

mouse_shut_done:
    ret
mouse_shutdown ENDP

;-----------------------------------------------------------------------------
; void MT_API mouse_show(void)
;-----------------------------------------------------------------------------
mouse_show PROC PUBLIC USES ds
    mov  ax, @data
    mov  ds, ax
    cmp  mouse_active, 0
    je   skip_show
    mov  ax, 0001h
    int  33h
skip_show:
    ret
mouse_show ENDP

;-----------------------------------------------------------------------------
; void MT_API mouse_hide(void)
;-----------------------------------------------------------------------------
mouse_hide PROC PUBLIC USES ds
    mov  ax, @data
    mov  ds, ax
    cmp  mouse_active, 0
    je   skip_hide
    mov  ax, 0002h
    int  33h
skip_hide:
    ret
mouse_hide ENDP

;-----------------------------------------------------------------------------
; void MT_API mouse_set_pos(unsigned char row, unsigned char col)
;-----------------------------------------------------------------------------
mouse_set_pos PROC PUBLIC USES ds, row: BYTE, col: BYTE
    mov  ax, @data
    mov  ds, ax
    cmp  mouse_active, 0
    je   skip_set_pos

    xor  ch, ch
    mov  cl, col
    SHL_IMM cx, 3                   ; CX = col * 8

    xor  dh, dh
    mov  dl, row
    SHL_IMM dx, 3                   ; DX = row * 8

    mov  ax, 0004h
    int  33h
skip_set_pos:
    ret
mouse_set_pos ENDP

;-----------------------------------------------------------------------------
; int MT_API mouse_available(void)
;-----------------------------------------------------------------------------
mouse_available PROC PUBLIC USES ds
    mov  ax, @data
    mov  ds, ax
    xor  ax, ax
    cmp  mouse_q_count, 0
    je   no_m_ev
    mov  ax, 1
no_m_ev:
    ret
mouse_available ENDP

;-----------------------------------------------------------------------------
; int MT_API mouse_get_event(mouse_event_t *out_ev)
;-----------------------------------------------------------------------------
mouse_get_event PROC PUBLIC USES si di ds, out_ev: DATA_PTR
    mov  ax, @data
    mov  ds, ax
    LOAD_DATA_PTR di, out_ev

    pushf
    cli
    cmp  mouse_q_count, 0
    je   m_get_empty

    mov  bl, mouse_q_tail
    mov  al, bl
    inc  al
    and  al, MOUSE_QUEUE_MASK
    mov  mouse_q_tail, al
    dec  mouse_q_count

    xor  bh, bh
    mov  si, bx
    SHL_IMM si, 1
    SHL_IMM bx, 3
    add  si, bx
    add  si, OFFSET mouse_queue

    mov  cx, (SIZEOF MOUSE_EVENT / 2)
    cld
    rep  movsw

    popf
    mov  ax, 1
    ret

m_get_empty:
    popf
    xor  ax, ax
    ret
mouse_get_event ENDP

;-----------------------------------------------------------------------------
; void MT_API mouse_get_state(mouse_event_t *out_state)
;-----------------------------------------------------------------------------
mouse_get_state PROC PUBLIC USES si di ds, out_state: DATA_PTR
    mov  ax, @data
    mov  ds, ax
    LOAD_DATA_PTR di, out_state

    pushf
    cli
    mov  si, OFFSET mouse_live
    mov  cx, (SIZEOF MOUSE_EVENT / 2)
    cld
    rep  movsw
    popf
    ret
mouse_get_state ENDP

END