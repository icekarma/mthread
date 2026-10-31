/*===========================================================================
 * input.h - Interrupt-Driven Keyboard (INT 09h) & Mouse (INT 33h) Module
 * Signals a shared mt_event_t via event_set_isr() when input arrives.
 *===========================================================================*/
#if !defined _INPUT_H_
#define _INPUT_H_

#include "mthread.h"

#if defined __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 1. Keyboard API (kbd.asm)
 *===========================================================================*/

/* BIOS Shift/Modifier flags returned by kbd_get_modifiers() (0040:0017h) */
#define KBD_MOD_RSHIFT      0x01
#define KBD_MOD_LSHIFT      0x02
#define KBD_MOD_CTRL        0x04
#define KBD_MOD_ALT         0x08
#define KBD_MOD_SCROLLLOCK  0x10
#define KBD_MOD_NUMLOCK     0x20
#define KBD_MOD_CAPSLOCK    0x40
#define KBD_MOD_INSERT      0x80

/* Hooks INT 09h and signals `notify_ev` via event_set_isr() when keystrokes
 * enter the BIOS keyboard buffer (or when Ctrl+Break is pressed). */
void          MT_API kbd_init( mt_event_t* notify_ev );
void          MT_API kbd_shutdown( void );

/* Returns 1 if at least one keystroke is waiting in the BIOS buffer, 0 if empty */
int           MT_API kbd_available( void );

/* Non-blocking key read: returns 16-bit BIOS key word (low byte = ASCII,
 * high byte = scan code), or 0x0000 if no key is available. */
unsigned int  MT_API kbd_getkey( void );

/* Returns current BIOS modifier flags byte from 0040:0017h */
unsigned char MT_API kbd_get_modifiers( void );

/*===========================================================================
 * 2. Mouse API (mouse.asm)
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

#if defined _MTHRWRAP_H_
/*===========================================================================
 * C++ RAII Wrappers (available when mthrwrap.h is included before input.h)
 *===========================================================================*/

class MKeyboard {

    MKeyboard( MKeyboard const& other );
    MKeyboard& operator=( MKeyboard const& other );

public:

    MKeyboard( MEvent& notify_ev ) {
        kbd_init( notify_ev.GetHandle( ) );
    }

    ~MKeyboard( ) {
        kbd_shutdown( );
    }

    static int IsAvailable( ) {
        return kbd_available( );
    }

    static unsigned int GetKey( ) {
        return kbd_getkey( );
    }

    static unsigned char GetModifiers( ) {
        return kbd_get_modifiers( );
    }

};

class MMouse {

    MMouse( MMouse const& other );
    MMouse& operator=( MMouse const& other );

public:

    MMouse( MEvent& notify_ev, unsigned int event_mask = MOUSE_EVT_ALL ) {
        buttons_count = mouse_init( notify_ev.GetHandle( ), event_mask );
    }

    ~MMouse( ) {
        mouse_shutdown( );
    }

    int IsInstalled( ) const {
        return buttons_count > 0;
    }

    int GetButtonCount( ) const {
        return buttons_count;
    }

    static void Show( ) {
        mouse_show( );
    }

    static void Hide( ) {
        mouse_hide( );
    }

    static void SetPos( unsigned char row, unsigned char col ) {
        mouse_set_pos( row, col );
    }

    static int IsAvailable( ) {
        return mouse_available( );
    }

    static int GetEvent( mouse_event_t& out_ev ) {
        return mouse_get_event( &out_ev );
    }

    static void GetState( mouse_event_t& out_state ) {
        mouse_get_state( &out_state );
    }

private:

    int buttons_count;

};
#endif /* defined _MTHRWRAP_H_ */

#endif /* defined __cplusplus */

#endif /* !defined _INPUT_H_ */
