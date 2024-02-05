#ifndef TST_MAINTESTS_H
#define TST_MAINTESTS_H

#include <QtTest>

#include <QScopedPointer>
#include <QHostInfo>

#include "cirbuf.h"
#include "mainapp.h"
#include "threadlocalutils.h"
#include "retainedmessagesdb.h"
#include "sessionsandsubscriptionsdb.h"
#include "session.h"
#include "threaddata.h"
#include "threadglobals.h"
#include "packetdatatypes.h"
#include "qospacketqueue.h"
#include "network.h"
#include "pluginloader.h"
#include "mainappthread.h"
#include "conffiletemp.h"
#include "flashmqtestclient.h"
#include "flashmqtempdir.h"

// Dumb Qt version gives warnings when comparing uint with number literal.
template <typename T1, typename T2>
inline bool myCastCompare(const T1 &t1, const T2 &t2, const char *actual, const char *expected,
                      const char *file, int line)
{
    T1 t2_ = static_cast<T1>(t2);
    return QTest::compare_helper(t1 == t2_, "Compared values are not the same",
                                 QTest::toString(t1), QTest::toString(t2), actual, expected, file, line);
}

#define MYCASTCOMPARE(actual, expected) \
do {\
    if (!myCastCompare(actual, expected, #actual, #expected, __FILE__, __LINE__))\
        return;\
} while (false)

class MainTests : public QObject
{
    Q_OBJECT

    QScopedPointer<MainAppThread> mainApp;
    std::shared_ptr<ThreadData> dummyThreadData;

    void testParsePacketHelper(const std::string &topic, uint8_t from_qos, bool retain);
    void testTopicMatchingInSubscriptionTreeHelper(const std::string &subscribe_topic, const std::string &publish_topic, int match_count=1);

public:
    MainTests();
    ~MainTests();

private slots:
    void init(const std::vector<std::string> &args);
    void init(); // will be called before each test function is executed
    void cleanup(); // will be called after every test function.

    void cleanupTestCase(); // will be called after the last test function was executed.

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
};


#endif // TST_MAINTESTS_H
