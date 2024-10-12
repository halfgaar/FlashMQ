#ifndef FDMANAGED_H
#define FDMANAGED_H


class FdManaged
{
    int fd = -1;

public:
    FdManaged() = default;
    FdManaged(int fd);
    FdManaged(const FdManaged &other) = delete;
    FdManaged(FdManaged &&other) = delete;
    ~FdManaged();

    int get() const { return fd; }
};

#endif // FDMANAGED_H
