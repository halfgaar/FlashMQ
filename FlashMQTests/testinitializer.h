#ifndef TESTINITIALIZER_H
#define TESTINITIALIZER_H

#include "maintests.h"

/**
 * @brief Simple RAII way to make sure test cleanup is run.
 */
class TestInitializer
{
    MainTests *tests = nullptr;

public:
    TestInitializer(MainTests *tests);
    virtual ~TestInitializer();

    TestInitializer(const TestInitializer &other) = delete;
    TestInitializer(TestInitializer &&other) = delete;
    TestInitializer &operator=(const TestInitializer &other) = delete;

    void init();
    void cleanup();
};

#endif // TESTINITIALIZER_H
