/*===========================================================================
 * isr.h - ISR management for mthread library
 *===========================================================================*/

#if !defined _ISR_H_
#define _ISR_H_

#include <mthread/synchron.h>

#if defined __cplusplus
extern "C" {
#endif

/*===========================================================================
 * ISR management API
 *===========================================================================*/

/* ISR entry/exit for incrementing/decrementing bios_lock from external
 * hardware ISRs & callbacks
 */
void MT_API mt_enter_isr( void );
void MT_API mt_leave_isr( void );

/* ISR-safe event_set() for signaling events from ISRs & callbacks */
void MT_API event_set_isr( mt_event_t* ev );

#if defined __cplusplus
}
#endif

#endif /* _ISR_H_ */
