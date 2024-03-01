#ifndef MAINTESTS_H
#define MAINTESTS_H

#include <memory>
#include <unordered_map>
#include <functional>

#include "mainappinthread.h"

#define REGISTER_FUNCTION(name) registerFunction(#name, std::bind(&MainTests::name, this))

class MainTests
{
    friend class TestInitializer;

    std::unique_ptr<MainAppInThread> mainApp;
    std::shared_ptr<ThreadData> dummyThreadData;

    std::unordered_map<std::string, std::function<void()>> testFunctions;

    void testAsserts();

    void initBeforeEachTest(const std::vector<std::string> &args);
    void initBeforeEachTest();
    void cleanupAfterEachTest();
    void registerFunction(const std::string &name, std::function<void()> f);

    // Compatability for porting the tests away from Qt. The function names are too vague so want to phase them out.
    void init(const std::vector<std::string> &args) { initBeforeEachTest(args);}
    void init() {initBeforeEachTest();}
    void cleanup() {cleanupAfterEachTest();}

    void testParsePacketHelper(const std::string &topic, uint8_t from_qos, bool retain);
    void testTopicMatchingInSubscriptionTreeHelper(const std::string &subscribe_topic, const std::string &publish_topic, int match_count=1);

    void testDummy();

    void test_circbuf();
    void test_circbuf_unwrapped_doubling();
    void test_circbuf_wrapped_doubling();
    void test_circbuf_full_wrapped_buffer_doubling();

    void test_validSubscribePath();

    /**
     * Retain tests
     */
    void test_retained();
    void test_retained_double_set();
    void test_retained_mode_drop();
    void test_retained_mode_downgrade();
    void test_retained_changed();
    void test_retained_removed();
    void test_retained_tree();
    void test_retained_global_expire();
    void test_retained_per_message_expire();
    void test_retained_tree_purging();
    void testRetainAsPublished();
    void testRetainAsPublishedNegative();
    void testRetainedParentOfWildcard();
    void testRetainedWildcard();
    void testRetainedAclReadCheck();

    void test_various_packet_sizes();

    void test_acl_tree();
    void test_acl_tree2();
    void test_acl_patterns_username();
    void test_acl_patterns_clientid();
    void test_loading_acl_file();

    void test_loading_second_value();
    void test_parsing_numbers();
    void test_validUtf8Generic();

#ifndef FMQ_NO_SSE
    void test_sse_split();
    void test_validUtf8Sse();
    void test_utf8_nonchars();
    void test_utf8_overlong();
    void test_utf8_compare_implementation();
#endif

    void testPacketInt16Parse();

    void testRetainedMessageDB();
    void testRetainedMessageDBNotPresent();
    void testRetainedMessageDBEmptyList();

    void testSavingSessions();

    void testParsePacket();

    void testDowngradeQoSOnSubscribeQos2to2();
    void testDowngradeQoSOnSubscribeQos2to1();
    void testDowngradeQoSOnSubscribeQos2to0();
    void testDowngradeQoSOnSubscribeQos1to1();
    void testDowngradeQoSOnSubscribeQos1to0();
    void testDowngradeQoSOnSubscribeQos0to0();

    void testNotMessingUpQosLevels();

    void testUnSubscribe();

    void testBasicsWithFlashMQTestClient();

    void testDontRemoveSessionGivenToNewClientWithSameId();
    void testKeepSubscriptionOnKickingOutExistingClientWithCleanSessionFalse();
    void testPickUpSessionWithSubscriptionsAfterDisconnect();

    /**
     * Will tests.
     */
    void testMqtt3will();
    void testMqtt3NoWillOnDisconnect();
    void testMqtt5NoWillOnDisconnect();
    void testMqtt5DelayedWill();
    void testMqtt5DelayedWillAlwaysOnSessionEnd();
    void testWillOnSessionTakeOvers();
    void testOverrideWillDelayOnSessionDestructionByTakeOver();
    void testDisabledWills();
    void testMqtt5DelayedWillsDisabled();


    void testIncomingTopicAlias();
    void testOutgoingTopicAlias();
    void testOutgoingTopicAliasStoredPublishes();

    void testReceivingRetainedMessageWithQoS();

    void testQosDowngradeOnOfflineClients();

    void testUserProperties();

    void testMessageExpiry();

    void testExpiredQueuedMessages();
    void testQoSPublishQueue();

    void testTimePointToAge();

    void testMosquittoPasswordFile();
    void testOverrideAllowAnonymousToTrue();
    void testOverrideAllowAnonymousToFalse();
    void testKeepAllowAnonymousFalse();
    void testAllowAnonymousWithoutPasswordsLoaded();

    void testAddrMatchesSubnetIpv4();
    void testAddrMatchesSubnetIpv6();

    /**
     * Shared subscriptions tests
     */
    void testSharedSubscribersUnit();
    void testSharedSubscribers();
    void testDisconnectedSharedSubscribers();
    void testUnsubscribedSharedSubscribers();
    void testSharedSubscribersSurviveRestart();
    void testSharedSubscriberDoesntGetRetainedMessages();

    /**
     * Plugin tests
     */
    void testExtendedAuthOneStepSucceed();
    void testExtendedAuthOneStepDeny();
    void testExtendedAuthOneStepBadAuthMethod();
    void testExtendedAuthTwoStep();
    void testExtendedAuthTwoStepSecondStepFail();
    void testExtendedReAuth();
    void testExtendedReAuthTwoStep();
    void testExtendedReAuthFail();
    void testSimpleAuthAsync();
    void testPluginAuthFail();
    void testPluginAuthSucceed();
    void testPluginOnDisconnect();
    void testPluginGetClientAddress();
    void testChangePublish();
    void testClientRemovalByPlugin();
    void testSubscriptionRemovalByPlugin();
    void testPublishByPlugin();
    void testWillDenialByPlugin();
    void testPluginMainInit();
    void testAsyncCurl();
    void testSubscribeWithoutRetainedDelivery();
    void testDontUpgradeWildcardDenyMode();
    void testAlsoDontApproveOnErrorInPluginWithWildcardDenyMode();
    void testDenyWildcardSubscription();

    void testPublishToItself();
    void testNoLocalPublishToItself();

    void testTopicMatchingInSubscriptionTree();

    void testDnsResolver();
    void testDnsResolverDontCancel();
    void testDnsResolverSecondQuery();
    void testDnsResolverInvalid();
    void testGetResultWhenThereIsNone();

    void testWebsocketPing();
    void testWebsocketCorruptLengthFrame();
    void testWebsocketHugePing();
    void testWebsocketManyBigPingFrames();
    void testWebsocketClose();


public:
    MainTests();

    bool test(const std::vector<std::string> &tests);
};

#endif // MAINTESTS_H
