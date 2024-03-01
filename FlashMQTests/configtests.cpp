#include "maintests.h"
#include "testhelpers.h"
#include "conffiletemp.h"
#include "exceptions.h"

void MainTests::test_loading_second_value()
{
    /* this is expected to work*/
    {
        ConfFileTemp config;
        config.writeLine("bridge {");
        config.writeLine("  address localhost");
        config.writeLine("  publish send/this 1"); // this value should be different from the default (0)
        config.writeLine("}");
        config.closeFile();

        ConfigFileParser parser(config.getFilePath());
        parser.loadFile(false);

        Settings settings = parser.getSettings();

        std::shared_ptr<BridgeConfig> bridge = settings.stealBridges().front();

        FMQ_COMPARE(bridge->publishes[0].topic, "send/this");
        FMQ_COMPARE(bridge->publishes[0].qos, (uint8_t)1);
    }

    /* this is expecte to fail because "address" doesn't take a second value */
    {
        ConfFileTemp config;
        config.writeLine("bridge {");
        config.writeLine("  address localhost thisisnotok");
        config.writeLine("  publish send/this 1");
        config.writeLine("}");
        config.closeFile();

        ConfigFileParser parser(config.getFilePath());
        try
        {
            parser.loadFile(false);
            FMQ_FAIL("The config parser is too liberal");
        }
        catch (ConfigFileException &ex)
        {
            /* Excellent, what we wanted */
        }
    }
}

void MainTests::test_parsing_numbers()
{
    /* this should work: 180 */
    {
        ConfFileTemp config;
        config.writeLine("expire_sessions_after_seconds 180");
        config.closeFile();

        ConfigFileParser parser(config.getFilePath());
        parser.loadFile(false);

        Settings settings = parser.getSettings();

        FMQ_COMPARE(settings.expireSessionsAfterSeconds, (uint32_t)180);
    }

    /* this should fail: 180days */
    {
        ConfFileTemp config;
        config.writeLine("expire_sessions_after_seconds 180days");
        config.closeFile();

        ConfigFileParser parser(config.getFilePath());
        try
        {
            parser.loadFile(false);
            FMQ_FAIL("The parser was too liberal");
        }
        catch (ConfigFileException&)
        {
            /* Good! This is where we want to end up in */
        }
    }

    /* this should also fail: 180 days */
    {
        ConfFileTemp config;
        config.writeLine("expire_sessions_after_seconds 180 days");
        config.closeFile();

        ConfigFileParser parser(config.getFilePath());
        try
        {
            parser.loadFile(false);
            FMQ_FAIL("The parser was too liberal");
        }
        catch (ConfigFileException&)
        {
            /* Good! This is where we want to end up in */
        }
    }

    /* Last one that should fail: 180 days and a bit */
    {
        ConfFileTemp config;
        config.writeLine("expire_sessions_after_seconds 180 days and a bit more");
        config.closeFile();

        ConfigFileParser parser(config.getFilePath());
        try
        {
            parser.loadFile(false);
            FMQ_FAIL("The parser was too liberal");
        }
        catch (ConfigFileException&)
        {
            /* Good! This is where we want to end up in */
        }
    }
}
