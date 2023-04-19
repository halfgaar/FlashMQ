#include "acksender.h"

#include "mqttpacket.h"
#include "client.h"

AckSender::AckSender(uint8_t qos, uint16_t packetId, ProtocolVersion protocolVersion, std::shared_ptr<Client> &client) :
    qos(qos),
    packetId(packetId),
    protocolVersion(protocolVersion),
    client(client)
{

}

AckSender::~AckSender()
{
    if (!sent)
        sendNow();
}

void AckSender::sendNow()
{
    this->sent = true;

    if (qos == 0)
        return;

    const PacketType responseType = qos == 1 ? PacketType::PUBACK : PacketType::PUBREC;
    PubResponse pubAck(this->protocolVersion, responseType, ackCode, packetId);
    MqttPacket response(pubAck);
    client->writeMqttPacket(response);
}

void AckSender::setAckCode(ReasonCodes ackCode)
{
    this->ackCode = ackCode;
}
