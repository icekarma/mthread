#include <mthread/mthread.h>
#include <mthread/mthrmgr.h>

//================================================
// Static member variables
//================================================

MThreadManager* MThreadManager::s_instance  = NULL;
unsigned int    MThreadManager::s_ref_count = 0;
int             MThreadManager::s_active    = 0;
