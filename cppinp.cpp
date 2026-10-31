//===========================================================================
// cppinp.cpp - C++ Interrupt-driven keyboard (INT 09h) & mouse (INT 33h) demo
// Demonstrates:
//   1. Shared auto-reset MEvent signaled from ISRs via event_set_isr()
//   2. Zero-polling high-priority MThread input worker (0% CPU while idle)
//   3. RAII MKeyboard wrapper & BIOS modifier inspection (kbd.asm)
//   4. RAII MMouse wrapper, coalesced event queue & UI buttons (mouse.asm)
//===========================================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mthrwrap.h"
#include "video.h"
#include "input.h"

#define TYPE_BUF_LEN 32

static volatile int           g_running       = 1;
static MEvent                 g_input_ev;

static volatile unsigned int  g_input_wakes   = 0;
static volatile unsigned int  g_kbd_events    = 0;
static volatile unsigned int  g_mouse_events  = 0;
static volatile unsigned long g_compute_iters = 0;
static volatile unsigned long g_spinner_iters = 0;

static char                   g_type_buf[TYPE_BUF_LEN + 1];
static unsigned int           g_type_len      = 0;

static const char             g_spinner[]     = "|/-\\";

//---------------------------------------------------------------------------
// Clear the 32-character interactive typeahead buffer on row 11
//---------------------------------------------------------------------------
static void clear_type_buffer( void ) {
    g_type_len = 0;
    g_type_buf[0] = '\0';
    video_puts( 11, 31, 0x1F, "                                " );
    video_putc( 11, 31, 0x9F, '_' );
}

//---------------------------------------------------------------------------
// Append a printable character (or handle Backspace) in the typeahead buffer
//---------------------------------------------------------------------------
static void update_type_buffer( unsigned char ascii ) {
    if ( ascii == 8 ) { // Backspace
        if ( g_type_len > 0 ) {
            video_putc( 11, 31 + (int) g_type_len, 0x1F, ' ' );
            --g_type_len;
            g_type_buf[g_type_len] = '\0';
            video_putc( 11, 31 + (int) g_type_len, 0x9F, '_' );
        }
        return;
    }

    if ( ( ascii >= 32 ) && ( ascii <= 126 ) ) {
        if ( g_type_len >= TYPE_BUF_LEN ) {
            clear_type_buffer( );
        }
        g_type_buf[g_type_len] = (char) ascii;
        video_putc( 11, 31 + (int) g_type_len, 0x1E, ascii );
        ++g_type_len;
        g_type_buf[g_type_len] = '\0';
        if ( g_type_len < TYPE_BUF_LEN ) {
            video_putc( 11, 31 + (int) g_type_len, 0x9F, '_' );
        }
    }
}

//---------------------------------------------------------------------------
// Render live BIOS keyboard modifier flags from 0040:0017h onto row 10
//---------------------------------------------------------------------------
static void render_modifiers( unsigned char mods ) {
    video_puts( 10, 30, (unsigned char) ( ( mods & KBD_MOD_LSHIFT     ) ? 0x2F : 0x19 ), "[LSH]" );
    video_puts( 10, 36, (unsigned char) ( ( mods & KBD_MOD_RSHIFT     ) ? 0x2F : 0x19 ), "[RSH]" );
    video_puts( 10, 42, (unsigned char) ( ( mods & KBD_MOD_CTRL       ) ? 0x2F : 0x19 ), "[CTL]" );
    video_puts( 10, 48, (unsigned char) ( ( mods & KBD_MOD_ALT        ) ? 0x2F : 0x19 ), "[ALT]" );
    video_puts( 10, 54, (unsigned char) ( ( mods & KBD_MOD_CAPSLOCK   ) ? 0x3F : 0x19 ), "[CAP]" );
    video_puts( 10, 60, (unsigned char) ( ( mods & KBD_MOD_NUMLOCK    ) ? 0x3F : 0x19 ), "[NUM]" );
    video_puts( 10, 66, (unsigned char) ( ( mods & KBD_MOD_SCROLLLOCK ) ? 0x3F : 0x19 ), "[SCR]" );
    video_puts( 10, 72, (unsigned char) ( ( mods & KBD_MOD_INSERT     ) ? 0x3F : 0x19 ), "[INS]" );
}

