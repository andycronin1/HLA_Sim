#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <RTI/RTIambassadorFactory.h>
#include <RTI/NullFederateAmbassador.h>
#include <RTI/RTIambassador.h>
#include <RTI/Exception.h>

struct SoldierState
{
    std::string objectName;
    std::string marking;
    double x = -73.94;
    double y = 40.7;
    double z = 24.0;
    float psi = 0.0f;
    float theta = 0.0f;
    float phi = 0.0f;
    uint16_t siteId = 0;
    uint16_t applicationId = 0;
    uint16_t entityNumber = 0;
    uint8_t forceIdentifier = 0;
    bool hasSpatial = false;
};

class SoldierFederate : public rti1516e::NullFederateAmbassador
{
public:
    explicit SoldierFederate(const std::string& federateName);
    ~SoldierFederate();

    void run();

private:
    // HLA setup 
    void initializeRTI();
    void createOrJoinFederation();
    void publishAndSubscribe();
    void reserveLocalObjectInstanceName(const std::wstring& objectInstanceName);

    // Main loop
    void mainLoop();

    // Helpers
    void updateSoldierAttributes();
    void handleReceivedInteraction(rti1516e::InteractionClassHandle theInteraction,
                                   const rti1516e::ParameterHandleValueMap& theParameterValues);
    bool isHostileTarget(const SoldierState& candidate) const;
    bool matchesLocalObjectId(const std::string& objectId) const;

    void openLogFile();
    void logMessage(const std::string& level, const std::string& message);

    void engageNearbyTargets();
    void sendWeaponFireAndDetonation(rti1516e::ObjectInstanceHandle targetHandle,
                                     const SoldierState& targetState,
                                     double dx,
                                     double dy,
                                     double dz,
                                     double distanceMeters);

    bool initializeSpawnFromRemoteEntities();
    bool tryGetRemoteSpawnReference(SoldierState& remoteState) const;

    // Callback overrides
    void connectionLost(const std::wstring& faultDescription) override;

    void objectInstanceNameReservationSucceeded(const std::wstring& objectInstanceName) override;
    void objectInstanceNameReservationFailed(const std::wstring& objectInstanceName) override;

    void discoverObjectInstance(rti1516e::ObjectInstanceHandle theObject,
                                rti1516e::ObjectClassHandle theObjectClass,
                                const std::wstring& objectName) override;

    void receiveInteraction(rti1516e::InteractionClassHandle theInteraction,
                            const rti1516e::ParameterHandleValueMap& theParameterValues,
                            const rti1516e::VariableLengthData& theUserSuppliedTag,
                            rti1516e::OrderType sentOrder,
                            rti1516e::TransportationType theType,
                            rti1516e::SupplementalReceiveInfo theReceiveInfo) override;

    void receiveInteraction(rti1516e::InteractionClassHandle theInteraction,
                            const rti1516e::ParameterHandleValueMap& theParameterValues,
                            const rti1516e::VariableLengthData& theUserSuppliedTag,
                            rti1516e::OrderType sentOrder,
                            rti1516e::TransportationType theType,
                            const rti1516e::LogicalTime& theTime,
                            rti1516e::OrderType receivedOrder,
                            rti1516e::SupplementalReceiveInfo theReceiveInfo) override;

    void receiveInteraction(rti1516e::InteractionClassHandle theInteraction,
                            const rti1516e::ParameterHandleValueMap& theParameterValues,
                            const rti1516e::VariableLengthData& theUserSuppliedTag,
                            rti1516e::OrderType sentOrder,
                            rti1516e::TransportationType theType,
                            const rti1516e::LogicalTime& theTime,
                            rti1516e::OrderType receivedOrder,
                            rti1516e::MessageRetractionHandle theHandle,
                            rti1516e::SupplementalReceiveInfo theReceiveInfo) override;

    void reflectAttributeValues(rti1516e::ObjectInstanceHandle theObject,
                                const rti1516e::AttributeHandleValueMap& theAttributeValues,
                                const rti1516e::VariableLengthData& theUserSuppliedTag,
                                rti1516e::OrderType sentOrder,
                                rti1516e::TransportationType theType,
                                rti1516e::SupplementalReflectInfo theReflectInfo) override;

private:
    std::string federateName_;
    std::wstring federationExecutionName_;
    std::unique_ptr<rti1516e::RTIambassador> rtiAmb_;
    rti1516e::FederateHandle federateHandle_;

