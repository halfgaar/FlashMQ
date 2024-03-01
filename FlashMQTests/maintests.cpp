#include "maintests.h"
#include "testhelpers.h"
#include "testinitializer.h"

#include "threadglobals.h"

void MainTests::testAsserts()
{
    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            QCOMPARE(1, 1);
        };
        a();
        if (assert_count != 1 || assert_fail_count != 0)
            throw std::exception();
    }

    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            QCOMPARE(1, 2);
        };
        a();
        if (assert_count != 1 || assert_fail_count != 1)
            throw std::exception();
    }

    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            QVERIFY(true);
        };
        a();
        if (assert_count != 1 || assert_fail_count != 0)
            throw std::exception();
    }

    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            QVERIFY(false);
        };
        a();
        if (assert_count != 1 || assert_fail_count != 1)
            throw std::exception();
    }

    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            QVERIFY2(true, "");
        };
        a();
        if (assert_count != 1 || assert_fail_count != 0)
            throw std::exception();
    }

    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            QVERIFY2(false, "");
        };
        a();
        if (assert_count != 1 || assert_fail_count != 1)
            throw std::exception();
    }

    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            QFAIL("");
        };
        a();
        if (assert_count != 1 || assert_fail_count != 1)
            throw std::exception();
    }

    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            MYCASTCOMPARE(static_cast<ssize_t>(1), static_cast<size_t>(1));
        };
        a();
        if (assert_count != 1 || assert_fail_count != 0)
            throw std::exception();
    }

    {
        auto a = [] {
            assert_count = 0;
            assert_fail_count = 0;
            MYCASTCOMPARE(static_cast<ssize_t>(1), static_cast<size_t>(2));
        };
        a();
        if (assert_count != 1 || assert_fail_count != 1)
            throw std::exception();
    }

}

void MainTests::initBeforeEachTest(const std::vector<std::string> &args)
{
    mainApp.reset();
    mainApp = std::make_unique<MainAppInThread>(args);
    mainApp->start();
    mainApp->waitForStarted();

    // We test functions directly that the server normally only calls from worker threads, in which thread data is available. This is kind of a dummy-fix, until
    // we actually need correct thread data at those points (at this point, it's only to increase message counters).
    Settings settings;
    PluginLoader pluginLoader;
    this->dummyThreadData = std::make_shared<ThreadData>(666, settings, pluginLoader);
    ThreadGlobals::assignThreadData(dummyThreadData.get());
}

void MainTests::initBeforeEachTest()
{
    std::vector<std::string> args;
    initBeforeEachTest(args);
}

void MainTests::cleanupAfterEachTest()
{
    this->mainApp->stopApp();
}

void MainTests::registerFunction(const std::string &name, std::function<void ()> f)
{
    this->testFunctions[name] = f;
}

void MainTests::testDummy()
{
    FMQ_COMPARE(1,1);
}

