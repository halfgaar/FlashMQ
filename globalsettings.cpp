#include "globalsettings.h"

GlobalSettings *GlobalSettings::instance = nullptr;

GlobalSettings::GlobalSettings()
{

}

GlobalSettings *GlobalSettings::getInstance()
{
    if (instance == nullptr)
        instance = new GlobalSettings();

    return instance;
}