    rti1516e::ObjectClassHandle humanClassHandle_;
    rti1516e::AttributeHandle entityTypeHandle_;
    rti1516e::AttributeHandle entityIdentifierHandle_;
    rti1516e::AttributeHandle spatialHandle_;
    rti1516e::AttributeHandle forceIdentifierHandle_;
    rti1516e::AttributeHandle markingHandle_;
    rti1516e::AttributeHandle damageStateHandle_;
    rti1516e::ObjectInstanceHandle localSoldierHandle_;

    rti1516e::InteractionClassHandle weaponFireClassHandle_;
    rti1516e::InteractionClassHandle munitionDetonationClassHandle_;

    rti1516e::ParameterHandle weaponFireEventIdentifierHandle_;
    rti1516e::ParameterHandle weaponFireFireControlSolutionRangeHandle_;
    rti1516e::ParameterHandle weaponFireFireMissionIndexHandle_;
    rti1516e::ParameterHandle weaponFireFiringLocationHandle_;
    rti1516e::ParameterHandle weaponFireFiringObjectIdentifierHandle_;
    rti1516e::ParameterHandle weaponFireFuseTypeHandle_;
    rti1516e::ParameterHandle weaponFireInitialVelocityVectorHandle_;
    rti1516e::ParameterHandle weaponFireMunitionObjectIdentifierHandle_;
    rti1516e::ParameterHandle weaponFireMunitionTypeHandle_;
    rti1516e::ParameterHandle weaponFireQuantityFiredHandle_;
    rti1516e::ParameterHandle weaponFireRateOfFireHandle_;
    rti1516e::ParameterHandle weaponFireTargetObjectIdentifierHandle_;
    rti1516e::ParameterHandle weaponFireWarheadTypeHandle_;

    rti1516e::ParameterHandle munitionDetonationDetonationLocationHandle_;
    rti1516e::ParameterHandle munitionDetonationDetonationResultCodeHandle_;
    rti1516e::ParameterHandle munitionDetonationEventIdentifierHandle_;
    rti1516e::ParameterHandle munitionDetonationFiringObjectIdentifierHandle_;
    rti1516e::ParameterHandle munitionDetonationFinalVelocityVectorHandle_;
    rti1516e::ParameterHandle munitionDetonationFuseTypeHandle_;
    rti1516e::ParameterHandle munitionDetonationMunitionObjectIdentifierHandle_;
    rti1516e::ParameterHandle munitionDetonationMunitionTypeHandle_;
    rti1516e::ParameterHandle munitionDetonationQuantityFiredHandle_;
    rti1516e::ParameterHandle munitionDetonationRateOfFireHandle_;
    rti1516e::ParameterHandle munitionDetonationRelativeDetonationLocationHandle_;
    rti1516e::ParameterHandle munitionDetonationTargetObjectIdentifierHandle_;
    rti1516e::ParameterHandle munitionDetonationWarheadTypeHandle_;

    SoldierState localSoldier_;
    std::map<rti1516e::ObjectInstanceHandle, SoldierState> knownSoldiers_;

    std::ofstream logFile_;
    std::string logFilePath_;
    std::string shutdownReason_ = "normal shutdown";
    bool joinedFederation_ = false;
    bool connectionLost_ = false;
    std::wstring pendingObjectInstanceName_;
    bool objectInstanceNameReservationPending_ = false;
    bool objectInstanceNameReserved_ = false;
    std::chrono::steady_clock::time_point nextShotTime_{};
    bool combatInteractionsEnabled_ = false;
    uint16_t nextEventCount_ = 1;
    bool hasDamageStateAttribute_ = false;
    int localHealth_ = 100;
    bool localAlive_ = true;
    uint32_t localDamageState_ = 0;
    std::string localObjectId_;
    std::string localObjectHandleId_;
    bool hasLockedTarget_ = false;
    rti1516e::ObjectInstanceHandle lockedTargetHandle_;
    std::string lockedTargetName_;
};
