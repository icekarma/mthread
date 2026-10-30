/*===========================================================================
 * syncdemo.c - Synchronization Primitives Verification Harness
 * Demonstrates:
 *   1. Manual-Reset & Auto-Reset Win32 Events (mt_event_t)
 *   2. Recursive Mutex with Priority Inheritance (mt_mutex_t)
 *   3. Bounded Producer-Consumer Ring Buffer via Semaphores (mt_sem_t)
 *   4. Condition Variable Batch Notification (mt_cond_t)
 *===========================================================================*/

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

#include "mthread.h"
#include "video.h"

#define RING_SIZE 8

static volatile int          g_running             = 1;

/* 1. Events: Manual-reset start gate & Auto-reset high-prio trigger */
static mt_event_t            g_start_ev;
static mt_event_t            g_high_trig_ev;

/* 2. Priority Inheritance Mutex */
static mt_mutex_t            g_pi_mtx;
static volatile unsigned int g_pi_handoffs         = 0;
static volatile unsigned int g_med_hog_loops       = 0;

/* 3. Counting Semaphores for 8-slot Ring Buffer */
static mt_sem_t              g_sem_empty;
static mt_sem_t              g_sem_filled;
static volatile char         g_ring_buf[RING_SIZE];
static volatile unsigned int g_head                = 0;
static volatile unsigned int g_tail                = 0;

/* 4. Condition Variable + Mutex for Completed Batches */
static mt_mutex_t            g_batch_mtx;
static mt_cond_t             g_batch_cv;
static volatile unsigned int g_items_consumed      = 0;
static volatile unsigned int g_batches_ready       = 0;

static const char            g_spinner[]           = "|/-\\";

/*---------------------------------------------------------------------------
 * TID 1 (LOW PRIO): Acquires g_pi_mtx, triggers HIGH thread, gets boosted!
 *---------------------------------------------------------------------------*/
void MT_API low_lock_holder( void* arg ) {
    int const             row  = 7;
    unsigned int          step;
    volatile unsigned int burn;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    event_wait( &g_start_ev );

    while ( g_running ) {
        video_puts( row, 18, 0x17, "[UNLOCKED ]" );
        thread_sleep( 10 );

        mutex_lock( &g_pi_mtx );
        /* Recursive lock test: lock a second time and immediately unlock */
        mutex_lock( &g_pi_mtx );
        mutex_unlock( &g_pi_mtx );

        video_puts( row, 18, 0x1B, "[HOLD PR=1]" );
        video_puts( row, 31, 0x19, "........................" );

        for ( step = 0; ( step < 24 ) && g_running; ++step ) {
            /* At step 8, wake TID 3 (HIGH) via Auto-Reset Event so it contends
             * on g_pi_mtx and boosts our priority from LOW (1) to HIGH (5)! */
            if ( step == 8 ) {
                video_puts( row, 18, 0x1A, "[BOOST->P5]" );
                event_set( &g_high_trig_ev );
            }

            for ( burn = 0; burn < 5000; ++burn ) {
                /* Busy work inside mutex */
            }

            video_putc( row, 31 + step, (unsigned char) ( step >= 8 ? 0x2F : 0x1F ), 0xFE );
        }

        mutex_unlock( &g_pi_mtx );
    }
}

/*---------------------------------------------------------------------------
 * TID 2 (NORMAL PRIO): CPU-bound thread that tries to preempt TID 1
 *---------------------------------------------------------------------------*/
void MT_API med_cpu_hog( void* arg ) {
    int const             row  = 9;
    unsigned int          idx  = 0;
    volatile unsigned int burn;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    event_wait( &g_start_ev );
    video_puts( row, 18, 0x1A, "[CPU HOG  ]" );

    while ( g_running ) {
        for ( burn = 0; burn < 6000; ++burn ) {
            /* Pure CPU burn */
        }
        ++g_med_hog_loops;
        idx = ( idx + 1 ) & 3;
        video_putc( row, 32, 0x1E, (unsigned char) g_spinner[idx] );
        video_putu16( row, 71, 0x1E, g_med_hog_loops );

        /* Brief sleep every 16 iterations so LOW thread can initially grab mutex */
        if ( ( g_med_hog_loops & 15 ) == 0 ) {
            video_puts( row, 18, 0x19, "[NAP: 4 TK]" );
            thread_sleep( 4 );
            video_puts( row, 18, 0x1A, "[CPU HOG  ]" );
        }
    }
}

/*---------------------------------------------------------------------------
 * TID 3 (HIGH PRIO): Waits on Auto-Reset Event, then locks g_pi_mtx
 *---------------------------------------------------------------------------*/
