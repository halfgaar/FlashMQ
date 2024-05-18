#include "testinitializer.h"

TestInitializer::TestInitializer(MainTests *tests) :
    tests(tests)
{

}

void TestInitializer::init(bool startServer)
{
    if (!tests)
        return;

    tests->initBeforeEachTest(startServer);
}

void TestInitializer::cleanup()
{
    if (!tests)
        return;

    tests->cleanupAfterEachTest();
    tests = nullptr;
}

TestInitializer::~TestInitializer()
{
    cleanup();
}
