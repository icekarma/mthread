#if !defined _MLKGUARD_H_
#define _MLKGUARD_H_

#if !defined __cplusplus
#error "mlkguard.h is a C++ header. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/mmutex.h>

//=======================================================
// MLockGuard
//=======================================================

class MLockGuard {

    MLockGuard( MLockGuard const& other );
    MLockGuard& operator=( MLockGuard const& other );

public:

    MLockGuard( MMutex& mutex ):
        mtx ( mutex )
    {
        mtx.Lock( );
    }

    ~MLockGuard( ) {
        mtx.Unlock( );
    }

private:

    MMutex& mtx;

};

#endif /* !defined _MLKGUARD_H_ */
