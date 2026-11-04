/*===========================================================================
 * mouse.h - Callback-driven mouse (INT 33h) module
 *===========================================================================*/

#if !defined _MOUSE_H_
#define _MOUSE_H_

#include <mthread/mthread.h>

#if defined __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Mouse API
 *===========================================================================*/

/* INT 33h callback event mask bits */
#define MOUSE_EVT_MOVE          0x0001
#define MOUSE_EVT_LBUTTON_DOWN  0x0002
#define MOUSE_EVT_LBUTTON_UP    0x0004
#define MOUSE_EVT_RBUTTON_DOWN  0x0008
#define MOUSE_EVT_RBUTTON_UP    0x0010
#define MOUSE_EVT_MBUTTON_DOWN  0x0020
#define MOUSE_EVT_MBUTTON_UP    0x0040
#define MOUSE_EVT_ALL_BUTTONS   0x007E
#define MOUSE_EVT_ALL           0x007F

/* Button state bits in mouse_event_t.buttons */
#define MOUSE_BTN_LEFT          0x01
#define MOUSE_BTN_RIGHT         0x02
#define MOUSE_BTN_MIDDLE        0x04

typedef struct {
    unsigned int  event_flags; /* MOUSE_EVT_* bitmask (0 for live state query) */
    unsigned char buttons;     /* MOUSE_BTN_* bitmask currently held down      */
    unsigned char row;         /* 0-based text row (virtual_y / 8)             */
    unsigned char col;         /* 0-based text column (virtual_x / 8)          */
    unsigned char reserved;
    unsigned int  raw_x;       /* Raw INT 33h virtual X coordinate (CX)        */
    unsigned int  raw_y;       /* Raw INT 33h virtual Y coordinate (DX)        */
} mouse_event_t;

/* Resets mouse driver and registers INT 33h callback for `event_mask`
 * (pass 0 for MOUSE_EVT_ALL). Signals `notify_ev` via event_set_isr().
 * Returns number of mouse buttons (2 or 3) on success, or 0 if no mouse. */
int  MT_API mouse_init( mt_event_t* notify_ev, unsigned int event_mask );
void MT_API mouse_shutdown( void );

void MT_API mouse_show( void );
void MT_API mouse_hide( void );
void MT_API mouse_set_pos( unsigned char row, unsigned char col );

/* Returns 1 if a mouse event is waiting in the 16-slot ring buffer, 0 if empty */
int  MT_API mouse_available( void );

/* Pops the oldest queued mouse event into *out_ev. Returns 1 if popped, 0 if empty. */
int  MT_API mouse_get_event( mouse_event_t* out_ev );

/* Atomically snapshots the current mouse position and button state into *out_state */
void MT_API mouse_get_state( mouse_event_t* out_state );

#if defined __cplusplus
}
#endif /* defined __cplusplus */

#endif /* !defined _MOUSE_H_ */
