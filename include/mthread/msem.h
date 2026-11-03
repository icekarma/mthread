#if !defined _MSEM_H_
#define _MSEM_H_

#if !defined __cplusplus
#error "msem.h is a C++ header. Please compile as C++."
#endif // !defined __cplusplus

#include <mthread/synchron.h>

//=======================================================
// MSemaphore
//=======================================================

class MSemaphore {

    MSemaphore( MSemaphore const& other );
    MSemaphore& operator=( MSemaphore const& other );

public:

    MSemaphore( unsigned int initial_count = 0 ) {
        sem_init( &sem, initial_count );
    }

    void Wait( ) {
        sem_wait( &sem );
    }

    int TryWait( ) {
        return sem_trywait( &sem );
    }

    void Post( ) {
        sem_post( &sem );
    }

private:

    mt_sem_t sem;

};

#endif /* !defined _MSEM_H_ */
