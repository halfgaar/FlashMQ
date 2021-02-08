#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

// 'Global' as in, needed outside of the mainapp, like listen ports.
class GlobalSettings
{
    static GlobalSettings *instance;
    GlobalSettings();
public:
    static GlobalSettings *getInstance();

    bool allow_unsafe_clientid_chars = false;
};
#endif // GLOBALSETTINGS_H
