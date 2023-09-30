#ifndef FILECLOSER_H
#define FILECLOSER_H


class FileCloser
{
    int fd = -1;

public:
    FileCloser(int fd);
    ~FileCloser();
};

#endif // FILECLOSER_H
