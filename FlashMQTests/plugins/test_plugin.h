#ifndef TESTPLUGIN_H
#define TESTPLUGIN_H

#include <thread>
#include "../../forward_declarations.h"

class TestPluginData
{
public:
    std::thread t;

    std::weak_ptr<Client> c;

    bool main_init_ran = false;

public:
    ~TestPluginData();
};

#endif // TESTPLUGIN_H
