/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "configfileparser.h"

#include <fcntl.h>
#include <unistd.h>
#include <sstream>
#include <fstream>
#include <regex>
#include <sys/stat.h>

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "exceptions.h"
#include "utils.h"
#include "globber.h"

/**
 * @brief Like std::stoi, but demands that the entire value is consumed
 * @param key Unused except for informing the user in case of problems
 * @param value the string to parse
 * @return the parsed integer
 */
int full_stoi(const std::string &key, const std::string &value)
{
    size_t ptr;
    int newVal = std::stoi(value, &ptr);
    if (ptr != value.length())
    {
        throw ConfigFileException(formatString("%s's value of '%s' can't be parsed to a number", key.c_str(), value.c_str()));
    }
    return newVal;
}

/**
 * @brief Like std::stoul, but demands that the entire value is consumed
 * @param key Unused except for informing the user in case of problems
 * @param value the string to parse
 * @return the parsed unsigned long
 **/
unsigned long full_stoul(const std::string &key, const std::string &value)
{
    size_t ptr;
    unsigned long newVal = std::stoul(value, &ptr);
    if (ptr != value.length())
    {
        throw ConfigFileException(formatString("%s's value of '%s' can't be parsed to a number", key.c_str(), value.c_str()));
    }
    return newVal;
}

void ConfigFileParser::testCorrectNumberOfValues(const std::string &key, size_t expected_values, const std::vector<std::string> &values)
{
    if (values.size() != expected_values)
    {
        std::ostringstream oss;
        oss << "Option " << key << " expected " << expected_values << ", got " << values.size() << " arguments";

        if (values.size() > expected_values)
        {
            oss << ". Superflous ones: ";

            for (size_t i = expected_values; i < values.size(); i++)
            {
                const std::string &rest = values.at(i);
                oss << rest;

                if (i + 1 >= values.size())
                    oss << ".";
                else
                    oss << ", ";
            }
        }

        throw ConfigFileException(oss.str());
    }
}

/**
 * @brief ConfigFileParser::testKeyValidity tests if two strings match and whether it's a valid config key.
 * @param key
 * @param matchKey
 * @param validKeys
 * @return
 *
 * Use of this function prevents adding config keys that you forget to add to the sets with valid keys.
 */
bool ConfigFileParser::testKeyValidity(const std::string &key, const std::string &matchKey, const std::set<std::string> &validKeys) const
{
    auto valid_key_it = validKeys.find(key);
    if (valid_key_it == validKeys.end())
    {
        std::ostringstream oss;
        oss << "Config key '" << key << "' is not valid (here).";

        auto alternative = findCloseStringMatch(validKeys.begin(), validKeys.end(), key);

        if (alternative != validKeys.end())
        {
            // The space before the question mark is to make copying using mouse-double-click possible.
            oss << " Did you mean: " << *alternative << " ?";
        }

        throw ConfigFileException(oss.str());
    }

    {
        auto valid_key_it = validKeys.find(matchKey);
        if (valid_key_it == validKeys.end())
        {
            std::ostringstream oss;
            oss << "BUG: you still need to add '" << matchKey << "' as valid config key.";
            throw ConfigFileException(oss.str());
        }
    }

    return key == matchKey;
}

void ConfigFileParser::checkFileExistsAndReadable(const std::string &key, const std::string &pathToCheck, ssize_t max_size)
{
    if (access(pathToCheck.c_str(), R_OK) != 0)
    {
        std::ostringstream oss;
        oss << "Error for '" << key << "': " << pathToCheck << " is not there or not readable";
        throw ConfigFileException(oss.str());
    }

    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    if (stat(pathToCheck.c_str(), &statbuf) < 0)
        throw ConfigFileException(formatString("Reading stat of '%s' failed.", pathToCheck.c_str()));

    if (!S_ISREG(statbuf.st_mode))
    {
        throw ConfigFileException(formatString("Error for '%s': '%s' is not a regular file.", key.c_str(), pathToCheck.c_str()));
    }

    if (statbuf.st_size > max_size)
    {
        throw ConfigFileException(formatString("Error for '%s': '%s' is bigger than %zd bytes.", key.c_str(), pathToCheck.c_str(), max_size));
    }
}

void ConfigFileParser::checkFileOrItsDirWritable(const std::string &filepath)
{
    if (access(filepath.c_str(), F_OK) == 0)
    {
        if (access(filepath.c_str(), W_OK) != 0)
        {
            std::string msg = formatString("File '%s' is there, but not writable", filepath.c_str());
            throw ConfigFileException(msg);
        }
        return;
    }
    std::string dirname(dirnameOf(filepath));

    if (access(dirname.c_str(), W_OK) != 0)
    {
        std::string msg = formatString("File '%s' is not there and can't be created, because '%s' is also not writable", filepath.c_str(), dirname.c_str());
        throw ConfigFileException(msg);
    }
}

