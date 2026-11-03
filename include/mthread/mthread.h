/*===========================================================================
 * mthread.h - Preemptive Multithreading Library for MS-DOS (8086 / MASM 6.11)
 *===========================================================================*/

#if !defined _MTHREAD_H_
#define _MTHREAD_H_

#include <stddef.h>
#include <stdlib.h>

#if defined __cplusplus
extern "C" {
#endif

#if !defined MT_API
#   define MT_API __cdecl
#endif

/* Thread ID and return codes */
typedef unsigned char tid_t;

#define MT_MAX_THREADS      8
#define MT_NO_THREAD        0xFF
#define MT_ERR_NONE         0x00
#define MT_ERR_NO_SLOTS     0xFE
#define MT_ERR_NO_MEMORY    0xFD
#define MT_ERR_INVALID      0xFC

#include <mthread/thread.h>
#include <mthread/synchron.h>
#include <mthread/atomic.h>

#if defined __cplusplus
}
#endif

#endif /* _MTHREAD_H_ */
