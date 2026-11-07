//=============================================================================
// cppfpu.cpp - C++ FPU Context Switching Demonstration
//=============================================================================

#include <mthread/mthread.h>
#include <mthread/video.h>

#include <conio.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

//=============================================================================
// Constants
//=============================================================================

#define NUM_FPU_THREADS 3

//=============================================================================
// Type declarations
//=============================================================================

typedef struct {
    int           row;
    double        speed;
    double        phase;
    unsigned char color;
} fpu_ctx_t;

//=============================================================================
// Global variables
//=============================================================================

volatile int g_quit = 0;

//=============================================================================
// Functions
//=============================================================================

void MT_API FpuWorkerFunc( void* arg ) {
    fpu_ctx_t* ctx      = (fpu_ctx_t*) arg;
    double     t        =  0;
    int        last_col = 40;

    // Explicitly opt into 94-byte FPU context switching
    MThread::EnableFpu( );

    while ( !g_quit && !MThreadManager::IsBreakRequested( ) ) {
        double val = sin( t * ctx->speed + ctx->phase );
        int    col = 40 + (int) ( 38.0 * val );

        video_putc( ctx->row, last_col, 0x17, ' ' );
        video_putc( ctx->row, col, ctx->color, 254 );

        last_col = col;
        t += 0.05;
        MThread::Sleep( 1 );
    }
}

int main( ) {
    MThreadManager* mgr = MThreadManager::GetInstance( );
    mgr->Initialize( MT_TICK_73HZ, MT_PRIO_NORMAL );

    video_init( );
    video_clear( 0x17 );
    video_gotoxy( 24, 79 );

    video_puts(  0, 0, 0x1F, " MTHREAD C++ FPU Context Switching Demo " );
    video_puts( 24, 0, 0x17, " Press any key or Ctrl+C to exit... " );

    MThread threads[NUM_FPU_THREADS];
    fpu_ctx_t ctxs[NUM_FPU_THREADS] = {
        { 10, 1.0, 0.0, 0x1A },
        { 12, 1.5, 1.0, 0x1B },
        { 14, 2.0, 2.0, 0x1C }
    };

    int i;
    for ( i = 0; i < NUM_FPU_THREADS; ++i ) {
        threads[i].Create( FpuWorkerFunc, &ctxs[i] );
    }

    while ( !g_quit && !MThreadManager::IsBreakRequested( ) ) {
        if ( _kbhit( ) ) {
            _getch( );
            break;
        }
        MThread::Yield( );
    }

    g_quit = 1;
    for ( i = 0; i < NUM_FPU_THREADS; ++i ) {
        threads[i].Join( );
    }

    putc( '\r', stdout );
    mgr->Release( );
    return 0;
}
