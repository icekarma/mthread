/*===========================================================================
 * mmouse.h - Definitions for C++ interface to callback-driven mouse (INT 33h) module
 *===========================================================================*/

#if !defined _MMOUSE_H_
#define _MMOUSE_H_

#if !defined __cplusplus
#error "mmouse.h is a C++ wrapper for mouse.h. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/mouse.h>

//===========================================================================
// Class MMouse - C++ interface to callback-driven mouse (INT 33h) module
//===========================================================================

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

#endif /* !defined _MMOUSE_H_ */
