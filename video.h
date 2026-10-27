#if !defined _VIDEO_H_
#define _VIDEO_H_

#if defined __cplusplus
extern "C" {
#endif

#if !defined MT_API
#   define MT_API __cdecl
#endif

void MT_API video_init( void );

void MT_API video_clear( unsigned char const attr );
void MT_API video_putc( int const row, int const col, unsigned char const attr, unsigned char const c );
void MT_API video_puts( int const row, int const col, unsigned char const attr, const char* s );
void MT_API video_putu16( int const row, int const col, unsigned char const attr, unsigned int const val );
void MT_API video_putu32( int const row, int const col, unsigned char const attr, unsigned long const val );
void MT_API video_ruler( void );
void MT_API video_gotoxy( unsigned char const row, unsigned char const col );

#if defined __cplusplus
}
#endif

#endif /* !defined _VIDEO_H_ */
