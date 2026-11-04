/*===========================================================================
 * mlkguard.h - Definitions for mthread's C++ lock guard
 *===========================================================================*/

#if !defined _MLKGUARD_H_
#define _MLKGUARD_H_

#if !defined __cplusplus
#error "mlkguard.h is a C++ header. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/mmutex.h>

//===========================================================================
// Class MLockGuard - mthread's C++ lock guard
//===========================================================================

class MLockGuard {

    MLockGuard( MLockGuard const& other );
    MLockGuard& operator=( MLockGuard const& other );

public:

    MLockGuard( MMutex& mutex ):
        mutex ( mutex )
    {
        mutex.Lock( );
    }

    ~MLockGuard( ) {
        mutex.Unlock( );
    }

private:

    MMutex& mutex;

};

#endif /* !defined _MLKGUARD_H_ */
