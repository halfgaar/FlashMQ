/*
 * This file is part of FlashMQ (https://www.flashmq.org). It defines the
 * plugin interface.
 *
 * - flashmq_plugin.h defines functions to be implemented by plugins.
 * - flashmq_public.h describes FlashMQ's functions available to plugins.
 *
 * Those two files are public domain and you are encouraged
 * to copy them to your plugin project for portability. Including those files
 * in your project does not make it a 'derivative work'.
 *
 * Compile like: gcc -fPIC -shared plugin.cpp -o plugin.so
 *
 * It's best practice to build your plugin with the same library versions of the
 * build of FlashMQ you're using. In practice, this means building on the OS
 * version you're running on. This also means using the AppImage build of FlashMQ
 * is not really compatible with plugins, because that includes older, and fixed,
 * versions of various libraries.
 *
 * For instance, if you use OpenSSL: by the time your plugin is loaded, FlashMQ will
 * have already dynamically linked OpenSSL. If you then try to call OpenSSL
 * functions, you'll run into ABI incompatibilities.
 */

#ifndef FLASHMQ_PLUGIN_H
#define FLASHMQ_PLUGIN_H

#include <unordered_map>
#include <string_view>
#include <optional>
#include "flashmq_public.h"

#define FLASHMQ_PLUGIN_VERSION 5

extern "C"
{

/**
 * @brief flashmq_plugin_version must return FLASHMQ_PLUGIN_VERSION.
 * @return FLASHMQ_PLUGIN_VERSION.
 *
 * [Must be implemented by plugin]
 */
int flashmq_plugin_version();

/**
 * @brief flashmq_plugin_main_init is called once before the event loops start.
 * @param plugin_opts
 *
 * [Can optionally be implemented by plugin]
 */
void flashmq_plugin_main_init(std::unordered_map<std::string, std::string> &plugin_opts);

/**
 * @brief flashmq_plugin_main_deinit is the complementary pair of flashmq_plugin_main_init(). It's called after the threads have stopped.
 * @param plugin_opts
 *
 * [Can optionally be implemented by plugin]
 */
void flashmq_plugin_main_deinit(std::unordered_map<std::string, std::string> &plugin_opts);

/**
 * @brief flashmq_plugin_allocate_thread_memory is called once by each thread. Never again.
 * @param thread_data. Create a memory structure and assign it to *thread_data.
 * @param plugin_opts. Map of flashmq_plugin_opt_* from the config file.
 *
 * Only allocate the plugin's memory here, or other things that you really only have to do once. Don't open connections, etc. That's
 * because the reload mechanism doesn't call this function.
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
 * create maps, etc. However, remember that for instance with libcurl, initing a 'multi handle' is best not done here, or at least not done
 * AGAIN on reload, because it will distrupt your ongoing transfers. Memory structures that you really only need to init once are best
 * done in 'flashmq_plugin_allocate_thread_memory()' or even 'flashmq_plugin_main_init()'.
 *
 * Keep in mind that libraries you use may not be thread safe (by default). Sometimes they use global scope in treacherous ways. As a random
 * example: Qt's QSqlDatabase needs a unique name for each connection, otherwise it is not thread safe and will crash. It will also hide away
 * libmysqlclient's requirement to do a global one-time init, that would be best done in 'flashmq_plugin_main_init()'.
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
 *
 * In case of shared subscriptions, you will see the original subscription path, like '$share/myshare/battery/voltage'. You have the
 * chance to change every aspect of it, like make it non-shared.
 *
 * [Can optionally be implemented by plugin]
 */
bool flashmq_plugin_alter_subscription(
    void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics,
    uint8_t &qos, const std::vector<std::pair<std::string, std::string>> *userProperties);

/**
 * @brief flashmq_plugin_alter_publish allows changing of the non-const arguments.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 * @return boolean indicating whether the packet was changed. It saves FlashMQ from having to do a full compare. Not returning the truth here
 * results in unpredictable behavior. Note: this only applies to the topic, because FlashMQ has to know whether to resplit
 * the topic string. You can change other flags and still return false.
 *
 * Be aware that changing publishes may incur a (slight) reduction in performance.
 *
 * [Can optionally be implemented by plugin]
 */
bool flashmq_plugin_alter_publish(
    void *thread_data, const std::string &clientid, std::string &topic, const std::vector<std::string> &subtopics,
    std::string_view payload, uint8_t &qos, bool &retain, const std::optional<std::string> &correlationData,
    const std::optional<std::string> &responseTopic, const std::optional<std::string> &contentType,
    std::vector<std::pair<std::string, std::string>> *userProperties);

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
AuthResult flashmq_plugin_login_check(
    void *thread_data, const std::string &clientid, const std::string &username, const std::string &password,
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
 * @brief flashmq_plugin_on_unsubscribe is called after unsubscribe. Unsubscribe actions can't be manipulated or blocked.
 * @param topic Does not contain the share name.
 * @param subtopics Does not contain the share name.
 *
 * [Can optionally be implemented by plugin]
 */
void flashmq_plugin_on_unsubscribe(
    void *thread_data, const std::weak_ptr<Session> &session, const std::string &clientid,
    const std::string &username, const std::string &topic, const std::vector<std::string> &subtopics,
    const std::string &shareName, const std::vector<std::pair<std::string, std::string>> *userProperties);

/**
 * @brief flashmq_plugin_acl_check is called on publish, deliver and subscribe.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 * @param shareName The shared subscription name in a filter like '$share/my_share_name/one/two'. Is only present on AclAccess::subscribe.
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
 * When the 'access' is 'subscribe' and it's a shared subscription (like '$share/myshare/one/two/three'), you only get
 * the effective topic filter (like 'one/two/three'). However, since plugin version 4, there is the argument 'shareName' for that.
 *
 * [Must be implemented by plugin]
 */
AuthResult flashmq_plugin_acl_check(
    void *thread_data, const AclAccess access, const std::string &clientid, const std::string &username,
    const std::string &topic, const std::vector<std::string> &subtopics, const std::string &shareName,
    std::string_view payload, const uint8_t qos, const bool retain,
    const std::optional<std::string> &correlationData, const std::optional<std::string> &responseTopic,
    const std::optional<std::string> &contentType,
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
AuthResult flashmq_plugin_extended_auth(
    void *thread_data, const std::string &clientid, ExtendedAuthStage stage, const std::string &authMethod,
    const std::string &authData, const std::vector<std::pair<std::string, std::string>> *userProperties, std::string &returnData,
    std::string &username, const std::weak_ptr<Client> &client);

/**
 * @brief Is called when the socket watched by 'flashmq_poll_add_fd()' has an event.
 * @param thread_data is memory allocated in flashmq_plugin_allocate_thread_memory().
 * @param fd
 * @param events contains the events as a bit flags. See 'man epoll'.
 * @param p can be made back into your type with 'std::shared_ptr<T> sp = std::static_pointer_cast<T>(b.lock())'.
 *        This allows you to properly lend pointers to the event system that you can actually check for expiration.
 *
 * [Can optionally be implemented by plugin]
 */
void flashmq_plugin_poll_event_received(void *thread_data, int fd, uint32_t events, const std::weak_ptr<void> &p);

}

#endif // FLASHMQ_PLUGIN_H
