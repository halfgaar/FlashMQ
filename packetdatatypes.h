#ifndef PACKETDATATYPES_H
#define PACKETDATATYPES_H

#include "mqtt5properties.h"

struct ConnectData
{
    uint8_t protocol_level_byte = 0;
    bool bridge = false;

    // Flags
    bool user_name_flag = false;
    bool password_flag = false;
    bool will_retain = false;
    uint8_t will_qos = false;
    bool will_flag = false;
    bool clean_start = false;

    uint16_t keep_alive = 0;

    // Content from properties
    uint16_t client_receive_max;
    uint32_t session_expire;
    uint32_t max_outgoing_packet_size;
    uint16_t max_outgoing_topic_aliases = 0; // Default MUST BE 0, meaning server won't initiate aliases;
    bool request_response_information = false;
    bool request_problem_information = false;
    std::string authenticationMethod;
    std::string authenticationData;

    // Content from Payload
    std::string client_id;
    WillPublish willpublish;
    std::string username;
    std::string password;

    Mqtt5PropertyBuilder builder;

    ConnectData();
};

struct ConnAckData
{
    // Flags
    bool sessionPresent = false;

    ReasonCodes reasonCode = ReasonCodes::ImplementationSpecificError; // default something that is never a parse result;

    std::string authData;
};

struct AuthPacketData
{
    std::string method;
    std::string data;
    ReasonCodes reasonCode = ReasonCodes::ImplementationSpecificError; // default something that is never a parse result;
};

struct DisconnectData
{
    ReasonCodes reasonCode = ReasonCodes::Success;
    std::string reasonString;

    bool session_expiry_interval_set = false;
    uint32_t session_expiry_interval = 0;
};

struct SubAckData
{
    uint16_t packet_id;
    std::string reasonString;
    std::vector<uint8_t> subAckCodes;
};

struct PubRecData
{
    ReasonCodes reasonCode = ReasonCodes::Success; // Default when not specified, or MQTT3;
};

#endif // PACKETDATATYPES_H
