/*
 * This file is part of FlashMQ (https://www.flashmq.org). It defines the
 * plugin interface.
 *
 * This interface definition is public domain and you are encouraged
 * to copy it to your plugin project, for portability. Including
 * this file in your project does not make it a 'derived work', does not require
 * your code to have a compatibile license nor requires you to open source it.
 *
 * Compile like: gcc -fPIC -shared plugin.cpp -o plugin.so
 *
 * It's best practice to build your plugin with the same library versions of the
 * build of FlashMQ you're using. In practice, this means building on the OS
 * version you're running on. This also means using the AppImage build of FlashMQ
 * is not really compabible with plugins, because that includes older, and fixed,
 * versions of various libraries.
 *
 * For instance, if you use OpenSSL, by the time your plugin is loaded, FlashMQ will
 * have already dynamically linked OpenSSL. If you then try to call OpenSSL
 * functions, you'll run into ABI incompatibilities.
 */

#ifndef FLASHMQ_PLUGIN_H
#define FLASHMQ_PLUGIN_H

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <arpa/inet.h>

#define FLASHMQ_PLUGIN_VERSION 1

// Compatible with Mosquitto, for (auth) plugin compatability.
#define LOG_NONE 0x00
#define LOG_INFO 0x01
#define LOG_NOTICE 0x02
#define LOG_WARNING 0x04
#define LOG_ERR 0x08
#define LOG_DEBUG 0x10
#define LOG_SUBSCRIBE 0x20
#define LOG_UNSUBSCRIBE 0x40