void ConfigFileParser::checkDirExists(const std::string &key, const std::string &dir)
{
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(struct stat));
    if (stat(dir.c_str(), &statbuf) < 0)
        throw ConfigFileException(formatString("Error for '%s': path '%s' does not exist or reading stat failed.", key.c_str(), dir.c_str()));

    if (!S_ISDIR(statbuf.st_mode))
    {
        throw ConfigFileException(formatString("Error for '%s': '%s' is not a directory.", key.c_str(), dir.c_str()));
    }
}

ConfigFileParser::ConfigFileParser(const std::string &path) :
    path(path)
{
    validKeys.insert("plugin");
    validKeys.insert("plugin_serialize_init");
    validKeys.insert("plugin_serialize_auth_checks");
    validKeys.insert("plugin_timer_period");
    validKeys.insert("log_file");
    validKeys.insert("quiet");
    validKeys.insert("allow_unsafe_clientid_chars");
    validKeys.insert("allow_unsafe_username_chars");
    validKeys.insert("client_initial_buffer_size");
    validKeys.insert("max_packet_size");
    validKeys.insert("log_debug");
    validKeys.insert("log_subscriptions");
    validKeys.insert("log_level");
    validKeys.insert("mosquitto_password_file");
    validKeys.insert("mosquitto_acl_file");
    validKeys.insert("allow_anonymous");
    validKeys.insert("rlimit_nofile");
    validKeys.insert("expire_sessions_after_seconds");
    validKeys.insert("thread_count");
    validKeys.insert("storage_dir");
    validKeys.insert("max_qos_msg_pending_per_client");
    validKeys.insert("max_qos_bytes_pending_per_client");
    validKeys.insert("wills_enabled");
    validKeys.insert("retained_messages_mode");
    validKeys.insert("retained_messages_node_limit");
    validKeys.insert("expire_retained_messages_after_seconds");
    validKeys.insert("retained_message_node_lifetime");
    validKeys.insert("expire_retained_messages_time_budget_ms");
    validKeys.insert("websocket_set_real_ip_from");
    validKeys.insert("shared_subscription_targeting");
    validKeys.insert("max_incoming_topic_alias_value");
    validKeys.insert("max_outgoing_topic_alias_value");
    validKeys.insert("client_max_write_buffer_size");
    validKeys.insert("retained_messages_delivery_limit");
    validKeys.insert("include_dir");
    validKeys.insert("rebuild_subscription_tree_interval_seconds");
    validKeys.insert("minimum_wildcard_subscription_depth");
    validKeys.insert("wildcard_subscription_deny_mode");
    validKeys.insert("zero_byte_username_is_anonymous");
    validKeys.insert("overload_mode");
    validKeys.insert("max_event_loop_drift");
    validKeys.insert("set_retained_message_defer_timeout");
    validKeys.insert("set_retained_message_defer_timeout_spread");

    validListenKeys.insert("port");
    validListenKeys.insert("protocol");
    validListenKeys.insert("fullchain");
    validListenKeys.insert("privkey");
    validListenKeys.insert("inet_protocol");
    validListenKeys.insert("inet4_bind_address");
    validListenKeys.insert("inet6_bind_address");
    validListenKeys.insert("haproxy");
    validListenKeys.insert("client_verification_ca_file");
    validListenKeys.insert("client_verification_ca_dir");
    validListenKeys.insert("client_verification_still_do_authn");
    validListenKeys.insert("allow_anonymous");
    validListenKeys.insert("tcp_nodelay");

    validBridgeKeys.insert("local_username");
    validBridgeKeys.insert("remote_username");
    validBridgeKeys.insert("remote_password");
    validBridgeKeys.insert("remote_clean_start");
    validBridgeKeys.insert("remote_session_expiry_interval");
    validBridgeKeys.insert("remote_retain_available");
    validBridgeKeys.insert("local_clean_start");
    validBridgeKeys.insert("local_session_expiry_interval");
    validBridgeKeys.insert("subscribe");
    validBridgeKeys.insert("publish");
    validBridgeKeys.insert("clientid_prefix");
    validBridgeKeys.insert("use_saved_clientid");
    validBridgeKeys.insert("inet_protocol");
    validBridgeKeys.insert("address");
    validBridgeKeys.insert("ca_file");
    validBridgeKeys.insert("ca_dir");
    validBridgeKeys.insert("port");
    validBridgeKeys.insert("tls");
    validBridgeKeys.insert("protocol_version");
    validBridgeKeys.insert("bridge_protocol_bit");
    validBridgeKeys.insert("keepalive");
    validBridgeKeys.insert("max_outgoing_topic_aliases");
    validBridgeKeys.insert("max_incoming_topic_aliases");
    validBridgeKeys.insert("tcp_nodelay");
}

