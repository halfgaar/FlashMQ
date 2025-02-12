#include "bridgeconfig.h"

#include <sstream>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <cassert>

#include "utils.h"
#include "exceptions.h"
#include "bridgeinfodb.h"
#include "globals.h"

std::string BridgeClientGroupIds::getClientGroupShareName(const std::string &client_id_prefix)
{
    std::string &s = this->bridge_share_names[client_id_prefix];

    if (s.empty())
        s = getSecureRandomString(12);

    return s;
}

void BridgeClientGroupIds::setClientGroupShareName(const std::string &client_id_prefix, const std::string &share_name)
{
    this->bridge_share_names[client_id_prefix] = share_name;
}

std::string BridgeClientGroupIds::getClientGroupId(const std::string &client_id_prefix)
{
    std::string &s = this->bridge_group_ids[client_id_prefix];

    if (s.empty())
        s = getSecureRandomString(12);

    return s;
}

void BridgeClientGroupIds::loadShareNames(const std::string &path, bool real)
{
    if (path.empty())
        return;

    try
    {
        BridgeInfoDb db(path);
        db.openRead();
        std::list<BridgeInfoForSerializing> data = db.readInfo();

        if (real)
        {
            for (const BridgeInfoForSerializing &d : data)
            {
                bridge_share_names[d.prefix] = d.client_group_share_name;
            }
        }
    }
    catch (PersistenceFileCantBeOpened &ex) {}
}

void BridgeClientGroupIds::saveShareNames(const std::string &path) const
{
    if (path.empty())
        return;

    std::list<BridgeInfoForSerializing> bridgeInfos;

    for (const auto &p : bridge_share_names)
    {
        BridgeInfoForSerializing ser;
        ser.prefix = p.first;
        ser.client_group_share_name = p.second;
        bridgeInfos.emplace_back(std::move(ser));
    }

    BridgeInfoDb bridgeInfoDb(path);
    bridgeInfoDb.openWrite();
    bridgeInfoDb.saveInfo(bridgeInfos);
}

bool BridgeTopicPath::isValidQos() const
{
    return qos < 3;
}

bool BridgeTopicPath::operator==(const BridgeTopicPath &other) const
{
    return this->topic == other.topic && this->qos == other.qos;
}


BridgeState::BridgeState(const BridgeConfig &config) :
    c(config)
{

}

FMQSockaddr BridgeState::popDnsResult()
{
    if (dnsResults.empty())
        throw std::runtime_error("Trying to get DNS results when there are none");

    FMQSockaddr addr = dnsResults.front();
    dnsResults.pop_front();
    return addr;
}

