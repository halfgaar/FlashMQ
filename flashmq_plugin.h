/*
 * This file is part of FlashMQ (https://www.flashmq.org). It defines the
 * authentication plugin interface.
 *
 * This interface definition is public domain and you are encouraged
 * to copy it to your authentication plugin project, for portability. Including
 * this file in your project does not require your code to have a compatibile
 * license nor requires you to open source it.
 *
 * Compile like: gcc -fPIC -shared authplugin.cpp -o authplugin.so
 */

#ifndef FLASHMQ_PLUGIN_H
#define FLASHMQ_PLUGIN_H

#include <string>
#include <vector>
#include <unordered_map>

#define FLASHMQ_PLUGIN_VERSION 1

// Compatible with Mosquitto, for auth plugin compatability.
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
    subscribe = 4
};

/**
 * @brief The AuthResult enum's numbers are compatible with Mosquitto's auth result.
 */
enum class AuthResult
{
    success = 0,
    acl_denied = 12,
    login_denied = 11,
    error = 13
};

/**
 * @brief The FlashMQMessage struct contains the meta data of a publish.
 *
 * The subtopics is the topic split, so you don't have to do that anymore.
 *
 * As for 'retain', keep in mind that for existing subscribers, this will always be false [MQTT-3.3.1-9]. Only publishes or
 * retain messages as a result of a subscribe can have that set to true.
 *
 * For subscribtions, 'retain' is always false.
 */
struct FlashMQMessage
{
    const std::string &topic;
    const std::vector<std::string> &subtopics;
    const char qos;
    const bool retain;

    FlashMQMessage(const std::string &topic, const std::vector<std::string> &subtopics, const char qos, const bool retain);
};

/**
 * @brief flashmq_logf calls the internal logger of FlashMQ. The logger mutexes all access, so is thread-safe.
 * @param level is any of the levels defined above, starting with LOG_.
 * @param str
 *
 * FlashMQ makes no distinction between INFO and NOTICE.
 */
void flashmq_logf(int level, const char *str, ...);

/**
 * @brief flashmq_plugin_version must return FLASHMQ_PLUGIN_VERSION.
 * @return FLASHMQ_PLUGIN_VERSION.
 */
int flashmq_auth_plugin_version();

/**
 * @brief flashmq_auth_plugin_allocate_thread_memory is called once by each thread. Never again.
 * @param thread_data. Create a memory structure and assign it to *thread_data.
 * @param global_data. The global data created in flashmq_auth_plugin_allocate_global_memory, if you use it.
 * @param auth_opts. Map of flashmq_auth_opt_* from the config file.
 *
 * Only allocate the plugin's memory here. Don't open connections, etc.
 *
 * The global data is created by flashmq_auth_plugin_allocate_global_memory() and if you need it, you can assign it to your
 * own thread_data storage. It is not passed as argument to other functions.
 *
 * You can use static variables for global scope if you must, but do provide proper locking where necessary.
 *
 * throw an exception on errors.
 */
void flashmq_auth_plugin_allocate_thread_memory(void **thread_data, std::unordered_map<std::string, std::string> &auth_opts);

/**
 * @brief flashmq_auth_plugin_deallocate_thread_memory is called once by each thread. Never again.
 * @param thread_data. Delete this memory.
 * @param auth_opts. Map of flashmq_auth_opt_* from the config file.
 *
 * throw an exception on errors.
 */
void flashmq_auth_plugin_deallocate_thread_memory(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts);

/**
 * @brief flashmq_auth_plugin_init is called on thread start and config reload. It is the main place to initialize the plugin.
 * @param thread_data is memory allocated in flashmq_auth_plugin_allocate_thread_memory().
 * @param auth_opts. Map of flashmq_auth_opt_* from the config file.
 * @param reloading.
 *
 * The best approach to state keeping is doing everything per thread. You can initialize connections to database servers, load encryption keys,
 * create maps, etc.
 *
 * Keep in mind that libraries you use may not be thread safe (by default). Sometimes they use global scope in treacherous ways. As a random
 * example: Qt's QSqlDatabase needs a unique name for each connection, otherwise it is not thread safe and will crash.
 *
 * There is the option to set 'auth_plugin_serialize_init true' in the config file, which allows some mitigation in
 * case you run into problems.
 *
 * throw an exception on errors.
 */
void flashmq_auth_plugin_init(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts, bool reloading);

/**
 * @brief flashmq_auth_plugin_deinit is called on thread stop and config reload. It is the precursor to initializing.
 * @param thread_data is memory allocated in flashmq_auth_plugin_allocate_thread_memory().
 * @param auth_opts. Map of flashmq_auth_opt_* from the config file.
 * @param reloading
 *
 * throw an exception on errors.
 */
void flashmq_auth_plugin_deinit(void *thread_data, std::unordered_map<std::string, std::string> &auth_opts, bool reloading);

/**
 * @brief flashmq_auth_plugin_periodic is called every x seconds as defined in the config file.
 * @param thread_data is memory allocated in flashmq_auth_plugin_allocate_thread_memory().
 *
 * You may need to periodically refresh data from a database, post stats, etc. You can do that from here. It's queued
 * in each thread at the same time, so you can perform somewhat synchronized events in all threads.
 *
 * Note that it's executed in the event loop, so it blocks the thread if you block here. If you need asynchronous operation,
 * you can make threads yourself. Be sure to synchronize data access properly in that case.
 *
 * The setting auth_plugin_timer_period sets this interval in seconds.
 *
 * Implementing this is optional.
 *
 * throw an exception on errors.
 */
void flashmq_auth_plugin_periodic_event(void *thread_data);

/**
 * @brief flashmq_auth_plugin_login_check is called on login of a client.
 * @param thread_data is memory allocated in flashmq_auth_plugin_allocate_thread_memory().
 * @param username
 * @param password
 * @return
 *
 * You could throw exceptions here, but that will be slow and pointless. It will just get converted into AuthResult::error,
 * because there's nothing else to do: the state of FlashMQ won't change.
 *
 * Note that there is a setting 'auth_plugin_serialize_auth_checks'. Use only as a last resort if your plugin is not
 * thread-safe. It will negate much of FlashMQ's multi-core model.
 */
AuthResult flashmq_auth_plugin_login_check(void *thread_data, const std::string &username, const std::string &password);

/**
 * @brief flashmq_auth_plugin_acl_check is called on publish, deliver and subscribe.
 * @param thread_data is memory allocated in flashmq_auth_plugin_allocate_thread_memory().
 * @param access
 * @param clientid
 * @param username
 * @param msg. See FlashMQMessage.
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
 * Note that there is a setting 'auth_plugin_serialize_auth_checks'. Use only as a last resort if your plugin is not
 * thread-safe. It will negate much of FlashMQ's multi-core model.
 */
AuthResult flashmq_auth_plugin_acl_check(void *thread_data, AclAccess access, const std::string &clientid, const std::string &username, const FlashMQMessage &msg);

}

#endif // FLASHMQ_PLUGIN_H
