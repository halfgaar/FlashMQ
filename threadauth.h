#ifndef THREADAUTH_H
#define THREADAUTH_H

class Authentication;

class ThreadAuth
{
    static thread_local Authentication *auth;
public:
    static void assign(Authentication *auth);
    static Authentication *getAuth();
};

#endif // THREADAUTH_H
