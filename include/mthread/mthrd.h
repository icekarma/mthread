/*===========================================================================
 * mthrd.h - Definitions for C++ thread class that wraps mthread's thread_* functions
 *===========================================================================*/

#if !defined _MTHRD_H_
#define _MTHRD_H_

#if !defined __cplusplus
#error "mthrd.h is a C++ header. Please compile as C++."
#endif // !defined __cplusplus

#include <stddef.h>

#include <mthread/thread.h>
#include <mthread/synchron.h>

//===========================================================================
// Class MThread - C++ wrapper for mthread.h's thread_* functions
//===========================================================================

class MThread {

    MThread( MThread const& other );
    MThread& operator=( MThread const& other );

public:

    MThread( ):
        func       ( NULL           ),
        arg        ( NULL           ),
        stack_ptr  ( NULL           ),
        stack_size ( 0              ),
        prio       ( MT_PRIO_NORMAL ),
        tid        ( MT_NO_THREAD   )
    {
        /*empty*/
    }

    MThread( thread_func_t func, void* arg, void* stack_ptr = NULL, unsigned int stack_size = 0, unsigned char priority = MT_PRIO_NORMAL ):
        func       ( func          ),
        arg        ( arg           ),
        stack_ptr  ( stack_ptr     ),
        stack_size ( stack_size    ),
        prio       ( priority      ),
        tid        ( MT_NO_THREAD  )
    {
        /*empty*/
    }

    ~MThread( ) {
        if ( ( tid != MT_NO_THREAD ) && ( tid != thread_self( ) ) ) {
            Join( );
        }
    }

    tid_t Create( ) {
        if ( !func || ( tid != MT_NO_THREAD ) ) {
            return MT_ERR_INVALID;
        }

        mt_enter_isr( );
        tid_t const ret = thread_create( func, arg, stack_ptr, stack_size, prio );
        if ( ret < MT_MAX_THREADS ) {
            tid = ret;
        }
        mt_leave_isr( );
        return ret;
    }

    tid_t Create( thread_func_t func, void* arg, void* stack_ptr = NULL, unsigned int stack_size = 0, unsigned char priority = MT_PRIO_NORMAL ) {
        if ( !func || ( tid != MT_NO_THREAD ) ) {
            return MT_ERR_INVALID;
        }

        this->func       = func;
        this->arg        = arg;
        this->stack_ptr  = stack_ptr;
        this->stack_size = stack_size;
        this->prio       = priority;
        return Create( );
    }

    int Join( ) {
        if ( tid == MT_NO_THREAD ) {
            return MT_ERR_INVALID;
        }

        int const ret = thread_join( tid );
        if ( ret == MT_ERR_NONE ) {
            tid = MT_NO_THREAD;
        }
        return ret;
    }

    tid_t GetTid( ) const {
        return tid;
    }

    unsigned char GetPriority( ) const {
        return thread_get_priority( tid );
    }

    int SetPriority( unsigned char new_prio ) {
        int const ret = thread_set_priority( tid, new_prio );
        if ( ret == MT_ERR_NONE ) {
            prio = new_prio;
        }
        return ret;
    }

    static int EnableFpu( ) {
        return thread_enable_fpu( 1 );
    }

    static void DisableFpu( ) {
        thread_enable_fpu( 0 );
    }

    static int IsFpuEnabled( ) {
        return thread_is_fpu_enabled( );
    }

    static void Yield( ) {
        thread_yield( );
    }

    static void Sleep( unsigned int ticks ) {
        thread_sleep( ticks );
    }

    static void Exit( ) {
        thread_exit( );
    }

    static void JoinAll( ) {
        thread_join_all( );
    }

    static tid_t Self( ) {
        return thread_self( );
    }

private:

    thread_func_t func;
    void*         arg;
    void*         stack_ptr;
    unsigned int  stack_size;
    unsigned char prio;
    tid_t         tid;

};

#endif /* !defined _MTHRD_H_ */
