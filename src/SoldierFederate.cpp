#include "SoldierFederate.h"

#include <memory>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
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
}

SoldierFederate::~SoldierFederate()
{
    try
    {
        if (rtiAmb_)
        {
            // resign and attempt to destroy federation
            rtiAmb_->resignFederationExecution(rti1516e::NO_ACTION);
            rtiAmb_->destroyFederationExecution(L"SoldierFederation");
        }
    }
    catch (const Exception& ex)
    {
        std::wcerr << L"Exception during shutdown: " << ex.what() << L"\n";
    }
}

void SoldierFederate::initializeRTI()
{
    // Create the RTI ambassador
    RTIambassadorFactory* rtiAmbFactory = new RTIambassadorFactory();
    RTIambassador* rtiAmb = rtiAmbFactory->createRTIambassador().release();
    rtiAmb_ = std::unique_ptr<RTIambassador>(rtiAmb);
    delete rtiAmbFactory;

    // Connect to RTI
    rtiAmb_->connect(*this, HLA_EVOKED);
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
    }
    catch (const FederationExecutionAlreadyExists&)
    {
        std::wcout << L"Federation already exists, joining existing federation.\n";
    }

    federateHandle_ = rtiAmb_->joinFederationExecution(
        std::wstring(federateName_.begin(), federateName_.end()),
        L"Soldier",
        federationName);
    std::wcout << L"Joined federation as " << std::wstring(federateName_.begin(), federateName_.end()) << L"\n";
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
}

void SoldierFederate::updateSoldierAttributes()
{
    AttributeHandleValueMap updates;
    updates[positionXHandle_] = VariableLengthData(encodeDouble(localSoldier_.x).data(), encodeDouble(localSoldier_.x).size());
    updates[positionYHandle_] = VariableLengthData(encodeDouble(localSoldier_.y).data(), encodeDouble(localSoldier_.y).size());
    updates[positionZHandle_] = VariableLengthData(encodeDouble(localSoldier_.z).data(), encodeDouble(localSoldier_.z).size());
    updates[healthHandle_] = VariableLengthData(encodeInt32(localSoldier_.health).data(), encodeInt32(localSoldier_.health).size());

    rtiAmb_->updateAttributeValues(localSoldierHandle_, updates, rti1516e::VariableLengthData());
}

void SoldierFederate::sendFireInteraction(const std::string& targetName)
{
    if (!localSoldier_.alive)
        return;

    ParameterHandleValueMap params;
    params[targetNameHandle_] = VariableLengthData(encodeString(targetName).data(), encodeString(targetName).size());
    params[damageHandle_] = VariableLengthData(encodeInt32(25).data(), encodeInt32(25).size());

    rtiAmb_->sendInteraction(fireInteractionHandle_, params, VariableLengthData());
    std::wcout << L"Fired at " << std::wstring(targetName.begin(), targetName.end()) << L"\n";
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

    std::wcout << L"Local soldier is dead. Exiting.\n";
}

void SoldierFederate::run()
{
    mainLoop();
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
        localSoldier_ = state;
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

    if (targetName == localSoldier_.name)
    {
        localSoldier_.health -= damage;

        if (localSoldier_.health <= 0)
        {
            localSoldier_.alive = false;
            std::cout << "I'm hit! Killed by enemy.\n";
        }
        else
        {
            std::cout << "I'm hit! Health=" << localSoldier_.health << "\n";
        }

        updateSoldierAttributes();
    }
}