extern "C"
{

class Client;

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
 */
enum class AuthResult
{
    success = 0,
    auth_method_not_supported = 10,
    acl_denied = 12,
    login_denied = 11,
    error = 13,
    async = 50,
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
 * @brief flashmq_logf calls the internal logger of FlashMQ. The logger mutexes all access, so is thread-safe, and writes to disk
 * asynchronously, so it won't hold you up.
 * @param level is any of the levels defined above, starting with LOG_.
 * @param str
 *
 * FlashMQ makes no distinction between INFO and NOTICE.
 */
void flashmq_logf(int level, const char *str, ...);

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
 * @brief flashmq_plugin_remove_client queues a removal of a client in the proper thread, including session if required. It can be called by
 *        plugin code (meaning this function does not need to be implemented).
 * @param clientid
 * @param alsoSession also remove the session if it would otherwise remain.
 * @param reasonCode is only for MQTT5, because MQTT3 doesn't have server-initiated disconnect packets.
 *
 * Many clients will automatically reconnect, so you'll have to also remove permissions of the client in question, probably.
 *
 * Can be called from any thread: the action will be queued properly.
 *
 * [Function provided by FlashMQ]
 */
void flashmq_plugin_remove_client(const std::string &clientid, bool alsoSession, ServerDisconnectReasons reasonCode);

/**
 * @brief flashmq_plugin_remove_subscription removes a client's subscription from the central store. It can be called by plugin code (meaning
 *        this function does not need to be implemented).
 * @param clientid
 * @param topicFilter
 *
 * It matches only literal filters. So removing '#' would only remove an active subscription on '#', not 'everything'.
 *
 * You need to keep track of subscriptions in 'flashmq_plugin_acl_check()' to be able to know what to remove.
 *
 * Can be called from any thread, because the global subscription store is mutexed.
 *
 * [Function provided by FlashMQ]
 */
void flashmq_plugin_remove_subscription(const std::string &clientid, const std::string &topicFilter);

/**
 * @brief flashmq_continue_async_authentication is to continue/finish async authentication.
 * @param client
 * @param result
 *
 * When you've previously returned AuthResult::async in the authentication check, because you need to perform a network call for instance,
 * you can submit the final result back to FlashMQ with this function. The action will be queued in the proper thread.
 *
 * It uses a weak pointer to Client instead of client id, because clients are in limbo at this point, and a client id isn't necessarily
 * correct (anymore). The login functions also give this weak pointer so you can store it with the async operation, to be used again later for
 * a call to this function.
 *
 * [Function provided by FlashMQ]
 */
void flashmq_continue_async_authentication(const std::weak_ptr<Client> &client, AuthResult result, const std::string &authMethod, const std::string &returnData);

/**
 * @brief flashmq_publish_message Publish a message from the plugin.
 *
 * Can be called from any thread.
 *
 * [Function provided by FlashMQ]
 */
void flashmq_publish_message(const std::string &topic, const uint8_t qos, const bool retain, const std::string &payload, uint32_t expiryInterval=0,
                             const std::vector<std::pair<std::string, std::string>> *userProperties = nullptr,
                             const std::string *responseTopic=nullptr, const std::string *correlationData=nullptr, const std::string *contentType=nullptr);

/**
 * @brief The FlashMQSockAddr class is a simple helper for the C-style polymorfism of all the sockaddr structs.
 *
 * Primary struct is an sockaddr_in6 because that is the biggest one.
 */
class FlashMQSockAddr
{
    struct sockaddr_in6 addr_in6;

public:
    struct sockaddr *getAddr();
    static constexpr int getLen();
};

/**
 * @brief flashmq_get_client_address
 * @param client A client pointer as provided by 'flashmq_plugin_login_check'.
 * @param text If not nullptr, will be assigned the address in text form, like 192.168.1.1 or "2001:0db8:85a3:0000:1319:8a2e:0370:7344".
 * @param addr If not nullptr, will provide a sockaddr struct, for low level operations.
 *
 * The text and addr can be pointers to local variables in the calling context.
 *
 * [Function provided by FlashMQ]
 */
void flashmq_get_client_address(const std::weak_ptr<Client> &client, std::string *text, FlashMQSockAddr *addr);

/**
 * @brief flashmq_plugin_version must return FLASHMQ_PLUGIN_VERSION.
 * @return FLASHMQ_PLUGIN_VERSION.
 *
 * [Must be implemented by plugin]
 */
int flashmq_plugin_version();

/**
 * @brief flashmq_plugin_allocate_thread_memory is called once by each thread. Never again.
 * @param thread_data. Create a memory structure and assign it to *thread_data.
 * @param plugin_opts. Map of flashmq_plugin_opt_* from the config file.
 *
 * Only allocate the plugin's memory here. Don't open connections, etc. That's because the reload mechanism doesn't call this function.
 *
 * Because of the multi-core design of FlashMQ, you should treat each thread as its own domain with its own data. You can use static
 * variables for global scope if you must, or even create threads, but do provide proper locking where necessary.
 *
 * You can throw exceptions on errors.
 *
 * [Must be implemented by plugin]
 */
void flashmq_plugin_allocate_thread_memory(void **thread_data, std::unordered_map<std::string, std::string> &plugin_opts);

/**
 * @brief flashmq_plugin_deallocate_thread_memory is called once by each thread. Never again.
 * @param thread_data. Delete this memory.
 * @param plugin_opts. Map of flashmq_plugin_opt_* from the config file.
 *
 * You can throw exceptions on errors.
 *
 * [Must be implemented by plugin]
 */
void flashmq_plugin_deallocate_thread_memory(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts);

/**
 * @brief flashmq_plugin_init is called on thread start and config reload. It is the main place to initialize the plugin.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 * @param plugin_opts. Map of flashmq_plugin_opt_* from the config file.
 * @param reloading.
 *
 * The best approach to state keeping is doing everything per thread. You can initialize connections to database servers, load encryption keys,
 * create maps, etc.
 *
 * Keep in mind that libraries you use may not be thread safe (by default). Sometimes they use global scope in treacherous ways. As a random
 * example: Qt's QSqlDatabase needs a unique name for each connection, otherwise it is not thread safe and will crash.
 *
 * There is the option to set 'plugin_serialize_init true' in the config file, which allows some mitigation in
 * case you run into problems.
 *
 * You can throw exceptions on errors.
 *
 * [Must be implemented by plugin]
 */
void flashmq_plugin_init(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts, bool reloading);

/**
 * @brief flashmq_plugin_deinit is called on thread stop and config reload. It is the precursor to initializing.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 * @param plugin_opts. Map of flashmq_plugin_opt_* from the config file.
 * @param reloading
 *
 * You can throw exceptions on errors.
 *
 * [Must be implemented by plugin]
 */
void flashmq_plugin_deinit(void *thread_data, std::unordered_map<std::string, std::string> &plugin_opts, bool reloading);

/**
 * @brief flashmq_plugin_periodic is called every x seconds as defined in the config file.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 *
 * You may need to periodically refresh data from a database, post stats, etc. You can do that from here. It's queued
 * in each thread at the same time, so you can perform somewhat synchronized events in all threads.
 *
 * Note that it's executed in the event loop, so it blocks the thread if you block here. If you need asynchronous operation,
 * you can make threads yourself. Be sure to synchronize data access properly in that case.
 *
 * The setting plugin_timer_period sets this interval in seconds.
 *
 * You can throw exceptions on errors.
 *
 * [Can optionally be implemented by plugin]
 */
void flashmq_plugin_periodic_event(void *thread_data);

/**
 * @brief flashmq_plugin_alter_subscription can optionally be implemented if you want to be able to change incoming subscriptions.
 * @param thread_data
 * @param clientid
 * @param topic non-const reference which can be changed.
 * @param subtopics
 * @param qos non-const reference which can be changed.
 * @param userProperties
 * @return boolean indicating whether the subscription was changed. Not returning the truth here results in unpredictable behavior.
 */
bool flashmq_plugin_alter_subscription(void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics,
                                       uint8_t &qos, const std::vector<std::pair<std::string, std::string>> *userProperties);

/**
 * @brief flashmq_plugin_alter_publish allows changing of the non-const arguments.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 * @return boolean indicating whether the packet was changed. It saves FlashMQ from having to do a full compare. Not returning the truth here
 * results in unpredictable behavior.
 *
 * Be aware that changing publishes may incur a (slight) reduction in performance.
 *
 * [Can optionally be implemented by plugin]
 */
bool flashmq_plugin_alter_publish(void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics,
                                  uint8_t &qos, bool &retain, std::vector<std::pair<std::string, std::string>> *userProperties);

/**
 * @brief flashmq_plugin_login_check is called on login of a client.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 * @param username
 * @param password
 * @param client Example use is for storing in a async operation and passing to flashmq_continue_async_authentication.
 * @return
 *
 * You could throw exceptions here, but that will be slow and pointless. It will just get converted into AuthResult::error,
 * because there's nothing else to do: the state of FlashMQ won't change.
 *
 * Note that there is a setting 'plugin_serialize_auth_checks'. Use only as a last resort if your plugin is not
 * thread-safe. It will negate much of FlashMQ's multi-core model.
 *
 * The AuthResult::async can be used if your auth check causes blocking IO (like network). You can save the weak pointer to the client
 * and do the auth in a thread or any kind of async way. FlashMQ's event loop will then continue. You can call flashmq_continue_async_authentication
 * later with the result.
 *
 * [Must be implemented by plugin]
 */
AuthResult flashmq_plugin_login_check(void *thread_data, const std::string &clientid, const std::string &username, const std::string &password,
                                      const std::vector<std::pair<std::string, std::string>> *userProperties, const std::weak_ptr<Client> &client);

/**
 * @brief flashmq_plugin_client_disconnected Called when clients disconnect or their keep-alive expire.
 * @param thread_data
 * @param clientid
 *
 * Is only called for authenticated clients, to avoid spoofing.
 *
 * [Can optionally be implemented by plugin]
 */
void flashmq_plugin_client_disconnected(void *thread_data, const std::string &clientid);

/**
 * @brief flashmq_plugin_acl_check is called on publish, deliver and subscribe.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 * @return
 *
 * You could throw exceptions here, but that will be slow and pointless. It will just get converted into AuthResult::error,
 * because there's nothing else to do: the state of FlashMQ won't change.
 *
 * Controlling subscribe access can have several benefits. For instance, you may want to avoid subscriptions that cause
 * a lot of server load. If clients pester you with many subscriptions like '+/+/+/+/+/+/+/+/+/', that causes a lot
 * of tree walking. Similarly, if all clients subscribe to '#' because it's easy, every single message passing through
 * the server will have to be ACL checked for every subscriber.
 *
 * Note that only MQTT 3.1.1 or higher has a 'failed' return code for subscribing, so older clients will see a normal
 * ack and won't know it failed.
 *
 * Note that there is a setting 'plugin_serialize_auth_checks'. Use only as a last resort if your plugin is not
 * thread-safe. It will negate much of FlashMQ's multi-core model.
 *
 * [Must be implemented by plugin]
 */
AuthResult flashmq_plugin_acl_check(void *thread_data, const AclAccess access, const std::string &clientid, const std::string &username,
                                    const std::string &topic, const std::vector<std::string> &subtopics, const uint8_t qos, const bool retain,
                                    const std::vector<std::pair<std::string, std::string>> *userProperties);

/**
 * @brief flashmq_plugin_extended_auth can be used to implement MQTT 5 extended auth. This is optional.
 * @param thread_data is the memory you allocated in flashmq_plugin_allocate_thread_memory.
 * @param clientid
 * @param stage
 * @param authMethod
 * @param authData
 * @param userProperties are optional (and are nullptr in that case)
 * @param returnData is a non-const string, that you can set to include data back to the client in an AUTH packet.
 * @param username is a non-const string. You can set it, which will then apply to ACL checking and show in the logs.
 * @param client Use this for AuthResult::async. See flashmq_plugin_login_check().
 * @return an AuthResult enum class value
 *
 * [Can optionally be implemented by plugin]
 */
AuthResult flashmq_plugin_extended_auth(void *thread_data, const std::string &clientid, ExtendedAuthStage stage, const std::string &authMethod,
                                        const std::string &authData, const std::vector<std::pair<std::string, std::string>> *userProperties, std::string &returnData,
                                        std::string &username, const std::weak_ptr<Client> &client);

}

#endif // FLASHMQ_PLUGIN_H
