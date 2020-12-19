#ifndef RETAINEDMESSAGE_H
#define RETAINEDMESSAGE_H

#include <string>

struct RetainedMessage
{
    std::string topic;
    std::string payload;
    char qos;

    RetainedMessage(const std::string &topic, const std::string &payload, char qos);

    bool operator==(const RetainedMessage &rhs) const;
};

namespace std {

    template <>
    struct hash<RetainedMessage>
    {
        std::size_t operator()(const RetainedMessage& k) const
        {
            using std::size_t;
            using std::hash;
            using std::string;

            return hash<string>()(k.topic);
        }
    };

}

#endif // RETAINEDMESSAGE_H