void MT_API high_lock_waiter( void* arg ) {
    int const row = 11;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    event_wait( &g_start_ev );

    while ( g_running ) {
        video_puts( row, 18, 0x19, "[WAIT EVNT]" );
        event_wait( &g_high_trig_ev );
        if ( !g_running ) {
            break;
        }

        /* Contend on g_pi_mtx -> boosts TID 1 to MT_PRIO_HIGH! */
        video_puts( row, 18, 0x1C, "[WAIT MTX ]" );
        mutex_lock( &g_pi_mtx );

        ++g_pi_handoffs;
        video_puts( row, 18, 0x1A, "[GOT LOCK!]" );
        video_putu16( row, 71, 0x1A, g_pi_handoffs );

        mutex_unlock( &g_pi_mtx );
    }
}

/*---------------------------------------------------------------------------
 * TID 4 (PRODUCER): Pushes bursts into 8-slot ring buffer using Semaphores
 *---------------------------------------------------------------------------*/
void MT_API sem_producer( void* arg ) {
    int const    row      = 13;
    char         next_ch  = 'A';
    unsigned int produced = 0;
    unsigned int slot;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    event_wait( &g_start_ev );

    while ( g_running ) {
        video_puts( row, 18, 0x1C, "[WAIT SEM ]" );
        sem_wait( &g_sem_empty );
        if ( !g_running ) {
            break;
        }

        video_puts( row, 18, 0x1A, "[PRODUCING]" );
        slot = g_head;
        g_ring_buf[slot] = next_ch;
        g_head = ( slot + 1 ) & ( RING_SIZE - 1 );

        /* Render item into visual ring buffer on row 14 */
        video_putc( 14, 33 + slot * 3, 0x2F, next_ch );

        next_ch = (char) ( ( next_ch == 'Z' ) ? 'A' : ( next_ch + 1 ) );
        ++produced;
        video_putu16( row, 71, 0x1E, produced );

        sem_post( &g_sem_filled );

        /* Produce in fast bursts of 4, then pause */
        if ( ( produced & 3 ) == 0 ) {
            thread_sleep( 10 );
        } else {
            thread_sleep( 2 );
        }
    }
}

/*---------------------------------------------------------------------------
 * TID 5 (CONSUMER): Pops from Semaphores & signals Condition Variable on batch
 *---------------------------------------------------------------------------*/
void MT_API sem_consumer( void* arg ) {
    int const    row  = 16;
    unsigned int slot;
    char         ch;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    event_wait( &g_start_ev );

    while ( g_running ) {
        video_puts( row, 18, 0x19, "[WAIT ITEM]" );
        sem_wait( &g_sem_filled );
        if ( !g_running ) {
            break;
        }

        video_puts( row, 18, 0x1B, "[CONSUMING]" );
        slot = g_tail;
        ch = g_ring_buf[slot];
        g_tail = ( slot + 1 ) & ( RING_SIZE - 1 );

        /* Clear slot in visual ring buffer on row 14 */
        video_putc( 14, 33 + slot * 3, 0x19, '.' );
        video_putc( row, 43, 0x1E, ch );

        sem_post( &g_sem_empty );

        /* Update batch state under g_batch_mtx and signal CondVar every 8 items */
        mutex_lock( &g_batch_mtx );
        ++g_items_consumed;
        video_putu16( row, 71, 0x1E, g_items_consumed );
        if ( ( g_items_consumed & 7 ) == 0 ) {
            ++g_batches_ready;
            cond_signal( &g_batch_cv );
        }
        mutex_unlock( &g_batch_mtx );

        thread_sleep( 5 );
    }
}

/*---------------------------------------------------------------------------
 * Main thread (TID 0): Waits on Condition Variable & monitors keyboard
 *---------------------------------------------------------------------------*/
