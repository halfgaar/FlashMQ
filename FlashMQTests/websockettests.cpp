#include "tst_maintests.h"

#include <fstream>
#include <sys/types.h>
#include <sys/socket.h>

#include "filecloser.h"

// TODO: perhaps also an expected byte count and loop a while until we have it?
std::vector<char> readFromSocket(int fd)
{
    std::vector<char> answer;
    char buf[1024];

    int n = 0;

    while ((n = read(fd, buf, 1024)) > 0)
    {
        if (n > 0)
            answer.insert(answer.end(), buf, buf + n);
        if (errno == EWOULDBLOCK)
            break;
        else
            throw std::runtime_error(strerror(errno));
    }

    return answer;
}

void MainTests::testWebsocketPing()
{
    try
    {
        Settings settings;
        PluginLoader pluginLoader;
        std::shared_ptr<SubscriptionStore> store(new SubscriptionStore());
        std::shared_ptr<ThreadData> t(new ThreadData(0, settings, pluginLoader));

        // Kind of a hack...
        Authentication auth(settings);
        ThreadGlobals::assign(&auth);
        ThreadGlobals::assignThreadData(t.get());

        int listen_socket = socket(AF_INET, SOCK_STREAM, 0);
        FileCloser listener_closer(listen_socket);

        int optval = 1;
        check<std::runtime_error>(setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval)));

        BindAddr bindAddr = getBindAddr(AF_INET, "127.0.0.1", 22123);

        check<std::runtime_error>(bind(listen_socket, bindAddr.p.get(), bindAddr.len));
        check<std::runtime_error>(listen(listen_socket, 64));

        int client_socket = socket(AF_INET, SOCK_STREAM, 0);
        int flags = fcntl(listen_socket, F_GETFL);
        check<std::runtime_error>(fcntl(client_socket, F_SETFL, flags | O_NONBLOCK ));

        std::shared_ptr<Client> c1(new Client(client_socket, t, nullptr, true, false, nullptr, settings, false));
        std::shared_ptr<Client> client = c1;
        t->giveClient(std::move(c1));

        ::connect(client_socket, bindAddr.p.get(), bindAddr.len);

        int socket_to_client = accept(listen_socket, nullptr, nullptr);
        FileCloser socket_to_client_closer(socket_to_client);

        if (socket_to_client < 0)
            throw std::runtime_error("Couldn't accept socket.");

        flags = fcntl(listen_socket, F_GETFL);
        check<std::runtime_error>(fcntl(socket_to_client, F_SETFL, flags | O_NONBLOCK ));

        int error = 0;
        socklen_t optlen = sizeof(int);
        int count = 0;
        do
        {
            check<std::runtime_error>(getsockopt(client_socket, SOL_SOCKET, SO_ERROR, &error, &optlen));
        }
        while(error == EINPROGRESS && count++ < 1000);

        if (error > 0 && error != EINPROGRESS)
            throw std::runtime_error(strerror(error));

        std::ifstream input("plainwebsocketpacket1_handshake.dat", std::ios::binary);
        std::vector<unsigned char> websocketstart(std::istreambuf_iterator<char>(input), {});

        {
            write(socket_to_client, websocketstart.data(), websocketstart.size());
            client->readFdIntoBuffer();
            client->writeBufIntoFd();
            std::vector<char> answer = readFromSocket(socket_to_client);
            std::string answer_string(answer.begin(), answer.end());

            QVERIFY(startsWith(answer_string, "HTTP/1.1 101 Switching Protocols"));

        }

        // We now have an upgraded connection, and can test websocket frame decoding.

        {
            size_t l = 0;
            std::vector<char> pingFrame(1024);
            pingFrame[l++] = 0x09; // opcode 9
            pingFrame[l++] = 0x00; // Unmasked. payload length;
            write(socket_to_client, pingFrame.data(), l);
            client->readFdIntoBuffer();
            client->writeBufIntoFd();

            std::vector<char> answer = readFromSocket(socket_to_client);
            MYCASTCOMPARE(answer.at(0), 0x8A); // 'final bit', final fragment of message, opcode A (pong).
            MYCASTCOMPARE(answer.at(1), 0x00); // Zero payload.
        }

        {
            size_t l = 0;
            std::vector<char> pingFrameWithPayload(1024);
            pingFrameWithPayload[l++] = 0x09; // opcode 9
            pingFrameWithPayload[l++] = 0x05; // Unmasked. payload length;
            pingFrameWithPayload[l++] = 'h';
            pingFrameWithPayload[l++] = 'e';
            pingFrameWithPayload[l++] = 'l';
            pingFrameWithPayload[l++] = 'l';
            pingFrameWithPayload[l++] = 'o';
            write(socket_to_client, pingFrameWithPayload.data(), l);
            client->readFdIntoBuffer();
            client->writeBufIntoFd();
            {
                std::vector<char> answer = readFromSocket(socket_to_client);
                int i = 0;
                MYCASTCOMPARE(answer.at(i++), 0x8A); // 'final bit', final fragment of message, opcode A (pong).
                MYCASTCOMPARE(answer.at(i++), 0x05); // Payload length
                MYCASTCOMPARE(answer.at(i++), 'h');
                MYCASTCOMPARE(answer.at(i++), 'e');
                MYCASTCOMPARE(answer.at(i++), 'l');
                MYCASTCOMPARE(answer.at(i++), 'l');
                MYCASTCOMPARE(answer.at(i++), 'o');
            }

            // Again, but don't send all data. This would get stuck in a loop before, which should be fixed now.
            write(socket_to_client, pingFrameWithPayload.data(), l-1);
            client->readFdIntoBuffer();
            client->writeBufIntoFd();
            usleep(10000);
            {
                std::vector<char> answer = readFromSocket(socket_to_client);
                QVERIFY(answer.empty());
            }

            // And complete the last byte
            write(socket_to_client, pingFrameWithPayload.data() + (l-1), 1);
            client->readFdIntoBuffer();
            client->writeBufIntoFd();
            {
                std::vector<char> answer = readFromSocket(socket_to_client);
                int i = 0;
                MYCASTCOMPARE(answer.at(i++), 0x8A); // 'final bit', final fragment of message, opcode A (pong).
                MYCASTCOMPARE(answer.at(i++), 0x05); // Payload length
                MYCASTCOMPARE(answer.at(i++), 'h');
                MYCASTCOMPARE(answer.at(i++), 'e');
                MYCASTCOMPARE(answer.at(i++), 'l');
                MYCASTCOMPARE(answer.at(i++), 'l');
                MYCASTCOMPARE(answer.at(i++), 'o');
            }
        }
    }
    catch (std::exception &ex)
    {
        QVERIFY2(false, ex.what());
    }
}
