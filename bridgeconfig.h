#ifndef BRIDGECONFIG_H
#define BRIDGECONFIG_H

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include "session.h"
#include "dnsresolver.h"
#include "sslctxmanager.h"
#include "utils.h"

enum class BridgeTLSMode
{
    None,
    Unverified,
    On
};

struct BridgeTopicPath
{
    std::string topic;
    uint8_t qos = 0;

    bool isValidQos() const;
};

class BridgeConfig
{
    std::string clientid;

    void setClientId();

public:
    ListenerProtocol inet_protocol = ListenerProtocol::IPv46;
    std::string address;
    uint16_t port = 0;
    BridgeTLSMode tlsMode = BridgeTLSMode::None;
    std::string caFile;
    std::string caDir;
    ProtocolVersion protocolVersion = ProtocolVersion::Mqtt311;
    bool bridgeProtocolBit = true;
    std::string clientidPrefix = "fmqbridge";
    std::optional<std::string> local_username;
    std::optional<std::string> remote_username;
    std::optional<std::string> remote_password;
    bool remoteCleanStart = true;
    uint32_t remoteSessionExpiryInterval = 0;
    bool localCleanStart = true;
    uint32_t localSessionExpiryInterval = 0;
    uint16_t keepalive = 60;
    uint16_t maxIncomingTopicAliases = 0;
    uint16_t maxOutgoingTopicAliases = 0;
    bool useSavedClientId = false;
    bool remoteRetainAvailable = true;
    std::vector<BridgeTopicPath> subscribes;
    std::vector<BridgeTopicPath> publishes;

    void setClientId(const std::string &prefix, const std::string &id);
    const std::string &getClientid() const;
    void isValid();
};

class BridgeState
{
    bool sslInitialized = false;
    std::chrono::time_point<std::chrono::steady_clock> lastReconnectAttempt;
    int reconnectCounter = 0;
    const int baseReconnectInterval = (get_random_int<int>() % 30) + 30;
    int intervalLogged = 0;
public:
    const BridgeConfig c;
    std::weak_ptr<Session> session;
    std::weak_ptr<ThreadData> threadData; // kind of hacky, but I need it later.
    std::unique_ptr<SslCtxManager> sslctx;

    BridgeState(const BridgeConfig &config);
    DnsResolver dns;
    std::list<FMQSockaddr_in6> dnsResults;

    FMQSockaddr_in6 popDnsResult();
    void initSSL(bool reloadCertificates);

    bool timeForNewReconnectAttempt();
    void registerReconnect();
    void resetReconnectCounter();
};

#endif // BRIDGECONFIG_H
