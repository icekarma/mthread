#include "video.h"

/*===========================================================================*
 * Constants
 *===========================================================================*/

static unsigned int  far*              const bios_screen_cols     = (unsigned int  far*) 0x0000044AL;
static unsigned int  far*              const bios_cur_page_offset = (unsigned int  far*) 0x0000044EL;
static unsigned char far*              const bios_screen_rows     = (unsigned char far*) 0x00000484L;
static __segment                       const vram_seg             = 0xB800;

/*===========================================================================*
* Private variables
*===========================================================================*/

static unsigned int __based(vram_seg)*       vram;

static unsigned int                          screen_cols;
static unsigned int                          screen_rows;

/*===========================================================================*
* Semiprivate variables
*===========================================================================*/

unsigned int                                 row_offset;
unsigned int                                 col_offset;

/*===========================================================================*
* Private functions
*===========================================================================*/

static unsigned int MT_API get_offset( unsigned int const row, unsigned int const col ) {
    return ( row + row_offset ) * screen_cols + ( col + col_offset );
}

/*===========================================================================*
* Global functions
*===========================================================================*/

/*---------------------------------------------------------------------------*
 * video_init() - Video subsystem initialization
 *---------------------------------------------------------------------------*/
void MT_API video_init( void ) {
    screen_cols = ( *bios_screen_cols == 0u ) ? 80u :   *bios_screen_cols;
    screen_rows = ( *bios_screen_rows == 0u ) ? 25u : ( *bios_screen_rows + 1u );

#if defined OFFSET_DISPLAY
    col_offset  = ( screen_cols > 80u ) ? ( ( screen_cols - 80u ) / 2u ) : 0u;
    row_offset  = ( screen_rows > 25u ) ? ( ( screen_rows - 25u ) / 2u ) : 0u;
#endif

    vram = (unsigned int __based(vram_seg)*) *bios_cur_page_offset;
}

/*---------------------------------------------------------------------------*
 * video_clear() - Clear screen to specified attribute
 *---------------------------------------------------------------------------*/
void MT_API video_clear( unsigned char const attr ) {
    unsigned int const value = ( ( (unsigned int) attr << 8u ) | (unsigned int) ' ' );
    unsigned int const limit = screen_cols * screen_rows;
    unsigned int i;

    for ( i = 0; i < limit; ++i ) {
        vram[i] = value;
    }
}

/*---------------------------------------------------------------------------*
 * video_putc() - Write character to screen at specified row/col with attribute
 *---------------------------------------------------------------------------*/
void MT_API video_putc( int const row, int const col, unsigned char const attr, unsigned char const c ) {
    vram[get_offset( row, col )] = (unsigned int) ( attr << 8 ) | (unsigned char) c;
}

/*---------------------------------------------------------------------------*
 * video_puts() - Write null-terminated string to screen at specified row/col
 *---------------------------------------------------------------------------*/
void MT_API video_puts( int const row, int const col, unsigned char const attr, const char* s ) {
    unsigned int       offset = get_offset( row, col );
    unsigned int const hi     = (unsigned int) attr << 8u;

    while ( *s ) {
        vram[offset++] = hi | (unsigned char) *s++;
    }
}

/*---------------------------------------------------------------------------*
 * video_putu16() - Write unsigned 16-bit integer to screen at specified row/col
 *---------------------------------------------------------------------------*/
void MT_API video_putu16( int const row, int const col, unsigned char const attr, unsigned int const val_ ) {
    unsigned int val = val_;
    char buf[6];
    int  i;

    for ( i = 4; i >= 0; --i ) {
        buf[i] = (char) ( '0' + ( val % 10 ) );
        val /= 10;
    }
    buf[5] = '\0';

    i = 0;
    while ( ( i < 4 ) && ( buf[i] == '0' ) ) {
        buf[i++] = ' ';
    }

    video_puts( row, col, attr, buf );
}

/*---------------------------------------------------------------------------*
 * video_putu32() - Write unsigned 32-bit integer to screen at specified row/col
 *---------------------------------------------------------------------------*/
void MT_API video_putu32( int const row, int const col, unsigned char const attr, unsigned long const val_ ) {
    unsigned long val = val_;
    char buf[11];
    int i;

    for ( i = 9; i >= 0; --i ) {
        buf[i] = (char) ( '0' + ( val % 10 ) );
        val /= 10;
    }
    buf[10] = '\0';

    i = 0;
    while ( ( i < 9 ) && ( buf[i] == '0' ) ) {
        buf[i++] = ' ';
    }

    video_puts( row, col, attr, buf );
}

/*---------------------------------------------------------------------------*
 * video_ruler() - Draw a ruler on the screen
 *---------------------------------------------------------------------------*/
void MT_API video_ruler( void ) {
    int row;
    int col;

    for ( row = 0; row < 25; ++row ) {
        for ( col = 0; col < 80; col += 10 ) {
            video_puts( row, col, 0x4C, "0123456789" );
        }

        video_putc( row, 0, 0x5D, (unsigned char) ( ( row / 10 ) + '0' ) );
        video_putc( row, 1, 0x5D, (unsigned char) ( ( row % 10 ) + '0' ) );
    }
}
