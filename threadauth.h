#ifndef THREADAUTH_H
#define THREADAUTH_H

#include "forward_declarations.h"

class Authentication;

// TODO: rename, this is no longer just auth, but thread local globals.
class ThreadAuth
{
    static thread_local Authentication *auth;
    static thread_local ThreadData *threadData;
    static thread_local Settings *settings;
public:
    static void assign(Authentication *auth);
    static Authentication *getAuth();

    static void assignThreadData(ThreadData *threadData);
    static ThreadData *getThreadData();

    static void assignSettings(Settings *settings);
    static Settings *getSettings();
};

#endif // THREADAUTH_H
