/*
This file is part of FlashMQ (https://www.flashmq.org)
Copyright (C) 2021-2023 Wiebe Cazemier

FlashMQ is free software: you can redistribute it and/or modify
it under the terms of The Open Software License 3.0 (OSL-3.0).

See LICENSE for license details.
*/

#include "haproxy.h"

#include <errno.h>
#include <stdexcept>
#include "utils.h"

/**
 * @brief read_ha_proxy_helper This function simplyfies reading for HAProxy in that it doesn't need to deal with incomplete data (because HAProxy frames
 * are always complete), so those cases can just be an error.
 * @param fd
 * @param buf
 * @param nbytes
 * @return
 */
size_t read_ha_proxy_helper(int fd, void *buf, size_t nbytes)
{
    if (nbytes == 0)
        return 0;

    ssize_t n;
    while ((n = read(fd, buf, nbytes)) != 0)
    {
        if (n == 0)
            throw std::runtime_error("Client disconnected before all HA proxy data could be read");
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            else if (errno == EAGAIN || errno == EWOULDBLOCK)
                throw std::runtime_error("Incomplete HAProxy data");
            else
                check<std::runtime_error>(n);
        }

        break;
    }

    if (static_cast<ssize_t>(nbytes) != n)
        throw std::runtime_error("Not an HAProxy frame.");

    return n;
}
