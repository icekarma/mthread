;=============================================================================
; VIDEOHLP.ASM: Helper routines for video display.
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

.DATA

EXTERN row_offset: WORD
EXTERN col_offset: WORD

.CODE

;-----------------------------------------------------------------------------
; void MT_API video_gotoxy(unsigned char row, unsigned char col)
; Moves the text-mode cursor to 0-based (row, col) on the current display page
;-----------------------------------------------------------------------------
video_gotoxy PROC PUBLIC, row: BYTE, col: BYTE
    push es
    xor  ax, ax
    mov  es, ax
    mov  bh, es:[0462h]            ; Load current active display page (0..7)
    pop  es

    mov  ah, 02h                   ; BIOS Set Cursor Position
    mov  dh, row                   ; Load 0-based row
    add  dh, byte ptr [row_offset] ; Adjust for row offset
    mov  dl, col                   ; Load 0-based column
    add  dl, byte ptr [col_offset] ; Adjust for column offset
    int  10h
    ret
video_gotoxy ENDP

END
