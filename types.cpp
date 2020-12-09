#include "types.h"

ConnAck::ConnAck(ConnAckReturnCodes return_code) :
    return_code(return_code)
{

}

SubAck::SubAck(uint16_t packet_id, const std::list<std::string> &subs) :
    packet_id(packet_id)
{
    // dummy
    for(size_t i = 0; i < subs.size(); i++)
    {
        responses.push_back(SubAckReturnCodes::MaxQoS0);
    }
}
