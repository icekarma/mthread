/*===========================================================================
 * cppsync.cpp - C++ Synchronization Primitives Verification Harness
 * Demonstrates:
 *   1. Manual-Reset & Auto-Reset Win32 Events (MEvent)
 *   2. Recursive Mutex with Priority Inheritance & RAII Guards (MMutex / MLockGuard)
 *   3. Bounded Producer-Consumer Ring Buffer via Semaphores (MSemaphore)
 *   4. Condition Variable Batch Notification (MCondVar)
 *===========================================================================*/

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>

#include "mthrwrap.h"
#include "video.h"

#define RING_SIZE 8

static volatile int          g_running             = 1;

/* 1. Events: Manual-reset start gate (1, 0) & Auto-reset high-prio trigger (0, 0) */
static MEvent                g_start_ev            ( 1, 0 );
static MEvent                g_high_trig_ev        ( 0, 0 );

/* 2. Priority Inheritance Mutex */
static MMutex                g_pi_mtx;
static volatile unsigned int g_pi_handoffs         = 0;
static volatile unsigned int g_med_hog_loops       = 0;

/* 3. Counting Semaphores for 8-slot Ring Buffer */
static MSemaphore            g_sem_empty           ( RING_SIZE );
static MSemaphore            g_sem_filled          ( 0 );
static volatile char         g_ring_buf[RING_SIZE];
static volatile unsigned int g_head                = 0;
static volatile unsigned int g_tail                = 0;

/* 4. Condition Variable + Mutex for Completed Batches */
static MMutex                g_batch_mtx;
static MCondVar              g_batch_cv;
static volatile unsigned int g_items_consumed      = 0;
static volatile unsigned int g_batches_ready       = 0;

static const char            g_spinner[]           = "|/-\\";

/*---------------------------------------------------------------------------
 * TID 1 (LOW PRIO): Acquires g_pi_mtx, triggers HIGH thread, gets boosted!
 *---------------------------------------------------------------------------*/
static void MT_API low_lock_holder( void* arg ) {
    int const             row = 7;
    unsigned int          step;
    volatile unsigned int burn;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    g_start_ev.Wait( );

    while ( g_running ) {
        video_puts( row, 18, 0x17, "[UNLOCKED ]" );
        MThread::Sleep( 10 );

        {
            MLockGuard lock( g_pi_mtx );

            /* Recursive lock test via nested RAII MLockGuard */
            {
                MLockGuard recursive_lock( g_pi_mtx );
            }

            video_puts( row, 18, 0x1B, "[HOLD PR=1]" );
            video_puts( row, 31, 0x19, "........................" );

            for ( step = 0; ( step < 24 ) && g_running; ++step ) {
                /* At step 8, wake TID 3 (HIGH) via Auto-Reset Event so it contends
                 * on g_pi_mtx and boosts our priority from LOW (1) to HIGH (5)! */
                if ( step == 8 ) {
                    video_puts( row, 18, 0x1A, "[BOOST->P5]" );
                    g_high_trig_ev.Set( );
                }

                for ( burn = 0; burn < 5000; ++burn ) {
                    /* Busy work inside mutex */
                }

                video_putc( row, 31 + step, (unsigned char) ( step >= 8 ? 0x2F : 0x1F ), 0xFE );
            }
        }
    }
}

/*---------------------------------------------------------------------------
 * TID 2 (NORMAL PRIO): CPU-bound thread that tries to preempt TID 1
 *---------------------------------------------------------------------------*/
static void MT_API med_cpu_hog( void* arg ) {
    int const             row = 9;
    unsigned int          idx = 0;
    volatile unsigned int burn;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    g_start_ev.Wait( );
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
            MThread::Sleep( 4 );
            video_puts( row, 18, 0x1A, "[CPU HOG  ]" );
        }
    }
}

/*---------------------------------------------------------------------------
 * TID 3 (HIGH PRIO): Waits on Auto-Reset Event, then locks g_pi_mtx
 *---------------------------------------------------------------------------*/
static void MT_API high_lock_waiter( void* arg ) {
    int const row = 11;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    g_start_ev.Wait( );

    while ( g_running ) {
        video_puts( row, 18, 0x19, "[WAIT EVNT]" );
        g_high_trig_ev.Wait( );
        if ( !g_running ) {
            break;
        }

        /* Contend on g_pi_mtx -> boosts TID 1 to MT_PRIO_HIGH! */
        video_puts( row, 18, 0x1C, "[WAIT MTX ]" );
        {
            MLockGuard lock( g_pi_mtx );

            ++g_pi_handoffs;
            video_puts( row, 18, 0x1A, "[GOT LOCK!]" );
            video_putu16( row, 71, 0x1A, g_pi_handoffs );
        }
    }
}

