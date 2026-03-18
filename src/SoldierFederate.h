#pragma once

#include <fstream>
#include <memory>
#include <map>
#include <string>
#include <vector>

#include <RTI/RTIambassadorFactory.h>
#include <RTI/NullFederateAmbassador.h>
#include <RTI/RTIambassador.h>
#include <RTI/Exception.h>

struct SoldierState
{
    std::string name;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int health = 100;
    bool alive = true;
};

class SoldierFederate : public rti1516e::NullFederateAmbassador
{
public:
    SoldierFederate(const std::string& federateName);
    ~SoldierFederate();

    void run();

private:
    // HLA setup
    void initializeRTI();
    void createOrJoinFederation();
    void publishAndSubscribe();

    // Main loop
    void mainLoop();

    // Helpers
    void updateSoldierAttributes();
    void sendFireInteraction(const std::string& targetName);
    void maybeFireAtEnemy();

    void openLogFile();
    void logMessage(const std::string& level, const std::string& message);

    // Callback overrides
    void discoverObjectInstance(rti1516e::ObjectInstanceHandle theObject,
                                rti1516e::ObjectClassHandle theObjectClass,
                                const std::wstring& objectName) override;

    void reflectAttributeValues(rti1516e::ObjectInstanceHandle theObject,
                                const rti1516e::AttributeHandleValueMap& theAttributeValues,
                                const rti1516e::VariableLengthData& theUserSuppliedTag,
                                rti1516e::OrderType sentOrder,
                                rti1516e::TransportationType theType,
                                rti1516e::SupplementalReflectInfo theReflectInfo) override;

    void receiveInteraction(rti1516e::InteractionClassHandle theInteraction,
                            const rti1516e::ParameterHandleValueMap& theParameterValues,
                            const rti1516e::VariableLengthData& theUserSuppliedTag,
                            rti1516e::OrderType sentOrder,
                            rti1516e::TransportationType theType,
                            rti1516e::SupplementalReceiveInfo theReceiveInfo) override;

private:
    std::string federateName_;
    std::unique_ptr<rti1516e::RTIambassador> rtiAmb_;
    rti1516e::FederateHandle federateHandle_;

    rti1516e::ObjectClassHandle soldierClassHandle_;
    rti1516e::AttributeHandle positionXHandle_;
    rti1516e::AttributeHandle positionYHandle_;
    rti1516e::AttributeHandle positionZHandle_;
    rti1516e::AttributeHandle healthHandle_;
    rti1516e::InteractionClassHandle fireInteractionHandle_;
    rti1516e::ParameterHandle targetNameHandle_;
    rti1516e::ParameterHandle damageHandle_;
    rti1516e::ObjectInstanceHandle localSoldierHandle_;

    SoldierState localSoldier_;
    std::map<rti1516e::ObjectInstanceHandle, SoldierState> knownSoldiers_;

    std::ofstream logFile_;
    std::string logFilePath_;
    std::string shutdownReason_ = "normal shutdown";
    bool joinedFederation_ = false;
};
