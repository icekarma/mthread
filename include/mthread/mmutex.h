/*===========================================================================
 * mmutex.h - Definitions for C++ interface for mthread's mutex synchronization primitive
 *===========================================================================*/

#if !defined _MMUTEX_H_
#define _MMUTEX_H_

#if !defined __cplusplus
#error "mmutex.h is a C++ header. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/synchron.h>

//===========================================================================
// Class MMutex - C++ interface for mthread's mutex synchronization primitive
//===========================================================================

class MMutex {

    friend class MCondVar;

    MMutex( MMutex const& other );
    MMutex& operator=( MMutex const& other );

public:

    MMutex( ) {
        mutex_init( &mtx );
    }

    void Lock( ) {
        mutex_lock( &mtx );
    }

    int TryLock( ) {
        return mutex_trylock( &mtx );
    }

    void Unlock( ) {
        mutex_unlock( &mtx );
    }

private:

    mt_mutex_t mtx;

};

#endif /* !defined _MMUTEX_H_ */