/*---------------------------------------------------------------------------
 * TID 4 (PRODUCER): Pushes bursts into 8-slot ring buffer using Semaphores
 *---------------------------------------------------------------------------*/
static void MT_API sem_producer( void* arg ) {
    int const    row      = 13;
    char         next_ch  = 'A';
    unsigned int produced = 0;
    unsigned int slot;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    g_start_ev.Wait( );

    while ( g_running ) {
        video_puts( row, 18, 0x1C, "[WAIT SEM ]" );
        g_sem_empty.Wait( );
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

        g_sem_filled.Post( );

        /* Produce in fast bursts of 4, then pause */
        if ( ( produced & 3 ) == 0 ) {
            MThread::Sleep( 10 );
        } else {
            MThread::Sleep( 2 );
        }
    }
}

/*---------------------------------------------------------------------------
 * TID 5 (CONSUMER): Pops from Semaphores & signals Condition Variable on batch
 *---------------------------------------------------------------------------*/
static void MT_API sem_consumer( void* arg ) {
    int const    row = 16;
    unsigned int slot;
    char         ch;
    (void) arg;

    video_puts( row, 18, 0x1E, "[WAIT GATE]" );
    g_start_ev.Wait( );

    while ( g_running ) {
        video_puts( row, 18, 0x19, "[WAIT ITEM]" );
        g_sem_filled.Wait( );
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

        g_sem_empty.Post( );

        /* Update batch state under g_batch_mtx and signal CondVar every 8 items */
        {
            MLockGuard lock( g_batch_mtx );
            ++g_items_consumed;
            video_putu16( row, 71, 0x1E, g_items_consumed );
            if ( ( g_items_consumed & 7 ) == 0 ) {
                ++g_batches_ready;
                g_batch_cv.Signal( );
            }
        }

        MThread::Sleep( 5 );
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

    /* Start scheduler at 72.8 Hz (4x stock PIT rate) */
    MThreadManager* mgr = MThreadManager::GetInstance( );
    mgr->Initialize( MT_TICK_73HZ, MT_PRIO_LOW );

    /* Construct and spawn threads for each demo participant */
    MThread t_low(  low_lock_holder,  NULL, NULL, 1024, MT_PRIO_LOW    );
    MThread t_med(  med_cpu_hog,      NULL, NULL, 1024, MT_PRIO_NORMAL );
    MThread t_high( high_lock_waiter, NULL, NULL, 1024, MT_PRIO_HIGH   );
    MThread t_prod( sem_producer,     NULL, NULL, 1024, MT_PRIO_NORMAL );
    MThread t_cons( sem_consumer,     NULL, NULL, 1024, MT_PRIO_NORMAL );

    t_low.Create( );
    t_med.Create( );
    t_high.Create( );
    t_prod.Create( );
    t_cons.Create( );

    /* Countdown before firing the manual-reset starting gun event */
    for ( countdown = 3; countdown > 0; --countdown ) {
        video_putu16( 21, 53, 0x1C, countdown );
        MThread::Sleep( 72 );
    }

    video_puts( 21, 15, 0x1A, "Manual-reset event FIRED! Press 'Q' or Esc to quit" );
    video_gotoxy( 21, 65 );
    g_start_ev.Set( );

    /* Main loop: wait on Condition Variable for batches while checking keyboard */
    while ( g_running ) {
        if ( MThreadManager::BreakRequested( ) ) {
            g_running = 0;
            break;
        }

        if ( _kbhit( ) ) {
            int const ch = _getch( );
            if ( ( ch == 27 ) || ( ch == 'q' ) || ( ch == 'Q' ) ) {
                g_running = 0;
                break;
            }
        }

        {
            MLockGuard lock( g_batch_mtx );
            while ( ( g_batches_ready == last_batch_seen ) && g_running ) {
                /* Check keyboard or break periodically or wait for CondVar signal */
                if ( _kbhit( ) || MThreadManager::BreakRequested( ) ) {
                    break;
                }
                g_batch_cv.Wait( g_batch_mtx );
            }
            last_batch_seen = g_batches_ready;
        }

        video_putu16( 18, 71, 0x1A, last_batch_seen );
    }

    /* Wake any blocked threads so they observe g_running == 0 and exit cleanly */
    g_high_trig_ev.Set( );
    g_sem_empty.Post( );
    g_sem_filled.Post( );
    g_batch_cv.Broadcast( );

    t_low.Join( );
    t_med.Join( );
    t_high.Join( );
    t_prod.Join( );
    t_cons.Join( );

    mgr->Shutdown( );
    mgr->Release( );

    video_gotoxy( 22, 0 );
    printf( "\rCppSync complete: %u priority-inherited handoffs, %u CondVar batches.\n", g_pi_handoffs, last_batch_seen );
    return 0;
}
