#include "SoldierFederate.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <thread>

namespace std {
template<class T>
class auto_ptr {
public:
    auto_ptr(T* p = nullptr) : ptr(p) {}
    ~auto_ptr() { delete ptr; }
    T* release() { T* p = ptr; ptr = nullptr; return p; }
    T* get() const { return ptr; }
    auto_ptr& operator=(auto_ptr& other) { if (this != &other) { delete ptr; ptr = other.release(); } return *this; }
private:
    T* ptr;
};
}

using namespace rti1516e;

namespace
{
std::string toNarrow(const std::wstring& text)
{
    return std::string(text.begin(), text.end());
}

std::string toNarrow(const wchar_t* text)
{
    if (text == nullptr)
    {
        return std::string();
    }

    std::wstring wide(text);
    return std::string(wide.begin(), wide.end());
}

std::tm localTimeNow()
{
    const std::time_t now = std::time(nullptr);
    std::tm tmValue{};
#ifdef _WIN32
    localtime_s(&tmValue, &now);
#else
    localtime_r(&now, &tmValue);
#endif
    return tmValue;
}

std::string timestampForFile()
{
    std::ostringstream out;
    const std::tm tmValue = localTimeNow();
    out << std::put_time(&tmValue, "%Y%m%d_%H%M%S");
    return out.str();
}

std::string timestampForLogLine()
{
    std::ostringstream out;
    const std::tm tmValue = localTimeNow();
    out << std::put_time(&tmValue, "%Y-%m-%d %H:%M:%S");
    return out.str();
}
}

static std::vector<uint8_t> encodeDouble(double value)
{
    std::vector<uint8_t> data(sizeof(double));
    std::memcpy(data.data(), &value, sizeof(double));
    return data;
}

static double decodeDouble(const VariableLengthData& data)
{
    double value = 0.0;
    if (data.size() >= sizeof(double))
    {
        std::memcpy(&value, data.data(), sizeof(double));
    }
    return value;
}

static std::vector<uint8_t> encodeInt32(int32_t value)
{
    std::vector<uint8_t> data(sizeof(int32_t));
    std::memcpy(data.data(), &value, sizeof(int32_t));
    return data;
}

static int32_t decodeInt32(const VariableLengthData& data)
{
    int32_t value = 0;
    if (data.size() >= sizeof(int32_t))
    {
        std::memcpy(&value, data.data(), sizeof(int32_t));
    }
    return value;
}

static std::vector<uint8_t> encodeString(const std::string& str)
{
    return std::vector<uint8_t>(str.begin(), str.end());
}

static std::string decodeString(const VariableLengthData& data)
{
    return std::string(reinterpret_cast<const char*>(data.data()), data.size());
}

SoldierFederate::SoldierFederate(const std::string& federateName)
    : federateName_(federateName)
{
    openLogFile();
    logMessage("INFO", "Starting federate process.");

    try
    {
        initializeRTI();
        createOrJoinFederation();
        publishAndSubscribe();

        // Set initial local soldier state
        localSoldier_.name = federateName;
        localSoldier_.x = 0.0;
        localSoldier_.y = 0.0;
        localSoldier_.z = 0.0;
        localSoldier_.health = 100;
        localSoldier_.alive = true;

        logMessage("INFO", "Initialization completed successfully.");
    }
    catch (const Exception& ex)
    {
        shutdownReason_ = "Initialization RTI exception: " + toNarrow(ex.what());
        logMessage("ERROR", shutdownReason_);
        throw;
    }
    catch (const std::exception& ex)
    {
        shutdownReason_ = std::string("Initialization exception: ") + ex.what();
        logMessage("ERROR", shutdownReason_);
        throw;
    }
    catch (...)
    {
        shutdownReason_ = "Initialization failed with unknown exception.";
        logMessage("ERROR", shutdownReason_);
        throw;
    }
}

