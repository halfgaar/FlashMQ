/*
 * This file is part of FlashMQ (https://www.flashmq.org). It describes
 * public FlashMQ functions available to plugins.
 *
 * This interface definition is public domain and you are encouraged
 * to copy it to your plugin project together with flashmq_plugin.h,
 * for portability. Including this file in your project does not make
 * it a 'derivative work'.
 */

#ifndef FLASHMQ_PUBLIC_H
#define FLASHMQ_PUBLIC_H

#include <string>
#include <vector>
#include <memory>
#include <arpa/inet.h>
#include <functional>

// Compatible with Mosquitto, for (auth) plugin compatability.
#define LOG_NONE 0x00
#define LOG_INFO 0x01
#define LOG_NOTICE 0x02
#define LOG_WARNING 0x04
#define LOG_ERR 0x08
#define LOG_ERROR 0x08
#define LOG_DEBUG 0x10
#define LOG_SUBSCRIBE 0x20
#define LOG_UNSUBSCRIBE 0x40
#define LOG_PUBLISH 0x80 // Not compatible with Mosquitto

#define API __attribute__((visibility("default")))

extern "C"
{

class Client;
class Session;

/**
 * @brief The AclAccess enum's numbers are compatible with Mosquitto's 'int access'.
 *
 * read = reading a publish published by someone else.
 * write = doing a publish.
 * subscribe = subscribing.
 */
enum class AclAccess
{
    none = 0,
    read = 1,
    write = 2,
    subscribe = 4,
    register_will = 100
};

/**
 * @brief The AuthResult enum's numbers are compatible with Mosquitto's auth result.
 *
 * async = defer the decision until you have the result from an async call, which can be submitted with flashmq_continue_async_authentication().
 *
 * auth_continue = part of MQTT5 extended authentication, which can be a back-and-forth between server and client.
 *
 * success_without_retained_delivery = allow the subscription action, but don't try to give client the matching retained messages. This
 * can be used prevent load on the server. For instance, if there are many retained messages and clients subscribe to '#'. This value
 * is only valid for AclAccess::subscribe, and requires FlashMQ version 1.9.0 or newer.
 *
 * success_without_setting_retained = allow the write action, but don't set the retained message (if it was set to retain to begin
 * with). MQTT5 subscribers with 'retain as published' will still see the retain flag set. This value is only valid for
 * AclAccess::write and requires a FlashMQ version 1.16.0 or higher.
 *
 * success_but_drop_publish / success_but_drop = send success SUBACK or PUBACK back to client (if QoS) but don't process the packet. This
 * may be useful in combination with flashmq_publish_message() or flashmq_plugin_add_subscription() if you need to publish or subscribe
 * something new entirely (different topic(s) or payload for instance). This only works with AclAccess::write (FlashMQ 1.20.0 or higher)
 * and AclAccess::subscribe (FlashMQ 1.21.0 or higher).
 *
 * server_not_available = to be used as log-in result, for when you don't have auth data yet, for instance. MQTT3 and MQTT5 both support
 * sending 'ServerUnavailble' in their CONNACK, when this result is used. Requires FlashMQ 1.17.0 or newer.
 */
enum class AuthResult
{
    success = 0,
    auth_method_not_supported = 10,
    acl_denied = 12,
    login_denied = 11,
    error = 13,
    server_not_available = 14,
    async = 50,
    success_without_retained_delivery = 51,
    success_without_setting_retained = 52,
    success_but_drop_publish = 53,
    success_but_drop = 53,
    auth_continue = -4
};

enum class ExtendedAuthStage
{
    None = 0,
    Auth = 10,
    Reauth = 20,
    Continue = 30
};

/**
 * @brief The ServerDisconnectReasons enum lists the possible values to initiate client disconnect with.
 *
 * This is a subset of all MQTT5 reason codes that are allowed in a disconnect packet, and that make sense for plugin use.
 */
enum class ServerDisconnectReasons
{
    NormalDisconnect = 0,
    UnspecifiedError = 128,
    ProtocolError = 130,
    ImplementationSpecificError = 131,
    NotAuthorized = 135,
    ServerBusy = 137,
    MessageRateTooHigh = 150
};


/**
 * @brief flashmq_logf calls the internal logger of FlashMQ. The logger mutexes all access, so is thread-safe, and writes to disk
 * asynchronously, so it won't hold you up.
 * @param level is any of the levels defined above, starting with LOG_.
 * @param str
 *
 * FlashMQ makes no distinction between INFO and NOTICE.
 */
void API flashmq_logf(int level, const char *str, ...);

/**
 * @brief flashmq_plugin_remove_client queues a removal of a client in the proper thread, including session if required. It can be called by
 *        plugin code (meaning this function does not need to be implemented).
 * @param session Can be obtained with flashmq_get_session_pointer().
 * @param alsoSession also remove the session if it would otherwise remain.
 * @param reasonCode is only for MQTT5, because MQTT3 doesn't have server-initiated disconnect packets.
 *
 * Many clients will automatically reconnect, so you'll have to also remove permissions of the client in question, probably.
 *
 * Can be called from any thread: the action will be queued properly.
 *
 * [New version since plugin version 4]
 */
void API flashmq_plugin_remove_client_v4(const std::weak_ptr<Session> &session, bool alsoSession, ServerDisconnectReasons reasonCode);

/**
 * @brief flashmq_plugin_remove_subscription removes a client's subscription from the central store. It can be called by plugin code (meaning
 *        this function does not need to be implemented).
 * @param session Can be obtained with flashmq_get_session_pointer().
 * @param topicFilter Like 'one/two/three' or '$share/myshare/one/two/three'.
 *
 * It matches only literal filters. So removing '#' would only remove an active subscription on '#', not 'everything'.
 *
 * Will throw exceptions on certain errors.
 *
 * Can be called from any thread, because the global subscription store is mutexed.
 *
 * [New version since plugin version 4]
 */
void API flashmq_plugin_remove_subscription_v4(const std::weak_ptr<Session> &session, const std::string &topicFilter);


/**
 * @brief flashmq_plugin_add_subscription
 * @param session Can be obtained with flashmq_get_session_pointer().
 * @param topicFilter Like 'one/two/three' or '$share/myshare/one/two/three'.
 * @return boolean True when session found and subscription actually added.
 *
 * Will throw exceptions on certain errors.
 *
 * Can be called from any thread, because the global subscription store is mutexed.
 */
bool API flashmq_plugin_add_subscription(
    const std::weak_ptr<Session> &session, const std::string &topicFilter, uint8_t qos, bool noLocal, bool retainAsPublished,
    const uint32_t subscriptionIdentifier);

/**
 * @brief flashmq_continue_async_authentication is to continue/finish async authentication.
 * @param client
 * @param result
 * @param delay Introducing a delay on failure can be a benificial security feature.
 *
 * When you've previously returned AuthResult::async in the authentication check, because you need to perform a network call for instance,
 * you can submit the final result back to FlashMQ with this function. The action will be queued in the proper thread.
 *
 * It uses a weak pointer to Client instead of client id, because clients are in limbo at this point, and a client id isn't necessarily
 * correct (anymore). The login functions also give this weak pointer so you can store it with the async operation, to be used again later for
 * a call to this function.
 *
 * [New version since plugin version 4, FlashMQ version 1.25.0]
 */
void API flashmq_continue_async_authentication_v4(
    const std::weak_ptr<Client> &client, AuthResult result, const std::string &authMethod, const std::string &returnData, const uint32_t delay_in_ms);

/**
 * @brief flashmq_publish_message Publish a message from the plugin.
 *
 * Can be called from any thread.
 */
void API flashmq_publish_message(
    const std::string &topic, const uint8_t qos, const bool retain, const std::string &payload, uint32_t expiryInterval=0,
    const std::vector<std::pair<std::string, std::string>> *userProperties = nullptr,
    const std::string *responseTopic=nullptr, const std::string *correlationData=nullptr, const std::string *contentType=nullptr);

/**
 * @brief flashmq_get_client_address_v4
 * @param client A client pointer as provided by 'flashmq_plugin_login_check'.
 * @param text If not nullptr, will be assigned the address in text form, like 192.168.1.1 or "2001:0db8:85a3:0000:1319:8a2e:0370:7344".
 * @param addr If not nullptr, will fill a sockaddr struct, for low level operations.
 * @param addrlen Size of addr. Supply the length. Afterwards, the actual size will be reported.
 *
 * The text, addr and addrlen must be pointers to local variables in the calling context.
 *
 * Note that the sockaddr API is hard to use safely in C++. Use of addr can very easily lead to undetected undefined behavior in
 * C++ because of type aliasing violations. The only safe way is to avoid using a casted object, instead use memcpy into structs of the
 * correct type: first 'struct sockaddr', to read the family, then like 'struct sockaddr_in' or 'struct sockaddr_in6'. In other
 * words, avoid accessing the members of the struct, even those of 'struct sockaddr'.
 *
 * Example of initializing the addr variable:
 *
 * struct sockaddr_storage addr_mem;
 * struct sockaddr *addr = reinterpret_cast<sockaddr*>(&addr_mem);
 * socklen_t addrlen = sizeof(addr_mem);
 *
 * Afterwards, do the memcpy stuff to read it, or pass it verbatim to library functions.
 *
 * [New version since plugin version 4]
 */
void API flashmq_get_client_address_v4(const std::weak_ptr<Client> &client, std::string *text, sockaddr *addr, socklen_t *addrlen);

/**
 * @brief flashmq_get_session_pointer Get reference counted weak pointer of a session.
 * @param clientid The client ID of the session you're retrieving.
 * @param username The username is used for verification, as a security measure.
 * @param sessionOut The result (has to be an output parameter because we can't return it).
 *
 * The weak pointer will acurately reflect the original session. If it has been replaced with a new one with
 * the same client ID, this weak pointer will be 'expired'.
 */
void API flashmq_get_session_pointer(const std::string &clientid, const std::string &username, std::weak_ptr<Session> &sessionOut);

/**
 * @brief flashmq_get_client_pointer Get reference counted client pointer of a session.
 * @param clientOut The result (has to be an output parameter because we can't return it).
 *
 * Can we used to feed to other functions, or to check if the client is still online.
 */
void API flashmq_get_client_pointer(const std::weak_ptr<Session> &session, std::weak_ptr<Client> &clientOut);

/**
 * @brief Allows async operation of outgoing connections you may need to make. It adds the file descriptor to
 *        the epoll listener.
 * @param fd
 * @param events epoll events, typically EPOLLIN (ready read) and EPOLLOUT (ready write). Should be or'ed together,
 *        like 'EPOLLOUT | EPOLLIN'. See 'man epoll'.
 * @param p weak pointer. Can be a weak copy of a shared pointer with proper type. Like p = std::make_share<T>().
 *        You'll get it back in 'flashmq_plugin_poll_event_received()'. Use is optional. For libcurl multi socket,
 *        you don't need it.
 *
 * You can do this once you have a connection with something external.
 *
 * You can also call it again with different events, in which case it will modify the existing entry. If you specify
 * a non-expired p, it will overwrite the original data associated with the fd.
 *
 * Will throw exceptions on error, so be sure to handle them.
 */
void API flashmq_poll_add_fd(int fd, uint32_t events, const std::weak_ptr<void> &p);

/**
 * @brief Remove the fd from the event polling system.
 * @param fd
 *
 * Closing a socket will also remove it from the epoll system, but if you don't call this function on close, you may get stray
 * events once the fd number is reused. There is protection against it, but you may end up with unpredictable behavior.
 *
 * Will throw exceptions on error, so be sure to handle them.
 */
void API flashmq_poll_remove_fd(uint32_t fd);

/**
 * @brief call a task later, once.
 * @param f Function, that can be created with std::bind, for instance.
 * @param delay_in_ms
 * @return id of the timer, which can be used to remove it.
 *
 * The task queue is local to the thread, including the id returned.
 *
 * This can be necessary for asynchronous interfaces, like libcurl.
 *
 * Can throw an exceptions.
 */
uint32_t API flashmq_add_task(std::function<void()> f, uint32_t delay_in_ms);

/**
 * @brief Remove a task with id as given by 'flashmq_add_task()'.
 * @param id
 *
 * The task queue is local to the thread, including the id returned.
 */
void API flashmq_remove_task(uint32_t id);

}

#endif // FLASHMQ_PUBLIC_H
