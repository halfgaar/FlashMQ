#ifndef GLOBALSTATS_H
#define GLOBALSTATS_H

#include <stdint.h>
#include "derivablecounter.h"

class GlobalStats
{
    static GlobalStats *instance;

    GlobalStats();
public:
    static GlobalStats *getInstance();

    DerivableCounter socketConnects;
};

#endif // GLOBALSTATS_H
