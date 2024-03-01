#ifndef CONFFILETEMP_H
#define CONFFILETEMP_H

#include <string>

class ConfFileTemp
{
    int fd = -1;
    std::string filePath;

public:
    ConfFileTemp();
    ~ConfFileTemp();

    const std::string &getFilePath() const;
    void writeLine(const std::string &line);
    void closeFile();

    ConfFileTemp &operator=(const ConfFileTemp &other) = delete;
    ConfFileTemp(const ConfFileTemp &other) = delete;
    ConfFileTemp(ConfFileTemp &&other) = delete;
};

#endif // CONFFILETEMP_H
