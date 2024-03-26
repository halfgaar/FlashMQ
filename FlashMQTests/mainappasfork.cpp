#include "mainappasfork.h"
#include <mainapp.h>
#include "signal.h"
#include "fmqmain.h"
#include "sys/wait.h"

std::string MainAppAsFork::getConfigFileFromArgs(const std::vector<std::string> &args)
{
    std::string result = "";
    bool next = false;
    for(const std::string &arg : args)
    {
        if (arg == "--config-file")
        {
            next = true;
            continue;
        }

        if (next)
        {
            result = arg;
            break;
        }
    }
    return result;
}

MainAppAsFork::MainAppAsFork()
{
    defaultConf.writeLine("allow_anonymous true");
    defaultConf.closeFile();
    args.push_back("--config-file");
    args.push_back(defaultConf.getFilePath());
}

MainAppAsFork::MainAppAsFork(const std::vector<std::string> &args) :
    args(args)
{

}

MainAppAsFork::~MainAppAsFork()
{
    this->stop();
}

void MainAppAsFork::start()
{
    // We must not have threads when we fork.
    Logger::stopAndReset();

    if (MainApp::instance)
    {
        throw std::runtime_error("You can only use the forking test server if the main app is not constructed already. Do cleanup() first.");
    }

    pid_t pid = fork();

    if (pid < 0)
        throw std::runtime_error("What the fork?");

    if (pid == 0)
    {
        try
        {
            std::list<std::vector<char>> argCopies;

            const std::string programName = "FlashMQTests";
            std::vector<char> programNameCopy(programName.size() + 1, 0);
            std::copy(programName.begin(), programName.end(), programNameCopy.begin());
            argCopies.push_back(std::move(programNameCopy));

            for (const std::string &arg : args)
            {
                std::vector<char> copyArg(arg.size() + 1, 0);
                std::copy(arg.begin(), arg.end(), copyArg.begin());
                argCopies.push_back(std::move(copyArg));
            }

            char *argv[256];
            memset(argv, 0, 256*sizeof (char*));

            int i = 0;
            for (std::vector<char> &copy : argCopies)
            {
                argv[i++] = copy.data();
            }

            int r = fmqmain(i, argv);
            ::exit(r);
        }
        catch (std::exception &ex)
        {
            std::cout << "The forked process threw an exception: " << ex.what() << std::endl;
            std::cerr << "The forked process threw an exception: " << ex.what() << std::endl;
        }

        // Does not call destructors.
        abort();
    }

    this->child = pid;
}

void MainAppAsFork::stop()
{
    if (this->child <= 0)
        return;

    kill(this->child, SIGTERM);
    int status = 0;
    waitpid(this->child, &status, 0);

    this->child = -1;
}

void MainAppAsFork::waitForStarted(int port)
{
    int sockfd = check<std::runtime_error>(socket(AF_INET, SOCK_STREAM, 0));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_port = htons(port);
    addr.sin_family = AF_INET;

    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    struct sockaddr *addr2 = reinterpret_cast<sockaddr*>(&addr);

    bool timeout = false;
    int n = 0;
    while (connect(sockfd, addr2, sizeof(struct sockaddr_in)) != 0)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        if (n++ > 100)
        {
            timeout = true;
            break;
        }
    }

    close(sockfd);

    if (timeout)
        throw std::runtime_error("Forked mainapp failed to start?");
}
