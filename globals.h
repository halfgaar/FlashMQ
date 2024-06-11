#ifndef GLOBALS_H
#define GLOBALS_H


class Globals
{
    Globals() = default;
public:
    static Globals &getInstance();
    bool quitting = false;
};

#endif // GLOBALS_H