MainTests::MainTests()
{
    static int instanceCount = 0;

    if (instanceCount++ > 0)
        throw std::runtime_error("Don't instantiate this more than once.");

    asserts_print = false;
    testAsserts();
    asserts_print = true;

    assert_count = 0;
    assert_fail_count = 0;

    REGISTER_FUNCTION(testDummy);
    REGISTER_FUNCTION(test_circbuf);
    REGISTER_FUNCTION(test_circbuf_unwrapped_doubling);
    REGISTER_FUNCTION(test_circbuf_wrapped_doubling);
    REGISTER_FUNCTION(test_circbuf_full_wrapped_buffer_doubling);
    REGISTER_FUNCTION(test_validSubscribePath);
    REGISTER_FUNCTION(test_retained);
    REGISTER_FUNCTION(test_retained_double_set);
    REGISTER_FUNCTION(test_retained_mode_drop);
    REGISTER_FUNCTION(test_retained_mode_downgrade);
    REGISTER_FUNCTION(test_retained_changed);
    REGISTER_FUNCTION(test_retained_removed);
    REGISTER_FUNCTION(test_retained_tree);
    REGISTER_FUNCTION(test_retained_global_expire);
    REGISTER_FUNCTION(test_retained_per_message_expire);
    REGISTER_FUNCTION(test_retained_tree_purging);
    REGISTER_FUNCTION(testRetainAsPublished);
    REGISTER_FUNCTION(testRetainAsPublishedNegative);
    REGISTER_FUNCTION(testRetainedParentOfWildcard);
    REGISTER_FUNCTION(testRetainedWildcard);
    REGISTER_FUNCTION(testRetainedAclReadCheck);
    REGISTER_FUNCTION(test_various_packet_sizes);
    REGISTER_FUNCTION(test_acl_tree);
    REGISTER_FUNCTION(test_acl_tree2);
    REGISTER_FUNCTION(test_acl_patterns_username);
    REGISTER_FUNCTION(test_acl_patterns_clientid);
    REGISTER_FUNCTION(test_loading_acl_file);
    REGISTER_FUNCTION(test_loading_second_value);
    REGISTER_FUNCTION(test_parsing_numbers);
    REGISTER_FUNCTION(test_validUtf8Generic);

#ifndef FMQ_NO_SSE
    REGISTER_FUNCTION(test_sse_split);
    REGISTER_FUNCTION(test_validUtf8Sse);
    REGISTER_FUNCTION(test_utf8_nonchars);
    REGISTER_FUNCTION(test_utf8_overlong);
    REGISTER_FUNCTION(test_utf8_compare_implementation);
#endif

    REGISTER_FUNCTION(testPacketInt16Parse);
    REGISTER_FUNCTION(testRetainedMessageDB);
    REGISTER_FUNCTION(testRetainedMessageDBNotPresent);
    REGISTER_FUNCTION(testRetainedMessageDBEmptyList);
    REGISTER_FUNCTION(testSavingSessions);
    REGISTER_FUNCTION(testParsePacket);
    REGISTER_FUNCTION(testDowngradeQoSOnSubscribeQos2to2);
    REGISTER_FUNCTION(testDowngradeQoSOnSubscribeQos2to1);
    REGISTER_FUNCTION(testDowngradeQoSOnSubscribeQos2to0);
    REGISTER_FUNCTION(testDowngradeQoSOnSubscribeQos1to1);
    REGISTER_FUNCTION(testDowngradeQoSOnSubscribeQos1to0);
    REGISTER_FUNCTION(testDowngradeQoSOnSubscribeQos0to0);
    REGISTER_FUNCTION(testNotMessingUpQosLevels);
    REGISTER_FUNCTION(testUnSubscribe);
    REGISTER_FUNCTION(testBasicsWithFlashMQTestClient);
    REGISTER_FUNCTION(testDontRemoveSessionGivenToNewClientWithSameId);
    REGISTER_FUNCTION(testKeepSubscriptionOnKickingOutExistingClientWithCleanSessionFalse);
    REGISTER_FUNCTION(testPickUpSessionWithSubscriptionsAfterDisconnect);
    REGISTER_FUNCTION(testMqtt3will);
    REGISTER_FUNCTION(testMqtt3NoWillOnDisconnect);
    REGISTER_FUNCTION(testMqtt5NoWillOnDisconnect);
    REGISTER_FUNCTION(testMqtt5DelayedWill);
    REGISTER_FUNCTION(testMqtt5DelayedWillAlwaysOnSessionEnd);
    REGISTER_FUNCTION(testWillOnSessionTakeOvers);
    REGISTER_FUNCTION(testOverrideWillDelayOnSessionDestructionByTakeOver);
    REGISTER_FUNCTION(testDisabledWills);
    REGISTER_FUNCTION(testMqtt5DelayedWillsDisabled);
    REGISTER_FUNCTION(testIncomingTopicAlias);
    REGISTER_FUNCTION(testOutgoingTopicAlias);
    REGISTER_FUNCTION(testOutgoingTopicAliasStoredPublishes);
    REGISTER_FUNCTION(testReceivingRetainedMessageWithQoS);
    REGISTER_FUNCTION(testQosDowngradeOnOfflineClients);
    REGISTER_FUNCTION(testUserProperties);
    REGISTER_FUNCTION(testMessageExpiry);
    REGISTER_FUNCTION(testExpiredQueuedMessages);
    REGISTER_FUNCTION(testQoSPublishQueue);
    REGISTER_FUNCTION(testTimePointToAge);
    REGISTER_FUNCTION(testMosquittoPasswordFile);
    REGISTER_FUNCTION(testOverrideAllowAnonymousToTrue);
    REGISTER_FUNCTION(testOverrideAllowAnonymousToFalse);
    REGISTER_FUNCTION(testKeepAllowAnonymousFalse);
    REGISTER_FUNCTION(testAllowAnonymousWithoutPasswordsLoaded);
    REGISTER_FUNCTION(testAddrMatchesSubnetIpv4);
    REGISTER_FUNCTION(testAddrMatchesSubnetIpv6);
    REGISTER_FUNCTION(testSharedSubscribersUnit);
    REGISTER_FUNCTION(testSharedSubscribers);
    REGISTER_FUNCTION(testDisconnectedSharedSubscribers);
    REGISTER_FUNCTION(testUnsubscribedSharedSubscribers);
    REGISTER_FUNCTION(testSharedSubscribersSurviveRestart);
    REGISTER_FUNCTION(testSharedSubscriberDoesntGetRetainedMessages);
    REGISTER_FUNCTION(testExtendedAuthOneStepSucceed);
    REGISTER_FUNCTION(testExtendedAuthOneStepDeny);
    REGISTER_FUNCTION(testExtendedAuthOneStepBadAuthMethod);
    REGISTER_FUNCTION(testExtendedAuthTwoStep);
    REGISTER_FUNCTION(testExtendedAuthTwoStepSecondStepFail);
    REGISTER_FUNCTION(testExtendedReAuth);
    REGISTER_FUNCTION(testExtendedReAuthTwoStep);
    REGISTER_FUNCTION(testExtendedReAuthFail);
    REGISTER_FUNCTION(testSimpleAuthAsync);
    REGISTER_FUNCTION(testPluginAuthFail);
    REGISTER_FUNCTION(testPluginAuthSucceed);
    REGISTER_FUNCTION(testPluginOnDisconnect);
    REGISTER_FUNCTION(testPluginGetClientAddress);
    REGISTER_FUNCTION(testChangePublish);
    REGISTER_FUNCTION(testClientRemovalByPlugin);
    REGISTER_FUNCTION(testSubscriptionRemovalByPlugin);
    REGISTER_FUNCTION(testPublishByPlugin);
    REGISTER_FUNCTION(testWillDenialByPlugin);
    REGISTER_FUNCTION(testPluginMainInit);
    REGISTER_FUNCTION(testAsyncCurl);
    REGISTER_FUNCTION(testSubscribeWithoutRetainedDelivery);
    REGISTER_FUNCTION(testDontUpgradeWildcardDenyMode);
    REGISTER_FUNCTION(testAlsoDontApproveOnErrorInPluginWithWildcardDenyMode);
    REGISTER_FUNCTION(testDenyWildcardSubscription);
    REGISTER_FUNCTION(testPublishToItself);
    REGISTER_FUNCTION(testNoLocalPublishToItself);
    REGISTER_FUNCTION(testTopicMatchingInSubscriptionTree);
    REGISTER_FUNCTION(testDnsResolver);
    REGISTER_FUNCTION(testDnsResolverDontCancel);
    REGISTER_FUNCTION(testDnsResolverSecondQuery);
    REGISTER_FUNCTION(testDnsResolverInvalid);
    REGISTER_FUNCTION(testGetResultWhenThereIsNone);
    REGISTER_FUNCTION(testWebsocketPing);
    REGISTER_FUNCTION(testWebsocketCorruptLengthFrame);
    REGISTER_FUNCTION(testWebsocketHugePing);
    REGISTER_FUNCTION(testWebsocketManyBigPingFrames);
    REGISTER_FUNCTION(testWebsocketClose);
}

