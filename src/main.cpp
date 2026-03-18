#include "SoldierFederate.h"

#include <iostream>

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
    catch (const std::exception& ex)
    {
        std::cerr << "Exception: " << ex.what() << "\n";
        return 1;
    }
    catch (...)
    {
        std::cerr << "Unknown exception\n";
        return 2;
    }

    return 0;
}