std::list<std::string> ConfigFileParser::readFileRecursively(const std::string &path) const
{
    std::list<std::string> lines;

    if (path.empty())
        return lines;

    checkFileExistsAndReadable("application config file", path, 1024*1024*10);

    std::ifstream infile(path, std::ios::in);

    if (!infile.is_open())
    {
        std::ostringstream oss;
        oss << "Error loading " << path;
        throw ConfigFileException(oss.str());
    }

    for(std::string line; getline(infile, line ); )
    {
        lines.push_back(line);

        if (strContains(line, "include_dir"))
        {
            std::smatch matches;

            if (std::regex_match(line, matches, key_value_regex))
            {
                const std::string key = matches[1].str();
                const std::string value = matches[2].str();

                if (key == "include_dir")
                {
                    Logger *logger = Logger::getInstance();

                    checkDirExists(key, value);

                    Globber globber;
                    std::vector<std::string> files = globber.getGlob(value + "/*.conf");

                    if (files.empty())
                    {
                        logger->logf(LOG_WARNING, "Including '%s' yielded 0 files.", value.c_str());
                    }

                    for(const std::string &path_from_glob : files)
                    {
                        std::list<std::string> newLines = readFileRecursively(path_from_glob);
                        lines.insert(lines.cend(), newLines.begin(), newLines.end());
                    }
                }
            }
        }
    }

    return lines;
}

