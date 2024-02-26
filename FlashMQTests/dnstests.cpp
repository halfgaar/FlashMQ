#include "maintests.h"
#include "testhelpers.h"

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
        const std::string rnd = getSecureRandomString(16);
        const std::string domain = rnd + ".flashmq.org";
        resolver.query(domain, ListenerProtocol::IPv46, std::chrono::milliseconds(5000));

        int count = 0;
        while (count++ < 60)
        {
            std::list<FMQSockaddr_in6> results = resolver.getResult();
            if (!results.empty())
            {
                for (const auto &r : results)
                {
                    std::cerr << "Wrong DNS result: " << r.getText() << std::endl;
                }
                QVERIFY2(false, "A DNS result was returned when we expected nothing.");
                break;
            }

            usleep(100000);
        }

        QVERIFY2(false, "It took too long to get a result. That's weird, because we should have gotten a timeout exception.");
    }
    catch (std::exception &ex)
    {
        const std::string err = str_tolower(ex.what());
        std::cout << "For reference, the error that we're scanning: " << err << std::endl;
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
