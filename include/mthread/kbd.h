/*===========================================================================
 * kbd.h - Definitions for interrupt-driven keyboard (INT 09h) module
 *===========================================================================*/

#if !defined _KBD_H_
#define _KBD_H_

#include <mthread/synchron.h>

#if defined __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Keyboard API
 *===========================================================================*/

/* BIOS shift/modifier flags returned by kbd_get_modifiers() (0040:0017h) */
#define KBD_MOD_RSHIFT     0x01
#define KBD_MOD_LSHIFT     0x02
#define KBD_MOD_CTRL       0x04
#define KBD_MOD_ALT        0x08
#define KBD_MOD_SCROLLLOCK 0x10
#define KBD_MOD_NUMLOCK    0x20
#define KBD_MOD_CAPSLOCK   0x40
#define KBD_MOD_INSERT     0x80

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

#if defined __cplusplus
}
#endif /* defined __cplusplus */

#endif /* !defined _KBD_H_ */
