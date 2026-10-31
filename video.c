#include "video.h"

/*===========================================================================*
 * Constants
 *===========================================================================*/

static unsigned int  far*              const bios_screen_cols     = (unsigned int  far*) 0x0000044AL;
static unsigned int  far*              const bios_cur_page_offset = (unsigned int  far*) 0x0000044EL;
static unsigned char far*              const bios_screen_rows     = (unsigned char far*) 0x00000484L;
static __segment                       const vram_seg             = 0xB800;

static char                            const hex_digits[]         = "0123456789ABCDEF";

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
    vram[get_offset( row, col )] = ( (unsigned int) attr << 8u ) | (unsigned int) c;
}

/*---------------------------------------------------------------------------*
* video_puthex8() - Write unsigned 8-bit integer to screen in hex at specified row/col
*---------------------------------------------------------------------------*/
void MT_API video_puthex8( int const row, int const col, unsigned char const attr, unsigned char const val ) {
    video_putc( row, col,     attr, (unsigned char) hex_digits[( val >> 4 ) & 0x0F] );
    video_putc( row, col + 1, attr, (unsigned char) hex_digits[  val        & 0x0F] );
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
* video_putu8() - Write unsigned 8-bit integer to screen at specified row/col
*---------------------------------------------------------------------------*/
void MT_API video_putu8( int const row, int const col, unsigned char const attr, unsigned char const val ) {
    video_putc( row, col,     attr, (unsigned char) ( ( val >= 10 ) ? ( '0' + ( val / 10 ) ) : ' ' ) );
    video_putc( row, col + 1, attr, (unsigned char) ( '0' + ( val % 10 ) ) );
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
void MT_API video_ruler( unsigned char horiz_bkgr_attr, unsigned char vert_bkgr_attr ) {
    unsigned char const h          = horiz_bkgr_attr & 0x07;
    unsigned char const v          =  vert_bkgr_attr & 0x07;
    unsigned char const horiz_attr = (unsigned char) ( ( h << 4 ) | ( h | 8 ) );
    unsigned char const  vert_attr = (unsigned char) ( ( v << 4 ) | ( v | 8 ) );
    int row;
    int col;

    for ( row = 0; row < 25; ++row ) {
        for ( col = 0; col < 80; col += 10 ) {
            video_puts( row, col, horiz_attr, "0123456789" );
        }

        video_putc( row, 0, vert_attr, (unsigned char) ( ( row / 10 ) + '0' ) );
        video_putc( row, 1, vert_attr, (unsigned char) ( ( row % 10 ) + '0' ) );
    }
}
