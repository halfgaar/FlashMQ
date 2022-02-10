#include "threadauth.h"

thread_local Authentication *ThreadAuth::auth = nullptr;
thread_local ThreadData *ThreadAuth::threadData = nullptr;
thread_local Settings *ThreadAuth::settings = nullptr;

void ThreadAuth::assign(Authentication *auth)
{
    ThreadAuth::auth = auth;
}

Authentication *ThreadAuth::getAuth()
{
    return auth;
}

void ThreadAuth::assignThreadData(ThreadData *threadData)
{
    ThreadAuth::threadData = threadData;
}

ThreadData *ThreadAuth::getThreadData()
{
    return threadData;
}

void ThreadAuth::assignSettings(Settings *settings)
{
    ThreadAuth::settings = settings;
}

Settings *ThreadAuth::getSettings()
{
    return settings;
}