//---------------------------------------------------------------------------
// Render a mouse_event_t snapshot onto row 13
//---------------------------------------------------------------------------
static void render_mouse_telemetry( const mouse_event_t& mev ) {
    const char*   label = "[READY   ]";
    unsigned char attr  = 0x1A;

    if ( mev.event_flags & MOUSE_EVT_LBUTTON_DOWN ) {
        label = "[L-DOWN  ]";
        attr  = 0x2F;
    } else if ( mev.event_flags & MOUSE_EVT_LBUTTON_UP ) {
        label = "[L-UP    ]";
        attr  = 0x1B;
    } else if ( mev.event_flags & MOUSE_EVT_RBUTTON_DOWN ) {
        label = "[R-DOWN  ]";
        attr  = 0x4F;
    } else if ( mev.event_flags & MOUSE_EVT_RBUTTON_UP ) {
        label = "[R-UP    ]";
        attr  = 0x1B;
    } else if ( mev.event_flags & MOUSE_EVT_MBUTTON_DOWN ) {
        label = "[M-DOWN  ]";
        attr  = 0x5F;
    } else if ( mev.event_flags & MOUSE_EVT_MBUTTON_UP ) {
        label = "[M-UP    ]";
        attr  = 0x1B;
    } else if ( mev.event_flags & MOUSE_EVT_MOVE ) {
        label = "[MOVING  ]";
        attr  = 0x1E;
    }

    video_puts(  13, 18, attr, label );
    video_putu8( 13, 35, 0x1E, mev.col );
    video_putu8( 13, 44, 0x1E, mev.row );

    video_puts( 13, 55, (unsigned char) ( ( mev.buttons & MOUSE_BTN_LEFT   ) ? 0x2F : 0x19 ), "[L]" );
    video_puts( 13, 59, (unsigned char) ( ( mev.buttons & MOUSE_BTN_MIDDLE ) ? 0x2F : 0x19 ), "[M]" );
    video_puts( 13, 63, (unsigned char) ( ( mev.buttons & MOUSE_BTN_RIGHT  ) ? 0x2F : 0x19 ), "[R]" );
}

//---------------------------------------------------------------------------
// TID 1 (HIGH PRIO): Event-driven input processor (INT 09h + INT 33h)
//---------------------------------------------------------------------------
static void MT_API input_worker( void* arg ) {
    mouse_event_t mev;
    unsigned int  key;
    unsigned char ascii;
    unsigned char scan;
    (void) arg;

    while ( g_running ) {
        video_puts( 7, 18, 0x19, "[WAIT IRQ]" );

        // Sleep on shared event until INT 09h or INT 33h calls event_set_isr()
        g_input_ev.Wait( );
        if ( !g_running || MThreadManager::IsBreakRequested( ) ) {
            g_running = 0;
            break;
        }

        ++g_input_wakes;
        video_puts(   7, 18, 0x1A, "[ACTIVE  ]" );
        video_putu16( 7, 71, 0x1A, g_input_wakes );

        // 1. Drain all queued keystrokes from the BIOS buffer
        while ( MKeyboard::IsAvailable( ) ) {
            key   = MKeyboard::GetKey( );
            ascii = (unsigned char) ( key & 0x00FF );
            scan  = (unsigned char) ( ( key >> 8 ) & 0x00FF );

            ++g_kbd_events;
            video_puts(    9, 18, 0x1E, "[KEYPRESS]" );
            video_putc(    9, 36, 0x1F, (unsigned char) ( ( ascii >= 32 && ascii <= 126 ) ? ascii : ' ' ) );
            video_puthex8( 9, 49, 0x1E, ascii );
            video_puthex8( 9, 61, 0x1E, scan );
            video_putu16(  9, 71, 0x1E, g_kbd_events );

            // Esc quits; Backspace or printable chars update typeahead buffer
            if ( ascii == 27 ) {
                g_running = 0;
                break;
            }

            update_type_buffer( ascii );
        }

        // Always refresh modifier flags (Shift/Ctrl/Alt/Caps/Num/Scroll/Ins)
        render_modifiers( MKeyboard::GetModifiers( ) );

        // 2. Drain all queued mouse events from the 16-slot coalescing queue
        while ( MMouse::GetEvent( mev ) ) {
            ++g_mouse_events;
            render_mouse_telemetry( mev );
            video_putu16( 13, 71, 0x1E, g_mouse_events );

            // Hit-test clickable buttons on Row 14 when Left Button is pressed
            if ( ( mev.event_flags & MOUSE_EVT_LBUTTON_DOWN ) && ( mev.row == 14 ) ) {
                if ( ( mev.col >= 30 ) && ( mev.col <= 43 ) ) {
                    // Clicked [ CLEAR TEXT ]
                    MMouse::Hide( );
                    clear_type_buffer( );
                    MMouse::Show( );
                } else if ( ( mev.col >= 47 ) && ( mev.col <= 60 ) ) {
                    // Clicked [ QUIT (Esc) ]
                    g_running = 0;
                    break;
                }
            }
        }
    }
}

