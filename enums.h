#ifndef ENUMS_H
#define ENUMS_H

// Compatible with Mosquitto
enum class AclAccess
{
    none = 0,
    read = 1,
    write = 2
};

// Compatible with Mosquitto
enum class AuthResult
{
    success = 0,
    acl_denied = 12,
    login_denied = 11,
    error = 13
};

#endif // ENUMS_H
