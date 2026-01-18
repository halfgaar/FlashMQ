#include "mainappinthread.h"

MainAppInThread::MainAppInThread()
{

}

MainAppInThread::MainAppInThread(const std::vector<std::string> &args) :
    mArgs(args)
{

}

MainAppInThread::~MainAppInThread()
{
    stopApp();
}

void MainAppInThread::start()
{
    const auto args = mArgs;

    auto thread_task = [this, args]()
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

        std::shared_ptr<MainApp> mainapp;

        {
            auto mainapp_locked = mMainApp.lock();
            std::shared_ptr<MainApp> &mainapp_member = *mainapp_locked;
            mainapp_member = MainApp::initMainApp(i, argv);
            mainapp = mainapp_member;
        }

        // A hack: when I supply args I probably define a config for auth stuff.
        if (args.empty())
            mainapp->settings.allowAnonymous = true;

        mainapp->start();
    };

    this->thread = std::thread(thread_task);
}

void MainAppInThread::stopApp()
{
    {
        auto mainapp_locked = mMainApp.lock();
        std::shared_ptr<MainApp> &mainapp = *mainapp_locked;

        if (mainapp)
            mainapp->quit();
    }

    if (this->thread.joinable())
        this->thread.join();
}

void MainAppInThread::waitForStarted()
{
    auto started_f = [this]
    {
        std::shared_ptr<MainApp> mainapp;

        {
            auto mainapp_locked = mMainApp.lock();
            mainapp = *mainapp_locked;
        }

        if (!mainapp)
            return false;

        return mainapp->getStarted();
    };

    int n = 0;
    while(!started_f())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (n++ > 500)
            throw std::runtime_error("Waiting for app to start failed.");
    }
}

