#if !defined _MTHRWRAP_H_
#define _MTHRWRAP_H_

#if !defined __cplusplus
#error "mthrwrap.h is a C++ wrapper for mthread.h. Please compile as C++."
#endif // !defined __cplusplus

#include "mthread.h"

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

    int Initialize( unsigned char pit_shift, unsigned char main_prio ) {
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

//================================================
// C++ wrapper for mthread.h's thread_* functions
//================================================

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

        tid_t const ret = thread_create( func, arg, stack_ptr, stack_size, prio );
        if ( ret < MT_MAX_THREADS ) {
            tid = ret;
        }
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

//=========================================================
// C++ wrappers for MTHREAD synchronization primitives
//=========================================================

//
// MEvent
//

class MEvent {

    MEvent( MEvent const& other );
    MEvent& operator=( MEvent const& other );

public:

    MEvent( unsigned char manual_reset, unsigned char initial_state ) {
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

private:

    mt_event_t ev;

};

//
// MMutex
//

class MMutex {

    friend class MCondVar;

    MMutex( MMutex const& other );
    MMutex& operator=( MMutex const& other );

public:

    MMutex( ) {
        mutex_init( &mtx );
    }

    void Lock( ) {
        mutex_lock( &mtx );
    }

    int TryLock( ) {
        return mutex_trylock( &mtx );
    }

    void Unlock( ) {
        mutex_unlock( &mtx );
    }

private:

    mt_mutex_t mtx;

};

//
// MLockGuard
//

class MLockGuard {

    MLockGuard( MLockGuard const& other );
    MLockGuard& operator=( MLockGuard const& other );

public:

    MLockGuard( MMutex& mutex ):
        mtx ( mutex )
    {
        mtx.Lock( );
    }

    ~MLockGuard( ) {
        mtx.Unlock( );
    }

private:

    MMutex& mtx;

};

//
// MSemaphore
//

class MSemaphore {

    MSemaphore( MSemaphore const& other );
    MSemaphore& operator=( MSemaphore const& other );

public:

    MSemaphore( unsigned int initial_count ) {
        sem_init( &sem, initial_count );
    }

    void Wait( ) {
        sem_wait( &sem );
    }

    int TryWait( ) {
        return sem_trywait( &sem );
    }

    void Post( ) {
        sem_post( &sem );
    }

private:

    mt_sem_t sem;

};

//
// MCondVar
//

class MCondVar {

    MCondVar( MCondVar const& other );
    MCondVar& operator=( MCondVar const& other );

public:

    MCondVar( ) {
        cond_init( &cv );
    }

    void Wait( MMutex& mutex ) {
        cond_wait( &cv, &mutex.mtx );
    }

    void Signal( ) {
        cond_signal( &cv );
    }

    void Broadcast( ) {
        cond_broadcast( &cv );
    }

private:

    mt_cond_t cv;

};

#endif /* !defined _MTHRWRAP_H_ */