SoldierFederate::~SoldierFederate()
{
    logMessage("INFO", "Shutdown started. Reason: " + shutdownReason_);

    try
    {
        if (rtiAmb_)
        {
            if (joinedFederation_)
            {
                rtiAmb_->resignFederationExecution(rti1516e::NO_ACTION);
                logMessage("INFO", "Resigned from federation execution.");
            }

            try
            {
                rtiAmb_->destroyFederationExecution(L"SoldierFederation");
                logMessage("INFO", "Destroyed federation SoldierFederation.");
            }
            catch (const FederationExecutionDoesNotExist&)
            {
                logMessage("INFO", "Federation already destroyed.");
            }
            catch (const FederatesCurrentlyJoined&)
            {
                logMessage("INFO", "Federation still has joined federates; destroy skipped.");
            }
        }
    }
    catch (const Exception& ex)
    {
        logMessage("ERROR", "Exception during shutdown: " + toNarrow(ex.what()));
        std::wcerr << L"Exception during shutdown: " << ex.what() << L"\n";
    }
    catch (const std::exception& ex)
    {
        logMessage("ERROR", std::string("Exception during shutdown: ") + ex.what());
        std::cerr << "Exception during shutdown: " << ex.what() << "\n";
    }
    catch (...)
    {
        logMessage("ERROR", "Unknown exception during shutdown.");
        std::cerr << "Unknown exception during shutdown\n";
    }

    logMessage("INFO", "Shutdown complete.");
}

void SoldierFederate::openLogFile()
{
    namespace fs = std::filesystem;

    try
    {
        fs::create_directories("logs");
    }
    catch (...)
    {
        // If directory creation fails, opening the file below will report the issue.
    }

    const std::string fileName = federateName_ + "_" + timestampForFile() + ".log";
    const fs::path fullPath = fs::path("logs") / fileName;

    logFilePath_ = fullPath.string();
    logFile_.open(logFilePath_, std::ios::out | std::ios::app);

    if (logFile_)
    {
        std::cout << "Logging to " << logFilePath_ << "\n";
        logFile_ << "[" << timestampForLogLine() << "] [INFO] [" << federateName_
                 << "] Log file created at " << logFilePath_ << "\n";
        logFile_.flush();
    }
    else
    {
        std::cerr << "Failed to open log file: " << logFilePath_ << "\n";
    }
}

void SoldierFederate::logMessage(const std::string& level, const std::string& message)
{
    if (!logFile_)
    {
        return;
    }

    logFile_ << "[" << timestampForLogLine() << "] [" << level << "] [" << federateName_
             << "] " << message << "\n";
    logFile_.flush();
}

void SoldierFederate::initializeRTI()
{
    logMessage("INFO", "Creating RTI ambassador.");

    // Create the RTI ambassador
    std::unique_ptr<RTIambassadorFactory> rtiAmbFactory(new RTIambassadorFactory());
    RTIambassador* rtiAmb = rtiAmbFactory->createRTIambassador().release();
    rtiAmb_ = std::unique_ptr<RTIambassador>(rtiAmb);

    // Connect to RTI
    rtiAmb_->connect(*this, HLA_EVOKED);
    logMessage("INFO", "Connected to RTI.");
}

void SoldierFederate::createOrJoinFederation()
{
    // Simple FOM file path relative to executable
    const std::wstring federationName = L"SoldierFederation";
    const std::wstring fomPath = L"../foms/SoldierFOM.xml";

    try
    {
        rtiAmb_->createFederationExecution(federationName, std::vector<std::wstring>{fomPath});
        std::wcout << L"Created federation: SoldierFederation\n";
        logMessage("INFO", "Created federation SoldierFederation.");
    }
    catch (const FederationExecutionAlreadyExists&)
    {
        std::wcout << L"Federation already exists, joining existing federation.\n";
        logMessage("INFO", "Federation already exists. Joining existing federation.");
    }

    federateHandle_ = rtiAmb_->joinFederationExecution(
        std::wstring(federateName_.begin(), federateName_.end()),
        L"Soldier",
        federationName);
    joinedFederation_ = true;
    std::wcout << L"Joined federation as " << std::wstring(federateName_.begin(), federateName_.end()) << L"\n";
    logMessage("INFO", "Joined federation as " + federateName_ + ".");
}

