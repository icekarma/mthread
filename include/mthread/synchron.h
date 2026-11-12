/*===========================================================================
 * synchron.h - Synchronization primitives for mthread library
 *===========================================================================*/

#if !defined _SYNCHRON_H_
#define _SYNCHRON_H_

#include <mthread/_defs.h>

#if defined __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Synchronization primitives API (events, mutexes, semaphores, condition vars)
 *===========================================================================*/

/* ISR entry/exit for incrementing/decrementing bios_lock from external
 * hardware ISRs & callbacks
 */
void MT_API mt_enter_isr( void );
void MT_API mt_leave_isr( void );

/* Win32-style event object (4 bytes) */
typedef struct {
    unsigned char volatile signaled;     /* 0 = non-signaled, 1 = signaled            */
    unsigned char          manual_reset; /* 0 = auto-reset,   1 = manual-reset        */
    unsigned char volatile wait_mask;    /* Bitmask of waiting threads (1 << tid)     */
    unsigned char          reserved;
} mt_event_t;

void MT_API event_init( mt_event_t* ev, unsigned char manual_reset, unsigned char initial_state );
void MT_API event_set( mt_event_t* ev );
void MT_API event_reset( mt_event_t* ev );
void MT_API event_pulse( mt_event_t* ev );
void MT_API event_wait( mt_event_t* ev );

/* ISR-safe event_set() for signaling events from ISRs & callbacks */
void MT_API event_set_isr( mt_event_t* ev );

/* Recursive mutex with priority inheritance (4 bytes) */
typedef struct {
    tid_t         volatile owner;        /* Owning TID (0..7) or MT_NO_THREAD (0xFF)  */
    unsigned char volatile lock_count;   /* Recursive acquisition depth (0..255)      */
    unsigned char volatile wait_mask;    /* Bitmask of threads waiting to acquire     */
    unsigned char          reserved;
} mt_mutex_t;

void MT_API mutex_init( mt_mutex_t* mtx );
void MT_API mutex_lock( mt_mutex_t* mtx );
int  MT_API mutex_trylock( mt_mutex_t* mtx ); /* Returns 1 if acquired, 0 if busy */
void MT_API mutex_unlock( mt_mutex_t* mtx );

/* Counting semaphore (4 bytes) */
typedef struct {
    unsigned int  volatile count;        /* Available count (0..65535)                */
    unsigned char volatile wait_mask;    /* Bitmask of waiting threads                */
    unsigned char          reserved;
} mt_sem_t;

void MT_API sem_init( mt_sem_t* sem, unsigned int initial_count );
void MT_API sem_wait( mt_sem_t* sem );
int  MT_API sem_trywait( mt_sem_t* sem );    /* Returns 1 if acquired, 0 if zero */
void MT_API sem_post( mt_sem_t* sem );

/* Condition variable (2 bytes) */
typedef struct {
    unsigned char volatile wait_mask;    /* Bitmask of waiting threads                */
    unsigned char          reserved;
} mt_cond_t;

void MT_API cond_init( mt_cond_t* cv );
void MT_API cond_wait( mt_cond_t* cv, mt_mutex_t* mtx );
void MT_API cond_signal( mt_cond_t* cv );
void MT_API cond_broadcast( mt_cond_t* cv );

#if defined __cplusplus
}

#include <mthread/mevent.h>
#include <mthread/mmutex.h>
#include <mthread/msem.h>
#include <mthread/mcondvar.h>
#include <mthread/mlkguard.h>

#endif

#endif /* _SYNCHRON_H_ */
