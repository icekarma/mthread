#include "video.h"

unsigned int  far* const bios_screen_cols = ( unsigned int  far* ) 0x0000044AL;
unsigned char far* const bios_screen_rows = ( unsigned char far* ) 0x00000484L;
unsigned int  far* const vram             = ( unsigned int  far* ) 0xB8000000L;

unsigned int             screen_cols;
unsigned int             screen_rows;

unsigned int             row_offset       = 0;
unsigned int             col_offset       = 0;

void MT_API video_init( void ) {
    screen_cols = ( *bios_screen_cols == 0 ) ? 80 :   *bios_screen_cols;
    screen_rows = ( *bios_screen_rows == 0 ) ? 25 : ( *bios_screen_rows + 1 );

#if defined OFFSET_DISPLAY
    col_offset  = ( screen_cols > 80 ) ? ( ( screen_cols - 80 ) / 2 ) : 0;
    row_offset  = ( screen_rows > 25 ) ? ( ( screen_rows - 25 ) / 2 ) : 0;
#endif // defined OFFSET_DISPLAY
}

void MT_API video_clear( void ) {
    unsigned int i, limit;
    for ( i = 0, limit = screen_cols * screen_rows; i < limit; ++i ) {
        vram[i] = 0x0720;
    }
}

void MT_API video_putc( int const row, int const col, unsigned char const attr, unsigned char const c ) {
    vram[( row + row_offset ) * screen_cols + ( col + col_offset )] = (unsigned int) ( attr << 8 ) | (unsigned char) c;
}

void MT_API video_puts( int const row, int const col, unsigned char const attr, const char* s ) {
    unsigned int offset = (unsigned int) ( ( row + row_offset ) * screen_cols + ( col + col_offset ) );
    unsigned int hi     = (unsigned int) attr << 8;
    while ( *s ) {
        vram[offset++] = hi | (unsigned char) ( *s++ );
    }
}

void MT_API video_putu16( int const row, int const col, unsigned char const attr, unsigned int val ) {
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

void MT_API video_putu32( int const row, int const col, unsigned char const attr, unsigned long val ) {
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