void ConfigFileParser::loadFile(bool test)
{
    if (path.empty())
        return;

    const std::list<std::string> unprocessed_lines = readFileRecursively(path);
    std::list<std::string> lines;

    bool inBlock = false;
    std::ostringstream oss;
    int linenr = 0;

    // First parse the file and keep the valid lines.
    for (std::string line : unprocessed_lines)
    {
        trim(line);
        linenr++;

        if (startsWith(line, "#"))
            continue;

        if (line.empty())
            continue;

        // The regex matcher can be made to crash on very long lines, so we're protecting ourselves.
        if (line.length() > 256)
        {
            throw ConfigFileException(formatString("Error at line %d in '%s': line suspiciouly long.", linenr, path.c_str()));
        }

        std::smatch matches;

        const bool blockStartMatch = std::regex_search(line, matches, block_regex_start);
        const bool blockEndMatch = std::regex_search(line, matches, block_regex_end);

        if ((blockStartMatch && inBlock) || (blockEndMatch && !inBlock))
        {
            oss << "Unexpected block start or end at line " << linenr << ": " << line;
            throw ConfigFileException(oss.str());
        }

        if (!std::regex_search(line, matches, key_value_regex) && !blockStartMatch && !blockEndMatch)
        {
            oss << "Line '" << line << "' invalid";
            throw ConfigFileException(oss.str());
        }

        if (blockStartMatch)
            inBlock = true;
        if (blockEndMatch)
            inBlock = false;

        lines.push_back(line);
    }

    if (inBlock)
    {
        throw ConfigFileException("Unclosed config block. Expecting }");
    }

    std::unordered_map<std::string, std::string> pluginOpts;

    ConfigParseLevel curParseLevel = ConfigParseLevel::Root;
    std::shared_ptr<Listener> curListener;
    std::shared_ptr<BridgeConfig> curBridge;
    Settings tmpSettings;

    const std::set<std::string> blockNames {"listen", "bridge"};

    // Then once we know the config file is valid, process it.
    for (std::string &line : lines)
    {
        std::smatch matches;

        if (std::regex_match(line, matches, block_regex_start))
        {
            const std::string &key = matches[1].str();
            if (testKeyValidity(key, "listen", blockNames))
            {
                curParseLevel = ConfigParseLevel::Listen;
                curListener = std::make_shared<Listener>();
            }
            else if (testKeyValidity(key, "bridge", blockNames))
            {
                curParseLevel = ConfigParseLevel::Bridge;
                curBridge = std::make_unique<BridgeConfig>();
            }
            else
            {
                std::ostringstream oss;
                oss << "'" << key << "' is not a valid block.";

                auto alt = findCloseStringMatch(blockNames.begin(), blockNames.end(), key);

                if (alt != blockNames.end())
                {
                    oss << " Did you mean: " << *alt << " ?";
                }

                throw ConfigFileException(oss.str());
            }

            continue;
        }
        else if (std::regex_match(line, matches, block_regex_end))
        {
            if (curParseLevel == ConfigParseLevel::Listen)
            {
                curListener->isValid();
                tmpSettings.listeners.push_back(curListener);
                curListener.reset();
            }
            else if (curParseLevel == ConfigParseLevel::Bridge)
            {
                curBridge->isValid();

                tmpSettings.bridges.push_back(std::move(curBridge));
            }

            curParseLevel = ConfigParseLevel::Root;
            continue;
        }

        std::regex_match(line, matches, key_value_regex);

        std::string key = matches[1].str();
        const std::string value_unparsed = matches[2].str();
        const std::vector<std::string> values = parseValuesWithOptionalQuoting<ConfigFileException>(value_unparsed);
        const std::string &value = values.at(0);
        size_t number_of_expected_values = 1; // Most lines only accept 1 argument, a select few 2.
        std::string valueTrimmed = value;
        trim(valueTrimmed);

        try
        {
            if (curParseLevel == ConfigParseLevel::Listen)
            {
                if (testKeyValidity(key, "protocol", validListenKeys))
                {
                    if (value != "mqtt" && value != "websockets")
                        throw ConfigFileException(formatString("Protocol '%s' is not a valid listener protocol", value.c_str()));
                    curListener->websocket = value == "websockets";
                }
                else if (testKeyValidity(key, "port", validListenKeys))
                {
                    curListener->port = full_stoi(key, value);
                }
                else if (testKeyValidity(key, "fullchain", validListenKeys))
                {
                    checkFileExistsAndReadable("SSL fullchain", value, 1024*1024);
                    curListener->sslFullchain = value;
                }
                if (testKeyValidity(key, "privkey", validListenKeys))
                {
                    checkFileExistsAndReadable("SSL privkey", value, 1024*1024);
                    curListener->sslPrivkey = value;
                }
                if (testKeyValidity(key, "inet_protocol", validListenKeys))
                {
                    if (value == "ip4")
                        curListener->protocol = ListenerProtocol::IPv4;
                    else if (value == "ip6")
                        curListener->protocol = ListenerProtocol::IPv6;
                    else if (value == "ip4_ip6")
                        curListener->protocol = ListenerProtocol::IPv46;
                    else
                        throw ConfigFileException(formatString("Invalid inet protocol: %s", value.c_str()));
                }
                if (testKeyValidity(key, "inet4_bind_address", validListenKeys))
                {
                    curListener->inet4BindAddress = value;
                }
                if (testKeyValidity(key, "inet6_bind_address", validListenKeys))
                {
                    curListener->inet6BindAddress = value;
                }
                if (testKeyValidity(key, "haproxy", validListenKeys))
                {
                    bool val = stringTruthiness(value);
                    curListener->haproxy = val;
                }
                if (testKeyValidity(key, "client_verification_ca_file", validListenKeys))
                {
                    checkFileExistsAndReadable(key, valueTrimmed, 1024*1024);
                    curListener->clientVerificationCaFile = valueTrimmed;
                }
                if (testKeyValidity(key, "client_verification_ca_dir", validListenKeys))
                {
                    checkDirExists(key, value);
                    curListener->clientVerificationCaDir = valueTrimmed;
                }
                if (testKeyValidity(key, "client_verification_still_do_authn", validListenKeys))
                {
                    bool val = stringTruthiness(value);
                    curListener->clientVerifictionStillDoAuthn = val;
                }
                if (testKeyValidity(key, "allow_anonymous", validListenKeys))
                {
                    bool val = stringTruthiness(value);
                    curListener->allowAnonymous = val ? AllowListenerAnonymous::Yes : AllowListenerAnonymous::No;
                }
                if (testKeyValidity(key, "tcp_nodelay", validListenKeys))
                {
                    bool val = stringTruthiness(value);
                    curListener->tcpNoDelay = val;
                }

                testCorrectNumberOfValues(key, number_of_expected_values, values);
                continue;
            }
            else if (curParseLevel == ConfigParseLevel::Bridge)
            {
                if (testKeyValidity(key, "local_username", validBridgeKeys))
                {
                    curBridge->local_username = value;
                }
                if (testKeyValidity(key, "remote_username", validBridgeKeys))
                {
                    curBridge->remote_username = value;
                }
                if (testKeyValidity(key, "remote_password", validBridgeKeys))
                {
                    curBridge->remote_password = value;
                }
                if (testKeyValidity(key, "remote_clean_start", validBridgeKeys))
                {
                    curBridge->remoteCleanStart = stringTruthiness(value);
                }
                if (testKeyValidity(key, "remote_session_expiry_interval", validBridgeKeys))
                {
                    int64_t newVal = full_stoi(key, value);
                    if (newVal <= 0 || newVal > std::numeric_limits<uint32_t>::max())
                    {
                        throw ConfigFileException(formatString("Value '%d' doesn't fit in uint32_t.", newVal));
                    }
                    curBridge->remoteSessionExpiryInterval = newVal;
                }
                if (testKeyValidity(key, "local_clean_start", validBridgeKeys))
                {
                    curBridge->localCleanStart = stringTruthiness(value);
                }
                if (testKeyValidity(key, "local_session_expiry_interval", validBridgeKeys))
                {
                    int64_t newVal = full_stoi(key, value);
                    if (newVal <= 0 || newVal > std::numeric_limits<uint32_t>::max())
                    {
                        throw ConfigFileException(formatString("Value '%d' doesn't fit in uint32_t.", newVal));
                    }
                    curBridge->localSessionExpiryInterval = newVal;
                }
                if (testKeyValidity(key, "subscribe", validBridgeKeys))
                {
                    if (!isValidUtf8(value) || !isValidSubscribePath(value))
                        throw ConfigFileException(formatString("Path '%s' is not a valid subscribe match", value.c_str()));

                    BridgeTopicPath topicPath;

                    if (values.size() >= 2)
                    {
                        number_of_expected_values = 2;
                        const std::string &qosstr = values.at(1);

                        if (!qosstr.empty())
                        {
                            topicPath.qos = full_stoul(key, qosstr);

                            if (!topicPath.isValidQos())
                                throw ConfigFileException(formatString("Qos '%s' is not a valid qos level", qosstr.c_str()));
                        }
                    }

                    topicPath.topic = value;
                    curBridge->subscribes.push_back(topicPath);
                }
                if (testKeyValidity(key, "publish", validBridgeKeys))
                {
                    if (!isValidUtf8(value) || !isValidSubscribePath(value))
                        throw ConfigFileException(formatString("Path '%s' is not a valid publish match", value.c_str()));

                    BridgeTopicPath topicPath;

                    if (values.size() >= 2)
                    {
                        number_of_expected_values = 2;
                        const std::string &qosstr = values.at(1);

                        if (!qosstr.empty())
                        {
                            topicPath.qos = full_stoul(key, qosstr);

                            if (!topicPath.isValidQos())
                                throw ConfigFileException(formatString("Qos '%s' is not a valid qos level", qosstr.c_str()));
                        }
                    }

                    topicPath.topic = value;
                    curBridge->publishes.push_back(topicPath);
                }
                if (testKeyValidity(key, "clientid_prefix", validBridgeKeys))
                {
                    if (value.length() > 10)
                        throw ConfigFileException("Value for 'clientid_prefix' can't be longer than 10 chars");

                    if (!isValidShareName(value))
                        throw ConfigFileException("Value for 'clientid_prefix' contains invalid charachters");

                    curBridge->clientidPrefix = value;
                }
                if (testKeyValidity(key, "address", validBridgeKeys))
                {
                    curBridge->address = value;
                }
                if (testKeyValidity(key, "port", validBridgeKeys))
                {
                    const int64_t newVal = full_stoi(key, value);
                    if (newVal <= 0 || newVal > std::numeric_limits<uint16_t>::max())
                    {
                        throw ConfigFileException(formatString("Value '%d' doesn't fit in uint16_t.", newVal));
                    }
                    curBridge->port = newVal;
                }
                if (testKeyValidity(key, "protocol_version", validBridgeKeys))
                {
                    ProtocolVersion v;

                    if (value == "mqtt3.1")
                        v = ProtocolVersion::Mqtt31;
                    else if (value == "mqtt3.1.1")
                        v = ProtocolVersion::Mqtt311;
                    else if (value == "mqtt5")
                        v = ProtocolVersion::Mqtt5;
                    else
                        throw ConfigFileException(formatString("Value '%s' is not valid for 'protocol_version'", value.c_str()));

                    curBridge->protocolVersion = v;
                }
                if (testKeyValidity(key, "bridge_protocol_bit", validBridgeKeys))
                {
                    curBridge->bridgeProtocolBit = stringTruthiness(value);
                }
                if (testKeyValidity(key, "keepalive", validBridgeKeys))
                {
                    int64_t newVal = full_stoi(key, value);
                    if (newVal < 10 || newVal > std::numeric_limits<uint16_t>::max())
                    {
                        throw ConfigFileException(formatString("Value '%d' doesn't fit in uint16_t and must be at least 10.", newVal));
                    }
                    curBridge->keepalive = newVal;
                }
                if (testKeyValidity(key, "tls", validBridgeKeys))
                {
                    BridgeTLSMode mode = BridgeTLSMode::None;

                    if (value == "unverified")
                        mode = BridgeTLSMode::Unverified;
                    else if (value == "on")
                        mode = BridgeTLSMode::On;
                    else if (value == "off")
                        mode = BridgeTLSMode::None;
                    else
                        throw ConfigFileException(formatString("Value '%s' is not valid for 'tls'", value.c_str()));

                    curBridge->tlsMode = mode;
                }
                if (testKeyValidity(key, "ca_file", validBridgeKeys))
                {
                    checkFileExistsAndReadable(key, value, 1024*1024*100);
                    curBridge->caFile = value;
                }
                if (testKeyValidity(key, "ca_dir", validBridgeKeys))
                {
                    checkDirExists(key, value);
                    curBridge->caDir = value;
                }
                if (testKeyValidity(key, "max_incoming_topic_aliases", validBridgeKeys))
                {
                    const int64_t newVal = full_stoi(key, value);
                    if (newVal < 0 || newVal > std::numeric_limits<uint16_t>::max())
                    {
                        throw ConfigFileException(formatString("Value '%d' doesn't fit in uint16_t.", newVal));
                    }
                    curBridge->maxIncomingTopicAliases = newVal;
                }
                if (testKeyValidity(key, "max_outgoing_topic_aliases", validBridgeKeys))
                {
                    const int64_t newVal = full_stoi(key, value);
                    if (newVal < 0 || newVal > std::numeric_limits<uint16_t>::max())
                    {
                        throw ConfigFileException(formatString("Value '%d' doesn't fit in uint16_t.", newVal));
                    }
                    curBridge->maxOutgoingTopicAliases = newVal;
                }
                if (testKeyValidity(key, "inet_protocol", validBridgeKeys))
                {
                    if (value == "ip4")
                        curBridge->inet_protocol = ListenerProtocol::IPv4;
                    else if (value == "ip6")
                        curBridge->inet_protocol = ListenerProtocol::IPv6;
                    else if (value == "ip4_ip6")
                        curBridge->inet_protocol = ListenerProtocol::IPv46;
                    else
                        throw ConfigFileException(formatString("Invalid inet protocol: %s", value.c_str()));
                }
                if (testKeyValidity(key, "use_saved_clientid", validBridgeKeys))
                {
                    curBridge->useSavedClientId = stringTruthiness(value);
                }
                if (testKeyValidity(key, "remote_retain_available", validBridgeKeys))
                {
                    curBridge->remoteRetainAvailable = stringTruthiness(value);
                }
                if (testKeyValidity(key, "tcp_nodelay", validBridgeKeys))
                {
                    curBridge->tcpNoDelay = true;
                }

                testCorrectNumberOfValues(key, number_of_expected_values, values);
                continue;
            }


            const std::string plugin_opt_ = "plugin_opt_";
            if (startsWith(key, plugin_opt_))
            {
                key.replace(0, plugin_opt_.length(), "");
                pluginOpts[key] = value;
            }
            else
            {
                if (testKeyValidity(key, "plugin", validKeys))
                {
                    checkFileExistsAndReadable(key, value, 1024*1024*100);
                    tmpSettings.pluginPath = value;
                }

                if (testKeyValidity(key, "log_file", validKeys))
                {
                    checkFileOrItsDirWritable(value);
                    tmpSettings.logPath = value;
                }

                if (testKeyValidity(key, "quiet", validKeys))
                {
                    Logger::getInstance()->log(LOG_WARNING) << "The config option '" << key << "' is deprecated. Use log_level instead.";

                    bool tmp = stringTruthiness(value);
                    tmpSettings.quiet = tmp;
                }

                if (testKeyValidity(key, "allow_unsafe_clientid_chars", validKeys))
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings.allowUnsafeClientidChars = tmp;
                }

                if (testKeyValidity(key, "allow_unsafe_username_chars", validKeys))
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings.allowUnsafeUsernameChars = tmp;
                }

                if (testKeyValidity(key, "plugin_serialize_init", validKeys))
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings.pluginSerializeInit = tmp;
                }

                if (testKeyValidity(key, "plugin_serialize_auth_checks", validKeys))
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings.pluginSerializeAuthChecks = tmp;
                }

                if (testKeyValidity(key, "client_initial_buffer_size", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (!isPowerOfTwo(newVal))
                        throw ConfigFileException("client_initial_buffer_size value " + value + " is not a power of two.");
                    tmpSettings.clientInitialBufferSize = newVal;
                }

                if (testKeyValidity(key, "max_packet_size", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (newVal > ABSOLUTE_MAX_PACKET_SIZE)
                    {
                        std::ostringstream oss;
                        oss << "Value for max_packet_size " << newVal << "is higher than absolute maximum " << ABSOLUTE_MAX_PACKET_SIZE;
                        throw ConfigFileException(oss.str());
                    }
                    tmpSettings.maxPacketSize = newVal;
                }

                if (testKeyValidity(key, "log_debug", validKeys))
                {
                    Logger::getInstance()->log(LOG_WARNING) << "The config option '" << key << "' is deprecated. Use log_level instead.";

                    bool tmp = stringTruthiness(value);
                    tmpSettings.logDebug = tmp;
                }

                if (testKeyValidity(key, "log_level", validKeys))
                {
                    const std::string v = str_tolower(value);
                    LogLevel level = LogLevel::None;

                    if (v == "debug")
                        level = LogLevel::Debug;
                    else if (v == "info")
                        level = LogLevel::Info;
                    else if (v == "notice")
                        level = LogLevel::Notice;
                    else if (v == "warning")
                        level = LogLevel::Warning;
                    else if (v == "error")
                        level = LogLevel::Warning;
                    else if (v == "none")
                        level = LogLevel::None;
                    else
                        throw ConfigFileException("Invalid log level: " + value);

                    tmpSettings.logLevel = level;
                }

                if (testKeyValidity(key, "log_subscriptions", validKeys))
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings.logSubscriptions = tmp;
                }

                if (testKeyValidity(key, "mosquitto_password_file", validKeys))
                {
                    checkFileExistsAndReadable("mosquitto_password_file", value, 1024*1024*1024);
                    tmpSettings.mosquittoPasswordFile = value;
                }

                if (testKeyValidity(key, "mosquitto_acl_file", validKeys))
                {
                    checkFileExistsAndReadable("mosquitto_acl_file", value, 1024*1024*1024);
                    tmpSettings.mosquittoAclFile = value;
                }

                if (testKeyValidity(key, "allow_anonymous", validKeys))
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings.allowAnonymous = tmp;
                }

                if (testKeyValidity(key, "rlimit_nofile", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (newVal <= 0)
                    {
                        throw ConfigFileException(formatString("Value '%d' is negative.", newVal));
                    }
                    tmpSettings.rlimitNoFile = newVal;
                }

                if (testKeyValidity(key, "expire_sessions_after_seconds", validKeys))
                {
                    uint32_t newVal = full_stoi(key, value);
                    if (newVal > 0 && newVal < 60) // 0 means disable
                    {
                        throw ConfigFileException(formatString("expire_sessions_after_seconds value '%d' is invalid. Valid values are 0, or 60 or higher.", newVal));
                    }
                    tmpSettings.expireSessionsAfterSeconds = newVal;
                }

                if (testKeyValidity(key, "plugin_timer_period", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (newVal < 0)
                    {
                        throw ConfigFileException(formatString("plugin_timer_period value '%d' is invalid. Valid values are 0 or higher. 0 means disabled.", newVal));
                    }
                    tmpSettings.pluginTimerPeriod = newVal;
                }

                if (testKeyValidity(key, "storage_dir", validKeys))
                {
                    std::string newPath = value;
                    rtrim(newPath, '/');
                    checkWritableDir<ConfigFileException>(newPath);
                    tmpSettings.storageDir = newPath;
                }

                if (testKeyValidity(key, "thread_count", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (newVal < 0)
                    {
                        throw ConfigFileException(formatString("thread_count value '%d' is invalid. Valid values are 0 or higher. 0 means auto.", newVal));
                    }
                    tmpSettings.threadCount = newVal;
                }

                if (testKeyValidity(key, "max_qos_msg_pending_per_client", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (newVal < 32 || newVal > 65535)
                    {
                        throw ConfigFileException(formatString("max_qos_msg_pending_per_client value '%d' is invalid. Valid values between 32 and 65535.", newVal));
                    }
                    tmpSettings.maxQosMsgPendingPerClient = newVal;
                }

                if (testKeyValidity(key, "max_qos_bytes_pending_per_client", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (newVal < 4096)
                    {
                        throw ConfigFileException(formatString("max_qos_bytes_pending_per_client value '%d' is invalid. Valid values are 4096 or higher.", newVal));
                    }
                    tmpSettings.maxQosBytesPendingPerClient = newVal;
                }

                if (testKeyValidity(key, "max_incoming_topic_alias_value", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (newVal < 0 || newVal > 0xFFFF)
                    {
                        throw ConfigFileException(formatString("max_incoming_topic_alias_value value '%d' is invalid. Valid values are between 0 and 65535.", newVal));
                    }
                    tmpSettings.maxIncomingTopicAliasValue = newVal;
                }

                if (testKeyValidity(key, "max_outgoing_topic_alias_value", validKeys))
                {
                    int newVal = full_stoi(key, value);
                    if (newVal < 0 || newVal > 0xFFFF)
                    {
                        throw ConfigFileException(formatString("max_outgoing_topic_alias_value value '%d' is invalid. Valid values are between 0 and 65535.", newVal));
                    }
                    tmpSettings.maxOutgoingTopicAliasValue = newVal;
                }

                if (testKeyValidity(key, "wills_enabled", validKeys))
                {
                    bool tmp = stringTruthiness(value);
                    tmpSettings.willsEnabled = tmp;
                }

                if (testKeyValidity(key, "retained_messages_mode", validKeys))
                {
                    const std::string _val = str_tolower(value);

                    if (_val == "enabled")
                        tmpSettings.retainedMessagesMode = RetainedMessagesMode::Enabled;
                    else if (_val == "downgrade")
                        tmpSettings.retainedMessagesMode = RetainedMessagesMode::Downgrade;
                    else if (_val == "drop")
                        tmpSettings.retainedMessagesMode = RetainedMessagesMode::Drop;
                    else if (_val == "disconnect_with_error")
                        tmpSettings.retainedMessagesMode = RetainedMessagesMode::DisconnectWithError;
                    else
                        throw ConfigFileException(formatString("Value '%s' for '%s' is invalid.", value.c_str(), key.c_str()));
                }

                if (testKeyValidity(key, "expire_retained_messages_after_seconds", validKeys))
                {
                    uint32_t newVal = full_stoi(key, value);
                    if (newVal < 1)
                    {
                        throw ConfigFileException(formatString("expire_retained_messages_after_seconds value '%d' is invalid. Valid values are between 1 and 4294967296.", newVal));
                    }
                    tmpSettings.expireRetainedMessagesAfterSeconds = newVal;
                }

                if (testKeyValidity(key, "retained_message_node_lifetime", validKeys))
                {
                    const int val = full_stoi(key, value);

                    if (val < 0)
                        throw ConfigFileException("Option '" + key + "' must 0 or higher.");

                    tmpSettings.retainedMessageNodeLifetime = std::chrono::seconds(val);
                }

                if (testKeyValidity(key, "expire_retained_messages_time_budget_ms", validKeys))
                {
                    Logger::getInstance()->log(LOG_WARNING) << "The config option '" << key << "' is deprecated.";
                }

                if (testKeyValidity(key, "websocket_set_real_ip_from", validKeys))
                {
                    Network net(value);
                    tmpSettings.setRealIpFrom.push_back(std::move(net));
                }

                if (testKeyValidity(key, "shared_subscription_targeting", validKeys))
                {
                    const std::string _val = str_tolower(value);

                    if (_val == "round_robin")
                        tmpSettings.sharedSubscriptionTargeting = SharedSubscriptionTargeting::RoundRobin;
                    else if (_val == "sender_hash")
                        tmpSettings.sharedSubscriptionTargeting = SharedSubscriptionTargeting::SenderHash;
                    else
                        throw ConfigFileException(formatString("Value '%s' for '%s' is invalid.", value.c_str(), key.c_str()));
                }

                if (testKeyValidity(key, "client_max_write_buffer_size", validKeys))
                {
                    const uint32_t minVal = 4096;
                    uint32_t newVal = full_stoul(key, value);

                    if (newVal < minVal)
                        throw ConfigFileException(formatString("Value '%s' for '%s' is too low. It must be at least %d.", value.c_str(), key.c_str(), minVal));

                    tmpSettings.clientMaxWriteBufferSize = newVal;
                }

                if (testKeyValidity(key, "retained_messages_delivery_limit", validKeys))
                {
                    Logger::getInstance()->log(LOG_WARNING) << "The config option '" << key << "' is deprecated. Use 'retained_messages_node_limit' instead.";
                }

                if (testKeyValidity(key, "retained_messages_node_limit", validKeys))
                {
                    const uint32_t newVal = full_stoul(key, value);

                    if (newVal == 0)
                        throw ConfigFileException("Set '" + key + "' higher than 0, or use 'retained_messages_mode'.");

                    tmpSettings.retainedMessagesNodeLimit = newVal;
                }

                if (testKeyValidity(key, "minimum_wildcard_subscription_depth", validKeys))
                {
                    const unsigned long newVal = full_stoul(key, value);

                    if (newVal > 0xFFFF)
                        throw ConfigFileException("Option '" + key + "' must be between 0 and 65535.");

                    tmpSettings.minimumWildcardSubscriptionDepth = newVal;
                }

                if (testKeyValidity(key, "wildcard_subscription_deny_mode", validKeys))
                {
                    const std::string _val = str_tolower(value);

                    if (_val == "deny_all")
                        tmpSettings.wildcardSubscriptionDenyMode = WildcardSubscriptionDenyMode::DenyAll;
                    else if (_val == "deny_retained_only")
                        tmpSettings.wildcardSubscriptionDenyMode = WildcardSubscriptionDenyMode::DenyRetainedOnly;
                    else
                        throw ConfigFileException(formatString("Value '%s' for '%s' is invalid.", value.c_str(), key.c_str()));
                }

                if (testKeyValidity(key, "zero_byte_username_is_anonymous", validKeys))
                {
                    tmpSettings.zeroByteUsernameIsAnonymous = stringTruthiness(value);
                }

                if (testKeyValidity(key, "overload_mode", validKeys))
                {
                    const std::string _val = str_tolower(value);

                    if (_val == "log")
                        tmpSettings.overloadMode = OverloadMode::Log;
                    else if (_val == "close_new_clients")
                        tmpSettings.overloadMode = OverloadMode::CloseNewClients;
                    else
                        throw ConfigFileException(formatString("Value '%s' for '%s' is invalid.", value.c_str(), key.c_str()));
                }

                if (testKeyValidity(key, "max_event_loop_drift", validKeys))
                {
                    const int val = full_stoi(key, value);

                    if (val < 500)
                    {
                        throw ConfigFileException("Option '" + key + "' must be higher than 500 ms.");
                    }

                    tmpSettings.maxEventLoopDrift = std::chrono::milliseconds(val);
                }

                if (testKeyValidity(key, "set_retained_message_defer_timeout", validKeys))
                {
                    const int val = full_stoi(key, value);

                    if (val < 0)
                        throw ConfigFileException("Option '" + key + "' must 0 or higher.");

                    tmpSettings.setRetainedMessageDeferTimeout = std::chrono::milliseconds(val);
                }

                if (testKeyValidity(key, "set_retained_message_defer_timeout_spread", validKeys))
                {
                    const int val = full_stoi(key, value);

                    if (val < 0)
                        throw ConfigFileException("Option '" + key + "' must 0 or higher.");

                    tmpSettings.setRetainedMessageDeferTimeoutSpread = std::chrono::milliseconds(val);
                }
            }
        }
        catch (std::invalid_argument &ex) // catch for the stoi()
        {
            throw ConfigFileException(ex.what());
        }

        testCorrectNumberOfValues(key, number_of_expected_values, values);
    }

    tmpSettings.checkUniqueBridgeNames();
    tmpSettings.authOptCompatWrap = AuthOptCompatWrap(pluginOpts);
    tmpSettings.flashmqpluginOpts = std::move(pluginOpts);

    if (!test)
    {
        this->settings = tmpSettings;
    }
}

const Settings &ConfigFileParser::getSettings()
{
    return settings;
}



