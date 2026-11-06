/*===========================================================================
 * atomic.h - Atomic operations for mthread library
 *===========================================================================*/

#if !defined _ATOMIC_H_
#define _ATOMIC_H_

#include <mthread/_defs.h>

#if defined __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Atomic operations
 *===========================================================================*/

int MT_API cmp_xchg( int* valueptr, int const old_val, int const new_val );

#if defined __cplusplus
}
#endif

#endif /* _ATOMIC_H_ */
