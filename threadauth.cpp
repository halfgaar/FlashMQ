#include "threadauth.h"

thread_local Authentication *ThreadAuth::auth = nullptr;

void ThreadAuth::assign(Authentication *auth)
{
    ThreadAuth::auth = auth;
}

Authentication *ThreadAuth::getAuth()
{
    return auth;
}