//---------------------------------------------------------------------------
// TID 2 (NORMAL PRIO): Preemptive background compute bar
//---------------------------------------------------------------------------
static void MT_API compute_worker( void* arg ) {
    int const             row     = 16;
    unsigned int          bar_pos = 31;
    volatile unsigned int burn;
    (void) arg;

    while ( g_running ) {
        for ( burn = 0; burn < 6000; ++burn ) {
            if ( ( burn & 0x00FF ) == 0 ) {
                ++g_compute_iters;
            }
        }

        bar_pos = ( bar_pos + 1 ) & 31;
        if ( bar_pos == 0 ) {
            video_puts( row, 31, 0x2F, "                                " );
        }
        video_putc( row, 31 + (int) bar_pos, 0x2F, (unsigned char) ( ( bar_pos & 1 ) ? 0xFE : 0xDB ) );
        video_putu32( row, 66, 0x1E, g_compute_iters );

        if ( ( bar_pos & 7 ) == 0 ) {
            video_puts( row, 18, 0x19, "[SLEEPING]" );
            MThread::Sleep( 8 );
            video_puts( row, 18, 0x1A, "[RUNNING ]" );
        }
    }
}

//---------------------------------------------------------------------------
// TID 3 (LOW PRIO): Background CPU spinner (runs when TID 1 & 2 are idle)
//---------------------------------------------------------------------------
static void MT_API low_prio_spinner( void* arg ) {
    int const             row = 18;
    unsigned int          idx = 0;
    volatile unsigned int burn;
    (void) arg;

    while ( g_running ) {
        for ( burn = 0; burn < 4000; ++burn ) {
            // Preempted by timer or hardware IRQs
        }

        ++g_spinner_iters;
        idx = ( idx + 1 ) & 3;
        video_putc( row, 51, 0x1D, (unsigned char) g_spinner[idx] );
        video_putu32( row, 66, 0x1E, g_spinner_iters );
    }
}

