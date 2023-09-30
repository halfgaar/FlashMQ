#include "filecloser.h"

#include <unistd.h>

FileCloser::FileCloser(int fd) :
    fd(fd)
{

}

FileCloser::~FileCloser()
{
    if (fd >= 0)
        close(fd);
    fd = -1;
}
