#ifndef ACLTREE_H
#define ACLTREE_H

#include <memory>
#include <vector>
#include <unordered_map>

#include "utils.h"
#include "enums.h"
#include "exceptions.h"
#include "logger.h"

enum class AclGrant
{
    Deny,
    Read,
    Write,
    ReadWrite
};

enum class AclTopicType
{
    Strings,
    Patterns
};

AclGrant stringToAclGrant(const std::string &s);

/**
 * MQTT topic paths can be broken down into a tree of `AclNode`s.
 */
class AclNode
{
    bool empty = false;

    std::unordered_map<std::string, std::unique_ptr<AclNode>> children;

    std::unique_ptr<AclNode> childrenPlus; // The + sign in MQTT represents a single-level wildcard
    std::vector<AclGrant> grants;
    std::vector<AclGrant> grantsPound; // The # sign. This is short-hand for avoiding one memory access though a layer of std::unique_ptr<AclNode>

    bool _hasUserWildcard = false; // %u
    bool _hasClientidWildcard = false; // %c

public:
    AclNode *getChildren(const std::string &subtopic, bool registerPattern);
    const AclNode *getChildren(const std::string &subtopic) const;
    AclNode *getChildrenPlus();
    const AclNode *getChildrenPlus() const;
    bool hasChildrenPlus() const;
    bool hasChild(const std::string &subtopic) const;
    bool hasPoundGrants() const;
    bool hasUserWildcard() const;
    bool hasClientidWildcard() const;
    bool isEmpty() const;

    void addGrant(AclGrant grant);
    void addGrantPound(AclGrant grant);
    const std::vector<AclGrant> &getGrants() const;
    const std::vector<AclGrant> &getGrantsPound() const;
};

/**
 * @brief The AclTree class represents (Mosquitto compatible) permissions from mosquitto_acl_file. It's not thread safe, and designed for per-thread use.
 */
class AclTree
{
    Logger *logger = Logger::getInstance();
    AclNode rootAnonymous;
    std::unordered_map<std::string, AclNode> rootPerUser;
    AclNode rootPatterns;

    std::vector<AclGrant> collectedPermissions;

    void findPermissionRecursive(std::vector<std::string>::const_iterator cur_subtopic_it, std::vector<std::string>::const_iterator end,
                                 const AclNode *node, std::vector<AclGrant> &collectedPermissions, const std::string &username, const std::string &clientid) const;

public:
    AclTree();

    void addTopic(const std::string &pattern, AclGrant aclGrant, AclTopicType type, const std::string &username = std::string());
    AuthResult findPermission(const std::vector<std::string> &subtopicsPublish, AclGrant access, const std::string &username, const std::string &clientid);
};

#endif // ACLTREE_H
