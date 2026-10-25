/*===========================================================================
 * demo.c - Verification Harness for MTHREAD (MSC 7.00 / MASM 6.11)
 *===========================================================================*/

#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mthread.h"
#include "video.h"

static volatile int           g_running                    = 1;
static volatile unsigned int  g_disk_writes                = 0;
static volatile unsigned long g_iter_count[MT_MAX_THREADS] = { 0, };

/* Caller-supplied static stack for Thread 3 (tests non-malloc stack path) */
static unsigned char          g_custom_stack[1024];

static const char             g_spinner[]                  = "|/-\\";

/*---------------------------------------------------------------------------
 * Recursive function to trigger MSC 7.00 __chkstk against DGROUP:STKHQQ
 *---------------------------------------------------------------------------*/
static unsigned int stack_probe_test( unsigned int depth ) {
    volatile unsigned char local_pad[32]; /* Force stack frame allocation */
    local_pad[0] = (unsigned char) depth;

    if ( depth == 0 ) {
        return local_pad[0];
    } else {
        return local_pad[0] + stack_probe_test( depth - 1 );
    }
}

/*---------------------------------------------------------------------------
 * Worker Thread (TIDs 1 & 2): Normal Priority Preemptive Compute + Sleep
 *---------------------------------------------------------------------------*/
static void MT_API compute_worker( void* arg ) {
    int                   id      = (int) arg;
    tid_t                 self    = thread_self( );
    int const             row     = 6 + id * 2;
    unsigned char const   attr    = ( id == 1 ) ? 0x1F : 0x2F;
    unsigned int          bar_pos = 0;
    volatile unsigned int burn;

    while ( g_running ) {
        /* 1. Exercise __chkstk on this thread's stack */
        stack_probe_test( 6 );

        /* 2. Do pure CPU-bound work (NO yield!) so INT 08h must preempt us */
        for ( burn = 0; burn < 8000; ++burn ) {
            if ( ( burn & 0x03FF ) == 0 ) {
                g_iter_count[self]++;
            }
        }

        /* 3. Update progress bar */
        bar_pos = ( bar_pos + 1 ) & 31;
        if ( bar_pos == 0 ) {
            video_puts( row, 31, attr, "                                " );
        }
        video_putc( row, 31 + bar_pos, attr, (unsigned char) ( ( bar_pos & 1 ) ? 0xFE : 0xDB ) );
        video_putu32( row, 66, 0x0E, g_iter_count[self] );

        /* 4. Every 32 steps, sleep briefly so MT_PRIO_LOW thread can run */
        if ( bar_pos == 0 ) {
            video_puts( row, 18, 0x08, "[SLEEPING]" );
            thread_sleep( 12 );
            video_puts( row, 18, 0x0A, "[RUNNING ]" );
        }
    }
}

/*---------------------------------------------------------------------------
 * Background Thread (TID 3): Low-Priority CPU Spinner (never sleeps!)
 * Runs ONLY when higher-priority threads are sleeping or blocked.
 *---------------------------------------------------------------------------*/
static void MT_API low_prio_spinner( void* arg ) {
    tid_t                 self = thread_self( );
    int const             row  = 12;
    unsigned int          idx  = 0;
    volatile unsigned int burn;

    while ( g_running ) {
        stack_probe_test( 6 );

        for ( burn = 0; burn < 4000; ++burn ) {
            /* Busy loop preempted by timer */
        }

        g_iter_count[self]++;
        idx = ( idx + 1 ) & 3;
        video_putc( row, 40, 0x0D, (unsigned char) g_spinner[idx] );
        video_putu32( row, 66, 0x0E, g_iter_count[self] );
    }
}

/*---------------------------------------------------------------------------
 * Disk I/O Thread (TID 4): Exercises INT 21h DiskStack (fopen/fprintf/fclose)
 *---------------------------------------------------------------------------*/
static void MT_API disk_logger_worker( void* arg ) {
    tid_t     self = thread_self( );
    int const row  = 14;
    FILE*     fp;

    while ( g_running ) {
        stack_probe_test( 4 );
        video_puts( row, 18, 0x0C, "[DISK I/O]" );

        fp = fopen( "MTTEST.LOG", "at" );
        if ( fp ) {
            fprintf( fp,
                "Log entry #%u from TID %u (t1=%lu, t2=%lu, t3=%lu)\n",
                ++g_disk_writes,
                (unsigned int) self,
                g_iter_count[1],
                g_iter_count[2],
                g_iter_count[3]
            );
            fclose( fp );
        }

        g_iter_count[self] = g_disk_writes;
        video_puts( row, 18, 0x08, "[SLEEPING]" );
        video_putu32( row, 66, 0x0E, g_iter_count[self] );

        /* Sleep ~0.5s between disk writes */
        thread_sleep( 36 );
    }
}