int main( void ) {
    unsigned int last_batch_seen = 0;
    unsigned int countdown;

    video_init( );
    video_clear( 0x17 );
#if defined RULER
    video_ruler( 1, 3 );
#endif
    video_puts(    1,  7, 0x1F,        "\xCD\xCD\xCD MS-DOS 8086 SYNCHRONIZATION PRIMITIVES SHOWCASE (SYNCDEMO) \xCD\xCD\xCD" );
    video_puts(    3,  0, 0x17, "Primitives: Manual/auto events | Priority-inherited mutex | Semaphores | CondVar" );
    video_puts(    5,  4, 0x1B,     "Thread        State        Visual telemetry                        Count"     );
    video_puts(    6,  4, 0x17,     "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4"     );
    video_puts(    7,  4, 0x1F,     "TID 1 (LOW)   [INIT     ]                           (PI mutex)"               );
    video_puts(    7, 31, 0x19,                                "........................"                          );
    video_puts(    9,  4, 0x1F,     "TID 2 (NORM)  [INIT     ]  [ ] Medium CPU hog"                                );
    video_puts(   11,  4, 0x1F,     "TID 3 (HIGH)  [INIT     ]  Direct mutex handoffs:"                            );
    video_puts(   13,  4, 0x1F,     "TID 4 (PROD)  [INIT     ]  Semaphore ring producer"                           );
    video_puts(   14,  4, 0x17,     "              Ring buffer: [ .  .  .  .  .  .  .  . ]"                        );
    video_puts(   16,  4, 0x1F,     "TID 5 (CONS)  [INIT     ]  Last item: [ ]"                                    );
    video_puts(   18,  4, 0x1E,     "TID 0 (MAIN)  CondVar batches (8 items/batch) completed:"                     );
    video_puts(   21, 22, 0x1A,                       "Starting gun event countdown:  xxxxx"                       );
    video_putu16(  9, 71, 0x1E, 0 );
    video_putu16( 11, 71, 0x1A, 0 );
    video_putu16( 13, 71, 0x1E, 0 );
    video_putu16( 16, 71, 0x1E, 0 );
    video_putu16( 18, 71, 0x1A, 0 );

    video_gotoxy( 21, 58 );

    /* Initialize primitives */
    event_init( &g_start_ev, 1, 0 );     /* Manual-reset, initially non-signaled */
    event_init( &g_high_trig_ev, 0, 0 ); /* Auto-reset, initially non-signaled   */
    mutex_init( &g_pi_mtx );
    sem_init( &g_sem_empty, RING_SIZE ); /* 8 empty slots initially              */
    sem_init( &g_sem_filled, 0 );        /* 0 filled items initially             */
    mutex_init( &g_batch_mtx );
    cond_init( &g_batch_cv );

    /* Start scheduler at 72.8 Hz (4x stock PIT rate) */
    thread_init( MT_TICK_73HZ, MT_PRIO_LOW );

    /* Create threads for each demo participant */
    thread_create( low_lock_holder,  NULL, NULL, 1024, MT_PRIO_LOW    );
    thread_create( med_cpu_hog,      NULL, NULL, 1024, MT_PRIO_NORMAL );
    thread_create( high_lock_waiter, NULL, NULL, 1024, MT_PRIO_HIGH   );
    thread_create( sem_producer,     NULL, NULL, 1024, MT_PRIO_NORMAL );
    thread_create( sem_consumer,     NULL, NULL, 1024, MT_PRIO_NORMAL );

    /* Countdown before firing the manual-reset starting gun event */
    for ( countdown = 3; countdown > 0; --countdown ) {
        video_putu16( 21, 53, 0x1C, countdown );
        thread_sleep( 72 );
    }

    video_puts( 21, 15, 0x1A, "Manual-reset event FIRED! Press 'Q' or Esc to quit" );
    video_gotoxy( 21, 65 );
    event_set( &g_start_ev );

    /* Main loop: wait on Condition Variable for batches while checking keyboard */
    while ( g_running ) {
        if ( _kbhit( ) ) {
            int const ch = _getch( );
            if ( ( ch == 27 ) || ( ch == 'q' ) || ( ch == 'Q' ) ) {
                g_running = 0;
                break;
            }
        }

        mutex_lock( &g_batch_mtx );
        while ( ( g_batches_ready == last_batch_seen ) && g_running ) {
            /* Check keyboard periodically or wait for CondVar signal */
            if ( _kbhit( ) ) {
                break;
            }
            cond_wait( &g_batch_cv, &g_batch_mtx );
        }
        last_batch_seen = g_batches_ready;
        mutex_unlock( &g_batch_mtx );

        video_putu16( 18, 71, 0x1A, last_batch_seen );
    }

    /* Wake any blocked threads so they observe g_running == 0 and exit cleanly */
    event_set( &g_high_trig_ev );
    sem_post( &g_sem_empty );
    sem_post( &g_sem_filled );
    cond_broadcast( &g_batch_cv );

    thread_join_all( );
    thread_shutdown( );

    video_gotoxy( 22, 0 );
    printf( "\rSyncDemo complete: %u priority-inherited handoffs, %u CondVar batches.\n", g_pi_handoffs, last_batch_seen );
    return 0;
}
