#include "bridgeconfig.h"

#include <sstream>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <cassert>

#include "utils.h"
#include "exceptions.h"

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

    if (clientidPrefix.length() > 10)
        throw std::runtime_error("clientidPrefix can't be longer than 10");

    std::ostringstream oss;
    oss << clientidPrefix << "_" << getSecureRandomString(10);
    clientid = oss.str();
}

const std::string &BridgeConfig::getClientid() const
{
    return clientid;
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

    if (clientidPrefix.length() > 10)
        throw ConfigFileException("clientidPrefix can't be longer than 10");

    if (protocolVersion <= ProtocolVersion::Mqtt311 && remote_password.has_value() && !remote_username.has_value())
        throw ConfigFileException("MQTT 3.1.1 and lower require a username when you set a password.");

    if (local_prefix && !endsWith(local_prefix.value(), "/"))
        throw ConfigFileException("Option 'local_prefix' must end in a '/'.");

    if (remote_prefix && !endsWith(remote_prefix.value(), "/"))
        throw ConfigFileException("Option 'remote_prefix' must end in a '/'.");

    setClientId();
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
           && this->local_prefix == other.local_prefix && this->remote_prefix == other.remote_prefix;
}

bool BridgeConfig::operator !=(const BridgeConfig &other) const
{
    bool r = *this == other;
    return !r;
}


