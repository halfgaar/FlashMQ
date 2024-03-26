#include "mainappinthread.h"

MainAppInThread::MainAppInThread()
{
    MainApp::initMainApp(1, nullptr);
    appInstance = MainApp::getMainApp();
    appInstance->settings.allowAnonymous = true;
}

MainAppInThread::MainAppInThread(const std::vector<std::string> &args)
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

    MainApp::initMainApp(i, argv);
    appInstance = MainApp::getMainApp();

    // A hack: when I supply args I probably define a config for auth stuff.
    if (args.empty())
        appInstance->settings.allowAnonymous = true;
}

MainAppInThread::~MainAppInThread()
{
    stopApp();

    if (appInstance)
    {
        delete appInstance;
        MainApp::instance = nullptr;
    }
    appInstance = nullptr;
}

void MainAppInThread::start()
{
    auto f = std::bind(&MainApp::start, this->appInstance);
    this->thread = std::thread(f);
}

void MainAppInThread::stopApp()
{
    if (this->appInstance)
        this->appInstance->quit();

    if (this->thread.joinable())
        this->thread.join();
}

void MainAppInThread::waitForStarted()
{
    int n = 0;
    while(!appInstance->getStarted())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (n++ > 500)
            throw std::runtime_error("Waiting for app to start failed.");
    }
}

std::shared_ptr<SubscriptionStore> MainAppInThread::getStore()
{
    return appInstance->getSubscriptionStore();
}
