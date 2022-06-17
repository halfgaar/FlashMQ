#include "globalstats.h"

GlobalStats *GlobalStats::instance = nullptr;

GlobalStats::GlobalStats()
{

}

GlobalStats *GlobalStats::getInstance()
{
    if (GlobalStats::instance == nullptr)
        GlobalStats::instance = new GlobalStats();

    return GlobalStats::instance;
}

