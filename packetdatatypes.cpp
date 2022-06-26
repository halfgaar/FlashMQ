#include "packetdatatypes.h"

#include "threadglobals.h"
#include "settings.h"

ConnectData::ConnectData()
{
    const Settings *settings = ThreadGlobals::getSettings();

    client_receive_max = settings->maxQosMsgPendingPerClient;
    session_expire = settings->getExpireSessionAfterSeconds();
    max_outgoing_packet_size = settings->maxPacketSize;
}
