/*===========================================================================
 * mthread.h - Preemptive Multithreading Library for MS-DOS (8086 / MASM 6.11)
 * Compatible with Microsoft C/C++ 7.00, Borland C++ 3.1, and OpenWatcom
 *===========================================================================*/
#if !defined MTHREAD_H
#define MTHREAD_H

#include <stddef.h>
#include <stdlib.h>

#if defined __cplusplus
extern "C" {
#endif

#if defined(__WATCOMC__)
  #define MT_API __cdecl
#else
  #define MT_API _cdecl
#endif

/* Thread ID and return codes */
typedef unsigned char tid_t;

#define MT_MAX_THREADS      8
#define MT_NO_THREAD        0xFF
#define MT_ERR_NONE         0x00
#define MT_ERR_NO_SLOTS     0xFE
#define MT_ERR_NO_MEMORY    0xFD
#define MT_ERR_INVALID      0xFC

/* Priority levels (0 = lowest, 7 = highest) */
#define MT_PRIO_IDLE          0
#define MT_PRIO_LOW           1
#define MT_PRIO_LOW_PLUS      2
#define MT_PRIO_NORMAL_MINUS  2
#define MT_PRIO_NORMAL        3
#define MT_PRIO_NORMAL_PLUS   4
#define MT_PRIO_HIGH_MINUS    4
#define MT_PRIO_HIGH          5
#define MT_PRIO_HIGH_PLUS     6
#define MT_PRIO_HIGHEST_MINUS 6
#define MT_PRIO_HIGHEST       7

/* PIT tick rate shift factors for thread_init(pit_shift):
 *   0 = 1x stock ( 18.2 Hz / 54.9 ms)
 *   1 = 2x stock ( 36.4 Hz / 27.5 ms)
 *   2 = 4x stock ( 72.8 Hz / 13.7 ms)
 *   3 = 8x stock (145.6 Hz /  6.9 ms)
 */
#define MT_TICK_18HZ        0
#define MT_TICK_36HZ        1
#define MT_TICK_73HZ        2
#define MT_TICK_146HZ       3

/* Thread entry point prototype: void MT_API my_thread(void *arg) */
typedef void ( MT_API* thread_func_t )( void* arg );

/* Core Lifecycle API */
void          MT_API thread_init( unsigned char pit_shift, unsigned char main_prio );
void          MT_API thread_shutdown( void );
void          MT_API thread_abort( void );
int           MT_API thread_is_break_requested( void );
unsigned int  MT_API thread_get_critical_error( void );

/* Pass stack_ptr = NULL to auto-allocate via malloc();
 * Pass stack_size = 0 to use default stack size (1024 bytes).
 * Returns new thread ID (0..7) or MT_ERR_* (>= 0xFC) on error.
 */
tid_t         MT_API thread_create( thread_func_t func, void* arg, void* stack_ptr, unsigned int stack_size, unsigned char priority );
void          MT_API thread_yield( void );
void          MT_API thread_sleep( unsigned int ticks );
void          MT_API thread_exit( void );
int           MT_API thread_join( tid_t tid );
void          MT_API thread_join_all( void );
tid_t         MT_API thread_self( void );
unsigned char MT_API thread_get_priority( tid_t tid );
int           MT_API thread_set_priority( tid_t tid, unsigned char new_prio );

/* ISR entry/exit for incrementing/decrementing bios_lock from external
 * hardware ISRs & callbacks
 */
void          MT_API mt_enter_isr( void );
void          MT_API mt_leave_isr( void );

/*===========================================================================
 * Synchronization primitives (events, mutexes, semaphores, condition vars)
 *===========================================================================*/

/* 1. Win32-style event object (4 bytes) */
typedef struct {
    unsigned char signaled;     /* 0 = non-signaled, 1 = signaled            */
    unsigned char manual_reset; /* 0 = auto-reset,   1 = manual-reset        */
    unsigned char wait_mask;    /* Bitmask of waiting threads (1 << tid)     */
    unsigned char reserved;
} mt_event_t;

void MT_API event_init( mt_event_t* ev, unsigned char manual_reset, unsigned char initial_state );
void MT_API event_set( mt_event_t* ev );
void MT_API event_set_isr( mt_event_t* ev );
void MT_API event_reset( mt_event_t* ev );
void MT_API event_pulse( mt_event_t* ev );
void MT_API event_wait( mt_event_t* ev );

/* 2. Recursive mutex with priority inheritance (4 bytes) */
typedef struct {
    tid_t         owner;        /* Owning TID (0..7) or MT_NO_THREAD (0xFF)  */
    unsigned char lock_count;   /* Recursive acquisition depth (0..255)      */
    unsigned char wait_mask;    /* Bitmask of threads waiting to acquire     */
    unsigned char reserved;
} mt_mutex_t;

void MT_API mutex_init( mt_mutex_t* mtx );
void MT_API mutex_lock( mt_mutex_t* mtx );
int  MT_API mutex_trylock( mt_mutex_t* mtx ); /* Returns 1 if acquired, 0 if busy */
void MT_API mutex_unlock( mt_mutex_t* mtx );

/* 3. Counting semaphore (4 bytes) */
typedef struct {
    unsigned int  count;        /* Available count (0..65535)                */
    unsigned char wait_mask;    /* Bitmask of waiting threads                */
    unsigned char reserved;
} mt_sem_t;

void MT_API sem_init( mt_sem_t* sem, unsigned int initial_count );
void MT_API sem_wait( mt_sem_t* sem );
int  MT_API sem_trywait( mt_sem_t* sem );    /* Returns 1 if acquired, 0 if zero */
void MT_API sem_post( mt_sem_t* sem );

/* 4. Condition variable (2 bytes) */
typedef struct {
    unsigned char wait_mask;    /* Bitmask of waiting threads                */
    unsigned char reserved;
} mt_cond_t;

void MT_API cond_init( mt_cond_t* cv );
void MT_API cond_wait( mt_cond_t* cv, mt_mutex_t* mtx );
void MT_API cond_signal( mt_cond_t* cv );
void MT_API cond_broadcast( mt_cond_t* cv );

/* 5. Atomic operations */
int MT_API cmp_xchg( int* valueptr, int const old_val, int const new_val );

#if defined __cplusplus
}
#endif

#endif /* MTHREAD_H */
