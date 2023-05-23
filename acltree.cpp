#include <cassert>

#include "acltree.h"
#include "utils.h"
#include "exceptions.h"

/**
 * @brief AclNode::getChildren gets the children node, and makes it if not there. Use in places that you know this level in the tree exists or should be created.
 * @param subtopic
 * @return
 */
AclNode *AclNode::getChildren(const std::string &subtopic, bool registerPattern)
{
    std::unique_ptr<AclNode> &node = children[subtopic];

    if (!node)
    {
        node = std::make_unique<AclNode>();

        if (registerPattern)
        {
            if (subtopic == "%u")
                this->_hasUserWildcard = true;

            if (subtopic == "%c")
                this->_hasClientidWildcard = true;
        }
    }

    return node.get();
}

/**
 * @brief AclNode::getChildren is a const version, and will dererence the end iterator (crash) if it doesn't exist. So, hence the assert. Don't to that.
 * @param subtopic
 * @return
 */
const AclNode *AclNode::getChildren(const std::string &subtopic) const
{
    assert(children.find(subtopic) != children.end());
    auto node_it = children.find(subtopic);
    return node_it->second.get();
}

AclNode *AclNode::getChildrenPlus()
{
    if (!childrenPlus)
        childrenPlus = std::make_unique<AclNode>();

    return childrenPlus.get();
}

const AclNode *AclNode::getChildrenPlus() const
{
    assert(childrenPlus);
    return childrenPlus.get();
}

bool AclNode::hasChildrenPlus() const
{
    return childrenPlus.operator bool();
}

bool AclNode::hasChild(const std::string &subtopic) const
{
    if (children.empty())
        return false;

    auto child_it = children.find(subtopic);
    return child_it != children.end();
}

bool AclNode::hasPoundGrants() const
{
    return !grantsPound.empty();
}

bool AclNode::hasUserWildcard() const
{
    return this->_hasUserWildcard;
}

bool AclNode::hasClientidWildcard() const
{
    return _hasClientidWildcard;
}

bool AclNode::isEmpty() const
{
    return this->empty;
}

void AclNode::addGrant(AclGrant grant)
{
    this->empty = false;
    grants.push_back(grant);
}

void AclNode::addGrantPound(AclGrant grant)
{
    this->empty = false;
    grantsPound.push_back(grant);
}

const std::vector<AclGrant> &AclNode::getGrantsPound() const
{
    return this->grantsPound;
}

const std::vector<AclGrant> &AclNode::getGrants() const
{
    return this->grants;
}

AclTree::AclTree()
{
    collectedPermissions.reserve(16);
}

/**
 * @brief AclTree::addTopic adds a fixed topic or pattern to the ACL tree.
 * @param pattern
 * @param aclGrant
 * @param type
 * @param username is ignored for 'pattern' type, because patterns apply to all users;
 */
void AclTree::addTopic(const std::string &pattern, AclGrant aclGrant, AclTopicType type, const std::string &username)
{
    const std::vector<std::string> subtopics = splitTopic(pattern);

    AclNode *curEnd = &rootAnonymous;

    if (type == AclTopicType::Patterns)
        curEnd = &rootPatterns;
    else if (!username.empty())
    {
        curEnd = &rootPerUser[username];
    }

    for (const auto &subtop : subtopics)
    {
        AclNode *subnode = nullptr;

        if (subtop == "+")
            subnode = curEnd->getChildrenPlus();
        else if (subtop == "#")
        {
            curEnd->addGrantPound(aclGrant);
            return;
        }
        else
            subnode = curEnd->getChildren(subtop, type == AclTopicType::Patterns);

        curEnd = subnode;
    }

    curEnd->addGrant(aclGrant);
}