void SoldierFederate::publishAndSubscribe()
{
    soldierClassHandle_ = rtiAmb_->getObjectClassHandle(L"HLAobjectRoot.Soldier");
    positionXHandle_ = rtiAmb_->getAttributeHandle(soldierClassHandle_, L"positionX");
    positionYHandle_ = rtiAmb_->getAttributeHandle(soldierClassHandle_, L"positionY");
    positionZHandle_ = rtiAmb_->getAttributeHandle(soldierClassHandle_, L"positionZ");
    healthHandle_ = rtiAmb_->getAttributeHandle(soldierClassHandle_, L"health");

    AttributeHandleSet publishSet;
    publishSet.insert(positionXHandle_);
    publishSet.insert(positionYHandle_);
    publishSet.insert(positionZHandle_);
    publishSet.insert(healthHandle_);

    rtiAmb_->publishObjectClassAttributes(soldierClassHandle_, publishSet);
    rtiAmb_->subscribeObjectClassAttributes(soldierClassHandle_, publishSet);

    // Interaction
    fireInteractionHandle_ = rtiAmb_->getInteractionClassHandle(L"HLAinteractionRoot.FireWeapon");
    targetNameHandle_ = rtiAmb_->getParameterHandle(fireInteractionHandle_, L"targetName");
    damageHandle_ = rtiAmb_->getParameterHandle(fireInteractionHandle_, L"damage");

    rtiAmb_->publishInteractionClass(fireInteractionHandle_);
    rtiAmb_->subscribeInteractionClass(fireInteractionHandle_);

    // Register ourselves
    localSoldierHandle_ = rtiAmb_->registerObjectInstance(soldierClassHandle_, std::wstring(federateName_.begin(), federateName_.end()));
    std::wcout << L"Registered local soldier object instance.\n";
    logMessage("INFO", "Published/subscribed Soldier class and FireWeapon interaction.");
    logMessage("INFO", "Registered local soldier object instance.");
}

void SoldierFederate::updateSoldierAttributes()
{
    const auto xData = encodeDouble(localSoldier_.x);
    const auto yData = encodeDouble(localSoldier_.y);
    const auto zData = encodeDouble(localSoldier_.z);
    const auto healthData = encodeInt32(localSoldier_.health);

    AttributeHandleValueMap updates;
    updates[positionXHandle_] = VariableLengthData(xData.data(), xData.size());
    updates[positionYHandle_] = VariableLengthData(yData.data(), yData.size());
    updates[positionZHandle_] = VariableLengthData(zData.data(), zData.size());
    updates[healthHandle_] = VariableLengthData(healthData.data(), healthData.size());

    rtiAmb_->updateAttributeValues(localSoldierHandle_, updates, rti1516e::VariableLengthData());
}

void SoldierFederate::sendFireInteraction(const std::string& targetName)
{
    if (!localSoldier_.alive)
        return;

    const auto targetData = encodeString(targetName);
    const auto damageData = encodeInt32(25);

    ParameterHandleValueMap params;
    params[targetNameHandle_] = VariableLengthData(targetData.data(), targetData.size());
    params[damageHandle_] = VariableLengthData(damageData.data(), damageData.size());

    rtiAmb_->sendInteraction(fireInteractionHandle_, params, VariableLengthData());
    std::wcout << L"Fired at " << std::wstring(targetName.begin(), targetName.end()) << L"\n";
    logMessage("INFO", "Sent FireWeapon interaction to target=" + targetName + ".");
}

void SoldierFederate::maybeFireAtEnemy()
{
    if (knownSoldiers_.empty())
        return;

    // Choose a random soldier other than ourselves
    std::vector<std::string> candidates;
    for (auto& kv : knownSoldiers_)
    {
        if (kv.first != localSoldierHandle_ && kv.second.alive)
            candidates.push_back(kv.second.name);
    }

    if (candidates.empty())
        return;

    static std::mt19937 rng((unsigned)std::chrono::system_clock::now().time_since_epoch().count());
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    auto targetName = candidates[dist(rng)];

    sendFireInteraction(targetName);
}

