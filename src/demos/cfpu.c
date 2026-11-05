/*=============================================================================
 * cfpu.c - C FPU Context Switching Demonstration
 *=============================================================================*/

#include <mthread/mthread.h>
#include <mthread/video.h>

#include <conio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/*=============================================================================
 * Constants
 *=============================================================================*/

#define NUM_FPU_THREADS 3

/*=============================================================================
 * Type declarations
 *=============================================================================*/

typedef struct {
    int           row;
    double        speed;
    double        phase;
    unsigned char color;
} fpu_ctx_t;

/*=============================================================================
 * Global variables
 *=============================================================================*/

volatile int g_quit = 0;

/*=============================================================================
 * Functions
 *=============================================================================*/

void MT_API fpu_worker( void* arg ) {
    fpu_ctx_t* ctx      = (fpu_ctx_t*) arg;
    double     t        =  0;
    int        last_col = 40;

    /* Explicitly opt into 94-byte FPU context switching */
    thread_enable_fpu( 1 );

    while ( !g_quit && !threading_is_break_requested( ) ) {
        /* FPU-intensive math: animate a sine wave */
        double val = sin( t * ctx->speed + ctx->phase );
        int    col = 40 + (int) ( 38.0 * val );

        /* Update text-mode display */
        video_putc( ctx->row, last_col, 0x17, ' ' );
        video_putc( ctx->row, col, ctx->color, 254 ); /* Square block */

        last_col = col;
        t += 0.05;

        /* Sleep forces a context switch mid-math to prove FPU safety */
        thread_sleep( 1 );
    }
}

int main( void ) {
    int       i;
    tid_t     tids[NUM_FPU_THREADS];
    fpu_ctx_t ctxs[NUM_FPU_THREADS] = {
        { 10, 1.0, 0.0, 0x1A },
        { 12, 1.5, 1.0, 0x1B },
        { 14, 2.0, 2.0, 0x1C }
    };

    threading_init( MT_TICK_73HZ, MT_PRIO_NORMAL );

    video_init( );
    video_clear( 0x17 );
    video_gotoxy( 24, 79 );

    video_puts(  0, 0, 0x1F, " MTHREAD C FPU Context Switching Demo " );
    video_puts( 24, 0, 0x17, " Press any key or Ctrl+C to exit... " );

    for ( i = 0; i < NUM_FPU_THREADS; ++i ) {
        tids[i] = thread_create( fpu_worker, &ctxs[i], NULL, 0, MT_PRIO_NORMAL );
    }

    while ( !g_quit && !threading_is_break_requested( ) ) {
        if ( _kbhit( ) ) {
            _getch( );
            break;
        }
        thread_yield( );
    }

    g_quit = 1;
    for ( i = 0; i < NUM_FPU_THREADS; ++i ) {
        thread_join( tids[i] );
    }

    putc( '\r', stdout );
    threading_shutdown( );
    return 0;
}
