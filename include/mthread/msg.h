/*===========================================================================
 * msg.h - Message queue for MTHREAD
 *===========================================================================*/

#if !defined _MMSG_H_
#define _MMSG_H_

#include <mthread/_defs.h>

#if defined __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Message queue API
 *===========================================================================*/

/* Standard message IDs */
#define MT_MSG_QUIT       0x0001
#define MT_MSG_KEY        0x0002
#define MT_MSG_MOUSE      0x0003
#define MT_MSG_MOUSE_MOVE 0x0004
#define MT_MSG_USER       0x0400

/* Delivery flags */
#define MT_MSG_FLG_NONE   0x00
#define MT_MSG_FLG_SYNC   0x01 /* Requires msg_reply() */

/* PeekMessage actions */
#define MT_PM_NOREMOVE    0x00
#define MT_PM_REMOVE      0x01

/* 10-byte message packet */
typedef struct {
    unsigned int  message;
    unsigned int  wParam;
    unsigned long lParam;
    tid_t         sender;
    unsigned char flags;
} mt_msg_t;

/* Asynchronous (non-blocking) delivery */
int           MT_API msg_post( tid_t target_tid, unsigned int msg, unsigned int wParam, unsigned long lParam );
int           MT_API msg_post_isr( tid_t target_tid, unsigned int msg, unsigned int wParam, unsigned long lParam );
void          MT_API msg_post_quit( tid_t target_tid, unsigned int exit_code );

/* Synchronous (blocking) delivery */
unsigned long MT_API msg_send( tid_t target_tid, unsigned int msg, unsigned int wParam, unsigned long lParam );
void          MT_API msg_reply( unsigned long result );

/* Retrieval */
int           MT_API msg_get( mt_msg_t* out_msg );
int           MT_API msg_peek( mt_msg_t* out_msg, unsigned char remove_flag );

#if defined __cplusplus
}
#endif

#endif /* _MMSG_H_ */