void SoldierFederate::mainLoop()
{
    std::wcout << L"Entering main loop. Press Ctrl+C to exit.\n";
    logMessage("INFO", "Entering main loop.");

    try
    {
        while (localSoldier_.alive)
        {
            // simulate movement
            localSoldier_.x += (std::rand() % 21 - 10) * 0.1;
            localSoldier_.y += (std::rand() % 21 - 10) * 0.1;

            updateSoldierAttributes();

            maybeFireAtEnemy();

            rtiAmb_->evokeMultipleCallbacks(0.1, 0.2);
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    catch (const Exception& ex)
    {
        shutdownReason_ = "Main loop RTI exception: " + toNarrow(ex.what());
        logMessage("ERROR", shutdownReason_);
        throw;
    }
    catch (const std::exception& ex)
    {
        shutdownReason_ = std::string("Main loop exception: ") + ex.what();
        logMessage("ERROR", shutdownReason_);
        throw;
    }
    catch (...)
    {
        shutdownReason_ = "Main loop unknown exception.";
        logMessage("ERROR", shutdownReason_);
        throw;
    }

    shutdownReason_ = "Local soldier was killed (health <= 0).";
    logMessage("WARN", shutdownReason_);
    std::wcout << L"Local soldier is dead. Exiting.\n";
}

void SoldierFederate::run()
{
    logMessage("INFO", "Run started.");

    try
    {
        mainLoop();
        logMessage("INFO", "Run completed.");
    }
    catch (const Exception& ex)
    {
        shutdownReason_ = "Run RTI exception: " + toNarrow(ex.what());
        logMessage("ERROR", shutdownReason_);
        throw;
    }
    catch (const std::exception& ex)
    {
        shutdownReason_ = std::string("Run exception: ") + ex.what();
        logMessage("ERROR", shutdownReason_);
        throw;
    }
    catch (...)
    {
        shutdownReason_ = "Run failed with unknown exception.";
        logMessage("ERROR", shutdownReason_);
        throw;
    }
}

void SoldierFederate::discoverObjectInstance(rti1516e::ObjectInstanceHandle theObject,
                                            rti1516e::ObjectClassHandle theObjectClass,
                                            const std::wstring& objectName)
{
    // Track discovered soldiers
    SoldierState state;
    state.name = std::string(objectName.begin(), objectName.end());
    knownSoldiers_[theObject] = state;

    std::wcout << L"Discovered soldier: " << std::wstring(state.name.begin(), state.name.end()) << L" (handle=" << theObject.toString() << L")\n";
    logMessage("INFO", "Discovered soldier=" + state.name + " handle=" + toNarrow(theObject.toString()) + ".");
}

void SoldierFederate::reflectAttributeValues(rti1516e::ObjectInstanceHandle theObject,
                                            const rti1516e::AttributeHandleValueMap& theAttributeValues,
                                            const rti1516e::VariableLengthData& theUserSuppliedTag,
                                            rti1516e::OrderType sentOrder,
                                            rti1516e::TransportationType theType,
                                            rti1516e::SupplementalReflectInfo theReflectInfo)
{
    auto it = knownSoldiers_.find(theObject);
    if (it == knownSoldiers_.end())
        return;

    SoldierState& state = it->second;

    for (const auto& kv : theAttributeValues)
    {
        if (kv.first == positionXHandle_)
            state.x = decodeDouble(kv.second);
        else if (kv.first == positionYHandle_)
            state.y = decodeDouble(kv.second);
        else if (kv.first == positionZHandle_)
            state.z = decodeDouble(kv.second);
        else if (kv.first == healthHandle_)
        {
            state.health = decodeInt32(kv.second);
            state.alive = state.health > 0;
        }
    }

    if (theObject == localSoldierHandle_)
    {
        const bool wasAlive = localSoldier_.alive;
        localSoldier_ = state;

        if (wasAlive && !localSoldier_.alive)
        {
            shutdownReason_ = "Local soldier marked dead by reflected attributes.";
            logMessage("WARN", shutdownReason_);
        }
    }
}

void SoldierFederate::receiveInteraction(rti1516e::InteractionClassHandle theInteraction,
                                        const rti1516e::ParameterHandleValueMap& theParameterValues,
                                        const rti1516e::VariableLengthData& theUserSuppliedTag,
                                        rti1516e::OrderType sentOrder,
                                        rti1516e::TransportationType theType,
                                        rti1516e::SupplementalReceiveInfo theReceiveInfo)
{
    if (theInteraction != fireInteractionHandle_)
        return;

    auto itTarget = theParameterValues.find(targetNameHandle_);
    auto itDamage = theParameterValues.find(damageHandle_);

    if (itTarget == theParameterValues.end() || itDamage == theParameterValues.end())
        return;

    auto targetName = decodeString(itTarget->second);
    auto damage = decodeInt32(itDamage->second);
    logMessage("INFO", "Received FireWeapon interaction target=" + targetName + " damage=" + std::to_string(damage) + ".");

    if (targetName == localSoldier_.name)
    {
        localSoldier_.health -= damage;

        if (localSoldier_.health <= 0)
        {
            localSoldier_.alive = false;
            shutdownReason_ = "Killed by FireWeapon interaction (damage=" + std::to_string(damage) + ").";
            logMessage("ERROR", shutdownReason_);
            std::cout << "I'm hit! Killed by enemy.\n";
        }
        else
        {
            logMessage("WARN", "Hit by enemy fire. Remaining health=" + std::to_string(localSoldier_.health) + ".");
            std::cout << "I'm hit! Health=" << localSoldier_.health << "\n";
        }

        updateSoldierAttributes();
    }
}
