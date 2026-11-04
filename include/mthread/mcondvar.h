/*===========================================================================
 * mcondvar.h - Definitions for C++ interface for mthread's condition variable synchronization primitive
 *===========================================================================*/

#if !defined _MCONDVAR_H_
#define _MCONDVAR_H_

#if !defined __cplusplus
#error "mcondvar.h is a C++ header. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/synchron.h>
#include <mthread/mmutex.h>

//===========================================================================
// Class MCondVar - C++ interface for mthread's condition variable synchronization primitive
//===========================================================================

class MCondVar {

    MCondVar( MCondVar const& other );
    MCondVar& operator=( MCondVar const& other );

public:

    MCondVar( ) {
        cond_init( &cv );
    }

    void Wait( MMutex& mutex ) {
        cond_wait( &cv, &mutex.mtx );
    }

    void Signal( ) {
        cond_signal( &cv );
    }

    void Broadcast( ) {
        cond_broadcast( &cv );
    }

private:

    mt_cond_t cv;

};

#endif /* !defined _MCONDVAR_H_ */