void AclTree::findPermissionRecursive(std::vector<std::string>::const_iterator cur_published_subtopic_it, std::vector<std::string>::const_iterator end,
                                      const AclNode *this_node, std::vector<AclGrant> &collectedPermissions, const std::string &username,
                                      const std::string &clientid) const
{
    const std::string &cur_published_subtop = *cur_published_subtopic_it;

    if (cur_published_subtopic_it == end)
    {
        const std::vector<AclGrant> &grants = this_node->getGrants();
        collectedPermissions.insert(collectedPermissions.end(), grants.begin(), grants.end());
        return;
    }

    if (this_node->hasPoundGrants())
    {
        const std::vector<AclGrant> &grants = this_node->getGrantsPound();
        collectedPermissions.insert(collectedPermissions.end(), grants.begin(), grants.end());
    }

    const auto next_subtopic_it = ++cur_published_subtopic_it;

    if (this_node->hasChild(cur_published_subtop))
    {
        const AclNode *sub_node = this_node->getChildren(cur_published_subtop);
        findPermissionRecursive(next_subtopic_it, end, sub_node, collectedPermissions, username, clientid);
    }

    if (this_node->hasUserWildcard() && cur_published_subtop == username)
    {
        const AclNode *sub_node = this_node->getChildren("%u");
        findPermissionRecursive(next_subtopic_it, end, sub_node, collectedPermissions, username, clientid);
    }

    if (this_node->hasClientidWildcard() && cur_published_subtop == clientid)
    {
        const AclNode *sub_node = this_node->getChildren("%c");
        findPermissionRecursive(next_subtopic_it, end, sub_node, collectedPermissions, username, clientid);
    }

    if (this_node->hasChildrenPlus())
    {
        findPermissionRecursive(next_subtopic_it, end, this_node->getChildrenPlus(), collectedPermissions, username, clientid);
    }
}

/**
 * @brief AclTree::findPermission tests permissions as loaded from the Mosquitto-compatible acl_file.
 * @param subtopicsPublish
 * @param access Whether to test read access or write access (`AclGrant::Read` or `AclGrant::Write` respectively).
 * @param username The user to test permission for.
 * @return
 *
 * It behaves like Mosquitto's ACL file. Some of that behavior is a bit limited, but sticking to it for compatability:
 *
 * - If your user is authenticated, there must a user specific definition for that user; it won't fall back on anonymous ACLs.
 * - You can't combine ACLs, like 'all clients read bla/#' and add 'user john readwrite bla/#. User specific ACLs don't add
 *   to the general (anonymous) ACLs.
 * - You can't specify 'any authenticated user'.
 */
AuthResult AclTree::findPermission(const std::vector<std::string> &subtopicsPublish, AclGrant access, const std::string &username, const std::string &clientid)
{
    assert(access == AclGrant::Read || access == AclGrant::Write);
    assert(!clientid.empty());

    collectedPermissions.clear();

    if (username.empty() && !rootAnonymous.isEmpty())
        findPermissionRecursive(subtopicsPublish.begin(), subtopicsPublish.end(), &rootAnonymous, collectedPermissions, username, clientid);
    else
    {
        auto it = rootPerUser.find(username);
        if (it != rootPerUser.end())
        {
            AclNode &rootOfUser = it->second;
            if (!rootOfUser.isEmpty())
                findPermissionRecursive(subtopicsPublish.begin(), subtopicsPublish.end(), &rootOfUser, collectedPermissions, username, clientid);
        }
    }

    if (std::find(collectedPermissions.begin(), collectedPermissions.end(), AclGrant::Deny) != collectedPermissions.end())
        return AuthResult::acl_denied;

    if (!rootPatterns.isEmpty())
        findPermissionRecursive(subtopicsPublish.begin(), subtopicsPublish.end(), &rootPatterns, collectedPermissions, username, clientid);

    if (collectedPermissions.empty())
        return AuthResult::acl_denied;

    bool allowed = false;

    for(AclGrant grant : collectedPermissions)
    {
        // A deny always overrides all other declarations.
        if (grant == AclGrant::Deny)
            return AuthResult::acl_denied;

        if (access == AclGrant::Read && (grant == AclGrant::Read || grant == AclGrant::ReadWrite))
            allowed = true;
        if (access == AclGrant::Write && (grant == AclGrant::Write || grant == AclGrant::ReadWrite))
            allowed = true;
    }

    AuthResult result = allowed ? AuthResult::success : AuthResult::acl_denied;
    return result;
}



AclGrant stringToAclGrant(const std::string &s)
{
    const std::string s2 = str_tolower(s);
    AclGrant x = AclGrant::Deny;

    if (s2 == "read")
        x = AclGrant::Read;
    else if (s2 == "write")
        x = AclGrant::Write;
    else if (s2 == "readwrite")
        x = AclGrant::ReadWrite;
    else if (s2 == "deny")
        x = AclGrant::Deny;
    else
        throw ConfigFileException(formatString("Acl grant '%s' is invalid", s.c_str()));

    return x;
}
