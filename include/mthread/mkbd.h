/*===========================================================================
 * mkbd.h - Definitions for C++ Interface to Interrupt-Driven Keyboard (INT 09h) Module
 *===========================================================================*/

#if !defined _MKBD_H_
#define _MKBD_H_

#if !defined __cplusplus
#error "mkbd.h is a C++ wrapper for kbd.h. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/kbd.h>
#include <mthread/mevent.h>

#if defined __cplusplus

/*===========================================================================
 * Class MKeyboard - C++ Interface to Interrupt-Driven Keyboard (INT 09h) Module
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

#endif /* defined __cplusplus */

#endif /* !defined _MKBD_H_ */
