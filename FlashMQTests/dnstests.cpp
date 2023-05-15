#include "tst_maintests.h"

#include "utils.h"

void MainTests::testDnsResolver()
{
    try
    {
        DnsResolver resolver;
        resolver.query("demo.flashmq.org", ListenerProtocol::IPv46, std::chrono::milliseconds(5000));

        int count = 0;
        while (++count < 100)
        {
            std::list<FMQSockaddr_in6> results = resolver.getResult();
            if (!results.empty())
            {
                QVERIFY(std::any_of(results.begin(), results.end(), [](FMQSockaddr_in6 &x){return x.getText() == "83.137.146.230";}));
                QVERIFY(std::any_of(results.begin(), results.end(), [](FMQSockaddr_in6 &x){return x.getText() == "2a01:1b0:7996:418:83:137:146:230";}));
                break;
            }

            usleep(10000);
        }

        if (count >= 100)
            QVERIFY(false);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }
}

void MainTests::testDnsResolverDontCancel()
{
    try
    {
        DnsResolver resolver;
        resolver.query("demo.flashmq.org", ListenerProtocol::IPv46, std::chrono::milliseconds(5000));
        resolver.query("demo.flashmq.org", ListenerProtocol::IPv46, std::chrono::milliseconds(5000));

        int count = 0;
        while (++count < 100)
        {
            std::list<FMQSockaddr_in6> results = resolver.getResult();
            if (!results.empty())
            {
                QVERIFY(std::any_of(results.begin(), results.end(), [](FMQSockaddr_in6 &x){return x.getText() == "83.137.146.230";}));
                QVERIFY(std::any_of(results.begin(), results.end(), [](FMQSockaddr_in6 &x){return x.getText() == "2a01:1b0:7996:418:83:137:146:230";}));
                break;
            }

            usleep(10000);
        }

        if (count >= 100)
            QVERIFY(false);
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }
}

void MainTests::testDnsResolverSecondQuery()
{
    try
    {
        DnsResolver resolver;
        for (int i = 0; i < 2; i++)
        {
            resolver.query("demo.flashmq.org", ListenerProtocol::IPv46, std::chrono::milliseconds(5000));

            int count = 0;
            while (++count < 100)
            {
                std::list<FMQSockaddr_in6> results = resolver.getResult();
                if (!results.empty())
                {
                    QVERIFY(std::any_of(results.begin(), results.end(), [](FMQSockaddr_in6 &x){return x.getText() == "83.137.146.230";}));
                    QVERIFY(std::any_of(results.begin(), results.end(), [](FMQSockaddr_in6 &x){return x.getText() == "2a01:1b0:7996:418:83:137:146:230";}));
                    break;
                }

                usleep(10000);
            }

            if (count >= 100)
                QVERIFY(false);
        }
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }
}

void MainTests::testDnsResolverInvalid()
{
    try
    {
        DnsResolver resolver;
        resolver.query("qs2UqhKsX0gNKphIMEs65CZ9UGyWDz6H.org", ListenerProtocol::IPv46, std::chrono::milliseconds(5000));

        int count = 0;
        while (count++ < 100)
        {
            std::list<FMQSockaddr_in6> results = resolver.getResult();
            if (!results.empty())
            {
                break;
            }

            usleep(10000);
        }

        QVERIFY(false);
    }
    catch (std::exception &ex)
    {
        std::string err = str_tolower(ex.what());
        QVERIFY(strContains(err, "name or service not known"));
    }
}

void MainTests::testGetResultWhenThereIsNone()
{
    try
    {
        DnsResolver resolver;
        std::list<FMQSockaddr_in6> results = resolver.getResult();
        QVERIFY(results.empty());
        QVERIFY(false);
    }
    catch (std::exception &ex)
    {
        std::string err = str_tolower(ex.what());
        QVERIFY(strContains(err, "no dns query in progress"));
    }
}
