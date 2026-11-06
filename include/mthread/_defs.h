/*===========================================================================
* _defs.h - MTHREAD definitions
*===========================================================================*/

#if !defined __DEFS_H_
#define __DEFS_H_

#if !defined MT_API
#   define MT_API __cdecl
#endif

#define MT_MAX_THREADS      8

#define MT_NO_THREAD        0xFF

#define MT_ERR_NONE         0x00
#define MT_ERR_NO_SLOTS     0xFE
#define MT_ERR_NO_MEMORY    0xFD
#define MT_ERR_INVALID      0xFC

typedef unsigned char tid_t;

#endif /* __DEFS_H_ */
