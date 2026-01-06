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
    bool operator==(const BridgeTopicPath &other) const;
};

/**
 * @brief The BridgeClientGroupIds class manages the random IDs used in fmq_client_group_id and shared
 * subscription names for them.
 *
 * They need to remain constant during the program's lifetime, and also be settable when loading state from disk.
 */
class BridgeClientGroupIds
{
    std::unordered_map<std::string, std::string> bridge_group_ids;
    std::unordered_map<std::string, std::string> bridge_share_names;

public:
    BridgeClientGroupIds() = default;
    BridgeClientGroupIds(const BridgeClientGroupIds &other) = delete;
    BridgeClientGroupIds(BridgeClientGroupIds &&other) = delete;
    BridgeClientGroupIds &operator=(const BridgeClientGroupIds &other) = delete;

    std::string getClientGroupShareName(const std::string &client_id_prefix);
    void setClientGroupShareName(const std::string &client_id_prefix, const std::string &share_name);

    std::string getClientGroupId(const std::string &client_id_prefix);

    void loadShareNames(const std::string &path, bool real);
    void saveShareNames(const std::string &path) const;
};

class BridgeConfig
{
    std::string clientid;
    std::optional<std::string> fmq_client_group_id; // For a custom feature of no-local shared subscriptions.
    size_t client_id_max_length = 10;

    void setClientId();
    void appendConnectionNumber(size_t no);
    static void setSharedSubscriptionName(std::vector<BridgeTopicPath> &topics, const std::string &share_name);

public:
    ListenerProtocol inet_protocol = ListenerProtocol::IPv46;
    std::string address;
    uint16_t port = 0;
    BridgeTLSMode tlsMode = BridgeTLSMode::None;
    std::string sslFullchain;
    std::string sslPrivkey;
    std::string caFile;
    std::string caDir;
    ProtocolVersion protocolVersion = ProtocolVersion::Mqtt311;
    bool bridgeProtocolBit = true;
    std::string clientidPrefix = "fmqbridge";
    size_t connection_count = 1;
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
    std::weak_ptr<ThreadData> owner;
    bool queueForDelete = false;
    bool tcpNoDelay = false;
    TLSVersion minimumTlsVersion = TLSVersion::TLSv1_1;
    std::optional<uint32_t> maxBufferSize;

    std::optional<std::string> local_prefix;
    std::optional<std::string> remote_prefix;

    void setClientId(const std::string &prefix, const std::string &id);
    const std::string &getClientid() const;
    const std::optional<std::string> &getFmqClientGroupId() const;
    void isValid();
    std::vector<BridgeConfig> multiply() const;
    void setSharedSubscriptionName(const std::string &share_name);

    bool operator ==(const BridgeConfig &other) const;
    bool operator !=(const BridgeConfig &other) const;
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
    std::optional<SslCtxManager> sslctx;

    BridgeState(const BridgeConfig &config);
    DnsResolver dns;
    std::list<FMQSockaddr> dnsResults;

    FMQSockaddr popDnsResult();
    void initSSL(bool reloadCertificates);

    bool timeForNewReconnectAttempt();
    void registerReconnect();
    void resetReconnectCounter();
};

#endif // BRIDGECONFIG_H