bool MainTests::test(const std::vector<std::string> &tests)
{
    int testCount = 0;
    int testPassCount = 0;
    int testFailCount = 0;
    int testExceptionCount = 0;

    std::unordered_map<std::string, std::function<void()>> *selectedTests = &this->testFunctions;
    std::unordered_map<std::string, std::function<void()>> subset;

    for(const std::string &test_name : tests)
    {
        auto pos = this->testFunctions.find(test_name);

        if (pos == this->testFunctions.end())
        {
            std::cerr << "Test '" << test_name << "' not found." << std::endl;
            return false;
        }

        subset[test_name] = pos->second;
    }

    if (!subset.empty())
    {
        selectedTests = &subset;
    }

    std::vector<std::string> failedTests;

    for (const auto &pair : *selectedTests)
    {
        testCount++;

        try
        {
            std::cout << CYAN << "INIT" << COLOR_END << ": " << pair.first << std::endl;

            TestInitializer testInitializer(this);
            testInitializer.init();

            const int failCountBefore = assert_fail_count;
            const int assertCountBefore = assert_count;

            std::cout << CYAN << "RUN" << COLOR_END << ": " << pair.first << std::endl;
            pair.second();

            const int failCountAfter = assert_fail_count;
            const int assertCountAfter = assert_count;

            testInitializer.cleanup();

            if (assertCountBefore == assertCountAfter)
            {
                std::cout << RED << "FAIL" << COLOR_END << ": " << pair.first << ": no asserts performed" << std::endl;
                testFailCount++;
                failedTests.push_back(pair.first);
            }
            else if (failCountBefore != failCountAfter)
            {
                std::cout << RED << "FAIL" << COLOR_END << ": " << pair.first << std::endl;
                testFailCount++;
                failedTests.push_back(pair.first);
            }
            else
            {
                std::cout << GREEN << "PASS" << COLOR_END << ": " << pair.first << std::endl;
                testPassCount++;
            }
        }
        catch (std::exception &ex)
        {
            // TODO: get details

            testFailCount++;
            testExceptionCount++;
            failedTests.push_back(pair.first);

            std::cout << RED << "FAIL EXCEPTION" << COLOR_END << ": " << pair.first << ": " << ex.what() << std::endl;
        }

        std::cout << std::endl;
    }

    std::cout << "Tests run: " << testCount << ". Passed: " << testPassCount << ". Failed: "
              << testFailCount << " (of which " << testExceptionCount << " exceptions). Total assertions: "
              << assert_count << "." << std::endl;

    std::cout << std::endl << std::endl;

    if (assert_fail_count == 0 && testFailCount == 0)
    {
        std::cout << std::endl << GREEN << "TESTS PASSED" << COLOR_END << std::endl;
        return true;
    }
    else
    {
        std::cout << "Failed tests: " << std::endl;

        for (const std::string &test_name : failedTests)
        {
            std::cout << " - " << test_name << std::endl;
        }

        std::cout << std::endl << RED << "TESTS FAILED" << COLOR_END << std::endl;
        return false;
    }
}
