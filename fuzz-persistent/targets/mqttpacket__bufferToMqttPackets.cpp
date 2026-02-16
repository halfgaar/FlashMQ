#include <unistd.h>

#include "../../cirbuf.h"
#include "../../mqttpacket.h"
#include "../../settings.h"
#include "../../subscriptionstore.h"
#include "../../threaddata.h"
#include "../../threadglobals.h"
#include "../../exceptions.h"

__AFL_FUZZ_INIT();

int main()
{
    Settings settings;
    std::shared_ptr<SubscriptionStore> store(new SubscriptionStore());
    PluginLoader pluginLoader;
    std::shared_ptr<ThreadData> t(new ThreadData(0, settings, pluginLoader));

    // Kind of a hack...
    Authentication auth(settings);
    ThreadGlobals::assign(&auth);
    ThreadGlobals::assignSettings(&settings);
    ThreadGlobals::assignThreadData(t.get());

    std::shared_ptr<Client> dummyClient(new Client(0, t, nullptr, false, false, nullptr, settings, false));
    dummyClient->setClientProperties(ProtocolVersion::Mqtt5, "qostestclient", "user1", true, 60);
    store->registerClientAndKickExistingOne(dummyClient, false, 512, 120);

#ifdef __AFL_HAVE_MANUAL_CONTROL
    __AFL_INIT();
#endif

    // call to __AFL_FUZZ_TESTCASE_BUF must be after __AFL_INIT and before __AFL_LOOP
    unsigned char *buf = __AFL_FUZZ_TESTCASE_BUF;

    while (__AFL_LOOP(10000))
    {
        // Don't use the __AFL_FUZZ_TESTCASE_LEN macro direct in a call
        int len = __AFL_FUZZ_TESTCASE_LEN;
        CirBuf readbuf(1024);
        readbuf.ensureFreeSpace(len);
        readbuf.write(buf, len);
        std::vector<MqttPacket> parsedPackets;
        try
        {
            MqttPacket::bufferToMqttPackets(readbuf, parsedPackets, dummyClient);
        }
        catch (ProtocolError &)
        {
        }
    }
    return 0;
}