/*---------------------------------------------------------------------------
 * Main Thread (TID 0): Sets up UI, spawns threads, does DOS IOStack output
 *---------------------------------------------------------------------------*/
int main( void ) {
    unsigned int main_ticks = 0;
    tid_t        t1;
    tid_t        t2;
    tid_t        t3;
    tid_t        t4;
    //int          row;

    video_init( );
    video_clear( );
    //for ( row = 0; row < 25; ++row ) {
    //    int n;
    //    for ( n = 0; n < 80; n += 10 ) {
    //        video_puts( row, n, 0x4C, "0123456789" );
    //    }
    //}
    video_puts(  1, 11, 0x1F,        "=== MS-DOS 8086 PREEMPTIVE MULTITHREADING TEST HARNESS ==="        );
    video_puts(  3,  6, 0x07,   "PIT rate:  72.8 Hz (4x) | STKHQQ probes: ON | Hybrid SDA/IOStack: ON"   );
    video_puts(  5,  4, 0x0B, "Thread        State       Activity                                 Count" );
    video_puts(  6,  4, 0x07, "------------------------------------------------------------------------" );
    video_puts(  8,  4, 0x0F, "TID 1 (NORM)  [RUNNING ]  [                                ]"             );
    video_puts( 10,  4, 0x0F, "TID 2 (NORM)  [RUNNING ]  [                                ]"             );
    video_puts( 12,  4, 0x07, "TID 3 (LOW )  [PREEMPT ]  Spinner: [ ] (runs when 1&2 sleep)"             );
    video_puts( 14,  4, 0x07, "TID 4 (HIGH)  [SLEEPING]  File logger (MTTEST.LOG)"                       );
    video_puts( 17, 12, 0x0A,         "Press 'Q' or ESC to shut down cleanly and return to DOS."         );

    /* Remove previous log file if present */
    remove( "MTTEST.LOG" );

    /* Initialize scheduler at 4x stock PIT rate (72.8 Hz / 13.7 ms quantum) */
    thread_init( MT_TICK_73HZ, MT_PRIO_NORMAL );

    /* Spawn 2 Normal-priority compute threads with auto-allocated (NULL/0) stacks */
    t1 = thread_create( compute_worker, (void*) 1, NULL, 0, MT_PRIO_NORMAL );
    t2 = thread_create( compute_worker, (void*) 2, NULL, 0, MT_PRIO_NORMAL );

    /* Spawn 1 Low-priority spinner thread with caller-supplied static stack */
    t3 = thread_create( low_prio_spinner, NULL, g_custom_stack, sizeof( g_custom_stack ), MT_PRIO_LOW );

    /* Spawn 1 High-priority Disk I/O logger thread */
    t4 = thread_create( disk_logger_worker, NULL, NULL, 1536, MT_PRIO_HIGH );

    /* Main loop: exercises DOS IOStack (kbhit/getch & DOS AH=02h) */
    while ( g_running ) {
        if ( _kbhit( ) ) {
            int ch = _getch( );
            if ( ch == 27 || ch == 'q' || ch == 'Q' ) {
                g_running = 0;
                break;
            }
        }

        /* Position cursor on row 19 and print via DOS INT 21h IOStack (printf) */
        {
            char buffer[80];
            sprintf( buffer, "TID 0 (MAIN)  DOS IOStack Heartbeat #%u (Spawned TIDs: %u,%u,%u,%u)", ++main_ticks, t1, t2, t3, t4 );
            video_gotoxy( 19, (unsigned char) ( ( 80 - strlen( buffer ) ) / 2 ) );
            printf( "%s", buffer );
        }

        thread_sleep( 18 );
    }

    /* Wait for each worker thread to exit and reclaim its stack */
    thread_join( t1 );
    thread_join( t2 );
    thread_join( t3 );
    thread_join( t4 );
    thread_shutdown( );

    /* Move cursor below table before returning to COMMAND.COM */
    video_gotoxy( 20, 0 );
    printf( "\nClean shutdown complete. Disk logger wrote %u entries to MTTEST.LOG.\n", g_disk_writes );
    return 0;
}