void BridgeState::initSSL(bool reloadCertificates)
{
    if (this->c.tlsMode == BridgeTLSMode::None)
        return;

    if (reloadCertificates)
        this->sslInitialized = false;

    if (this->sslInitialized)
        return;

    sslctx = std::make_unique<SslCtxManager>(TLS_client_method());
    sslctx->setMinimumTlsVersion(c.minimumTlsVersion);
    SSL_CTX_set_mode(sslctx->get(), SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    const char *privkey = c.sslPrivkey.empty() ? nullptr : c.sslPrivkey.c_str();
    const char *fullchain = c.sslFullchain.empty() ? nullptr : c.sslFullchain.c_str();

    if (fullchain)
    {
        if (SSL_CTX_use_certificate_chain_file(sslctx->get(), fullchain) != 1)
        {
            ERR_print_errors_cb(logSslError, NULL);
            throw std::runtime_error("Loading bridge SSL fullchain failed. This was after test loading the certificate, so is very unexpected.");
        }
    }

    if (privkey)
    {
        if (SSL_CTX_use_PrivateKey_file(sslctx->get(), privkey, SSL_FILETYPE_PEM) != 1)
        {
            ERR_print_errors_cb(logSslError, NULL);
            throw std::runtime_error("Loading bridge SSL privkey failed. This was after test loading the certificate, so is very unexpected.");
        }

        if (SSL_CTX_check_private_key(sslctx->get()) != 1)
        {
            ERR_print_errors_cb(logSslError, NULL);
            throw std::runtime_error("Verifying bridge SSL privkey failed. This was after test loading the certificate, so is very unexpected.");
        }
    }

    const char *ca_file = c.caFile.empty() ? nullptr : c.caFile.c_str();
    const char *ca_dir = c.caDir.empty() ? nullptr : c.caDir.c_str();

    if (ca_file || ca_dir)
    {
        if (SSL_CTX_load_verify_locations(sslctx->get(), ca_file, ca_dir) != 1)
        {
            ERR_print_errors_cb(logSslError, NULL);
            throw std::runtime_error("Loading ca_dir/ca_file failed. This was after test loading the certificate, so is very unexpected.");
        }
    }
    else
    {
        if (SSL_CTX_set_default_verify_paths(sslctx->get()) != 1)
        {
            ERR_print_errors_cb(logSslError, NULL);
            throw std::runtime_error("Setting default SSL paths failed.");
        }
    }

    this->sslInitialized = true;
}

bool BridgeState::timeForNewReconnectAttempt()
{
    /*
     * When we are part of a group of connections to a server, don't back off reconnection, because we want to keep all
     * of the individual connections on-line, and not have some lag behind.
     */
    if (c.getFmqClientGroupId().has_value())
        return true;

    int next = 1;

    if (reconnectCounter > 0)
        next = baseReconnectInterval;

    if (reconnectCounter > 10)
        next = baseReconnectInterval + 300;

    if (next > 1 && intervalLogged != next)
    {
        intervalLogged = next;
        Logger *logger = Logger::getInstance();
        logger->log(LOG_NOTICE) << "Bridge '" << c.clientidPrefix << "' connection failure count: " << reconnectCounter
                                << ". Increasing reconnect interval to " << next << " seconds.";
    }

    return lastReconnectAttempt + std::chrono::seconds(next) < std::chrono::steady_clock::now();
}

void BridgeState::registerReconnect()
{
    lastReconnectAttempt = std::chrono::steady_clock::now();
    reconnectCounter++;
}

void BridgeState::resetReconnectCounter()
{
    lastReconnectAttempt = std::chrono::time_point<std::chrono::steady_clock>();
    reconnectCounter = 0;
    intervalLogged = 0;
}

/**
 * @brief BridgeConfig::setClientId is for setting the client ID on start to the one from a saved state. That's why it only works when the prefix matches.
 * @param prefix
 * @param id
 */
void BridgeConfig::setClientId(const std::string &prefix, const std::string &id)
{
    // This is protection against calling this method too early.
    assert(!clientid.empty());

    // Should never happen, but just in case; an empty client id can get confusing.
    if (id.empty())
        return;

    if (prefix != this->clientidPrefix)
        return;

    this->clientid = id;
}

void BridgeConfig::setClientId()
{
    if (!clientid.empty())
        return;

    if (clientidPrefix.length() > this->client_id_max_length)
        throw std::runtime_error("clientidPrefix can't be longer than 10");

    std::ostringstream oss;
    oss << clientidPrefix << "_" << getSecureRandomString(10);
    clientid = oss.str();
}

void BridgeConfig::appendConnectionNumber(size_t no)
{
    std::string no_s = std::to_string(no);
    this->client_id_max_length += no_s.length() + 1;
    this->clientidPrefix.append("_").append(std::to_string(no));
}

void BridgeConfig::setSharedSubscriptionName(const std::string &share_name)
{
    setSharedSubscriptionName(publishes, share_name);
    setSharedSubscriptionName(subscribes, share_name);
}

void BridgeConfig::setSharedSubscriptionName(std::vector<BridgeTopicPath> &topics, const std::string &share_name)
{
    for (BridgeTopicPath &t : topics)
    {
        std::vector<std::string> subtopics = splitTopic(t.topic);
        std::string _;
        std::string __;
        parseSubscriptionShare(subtopics, _, __);

        std::string new_topic("$share/");
        new_topic.append(share_name);

        for (const std::string &s : subtopics)
        {
            new_topic.append("/");
            new_topic.append(s);
        }

        t.topic = new_topic;
    }
}

const std::string &BridgeConfig::getClientid() const
{
    return clientid;
}

const std::optional<std::string> &BridgeConfig::getFmqClientGroupId() const
{
    return this->fmq_client_group_id;
}

void BridgeConfig::isValid()
{
    if (sslPrivkey.empty() != sslFullchain.empty())
        throw ConfigFileException("Specify both 'privkey' and 'fullchain' or neither.");

    if (tlsMode > BridgeTLSMode::None)
    {
        if (port == 0)
        {
            port = 8883;
        }

        if (sslFullchain.size() || sslPrivkey.size())
        {
            testSsl(sslFullchain, sslPrivkey);
        }
        testSslVerifyLocations(caFile, caDir, "Loading bridge ca_file/ca_dir failed.");
    }
    else
    {
        if (port == 0)
        {
            port = 1883;
        }
    }

    if (address.empty())
        throw ConfigFileException("No address specified in bridge");

    if (publishes.empty() && subscribes.empty())
        throw ConfigFileException("No subscribe or publish paths defined in bridge.");

    if (!caDir.empty() && !caFile.empty())
        throw ConfigFileException("Specify only one 'ca_file' or 'ca_dir'");

    if (clientidPrefix.length() > client_id_max_length)
        throw ConfigFileException("clientidPrefix can't be longer than 10");

    if (protocolVersion <= ProtocolVersion::Mqtt311 && remote_password.has_value() && !remote_username.has_value())
        throw ConfigFileException("MQTT 3.1.1 and lower require a username when you set a password.");

    if (local_prefix && !endsWith(local_prefix.value(), "/"))
        throw ConfigFileException("Option 'local_prefix' must end in a '/'.");

    if (remote_prefix && !endsWith(remote_prefix.value(), "/"))
        throw ConfigFileException("Option 'remote_prefix' must end in a '/'.");

    if (connection_count > 1 && protocolVersion < ProtocolVersion::Mqtt5)
        throw ConfigFileException("Using multiple bridge connections needs at least MQTT5");

    if (connection_count > 1)
    {
        auto check = [](const BridgeTopicPath &x)
        {
            if (startsWith(x.topic, "$share"))
            {
                throw ConfigFileException("Bridges with multiple connections can't already define share names in the topics.");
            }
        };

        std::for_each(publishes.begin(), publishes.end(), check);
        std::for_each(subscribes.begin(), subscribes.end(), check);
    }
}

std::vector<BridgeConfig> BridgeConfig::multiply() const
{
    std::vector<BridgeConfig> result;
    result.reserve(this->connection_count);

    const std::string share_name = globals->bridgeClientGroupIds.getClientGroupShareName(this->clientidPrefix);
    const std::string group_id = globals->bridgeClientGroupIds.getClientGroupId(this->clientidPrefix);

    for (size_t i = 0; i < this->connection_count; i++)
    {
        result.push_back(*this);

        if (this->connection_count > 1)
        {
            result.back().setSharedSubscriptionName(share_name);

            /*
             * This means that when people have an existing bridge config with `use_saved_clientid`, it will lose its state when
             * they change the amount of connections from 1 to something else. That's good, because otherwise one of the
             * connections will get session state that no longer applies to it.
             */
            result.back().appendConnectionNumber(i);

            result.back().fmq_client_group_id = group_id;
        }

        result.back().setClientId();
        result.back().connection_count = 1;
        result.back().isValid();
    }

    return result;
}

bool BridgeConfig::operator ==(const BridgeConfig &other) const
{
    return this->address == other.address && this->port == other.port && this->inet_protocol == other.inet_protocol && this->tlsMode == other.tlsMode
           && this->sslFullchain == other.sslFullchain && this->sslPrivkey == other.sslPrivkey && this->caFile == other.caFile && this->caDir == other.caDir && this->protocolVersion == other.protocolVersion
           && this->bridgeProtocolBit == other.bridgeProtocolBit && this->keepalive == other.keepalive && this->clientidPrefix == other.clientidPrefix
           && this->publishes == other.publishes && this->subscribes == other.subscribes && this->local_username == other.local_username
           && this->remote_username == other.remote_username && this->remote_password == other.remote_password && this->remoteCleanStart == other.remoteCleanStart
           && this->localCleanStart == other.localCleanStart && this->remoteSessionExpiryInterval == other.remoteSessionExpiryInterval
           && this->localSessionExpiryInterval == other.localSessionExpiryInterval && this->remoteRetainAvailable == other.remoteRetainAvailable
           && this->useSavedClientId == other.useSavedClientId && this->maxOutgoingTopicAliases == other.maxOutgoingTopicAliases
           && this->maxIncomingTopicAliases == other.maxIncomingTopicAliases && this->tcpNoDelay == other.tcpNoDelay
           && this->local_prefix == other.local_prefix && this->remote_prefix == other.remote_prefix && this->connection_count == other.connection_count;
}

bool BridgeConfig::operator !=(const BridgeConfig &other) const
{
    bool r = *this == other;
    return !r;
}


