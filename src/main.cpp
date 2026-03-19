#include "SoldierFederate.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace
{
std::string nowForLogLine()
{
    const std::time_t now = std::time(nullptr);
    std::tm tmValue{};
#ifdef _WIN32
    localtime_s(&tmValue, &now);
#else
    localtime_r(&now, &tmValue);
#endif

    std::ostringstream out;
    out << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void appendFatalLog(const std::string& message)
{
    namespace fs = std::filesystem;

    try
    {
        fs::create_directories("logs");
        std::ofstream fatalLog((fs::path("logs") / "fatal.log").string(), std::ios::out | std::ios::app);
        if (fatalLog)
        {
            fatalLog << "[" << nowForLogLine() << "] " << message << "\n";
        }
    }
    catch (...)
    {
        // Best-effort fallback logging only.
    }
}
}

int main(int argc, char* argv[])
{
    std::string name = "SoldierFederate";
    if (argc > 1)
    {
        name = argv[1];
    }

    try
    {
        SoldierFederate federate(name);
        federate.run();
    }
    catch (const rti1516e::Exception& ex)
    {
        std::wcerr << L"RTI Exception: " << ex.what() << L"\n";

        const std::wstring wideMessage(ex.what());
        const std::string narrowMessage(wideMessage.begin(), wideMessage.end());
        appendFatalLog(std::string("Unhandled RTI exception in main: ") + narrowMessage);
        return 1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Exception: " << ex.what() << "\n";
        appendFatalLog(std::string("Unhandled std::exception in main: ") + ex.what());
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown exception\n";
        appendFatalLog("Unhandled unknown exception in main.");
        return 2;
    }

    return 0;
}
