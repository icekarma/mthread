/*===========================================================================
 * mevent.h - Definitions for C++ interface for mthread's event synchronization primitive
 *===========================================================================*/

#if !defined _MEVENT_H_
#define _MEVENT_H_

#if !defined __cplusplus
#error "mevent.h is a C++ header. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/synchron.h>

//===========================================================================
// Class MEvent - C++ interface for mthread's event synchronization primitive
//===========================================================================

class MEvent {

    MEvent( MEvent const& other );
    MEvent& operator=( MEvent const& other );

public:

    MEvent( unsigned char manual_reset = 0, unsigned char initial_state = 0 ) {
        event_init( &ev, manual_reset, initial_state );
    }

    void Set( ) {
        event_set( &ev );
    }

    void Reset( ) {
        event_reset( &ev );
    }

    void Pulse( ) {
        event_pulse( &ev );
    }

    void Wait( ) {
        event_wait( &ev );
    }

    mt_event_t* GetHandle( ) {
        return &ev;
    }

private:

    mt_event_t ev;

};

#endif /* !defined _MEVENT_H_ */
