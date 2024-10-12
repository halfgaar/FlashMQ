#include "fdmanaged.h"

#include <unistd.h>

FdManaged::FdManaged(int fd) :
    fd(fd)
{

}

FdManaged::~FdManaged()
{
    if (fd > 0) // For some tech debt reasons we do > 0 instead of >= 0, which it should be.
    {
        close(fd);
        fd = -1;
    }
}