//---------------------------------------------------------------------------
// Main Thread (TID 0): Initializes UI, MKeyboard/MMouse, and MThreads
//---------------------------------------------------------------------------
int main( void ) {
    mouse_event_t init_mouse;

    video_init( );
    video_clear( 0x17 );
#if defined RULER
    video_ruler( 1, 3 );
#endif
    video_puts(    1,  7, 0x1F,    "\xCD\xCD\xCD MS-DOS 8086 C++ INTERRUPT-DRIVEN INPUT SHOWCASE (CPPINP) \xCD\xCD\xCD" );
    video_puts(    3,  4, 0x17, "Shared Auto-Reset MEvent | MKeyboard (INT 09h) | MMouse (INT 33h)"           );
    video_puts(    5,  4, 0x1B, "Subsystem     State       Telemetry                                Count"    );
    video_puts(    6,  4, 0x17, "\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4\xC4" );
    video_puts(    7,  4, 0x1F, "TID 1 (HIGH)  [WAIT IRQ]  Input worker (0% CPU while blocked)"               );
    video_puts(    9,  4, 0x1F, "KEYBOARD      [READY   ]  Key: [ ]  ASCII: 0x00  Scan: 0x00"                 );
    video_puts(   10,  4, 0x17, "              Modifiers:  [LSH] [RSH] [CTL] [ALT] [CAP] [NUM] [SCR] [INS]"   );
    video_puts(   11,  4, 0x17, "              Typeahead:  [                                ]"                );
    video_puts(   13,  4, 0x1F, "MOUSE         [NO MOUSE]  Col:  0, Row:  0   Btns: [L] [M] [R]"              );
    video_puts(   14, 18, 0x17,               "Controls:"                                                     );
    video_puts(   14, 30, 0x30,                           "[ CLEAR TEXT ]"                                    );
    video_puts(   14, 47, 0x4F,                                            "[ QUIT (Esc) ]"                   );
    video_puts(   16,  4, 0x1F, "TID 2 (NORM)  [RUNNING ]  [                                ]"                );
    video_puts(   16, 31, 0x2F,                            "                                "                 );
    video_puts(   18,  4, 0x17, "TID 3 (LOW )  [PREEMPT ]  Background spinner: [ ]"                           );
    video_puts(   21,  6, 0x1A,   "Type text, move/click the mouse, or press Esc or click QUIT to exit."      );
    video_putu16(  7, 71, 0x1A, 0 );
    video_putu16(  9, 71, 0x1E, 0 );
    video_putu16( 13, 71, 0x1E, 0 );
    video_putu32( 16, 66, 0x1E, 0 );
    video_putu32( 18, 66, 0x1E, 0 );

    clear_type_buffer( );
    render_modifiers( MKeyboard::GetModifiers( ) );

    // Park hardware text cursor off-screen on row 24
    video_gotoxy( 24, 79 );

    // 1. Start scheduler at 72.8 Hz (4x stock PIT rate)
    MThreadManager* mgr = MThreadManager::GetInstance( );
    mgr->Initialize( MT_TICK_73HZ, MT_PRIO_LOW );

    // 2. Construct worker MThreads
    MThread t_inp( input_worker,     NULL, NULL, 1024, MT_PRIO_HIGH   );
    MThread t_cmp( compute_worker,   NULL, NULL, 1024, MT_PRIO_NORMAL );
    MThread t_spn( low_prio_spinner, NULL, NULL, 1024, MT_PRIO_LOW    );

    {
        // 3. Hook INT 09h keyboard ISR and INT 33h mouse callback via RAII
        MKeyboard kbd( g_input_ev );
        MMouse    mouse( g_input_ev, MOUSE_EVT_ALL );

        if ( mouse.IsInstalled( ) ) {
            MMouse::GetState( init_mouse );
            render_mouse_telemetry( init_mouse );
            MMouse::Show( );
        }

        // 4. Spawn high-priority input thread & background compute threads
        t_inp.Create( );
        t_cmp.Create( );
        t_spn.Create( );

        // Main thread sleeps periodically while checking for Ctrl+Break and QUIT
        while ( g_running ) {
            if ( MThreadManager::IsBreakRequested( ) ) {
                g_running = 0;
                g_input_ev.Set( );
                break;
            }
            MThread::Sleep( 18 );
        }
    } // ~MMouse() and ~MKeyboard() automatically unhook INT 33h and INT 09h here

    // Ensure input thread wakes if it was blocked on g_input_ev
    g_input_ev.Set( );

    t_inp.Join( );
    t_cmp.Join( );
    t_spn.Join( );

    mgr->Shutdown( );
    mgr->Release( );

    video_gotoxy( 22, 0 );
    printf( "\rCppInp complete: %u IRQ wakeups (%u keyboard, %u mouse events).\n", g_input_wakes, g_kbd_events, g_mouse_events );
    return 0;
}
