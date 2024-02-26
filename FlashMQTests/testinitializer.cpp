#include "testinitializer.h"

TestInitializer::TestInitializer(MainTests *tests) :
    tests(tests)
{

}

void TestInitializer::init()
{
    if (!tests)
        return;

    tests->initBeforeEachTest();
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
