#include "globals.h"


Globals &Globals::getInstance()
{
    static Globals instance;
    return instance;
}
