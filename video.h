#ifndef _VIDEO_H_
#define _VIDEO_H_


#ifdef __cplusplus
extern "C" {
#endif

#if !defined MT_API
#   if defined __WATCOMC__
#       define MT_API __cdecl
#   else
#       define MT_API _cdecl
#   endif
#endif

void MT_API video_init( void );
void MT_API vram_clear( void );
void MT_API vram_putc( int const row, int const col, unsigned char const attr, unsigned char const c );
void MT_API vram_puts( int const row, int const col, unsigned char const attr, const char* s );
void MT_API vram_putu16( int const row, int const col, unsigned char const attr, unsigned int val );
void MT_API vram_putu32( int const row, int const col, unsigned char const attr, unsigned long val );
void MT_API video_gotoxy(unsigned char const row, unsigned char const col);

#ifdef __cplusplus
}
#endif

#endif // _VIDEO_H_
