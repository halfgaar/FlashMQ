#include <iostream>
#include <sys/socket.h>
#include <stdexcept>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <thread>
#include <vector>

#include "utils.h"
#include "threaddata.h"
#include "client.h"
#include "mqttpacket.h"

#define MAX_EVENTS 1024
#define NR_OF_THREADS 4



void do_thread_work(ThreadData *threadData)
{
    int epoll_fd = threadData->epollfd;

    struct epoll_event events[MAX_EVENTS];
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    std::vector<MqttPacket> packetQueueIn;

    while (1)
    {
        int fdcount = epoll_wait(epoll_fd, events, MAX_EVENTS, 100);

        if (fdcount > 0)
        {
            for (int i = 0; i < fdcount; i++)
            {
                struct epoll_event cur_ev = events[i];
                int fd = cur_ev.data.fd;

                Client_p client = threadData->getClient(fd);

                if (client) // TODO: is this check necessary?
                {
                    if (cur_ev.events | EPOLLIN)
                    {
                        if (!client->readFdIntoBuffer())
                            threadData->removeClient(client);
                        else
                        {
                            client->bufferToMqttPackets(packetQueueIn); // TODO: different, because now I need to give the packet a raw pointer.
                        }
                    }
                }
            }
        }

        for (MqttPacket &packet : packetQueueIn)
        {
            packet.handle();
        }
    }
}

int main()
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

    int optval = 1;
    check<std::runtime_error>(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof(optval)) < 0);

    int flags = fcntl(listen_fd, F_GETFL);
    check<std::runtime_error>(fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK ) < 0);

    struct sockaddr_in in_addr;
    in_addr.sin_family = AF_INET;
    in_addr.sin_addr.s_addr = INADDR_ANY;
    in_addr.sin_port = htons(1883);

    check<std::runtime_error>(bind(listen_fd, (struct sockaddr *)(&in_addr), sizeof(struct sockaddr_in)) < 0);
    check<std::runtime_error>(listen(listen_fd, 1024) < 0);

    int epoll_fd_accept = check<std::runtime_error>(epoll_create(999));

    struct epoll_event events[MAX_EVENTS];
    struct epoll_event ev;
    memset(&ev, 0, sizeof (struct epoll_event));
    memset(&events, 0, sizeof (struct epoll_event)*MAX_EVENTS);

    ev.data.fd = listen_fd;
    ev.events = EPOLLIN;
    check<std::runtime_error>(epoll_ctl(epoll_fd_accept, EPOLL_CTL_ADD, listen_fd, &ev) < 0);

    std::vector<std::shared_ptr<ThreadData>> threads;

    for (int i = 0; i < NR_OF_THREADS; i++)
    {
        std::shared_ptr<ThreadData> t(new ThreadData(i));
        std::thread thread(do_thread_work, t.get());
        t->thread = std::move(thread);
        threads.push_back(t);
    }

    std::cout << "Listening..." << std::endl;

    uint next_thread_index = 0;

    while (1)
    {
        int num_fds = epoll_wait(epoll_fd_accept, events, MAX_EVENTS, 100);

        for (int i = 0; i < num_fds; i++)
        {
            int cur_fd = events[i].data.fd;
            if (cur_fd == listen_fd)
            {
                std::shared_ptr<ThreadData> thread_data = threads[next_thread_index++ % NR_OF_THREADS];

                std::cout << "Accepting connection on thread " << thread_data->threadnr << std::endl;

                struct sockaddr addr;
                memset(&addr, 0, sizeof(struct sockaddr));
                socklen_t len = sizeof(struct sockaddr);
                int fd = check<std::runtime_error>(accept(cur_fd, &addr, &len));

                Client_p client(new Client(fd, thread_data));
                thread_data->giveClient(client);
            }
            else
            {
                throw std::runtime_error("The main thread had activity on an accepted socket?");
            }

        }

    }

    return 0;
}
