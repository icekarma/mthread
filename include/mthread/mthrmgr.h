#if !defined _MTHRMGR_H_
#define _MTHRMGR_H_

#if !defined __cplusplus
#error "mthrmgr.h is a C++ header. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/thread.h>
#include <mthread/atomic.h>

//=======================================================
// Reference-counted singleton lifecycle manager
//=======================================================

class MThreadManager {

    MThreadManager( ) {
        /*empty*/
    }

    ~MThreadManager( ) {
        Shutdown( );
    }

    MThreadManager( MThreadManager const& other );
    MThreadManager& operator=( MThreadManager const& other );

public:

    static MThreadManager* GetInstance( ) {
        if ( !s_instance ) {
            s_instance = new MThreadManager( );
            if ( !s_instance ) {
                return NULL;
            }
        }

        ++s_ref_count;
        return s_instance;
    }

    unsigned int Release( ) {
        if ( !s_ref_count ) {
            return 0;
        }

        if ( --s_ref_count == 0 ) {
            delete s_instance;
            s_instance = NULL;
        }

        return s_ref_count;
    }

    int Initialize( unsigned char pit_shift = MT_TICK_18HZ, unsigned char main_prio = MT_PRIO_NORMAL ) {
        if ( !cmp_xchg( &s_active, 0, 1 ) ) {
            return MT_ERR_INVALID;
        }

        thread_init( pit_shift, main_prio );
        return MT_ERR_NONE;
    }

    void Shutdown( ) {
        if ( !cmp_xchg( &s_active, 1, 2 ) ) {
            return;
        }

        thread_join_all( );
        thread_shutdown( );

        s_active = 0;
    }

    static void Abort( ) {
        s_active = 0;
        thread_abort( );
    }

    static int IsBreakRequested( ) {
        return thread_is_break_requested( );
    }

    static unsigned int GetCriticalError( ) {
        return thread_get_critical_error( );
    }

    static int IsActive( ) {
        return ( s_active == 1 );
    }

    static unsigned int GetRefCount( ) {
        return s_ref_count;
    }

private:

    static MThreadManager* s_instance;
    static unsigned int    s_ref_count;
    static int             s_active;

};

#endif /* !defined _MTHRMGR_H_ */
