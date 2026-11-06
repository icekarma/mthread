/*===========================================================================
 * thread.h - Thread management for MTHREAD
 *===========================================================================*/

#if !defined _THREAD_H_
#define _THREAD_H_

#include <mthread/_defs.h>

#if defined __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Thread management API
 *===========================================================================*/

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

/* PIT tick rate shift factors for threading_init's pit_shift argument:
*   0 = 1x stock ( 18.2 Hz / 54.9 ms)
*   1 = 2x stock ( 36.4 Hz / 27.5 ms)
*   2 = 4x stock ( 72.8 Hz / 13.7 ms)
*   3 = 8x stock (145.6 Hz /  6.9 ms)
*/
#define MT_TICK_18HZ  0
#define MT_TICK_36HZ  1
#define MT_TICK_73HZ  2
#define MT_TICK_146HZ 3

/* Thread entry point prototype: void MT_API my_thread(void *arg) */
typedef void ( MT_API *thread_func_t )( void* arg );

/* Core Lifecycle API */
void          MT_API threading_init( unsigned char pit_shift, unsigned char main_prio );
void          MT_API threading_shutdown( void );
void          MT_API threading_abort( void );
int           MT_API threading_is_break_requested( void );
unsigned int  MT_API threading_get_critical_error( void );

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

/* Enables (enable != 0) or disables (enable == 0) 94-byte x87 FPU / emulator
 * context saving for the calling thread. Returns 1 if FPU context saving is
 * active for the calling thread, or 0 if disabled/unavailable.
 */
int           MT_API thread_enable_fpu( int const enable );
int           MT_API thread_is_fpu_enabled( void );

#if defined __cplusplus
}

#include <mthread/mthrmgr.h>
#include <mthread/mthrd.h>

#endif

#endif /* _THREAD_H_ */
