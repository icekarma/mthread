;=============================================================================
; KBD.ASM - Interrupt-Driven Keyboard Handler (INT 09h) for MTHREAD
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

INCLUDE mthread/mthread.inc

EXTERNDEF C event_set_isr:                PROC
EXTERNDEF C mt_enter_isr:                 PROC
EXTERNDEF C mt_leave_isr:                 PROC
EXTERNDEF C threading_is_break_requested: PROC

;=============================================================================
; DATA SEGMENT (DGROUP)
;=============================================================================

.DATA

kbd_ev_off      DW  0
kbd_ev_seg      DW  0
kbd_active      DB  0

;=============================================================================
; CODE SEGMENT
;=============================================================================

.CODE

kbd_dgroup_seg  DW  0
old_int09       DD  0

;-----------------------------------------------------------------------------
; isr_int09 - Hardware keyboard interrupt handler (IRQ1 / INT 09h)
;-----------------------------------------------------------------------------
isr_int09 PROC FAR
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es

    ; Lock preemption before chaining (BIOS INT 09h executes STI)
    call mt_enter_isr

    ; Chain to original BIOS INT 09h handler to translate scan code -> BDA
    pushf
    call DWORD PTR cs:[old_int09]
    cli

    mov  ds, cs:[kbd_dgroup_seg]

    ; Did this keystroke trigger Ctrl+Break (INT 1Bh)?
    call threading_is_break_requested
    or   ax, ax
    jnz  signal_kbd_event

    ; Check BIOS keyboard buffer head (0040:001Ah) vs. tail (0040:001Ch)
    xor  ax, ax
    mov  es, ax
    mov  ax, es:[041Ah]
    cmp  ax, es:[041Ch]
    je   isr_int09_done             ; Buffer empty (e.g., key release, modifier)

signal_kbd_event:
    mov  ax, kbd_ev_off
    mov  dx, kbd_ev_seg
    mov  cx, ax
    or   cx, dx
    jz   isr_int09_done

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

isr_int09_done:
    call mt_leave_isr

    pop  es
    pop  ds
    pop  di
    pop  si
    pop  dx
    pop  cx
    pop  bx
    pop  ax
    iret
isr_int09 ENDP

;-----------------------------------------------------------------------------
; void MT_API kbd_init(mt_event_t *notify_ev)
;-----------------------------------------------------------------------------
kbd_init PROC PUBLIC USES ds, notify_ev: DATA_PTR
    LOAD_DS_DGROUP
    mov  cs:[kbd_dgroup_seg], ax

    cmp  kbd_active, 0
    jne  kbd_init_done

    IF @DataSize
        mov  ax, WORD PTR notify_ev
        mov  dx, WORD PTR notify_ev+2
        IF @DataSize EQ 2
            NORMALIZE_FAR_PTR dx, ax, cx
        ENDIF
    ELSE
        mov  ax, notify_ev
        mov  dx, ds
    ENDIF
    mov  kbd_ev_off, ax
    mov  kbd_ev_seg, dx

    ; Save original INT 09h vector
    mov  ax, 3509h
    int  21h
    mov  WORD PTR cs:[old_int09], bx
    mov  WORD PTR cs:[old_int09+2], es

    ; Hook INT 09h
    mov  kbd_active, 1
    push ds
    push cs
    pop  ds
    mov  dx, OFFSET isr_int09
    mov  ax, 2509h
    int  21h
    pop  ds

kbd_init_done:
    ret
kbd_init ENDP

;-----------------------------------------------------------------------------
; void MT_API kbd_shutdown(void)
;-----------------------------------------------------------------------------
kbd_shutdown PROC PUBLIC USES ds
    LOAD_DS_DGROUP

    cmp  kbd_active, 0
    je   kbd_shut_done
    mov  kbd_active, 0
    mov  kbd_ev_off, 0
    mov  kbd_ev_seg, 0

    lds  dx, cs:[old_int09]
    mov  ax, 2509h
    int  21h

kbd_shut_done:
    ret
kbd_shutdown ENDP

;-----------------------------------------------------------------------------
; int MT_API kbd_available(void)
;-----------------------------------------------------------------------------
kbd_available PROC PUBLIC USES es
    xor  ax, ax
    mov  es, ax
    mov  dx, es:[041Ah]
    cmp  dx, es:[041Ch]
    je   kbd_not_avail
    inc  ax
kbd_not_avail:
    ret
kbd_available ENDP

;-----------------------------------------------------------------------------
; unsigned int MT_API kbd_getkey(void)
; Non-blocking: returns 16-bit key (AL=ASCII, AH=scan) or 0 if buffer empty.
;-----------------------------------------------------------------------------
kbd_getkey PROC PUBLIC USES es
    xor  ax, ax
    mov  es, ax
    mov  dx, es:[041Ah]
    cmp  dx, es:[041Ch]
    je   kbd_no_key
    int  16h
kbd_no_key:
    ret
kbd_getkey ENDP

;-----------------------------------------------------------------------------
; unsigned char MT_API kbd_get_modifiers(void)
;-----------------------------------------------------------------------------
kbd_get_modifiers PROC PUBLIC USES es
    xor  ax, ax
    mov  es, ax
    mov  al, es:[0417h]
    ret
kbd_get_modifiers ENDP

END
