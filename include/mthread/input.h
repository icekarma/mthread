/*===========================================================================
 * input.h - Interrupt-Driven Keyboard (INT 09h) & Callback-Driven Mouse (INT 33h) Module
 *===========================================================================*/

#if !defined _INPUT_H_
#define _INPUT_H_

#include <mthread/mthread.h>

#include <mthread/kbd.h>
#include <mthread/mouse.h>

#if defined __cplusplus

#include <mthread/mkbd.h>
#include <mthread/mmouse.h>

#endif /* defined __cplusplus */

#endif /* !defined _INPUT_H_ */
