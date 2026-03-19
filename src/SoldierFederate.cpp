#include "SoldierFederate.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <RTI/encoding/BasicDataElements.h>
#include <RTI/encoding/HLAfixedArray.h>
#include <RTI/encoding/HLAfixedRecord.h>
#include <RTI/encoding/HLAvariantRecord.h>

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
constexpr uint8_t kDeadReckoningStatic = 1;
constexpr uint8_t kForceFriendly = 1;
constexpr uint8_t kMarkingAscii = 1;
constexpr double kEngagementRangeMeters = 25.0;
constexpr double kEngagementRangeMetersSquared = kEngagementRangeMeters * kEngagementRangeMeters;
constexpr std::chrono::milliseconds kShotCooldown(1000);
constexpr float kProjectileSpeedMetersPerSecond = 800.0f;
constexpr uint16_t kFuseTypeContact = 1000;
constexpr uint16_t kWarheadTypeHighExplosive = 1000;
constexpr uint8_t kDetonationResultEntityImpact = 1;
constexpr uint32_t kDamageStateNoDamage = 0;
constexpr uint32_t kDamageStateSlight = 1;
constexpr uint32_t kDamageStateModerate = 2;
constexpr uint32_t kDamageStateDestroyed = 3;
constexpr double kDetonationProximityDamageRadiusMeters = 8.0;
constexpr double kDetonationProximityDamageRadiusSquared =
    kDetonationProximityDamageRadiusMeters * kDetonationProximityDamageRadiusMeters;

struct EntityTypeData
{
    uint8_t entityKind = 3;   // Lifeform
    uint8_t domain = 1;       // Land
    uint16_t countryCode = 225;
    uint8_t category = 1;
    uint8_t subcategory = 0;
    uint8_t specific = 0;
    uint8_t extra = 0;
};

const EntityTypeData kMunitionEntityType{
    2,    // Munition
    1,    // Land
    225,
    1,
    0,
    0,
    0
};

struct EntityIdentifierData
{
    uint16_t siteId = 1;
    uint16_t applicationId = 1;
    uint16_t entityNumber = 1;
};

struct SpatialData
{
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float psi = 0.0f;
    float theta = 0.0f;
    float phi = 0.0f;
    bool isFrozen = false;
};

std::wstring toWide(const std::string& text)
{
    return std::wstring(text.begin(), text.end());
}

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

    return toNarrow(std::wstring(text));
}

std::string trimRight(const std::string& value)
{
    size_t end = value.size();
    while (end > 0 && (value[end - 1] == '\0' || value[end - 1] == ' '))
    {
        --end;
    }

    return value.substr(0, end);
}

std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
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

uint16_t parseEnvUInt16(const char* variableName, uint16_t defaultValue)
{
    const char* rawValue = std::getenv(variableName);
    if (rawValue == nullptr || rawValue[0] == '\0')
    {
        return defaultValue;
    }

    try
    {
        const long parsed = std::stol(rawValue);
        if (parsed < 0 || parsed > 65535)
        {
            return defaultValue;
        }

        return static_cast<uint16_t>(parsed);
    }
    catch (...)
    {
        return defaultValue;
    }
}

uint8_t parseEnvUInt8(const char* variableName, uint8_t defaultValue)
{
    const char* rawValue = std::getenv(variableName);
    if (rawValue == nullptr || rawValue[0] == '\0')
    {
        return defaultValue;
    }

    try
    {
        const long parsed = std::stol(rawValue);
        if (parsed < 0 || parsed > 255)
        {
            return defaultValue;
        }

        return static_cast<uint8_t>(parsed);
    }
    catch (...)
    {
        return defaultValue;
    }
}

bool parseEnvBool(const char* variableName, bool defaultValue)
{
    const char* rawValue = std::getenv(variableName);
    if (rawValue == nullptr || rawValue[0] == '\0')
    {
        return defaultValue;
    }

    std::string value(rawValue);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (value == "1" || value == "true" || value == "yes" || value == "on")
    {
        return true;
    }

    if (value == "0" || value == "false" || value == "no" || value == "off")
    {
        return false;
    }

    return defaultValue;
}

uint16_t deriveEntityNumber(const std::string& federateName)
{
    const size_t hashed = std::hash<std::string>{}(federateName);
    return static_cast<uint16_t>(1 + (hashed % 65534));
}

HLAfixedRecord makeWorldLocationRecord(double x, double y, double z)
{
    HLAfixedRecord record;
    record.appendElement(HLAfloat64BE(x));
    record.appendElement(HLAfloat64BE(y));
    record.appendElement(HLAfloat64BE(z));
    return record;
}

HLAfixedRecord makeWorldLocationPrototype()
{
    return makeWorldLocationRecord(0.0, 0.0, 0.0);
}

HLAfixedRecord makeOrientationRecord(float psi, float theta, float phi)
{
    HLAfixedRecord record;
    record.appendElement(HLAfloat32BE(psi));
    record.appendElement(HLAfloat32BE(theta));
    record.appendElement(HLAfloat32BE(phi));
    return record;
}

HLAfixedRecord makeOrientationPrototype()
{
    return makeOrientationRecord(0.0f, 0.0f, 0.0f);
}

HLAfixedRecord makeVector3FloatRecord(float x, float y, float z)
{
    HLAfixedRecord record;
    record.appendElement(HLAfloat32BE(x));
    record.appendElement(HLAfloat32BE(y));
    record.appendElement(HLAfloat32BE(z));
    return record;
}

HLAfixedRecord makeVector3FloatPrototype()
{
    return makeVector3FloatRecord(0.0f, 0.0f, 0.0f);
}

HLAfixedRecord makeSpatialStaticRecord(const SpatialData& spatial)
{
    HLAfixedRecord spatialStatic;
    spatialStatic.appendElement(makeWorldLocationRecord(spatial.x, spatial.y, spatial.z));
    spatialStatic.appendElement(HLAoctet(spatial.isFrozen ? 1 : 0));
    spatialStatic.appendElement(makeOrientationRecord(spatial.psi, spatial.theta, spatial.phi));
    return spatialStatic;
}

HLAfixedRecord makeSpatialStaticRecordPrototype()
{
    return makeSpatialStaticRecord(SpatialData{});
}

HLAfixedRecord makeSpatialFPRecordPrototype()
{
    HLAfixedRecord spatialFP;
    spatialFP.appendElement(makeWorldLocationPrototype());
    spatialFP.appendElement(HLAoctet(static_cast<Octet>(0)));
    spatialFP.appendElement(makeOrientationPrototype());
    spatialFP.appendElement(makeVector3FloatPrototype());
    return spatialFP;
}

HLAfixedRecord makeSpatialRPRecordPrototype()
{
    HLAfixedRecord spatialRP;
    spatialRP.appendElement(makeWorldLocationPrototype());
    spatialRP.appendElement(HLAoctet(static_cast<Octet>(0)));
    spatialRP.appendElement(makeOrientationPrototype());
    spatialRP.appendElement(makeVector3FloatPrototype());
    spatialRP.appendElement(makeVector3FloatPrototype());
    return spatialRP;
}

HLAfixedRecord makeSpatialRVRecordPrototype()
{
    HLAfixedRecord spatialRV;
    spatialRV.appendElement(makeWorldLocationPrototype());
    spatialRV.appendElement(HLAoctet(static_cast<Octet>(0)));
    spatialRV.appendElement(makeOrientationPrototype());
    spatialRV.appendElement(makeVector3FloatPrototype());
    spatialRV.appendElement(makeVector3FloatPrototype());
    spatialRV.appendElement(makeVector3FloatPrototype());
    return spatialRV;
}

HLAfixedRecord makeSpatialFVRecordPrototype()
{
    HLAfixedRecord spatialFV;
    spatialFV.appendElement(makeWorldLocationPrototype());
    spatialFV.appendElement(HLAoctet(static_cast<Octet>(0)));
    spatialFV.appendElement(makeOrientationPrototype());
    spatialFV.appendElement(makeVector3FloatPrototype());
    spatialFV.appendElement(makeVector3FloatPrototype());
    return spatialFV;
}

HLAvariantRecord makeSpatialVariantPrototype()
{
    const HLAoctet discriminantPrototype(static_cast<Octet>(0));
    HLAvariantRecord spatialVariant(discriminantPrototype);

    const HLAfixedRecord spatialStatic = makeSpatialStaticRecordPrototype();
    const HLAfixedRecord spatialFP = makeSpatialFPRecordPrototype();
    const HLAfixedRecord spatialRP = makeSpatialRPRecordPrototype();
    const HLAfixedRecord spatialRV = makeSpatialRVRecordPrototype();
    const HLAfixedRecord spatialFV = makeSpatialFVRecordPrototype();

    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(1)), spatialStatic);
    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(2)), spatialFP);
    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(3)), spatialRP);
    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(4)), spatialRV);
    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(5)), spatialFV);
    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(6)), spatialFP);
    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(7)), spatialRP);
    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(8)), spatialRV);
    spatialVariant.addVariant(HLAoctet(static_cast<Octet>(9)), spatialFV);

    return spatialVariant;
}

VariableLengthData encodeEntityType(const EntityTypeData& value)
{
    HLAfixedRecord record;
    record.appendElement(HLAoctet(value.entityKind));
    record.appendElement(HLAoctet(value.domain));
    record.appendElement(HLAinteger16BE(static_cast<Integer16>(value.countryCode)));
    record.appendElement(HLAoctet(value.category));
    record.appendElement(HLAoctet(value.subcategory));
    record.appendElement(HLAoctet(value.specific));
    record.appendElement(HLAoctet(value.extra));
    return record.encode();
}

EntityTypeData decodeEntityType(const VariableLengthData& data)
{
    HLAfixedRecord record;
    record.appendElement(HLAoctet());
    record.appendElement(HLAoctet());
    record.appendElement(HLAinteger16BE());
    record.appendElement(HLAoctet());
    record.appendElement(HLAoctet());
    record.appendElement(HLAoctet());
    record.appendElement(HLAoctet());
    record.decode(data);

    EntityTypeData value;
    value.entityKind = static_cast<uint8_t>(static_cast<const HLAoctet&>(record.get(0)).get());
    value.domain = static_cast<uint8_t>(static_cast<const HLAoctet&>(record.get(1)).get());
    value.countryCode = static_cast<uint16_t>(static_cast<const HLAinteger16BE&>(record.get(2)).get());
    value.category = static_cast<uint8_t>(static_cast<const HLAoctet&>(record.get(3)).get());
    value.subcategory = static_cast<uint8_t>(static_cast<const HLAoctet&>(record.get(4)).get());
    value.specific = static_cast<uint8_t>(static_cast<const HLAoctet&>(record.get(5)).get());
    value.extra = static_cast<uint8_t>(static_cast<const HLAoctet&>(record.get(6)).get());
    return value;
}

VariableLengthData encodeEntityIdentifier(const EntityIdentifierData& value)
{
    HLAfixedRecord federateIdentifier;
    federateIdentifier.appendElement(HLAinteger16BE(static_cast<Integer16>(value.siteId)));
    federateIdentifier.appendElement(HLAinteger16BE(static_cast<Integer16>(value.applicationId)));

    HLAfixedRecord entityIdentifier;
    entityIdentifier.appendElement(federateIdentifier);
    entityIdentifier.appendElement(HLAinteger16BE(static_cast<Integer16>(value.entityNumber)));

    return entityIdentifier.encode();
}

EntityIdentifierData decodeEntityIdentifier(const VariableLengthData& data)
{
    HLAfixedRecord federateIdentifier;
    federateIdentifier.appendElement(HLAinteger16BE());
    federateIdentifier.appendElement(HLAinteger16BE());

    HLAfixedRecord entityIdentifier;
    entityIdentifier.appendElement(federateIdentifier);
    entityIdentifier.appendElement(HLAinteger16BE());
    entityIdentifier.decode(data);

    const HLAfixedRecord& federateIdValue = static_cast<const HLAfixedRecord&>(entityIdentifier.get(0));

    EntityIdentifierData value;
    value.siteId = static_cast<uint16_t>(static_cast<const HLAinteger16BE&>(federateIdValue.get(0)).get());
    value.applicationId = static_cast<uint16_t>(static_cast<const HLAinteger16BE&>(federateIdValue.get(1)).get());
    value.entityNumber = static_cast<uint16_t>(static_cast<const HLAinteger16BE&>(entityIdentifier.get(1)).get());
    return value;
}

VariableLengthData encodeForceIdentifier(uint8_t forceIdentifier)
{
    HLAoctet encoded(forceIdentifier);
    return encoded.encode();
}

VariableLengthData encodeUnsignedInteger16(uint16_t value)
{
    HLAinteger16BE encoded(static_cast<Integer16>(value));
    return encoded.encode();
}

VariableLengthData encodeUnsignedInteger32(uint32_t value)
{
    HLAinteger32BE encoded(static_cast<Integer32>(value));
    return encoded.encode();
}

VariableLengthData encodeLengthMeterFloat32(float value)
{
    HLAfloat32BE encoded(value);
    return encoded.encode();
}

VariableLengthData encodeDetonationResultCode(uint8_t resultCode)
{
    HLAoctet encoded(resultCode);
    return encoded.encode();
}

uint8_t decodeDetonationResultCode(const VariableLengthData& data)
{
    HLAoctet encoded;
    encoded.decode(data);
    return static_cast<uint8_t>(encoded.get());
}

VariableLengthData encodeRtiObjectId(const std::string& objectId)
{
    std::vector<char> encoded;
    encoded.reserve(objectId.size() + 1);
    encoded.insert(encoded.end(), objectId.begin(), objectId.end());
    encoded.push_back('\0');
    return VariableLengthData(encoded.data(), encoded.size());
}

std::string decodeRtiObjectId(const VariableLengthData& data)
{
    const char* bytes = static_cast<const char*>(data.data());
    if (bytes == nullptr || data.size() == 0)
    {
        return std::string();
    }

    size_t length = 0;
    while (length < data.size() && bytes[length] != '\0')
    {
        ++length;
    }

    return std::string(bytes, bytes + length);
}

int computeDamageFromDetonationResult(uint8_t detonationResultCode)
{
    switch (detonationResultCode)
    {
    case 0: // Other
        return 20;
    case 1: // EntityImpact
        return 60;
    case 2: // EntityProximateDetonation
        return 35;
    case 6: // None
        return 0;
    case 7: // HE_hit_Small
        return 45;
    case 8: // HE_hit_Medium
        return 60;
    case 9: // HE_hit_Large
        return 80;
    case 10: // ArmorPiercingHit
        return 90;
    case 26: // Kill_with_fragment_type_1
        return 100;
    default:
        return 25;
    }
}

uint32_t computeDamageStateFromHealth(int health)
{
    if (health <= 0)
    {
        return kDamageStateDestroyed;
    }

    if (health <= 40)
    {
        return kDamageStateModerate;
    }

    if (health <= 75)
    {
        return kDamageStateSlight;
    }

    return kDamageStateNoDamage;
}

VariableLengthData encodeEventIdentifier(uint16_t eventCount, const std::string& issuingObjectId)
{
    std::vector<char> encoded;
    encoded.reserve(2 + issuingObjectId.size() + 1);
    encoded.push_back(static_cast<char>((eventCount >> 8) & 0xFF));
    encoded.push_back(static_cast<char>(eventCount & 0xFF));
    encoded.insert(encoded.end(), issuingObjectId.begin(), issuingObjectId.end());
    encoded.push_back('\0');
    return VariableLengthData(encoded.data(), encoded.size());
}

VariableLengthData encodeWorldLocation(double x, double y, double z)
{
    return makeWorldLocationRecord(x, y, z).encode();
}

bool decodeWorldLocation(const VariableLengthData& data, double& x, double& y, double& z)
{
    try
    {
        HLAfixedRecord worldLocation = makeWorldLocationPrototype();
        worldLocation.decode(data);
        x = static_cast<const HLAfloat64BE&>(worldLocation.get(0)).get();
        y = static_cast<const HLAfloat64BE&>(worldLocation.get(1)).get();
        z = static_cast<const HLAfloat64BE&>(worldLocation.get(2)).get();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

VariableLengthData encodeVelocityVector(float x, float y, float z)
{
    return makeVector3FloatRecord(x, y, z).encode();
}

uint8_t decodeForceIdentifier(const VariableLengthData& data)
{
    HLAoctet encoded;
    encoded.decode(data);
    return static_cast<uint8_t>(encoded.get());
}

VariableLengthData encodeMarking(const std::string& marking)
{
    std::string normalized = marking;
    if (normalized.size() > 11)
    {
        normalized.resize(11);
    }

    HLAfixedArray markingData(HLAoctet(), 11);
    for (size_t i = 0; i < 11; ++i)
    {
        const Octet value = (i < normalized.size()) ? static_cast<Octet>(normalized[i]) : 0;
        markingData.set(i, HLAoctet(value));
    }

    HLAfixedRecord markingRecord;
    markingRecord.appendElement(HLAoctet(kMarkingAscii));
    markingRecord.appendElement(markingData);
    return markingRecord.encode();
}

std::string decodeMarking(const VariableLengthData& data)
{
    HLAfixedRecord markingRecord;
    markingRecord.appendElement(HLAoctet());
    markingRecord.appendElement(HLAfixedArray(HLAoctet(), 11));
    markingRecord.decode(data);

    const HLAfixedArray& markingData = static_cast<const HLAfixedArray&>(markingRecord.get(1));

    std::string decoded;
    decoded.reserve(markingData.size());

    for (size_t i = 0; i < markingData.size(); ++i)
    {
        const Octet value = static_cast<const HLAoctet&>(markingData.get(i)).get();
        decoded.push_back(static_cast<char>(value));
    }

    return trimRight(decoded);
}

VariableLengthData encodeSpatialStatic(const SpatialData& spatial)
{
    const HLAoctet staticDiscriminant(kDeadReckoningStatic);
    const HLAfixedRecord spatialStatic = makeSpatialStaticRecord(spatial);

    const HLAoctet discriminantPrototype(static_cast<Octet>(0));
    HLAvariantRecord spatialVariant(discriminantPrototype);
    spatialVariant.addVariant(staticDiscriminant, spatialStatic);
    spatialVariant.setDiscriminant(staticDiscriminant);
    spatialVariant.setVariant(staticDiscriminant, spatialStatic);

    return spatialVariant.encode();
}

SpatialData decodeSpatial(const VariableLengthData& data)
{
    HLAvariantRecord spatialVariant = makeSpatialVariantPrototype();
    spatialVariant.decode(data);

    const HLAfixedRecord& spatialRecord = static_cast<const HLAfixedRecord&>(spatialVariant.getVariant());
    const HLAfixedRecord& worldLocation = static_cast<const HLAfixedRecord&>(spatialRecord.get(0));
    const HLAoctet& isFrozen = static_cast<const HLAoctet&>(spatialRecord.get(1));
    const HLAfixedRecord& orientation = static_cast<const HLAfixedRecord&>(spatialRecord.get(2));

    SpatialData decoded;
    decoded.x = static_cast<const HLAfloat64BE&>(worldLocation.get(0)).get();
    decoded.y = static_cast<const HLAfloat64BE&>(worldLocation.get(1)).get();
    decoded.z = static_cast<const HLAfloat64BE&>(worldLocation.get(2)).get();
    decoded.isFrozen = (isFrozen.get() != 0);
    decoded.psi = static_cast<const HLAfloat32BE&>(orientation.get(0)).get();
    decoded.theta = static_cast<const HLAfloat32BE&>(orientation.get(1)).get();
    decoded.phi = static_cast<const HLAfloat32BE&>(orientation.get(2)).get();

    return decoded;
}
} // namespace

SoldierFederate::SoldierFederate(const std::string& federateName)
    : federateName_(federateName), federationExecutionName_(L"SoldierFederation")
{
    localSoldier_.objectName = federateName_;
    localSoldier_.marking = federateName_.substr(0, std::min<size_t>(11, federateName_.size()));
    localSoldier_.siteId = parseEnvUInt16("RPR_SITE_ID", 1);
    localSoldier_.applicationId = parseEnvUInt16("RPR_APPLICATION_ID", 1);
    localSoldier_.entityNumber = parseEnvUInt16("RPR_ENTITY_NUMBER", deriveEntityNumber(federateName_));
    localSoldier_.forceIdentifier = parseEnvUInt8("RPR_FORCE_ID", kForceFriendly);
    localSoldier_.hasSpatial = true;

    openLogFile();
    logMessage("INFO", "Starting federate process.");

    try
    {
        initializeRTI();
        createOrJoinFederation();
        publishAndSubscribe();

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
            if (connectionLost_)
            {
                logMessage("WARN", "Skipping RTI resign and destroy because the connection was already lost.");
            }
            else if (joinedFederation_)
            {
                rtiAmb_->resignFederationExecution(rti1516e::NO_ACTION);
                joinedFederation_ = false;
                logMessage("INFO", "Resigned from federation execution.");

                try
                {
                    rtiAmb_->destroyFederationExecution(federationExecutionName_);
                    logMessage("INFO", "Destroyed federation " + toNarrow(federationExecutionName_) + ".");
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
            else
            {
                logMessage("INFO", "Skipping resign because the federate never fully joined.");
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

void SoldierFederate::connectionLost(const std::wstring& faultDescription)
{
    connectionLost_ = true;
    joinedFederation_ = false;
    shutdownReason_ = "RTI connection lost: " + toNarrow(faultDescription);
    logMessage("ERROR", shutdownReason_);
}

void SoldierFederate::objectInstanceNameReservationSucceeded(const std::wstring& objectInstanceName)
{
    if (objectInstanceName == pendingObjectInstanceName_)
    {
        objectInstanceNameReserved_ = true;
        objectInstanceNameReservationPending_ = false;
    }

    logMessage("INFO", "Object instance name reserved: " + toNarrow(objectInstanceName) + ".");
}

void SoldierFederate::objectInstanceNameReservationFailed(const std::wstring& objectInstanceName)
{
    if (objectInstanceName == pendingObjectInstanceName_)
    {
        objectInstanceNameReserved_ = false;
        objectInstanceNameReservationPending_ = false;
    }

    shutdownReason_ = "Object instance name reservation failed: " + toNarrow(objectInstanceName);
    logMessage("ERROR", shutdownReason_);
}

void SoldierFederate::reserveLocalObjectInstanceName(const std::wstring& objectInstanceName)
{
    pendingObjectInstanceName_ = objectInstanceName;
    objectInstanceNameReserved_ = false;
    objectInstanceNameReservationPending_ = true;

    logMessage("INFO", "Requesting object instance name reservation for " + toNarrow(objectInstanceName) + ".");
    rtiAmb_->reserveObjectInstanceName(objectInstanceName);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (objectInstanceNameReservationPending_)
    {
        rtiAmb_->evokeMultipleCallbacks(0.1, 0.2);

        if (std::chrono::steady_clock::now() >= deadline)
        {
            objectInstanceNameReservationPending_ = false;
            objectInstanceNameReserved_ = false;
            shutdownReason_ = "Timed out waiting for object instance name reservation: " + toNarrow(objectInstanceName);
            logMessage("ERROR", shutdownReason_);
            throw std::runtime_error(shutdownReason_);
        }
    }

    if (!objectInstanceNameReserved_)
    {
        if (shutdownReason_ == "normal shutdown")
        {
            shutdownReason_ = "Object instance name reservation was not granted: " + toNarrow(objectInstanceName);
        }

        logMessage("ERROR", shutdownReason_);
        throw std::runtime_error(shutdownReason_);
    }
}

void SoldierFederate::initializeRTI()
{
    logMessage("INFO", "Creating RTI ambassador.");

    std::unique_ptr<RTIambassadorFactory> rtiAmbFactory(new RTIambassadorFactory());
    RTIambassador* rtiAmb = rtiAmbFactory->createRTIambassador().release();
    rtiAmb_ = std::unique_ptr<RTIambassador>(rtiAmb);

    rtiAmb_->connect(*this, HLA_EVOKED);
    logMessage("INFO", "Connected to RTI.");
}

void SoldierFederate::createOrJoinFederation()
{
    namespace fs = std::filesystem;

    const char* federationNameEnv = std::getenv("FEDERATION_NAME");
    if (federationNameEnv != nullptr && federationNameEnv[0] != '\0')
    {
        federationExecutionName_ = toWide(federationNameEnv);
    }
    else
    {
        federationExecutionName_ = L"SoldierFederation";
    }

    std::string fomPath;
    const char* fomPathEnv = std::getenv("RPR_FOM_PATH");
    if (fomPathEnv != nullptr && fomPathEnv[0] != '\0')
    {
        fomPath = fomPathEnv;
    }
    else if (fs::exists(fs::path("../foms/RPR_FOM_v2.0_1516-2010.xml")))
    {
        fomPath = "../foms/RPR_FOM_v2.0_1516-2010.xml";
    }
    else
    {
        fomPath = "C:/MAK/vrforces5.2e/bin64/RPR_FOM_v2.0_1516-2010.xml";
    }

    if (!fs::exists(fs::path(fomPath)))
    {
        shutdownReason_ = "RPR FOM not found at path: " + fomPath;
        logMessage("ERROR", shutdownReason_);
        throw std::runtime_error(shutdownReason_);
    }

    logMessage("INFO", "Using federation " + toNarrow(federationExecutionName_) + ".");
    logMessage("INFO", "Using FOM module " + fomPath + ".");

    const std::wstring fomPathWide = toWide(fomPath);
    const std::wstring federateNameWide = toWide(federateName_);
    bool createdFederation = false;

    try
    {
        rtiAmb_->createFederationExecution(federationExecutionName_, std::vector<std::wstring>{fomPathWide});
        createdFederation = true;
        std::wcout << L"Created federation: " << federationExecutionName_ << L"\n";
        logMessage("INFO", "Created federation " + toNarrow(federationExecutionName_) + ".");
    }
    catch (const FederationExecutionAlreadyExists&)
    {
        std::wcout << L"Federation already exists, joining existing federation: " << federationExecutionName_ << L"\n ";
        logMessage("INFO", "Federation already exists. Joining existing federation.");
    }

    const bool joinWithAdditionalFom = parseEnvBool("RPR_JOIN_WITH_ADDITIONAL_FOM", false);

    if (createdFederation)
    {
        federateHandle_ = rtiAmb_->joinFederationExecution(
            federateNameWide,
            L"Human",
            federationExecutionName_);
    }
    else if (joinWithAdditionalFom)
    {
        try
        {
            logMessage("INFO", "Attempting join with additional RPR module because RPR_JOIN_WITH_ADDITIONAL_FOM is enabled.");
            federateHandle_ = rtiAmb_->joinFederationExecution(
                federateNameWide,
                L"Human",
                federationExecutionName_,
                std::vector<std::wstring>{fomPathWide});
        }
        catch (const Exception& ex)
        {
            logMessage("WARN", "Join with additional RPR module failed: " + toNarrow(ex.what()) + ". Retrying join without additional modules.");
            federateHandle_ = rtiAmb_->joinFederationExecution(
                federateNameWide,
                L"Human",
                federationExecutionName_);
        }
    }
    else
    {
        logMessage("INFO", "Joining existing federation without additional FOM modules (set RPR_JOIN_WITH_ADDITIONAL_FOM=1 to force a modular merge attempt).");
        federateHandle_ = rtiAmb_->joinFederationExecution(
            federateNameWide,
            L"Human",
            federationExecutionName_);
    }

    joinedFederation_ = true;

    std::wcout << L"Joined federation as " << toWide(federateName_) << L"\n";
    logMessage("INFO", "Joined federation as " + federateName_ + ".");
}

void SoldierFederate::publishAndSubscribe()
{
    std::wstring selectedClassName;
    const std::vector<std::wstring> classCandidates = {
        L"HLAobjectRoot.BaseEntity.PhysicalEntity.Lifeform.Human",
        L"BaseEntity.PhysicalEntity.Lifeform.Human"
    };

    for (const auto& className : classCandidates)
    {
        try
        {
            humanClassHandle_ = rtiAmb_->getObjectClassHandle(className);
            selectedClassName = className;
            break;
        }
        catch (const NameNotFound&)
        {
            logMessage("WARN", "RPR class lookup failed for: " + toNarrow(className));
        }
    }

    if (selectedClassName.empty())
    {
        shutdownReason_ = "RPR Human class not found in current federation FOM. Check FEDERATION_NAME and ensure RPR_FOM_v2.0_1516-2010.xml is part of the federation modules.";
        logMessage("ERROR", shutdownReason_);
        throw std::runtime_error(shutdownReason_);
    }

    logMessage("INFO", "Using RPR object class: " + toNarrow(selectedClassName));

    entityTypeHandle_ = rtiAmb_->getAttributeHandle(humanClassHandle_, L"EntityType");
    entityIdentifierHandle_ = rtiAmb_->getAttributeHandle(humanClassHandle_, L"EntityIdentifier");
    spatialHandle_ = rtiAmb_->getAttributeHandle(humanClassHandle_, L"Spatial");
    forceIdentifierHandle_ = rtiAmb_->getAttributeHandle(humanClassHandle_, L"ForceIdentifier");
    markingHandle_ = rtiAmb_->getAttributeHandle(humanClassHandle_, L"Marking");

    hasDamageStateAttribute_ = false;
    try
    {
        damageStateHandle_ = rtiAmb_->getAttributeHandle(humanClassHandle_, L"DamageState");
        hasDamageStateAttribute_ = true;
        logMessage("INFO", "DamageState attribute is available and will be published.");
    }
    catch (const NameNotFound&)
    {
        logMessage("WARN", "DamageState attribute not found for this class; damage visualization updates are limited.");
    }

    AttributeHandleSet attributes;
    attributes.insert(entityTypeHandle_);
    attributes.insert(entityIdentifierHandle_);
    attributes.insert(spatialHandle_);
    attributes.insert(forceIdentifierHandle_);
    attributes.insert(markingHandle_);
    if (hasDamageStateAttribute_)
    {
        attributes.insert(damageStateHandle_);
    }

    rtiAmb_->publishObjectClassAttributes(humanClassHandle_, attributes);
    rtiAmb_->subscribeObjectClassAttributes(humanClassHandle_, attributes);

    combatInteractionsEnabled_ = false;

    std::wstring weaponFireClassName;
    const std::vector<std::wstring> weaponFireClassCandidates = {
        L"HLAinteractionRoot.WeaponFire",
        L"WeaponFire"
    };

    for (const auto& className : weaponFireClassCandidates)
    {
        try
        {
            weaponFireClassHandle_ = rtiAmb_->getInteractionClassHandle(className);
            weaponFireClassName = className;
            break;
        }
        catch (const NameNotFound&)
        {
            logMessage("WARN", "WeaponFire class lookup failed for: " + toNarrow(className));
        }
    }

    std::wstring munitionDetonationClassName;
    const std::vector<std::wstring> munitionDetonationClassCandidates = {
        L"HLAinteractionRoot.MunitionDetonation",
        L"MunitionDetonation"
    };

    for (const auto& className : munitionDetonationClassCandidates)
    {
        try
        {
            munitionDetonationClassHandle_ = rtiAmb_->getInteractionClassHandle(className);
            munitionDetonationClassName = className;
            break;
        }
        catch (const NameNotFound&)
        {
            logMessage("WARN", "MunitionDetonation class lookup failed for: " + toNarrow(className));
        }
    }

    if (!weaponFireClassName.empty() && !munitionDetonationClassName.empty())
    {
        try
        {
            weaponFireEventIdentifierHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"EventIdentifier");
            weaponFireFireControlSolutionRangeHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"FireControlSolutionRange");
            weaponFireFireMissionIndexHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"FireMissionIndex");
            weaponFireFiringLocationHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"FiringLocation");
            weaponFireFiringObjectIdentifierHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"FiringObjectIdentifier");
            weaponFireFuseTypeHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"FuseType");
            weaponFireInitialVelocityVectorHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"InitialVelocityVector");
            weaponFireMunitionObjectIdentifierHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"MunitionObjectIdentifier");
            weaponFireMunitionTypeHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"MunitionType");
            weaponFireQuantityFiredHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"QuantityFired");
            weaponFireRateOfFireHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"RateOfFire");
            weaponFireTargetObjectIdentifierHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"TargetObjectIdentifier");
            weaponFireWarheadTypeHandle_ = rtiAmb_->getParameterHandle(weaponFireClassHandle_, L"WarheadType");

            munitionDetonationDetonationLocationHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"DetonationLocation");
            munitionDetonationDetonationResultCodeHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"DetonationResultCode");
            munitionDetonationEventIdentifierHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"EventIdentifier");
            munitionDetonationFiringObjectIdentifierHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"FiringObjectIdentifier");
            munitionDetonationFinalVelocityVectorHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"FinalVelocityVector");
            munitionDetonationFuseTypeHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"FuseType");
            munitionDetonationMunitionObjectIdentifierHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"MunitionObjectIdentifier");
            munitionDetonationMunitionTypeHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"MunitionType");
            munitionDetonationQuantityFiredHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"QuantityFired");
            munitionDetonationRateOfFireHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"RateOfFire");
            munitionDetonationRelativeDetonationLocationHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"RelativeDetonationLocation");
            munitionDetonationTargetObjectIdentifierHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"TargetObjectIdentifier");
            munitionDetonationWarheadTypeHandle_ = rtiAmb_->getParameterHandle(munitionDetonationClassHandle_, L"WarheadType");

            rtiAmb_->publishInteractionClass(weaponFireClassHandle_);
            rtiAmb_->publishInteractionClass(munitionDetonationClassHandle_);
            rtiAmb_->subscribeInteractionClass(weaponFireClassHandle_);
            rtiAmb_->subscribeInteractionClass(munitionDetonationClassHandle_);
            combatInteractionsEnabled_ = true;

            logMessage("INFO", "Published/subscribed RPR interactions: " + toNarrow(weaponFireClassName) + " and " + toNarrow(munitionDetonationClassName) + ".");
        }
        catch (const Exception& ex)
        {
            combatInteractionsEnabled_ = false;
            logMessage("WARN", "Failed to enable combat interactions: " + toNarrow(ex.what()));
        }
    }
    else
    {
        logMessage("WARN", "Combat interactions disabled because WeaponFire and/or MunitionDetonation classes were not found.");
    }

    AttributeHandleSet spawnSeedAttributes;
    spawnSeedAttributes.insert(spatialHandle_);
    spawnSeedAttributes.insert(entityIdentifierHandle_);
    spawnSeedAttributes.insert(forceIdentifierHandle_);
    spawnSeedAttributes.insert(markingHandle_);

    try
    {
        rtiAmb_->requestAttributeValueUpdate(humanClassHandle_, spawnSeedAttributes, VariableLengthData());
        logMessage("INFO", "Requested initial Human attribute updates for spawn seeding.");
    }
    catch (const Exception& ex)
    {
        logMessage("WARN", "Initial class attribute update request failed: " + toNarrow(ex.what()));
    }
    

    const std::wstring objectInstanceName = toWide(federateName_);
    reserveLocalObjectInstanceName(objectInstanceName);
    localSoldierHandle_ = rtiAmb_->registerObjectInstance(humanClassHandle_, objectInstanceName);
    localObjectHandleId_ = toNarrow(localSoldierHandle_.toString());
    localObjectId_ = localSoldier_.objectName.empty() ? localObjectHandleId_ : localSoldier_.objectName;

    std::wcout << L"Registered local human lifeform object instance.\n";
    logMessage("INFO", "Published/subscribed RPR Human attributes.");
    logMessage("INFO", "Registered local human object instance with reserved name " + federateName_ + ".");
}

void SoldierFederate::updateSoldierAttributes()
{
    AttributeHandleValueMap updates;

    const EntityTypeData entityType;
    const EntityIdentifierData entityIdentifier{localSoldier_.siteId, localSoldier_.applicationId, localSoldier_.entityNumber};
    const SpatialData spatial{localSoldier_.x, localSoldier_.y, localSoldier_.z,
                              localSoldier_.psi, localSoldier_.theta, localSoldier_.phi, false};

    updates[entityTypeHandle_] = encodeEntityType(entityType);
    updates[entityIdentifierHandle_] = encodeEntityIdentifier(entityIdentifier);
    updates[spatialHandle_] = encodeSpatialStatic(spatial);
    updates[forceIdentifierHandle_] = encodeForceIdentifier(localSoldier_.forceIdentifier);
    updates[markingHandle_] = encodeMarking(localSoldier_.marking);
    if (hasDamageStateAttribute_)
    {
        updates[damageStateHandle_] = encodeUnsignedInteger32(localDamageState_);
    }

    rtiAmb_->updateAttributeValues(localSoldierHandle_, updates, VariableLengthData());
}

bool SoldierFederate::isHostileTarget(const SoldierState& candidate) const
{
    if (candidate.forceIdentifier == 0 || localSoldier_.forceIdentifier == 0)
    {
        return true;
    }

    return candidate.forceIdentifier != localSoldier_.forceIdentifier;
}

bool SoldierFederate::matchesLocalObjectId(const std::string& objectId) const
{
    if (objectId.empty())
    {
        return false;
    }

    if (objectId == localObjectId_ ||
        objectId == localObjectHandleId_ ||
        objectId == localSoldier_.objectName ||
        objectId == federateName_)
    {
        return true;
    }

    const std::string lowered = toLowerAscii(objectId);
    const std::string loweredName = toLowerAscii(localSoldier_.objectName);
    const std::string loweredFederateName = toLowerAscii(federateName_);
    const std::string loweredHandleId = toLowerAscii(localObjectHandleId_);

    if (!loweredName.empty() && lowered == loweredName)
    {
        return true;
    }

    if (!loweredFederateName.empty() && lowered == loweredFederateName)
    {
        return true;
    }

    if (!loweredHandleId.empty() && lowered == loweredHandleId)
    {
        return true;
    }

    // Some simulators include the ID in a longer token; accept containment as a fallback.
    if (!localObjectHandleId_.empty() && objectId.find(localObjectHandleId_) != std::string::npos)
    {
        return true;
    }

    if (!localSoldier_.objectName.empty() && objectId.find(localSoldier_.objectName) != std::string::npos)
    {
        return true;
    }

    return false;
}

void SoldierFederate::handleReceivedInteraction(rti1516e::InteractionClassHandle theInteraction,
                                                const rti1516e::ParameterHandleValueMap& theParameterValues)
{
    if (theInteraction != munitionDetonationClassHandle_)
    {
        return;
    }

    if (localObjectId_.empty())
    {
        return;
    }

    std::string firingObjectId = "unknown";
    const auto firingIt = theParameterValues.find(munitionDetonationFiringObjectIdentifierHandle_);
    if (firingIt != theParameterValues.end())
    {
        firingObjectId = decodeRtiObjectId(firingIt->second);
        if (firingObjectId.empty())
        {
            firingObjectId = "unknown";
        }
    }

    // Ignore our own detonation interactions if the RTI reflects them back.
    if (firingObjectId != "unknown" && matchesLocalObjectId(firingObjectId))
    {
        return;
    }

    bool targetedLocal = false;
    std::string targetObjectId;

    const auto targetIt = theParameterValues.find(munitionDetonationTargetObjectIdentifierHandle_);
    if (targetIt != theParameterValues.end())
    {
        targetObjectId = decodeRtiObjectId(targetIt->second);
        targetedLocal = matchesLocalObjectId(targetObjectId);
    }

    if (!targetedLocal)
    {
        // Fallback: treat nearby detonations as hits when target object IDs are omitted or encoded differently.
        const auto detonationLocationIt = theParameterValues.find(munitionDetonationDetonationLocationHandle_);
        if (detonationLocationIt != theParameterValues.end())
        {
            double detonationX = 0.0;
            double detonationY = 0.0;
            double detonationZ = 0.0;

            if (decodeWorldLocation(detonationLocationIt->second, detonationX, detonationY, detonationZ))
            {
                const double dx = detonationX - localSoldier_.x;
                const double dy = detonationY - localSoldier_.y;
                const double dz = detonationZ - localSoldier_.z;
                const double detonationDistanceSquared = dx * dx + dy * dy + dz * dz;
                targetedLocal = (detonationDistanceSquared <= kDetonationProximityDamageRadiusSquared);
            }
        }
    }

    if (!targetedLocal)
    {
        return;
    }

    uint8_t detonationResultCode = 0;
    const auto resultIt = theParameterValues.find(munitionDetonationDetonationResultCodeHandle_);
    if (resultIt != theParameterValues.end())
    {
        detonationResultCode = decodeDetonationResultCode(resultIt->second);
    }

    if (!localAlive_)
    {
        logMessage("INFO", "Received detonation targeting local entity from " + firingObjectId + " while already destroyed.");
        return;
    }

    const int damage = computeDamageFromDetonationResult(detonationResultCode);
    if (damage <= 0)
    {
        logMessage("INFO", "Received non-damaging detonation for local entity from " + firingObjectId + ".");
        return;
    }

    localHealth_ = std::max(0, localHealth_ - damage);
    localDamageState_ = computeDamageStateFromHealth(localHealth_);

    if (localHealth_ <= 0)
    {
        localAlive_ = false;
        localSoldier_.marking = "KIA";
    }

    try
    {
        updateSoldierAttributes();
    }
    catch (const Exception& ex)
    {
        logMessage("WARN", "Failed to publish post-damage state update: " + toNarrow(ex.what()));
    }

    logMessage(
        "INFO",
        "Local entity hit by " + firingObjectId +
        "; damage=" + std::to_string(damage) +
        " health=" + std::to_string(localHealth_) +
        " damageState=" + std::to_string(localDamageState_) +
        (localAlive_ ? "." : " (destroyed)."));

    if (!localAlive_)
    {
        logMessage("WARN", "Local entity destroyed. Movement and firing are now disabled.");
    }
}

bool SoldierFederate::tryGetRemoteSpawnReference(SoldierState& remoteState) const
{
    for (const auto& kv : knownSoldiers_)
    {
        if (kv.first == localSoldierHandle_)
        {
            continue;
        }

        const SoldierState& state = kv.second;
        if (!state.hasSpatial)
        {
            continue;
        }

        remoteState = state;
        return true;
    }

    return false;
}

bool SoldierFederate::initializeSpawnFromRemoteEntities()
{
    AttributeHandleSet spawnSeedAttributes;
    spawnSeedAttributes.insert(spatialHandle_);
    spawnSeedAttributes.insert(entityIdentifierHandle_);
    spawnSeedAttributes.insert(forceIdentifierHandle_);
    spawnSeedAttributes.insert(markingHandle_);

    try
    {
        rtiAmb_->requestAttributeValueUpdate(humanClassHandle_, spawnSeedAttributes, VariableLengthData());
    }
    catch (const Exception& ex)
    {
        logMessage("WARN", "Spawn-time class attribute update request failed: " + toNarrow(ex.what()));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);

    while (!connectionLost_ && std::chrono::steady_clock::now() < deadline)
    {
        SoldierState remoteState;
        if (tryGetRemoteSpawnReference(remoteState))
        {
            localSoldier_.x = remoteState.x;
            localSoldier_.y = remoteState.y;
            localSoldier_.z = remoteState.z;
            localSoldier_.hasSpatial = true;

            logMessage(
                "INFO",
                "Initialized spawn from remote entity " + remoteState.objectName +
                " at x=" + std::to_string(localSoldier_.x) +
                " y=" + std::to_string(localSoldier_.y) +
                " z=" + std::to_string(localSoldier_.z) + ".");

            return true;
        }

        rtiAmb_->evokeMultipleCallbacks(0.1, 0.2);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    logMessage(
        "WARN",
        "No remote entity with spatial data was discovered before spawn timeout. Keeping current local spawn.");
    return false;
}

void SoldierFederate::sendWeaponFireAndDetonation(rti1516e::ObjectInstanceHandle targetHandle,
                                                  const SoldierState& targetState,
                                                  double dx,
                                                  double dy,
                                                  double dz,
                                                  double distanceMeters)
{
    if (!combatInteractionsEnabled_)
    {
        return;
    }

    const std::string firingObjectId = localObjectId_.empty() ? localObjectHandleId_ : localObjectId_;
    const std::string targetObjectId = targetState.objectName.empty() ? toNarrow(targetHandle.toString())
                                                                       : targetState.objectName;
    const uint16_t eventCount = nextEventCount_;

    ++nextEventCount_;
    if (nextEventCount_ == 0)
    {
        nextEventCount_ = 1;
    }

    const std::string munitionObjectId = firingObjectId + ":M:" + std::to_string(eventCount);

    float xVelocity = 0.0f;
    float yVelocity = 0.0f;
    float zVelocity = 0.0f;

    if (distanceMeters > 0.0001)
    {
        const double velocityScale = static_cast<double>(kProjectileSpeedMetersPerSecond) / distanceMeters;
        xVelocity = static_cast<float>(dx * velocityScale);
        yVelocity = static_cast<float>(dy * velocityScale);
        zVelocity = static_cast<float>(dz * velocityScale);
    }

    ParameterHandleValueMap weaponFireParameters;
    weaponFireParameters[weaponFireEventIdentifierHandle_] = encodeEventIdentifier(eventCount, firingObjectId);
    weaponFireParameters[weaponFireFireControlSolutionRangeHandle_] = encodeLengthMeterFloat32(static_cast<float>(distanceMeters));
    weaponFireParameters[weaponFireFireMissionIndexHandle_] = encodeUnsignedInteger32(static_cast<uint32_t>(eventCount));
    weaponFireParameters[weaponFireFiringLocationHandle_] = encodeWorldLocation(localSoldier_.x, localSoldier_.y, localSoldier_.z);
    weaponFireParameters[weaponFireFiringObjectIdentifierHandle_] = encodeRtiObjectId(firingObjectId);
    weaponFireParameters[weaponFireFuseTypeHandle_] = encodeUnsignedInteger16(kFuseTypeContact);
    weaponFireParameters[weaponFireInitialVelocityVectorHandle_] = encodeVelocityVector(xVelocity, yVelocity, zVelocity);
    weaponFireParameters[weaponFireMunitionObjectIdentifierHandle_] = encodeRtiObjectId(munitionObjectId);
    weaponFireParameters[weaponFireMunitionTypeHandle_] = encodeEntityType(kMunitionEntityType);
    weaponFireParameters[weaponFireQuantityFiredHandle_] = encodeUnsignedInteger16(1);
    weaponFireParameters[weaponFireRateOfFireHandle_] = encodeUnsignedInteger16(0);
    weaponFireParameters[weaponFireTargetObjectIdentifierHandle_] = encodeRtiObjectId(targetObjectId);
    weaponFireParameters[weaponFireWarheadTypeHandle_] = encodeUnsignedInteger16(kWarheadTypeHighExplosive);

    rtiAmb_->sendInteraction(weaponFireClassHandle_, weaponFireParameters, VariableLengthData());

    ParameterHandleValueMap munitionDetonationParameters;
    munitionDetonationParameters[munitionDetonationDetonationLocationHandle_] = encodeWorldLocation(targetState.x, targetState.y, targetState.z);
    munitionDetonationParameters[munitionDetonationDetonationResultCodeHandle_] = encodeDetonationResultCode(kDetonationResultEntityImpact);
    munitionDetonationParameters[munitionDetonationEventIdentifierHandle_] = encodeEventIdentifier(eventCount, firingObjectId);
    munitionDetonationParameters[munitionDetonationFiringObjectIdentifierHandle_] = encodeRtiObjectId(firingObjectId);
    munitionDetonationParameters[munitionDetonationFinalVelocityVectorHandle_] = encodeVelocityVector(xVelocity, yVelocity, zVelocity);
    munitionDetonationParameters[munitionDetonationFuseTypeHandle_] = encodeUnsignedInteger16(kFuseTypeContact);
    munitionDetonationParameters[munitionDetonationMunitionObjectIdentifierHandle_] = encodeRtiObjectId(munitionObjectId);
    munitionDetonationParameters[munitionDetonationMunitionTypeHandle_] = encodeEntityType(kMunitionEntityType);
    munitionDetonationParameters[munitionDetonationQuantityFiredHandle_] = encodeUnsignedInteger16(1);
    munitionDetonationParameters[munitionDetonationRateOfFireHandle_] = encodeUnsignedInteger16(0);
    munitionDetonationParameters[munitionDetonationRelativeDetonationLocationHandle_] = encodeVelocityVector(0.0f, 0.0f, 0.0f);
    munitionDetonationParameters[munitionDetonationTargetObjectIdentifierHandle_] = encodeRtiObjectId(targetObjectId);
    munitionDetonationParameters[munitionDetonationWarheadTypeHandle_] = encodeUnsignedInteger16(kWarheadTypeHighExplosive);

    rtiAmb_->sendInteraction(munitionDetonationClassHandle_, munitionDetonationParameters, VariableLengthData());
}

void SoldierFederate::engageNearbyTargets()
{
    if (!localAlive_)
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now < nextShotTime_)
    {
        return;
    }

    const SoldierState* selectedTarget = nullptr;
    const ObjectInstanceHandle* selectedTargetHandle = nullptr;
    double selectedDistanceSquared = kEngagementRangeMetersSquared;
    double selectedDx = 0.0;
    double selectedDy = 0.0;
    double selectedDz = 0.0;

    if (hasLockedTarget_)
    {
        const auto lockedIt = knownSoldiers_.find(lockedTargetHandle_);
        if (lockedIt != knownSoldiers_.end() && lockedIt->second.hasSpatial && isHostileTarget(lockedIt->second))
        {
            const SoldierState& candidate = lockedIt->second;
            const double dx = candidate.x - localSoldier_.x;
            const double dy = candidate.y - localSoldier_.y;
            const double dz = candidate.z - localSoldier_.z;
            const double distanceSquared = dx * dx + dy * dy + dz * dz;

            if (distanceSquared <= kEngagementRangeMetersSquared)
            {
                selectedTarget = &candidate;
                selectedTargetHandle = &lockedIt->first;
                selectedDistanceSquared = distanceSquared;
                selectedDx = dx;
                selectedDy = dy;
                selectedDz = dz;
            }
            else
            {
                hasLockedTarget_ = false;
                logMessage("INFO", "Target lock released for " + lockedTargetName_ + " (out of engagement range).");
                lockedTargetName_.clear();
            }
        }
        else
        {
            hasLockedTarget_ = false;
            if (!lockedTargetName_.empty())
            {
                logMessage("INFO", "Target lock released for " + lockedTargetName_ + " (target unavailable).");
            }
            lockedTargetName_.clear();
        }
    }

    if (selectedTarget == nullptr)
    {
        for (const auto& kv : knownSoldiers_)
        {
            if (kv.first == localSoldierHandle_)
            {
                continue;
            }

            const SoldierState& candidate = kv.second;
            if (!candidate.hasSpatial)
            {
                continue;
            }

            if (!isHostileTarget(candidate))
            {
                continue;
            }

            const double dx = candidate.x - localSoldier_.x;
            const double dy = candidate.y - localSoldier_.y;
            const double dz = candidate.z - localSoldier_.z;
            const double distanceSquared = dx * dx + dy * dy + dz * dz;

            if (distanceSquared <= selectedDistanceSquared)
            {
                selectedTarget = &candidate;
                selectedTargetHandle = &kv.first;
                selectedDistanceSquared = distanceSquared;
                selectedDx = dx;
                selectedDy = dy;
                selectedDz = dz;
            }
        }

        if (selectedTarget != nullptr && selectedTargetHandle != nullptr)
        {
            hasLockedTarget_ = true;
            lockedTargetHandle_ = *selectedTargetHandle;
            lockedTargetName_ = selectedTarget->objectName;
            logMessage("INFO", "Target lock acquired on " + lockedTargetName_ + ".");
        }
    }

    if (selectedTarget == nullptr || selectedTargetHandle == nullptr)
    {
        return;
    }

    if (std::fabs(selectedDx) > 0.0001 || std::fabs(selectedDy) > 0.0001)
    {
        localSoldier_.psi = static_cast<float>(std::atan2(selectedDy, selectedDx));
    }

    nextShotTime_ = now + kShotCooldown;

    const double distanceMeters = std::sqrt(selectedDistanceSquared);

    try
    {
        sendWeaponFireAndDetonation(*selectedTargetHandle, *selectedTarget, selectedDx, selectedDy, selectedDz, distanceMeters);
    }
    catch (const Exception& ex)
    {
        combatInteractionsEnabled_ = false;
        logMessage("WARN", "Failed to send combat interactions; disabling them for this run: " + toNarrow(ex.what()));
    }
    catch (const std::exception& ex)
    {
        combatInteractionsEnabled_ = false;
        logMessage("WARN", std::string("Failed to send combat interactions; disabling them for this run: ") + ex.what());
    }

    const std::string message = "Shot fired at " + selectedTarget->objectName +
                                " distance=" + std::to_string(distanceMeters) +
                                "m" +
                                (combatInteractionsEnabled_ ? " (RPR WeaponFire + MunitionDetonation sent)."
                                                            : " (local shot only; combat interactions disabled).");
    logMessage("INFO", message);
    std::cout << message << "\n";
}

void SoldierFederate::mainLoop()
{
    std::wcout << L"Entering main loop. Press Ctrl+C to exit.\n";
    logMessage("INFO", "Entering main loop.");

    std::mt19937 rng(static_cast<unsigned>(std::chrono::system_clock::now().time_since_epoch().count()));
    std::uniform_real_distribution<double> step(-1.0, 1.0);

    try
    {
        while (!connectionLost_)
        {
            if (localAlive_)
            {
                const double dx = step(rng) * 0.5;
                const double dy = step(rng) * 0.5;

                localSoldier_.x += dx;
                localSoldier_.y += dy;

                if (std::fabs(dx) > 0.0001 || std::fabs(dy) > 0.0001)
                {
                    localSoldier_.psi = static_cast<float>(std::atan2(dy, dx));
                }

                engageNearbyTargets();
            }

            updateSoldierAttributes();

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

    if (connectionLost_)
    {
        logMessage("WARN", "Main loop exited due to RTI connection loss.");
    }
}

void SoldierFederate::run()
{
    logMessage("INFO", "Run started.");

    try
    {
        initializeSpawnFromRemoteEntities();
        updateSoldierAttributes();
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
    SoldierState state;
    state.objectName = toNarrow(objectName);
    state.marking = state.objectName;
    knownSoldiers_[theObject] = state;

    AttributeHandleSet spawnSeedAttributes;
    spawnSeedAttributes.insert(spatialHandle_);
    spawnSeedAttributes.insert(entityIdentifierHandle_);
    spawnSeedAttributes.insert(forceIdentifierHandle_);
    spawnSeedAttributes.insert(markingHandle_);

    try
    {
        rtiAmb_->requestAttributeValueUpdate(theObject, spawnSeedAttributes, VariableLengthData());
    }
    catch (const Exception& ex)
    {
        logMessage("WARN", "Per-object attribute update request failed for " + state.objectName + ": " + toNarrow(ex.what()));
    }

    std::wcout << L"Discovered entity: " << objectName << L" (handle=" << theObject.toString() << L")\n";
    logMessage("INFO", "Discovered entity=" + state.objectName + " handle=" + toNarrow(theObject.toString()) + ".");
}

void SoldierFederate::receiveInteraction(rti1516e::InteractionClassHandle theInteraction,
                                        const rti1516e::ParameterHandleValueMap& theParameterValues,
                                        const rti1516e::VariableLengthData& theUserSuppliedTag,
                                        rti1516e::OrderType sentOrder,
                                        rti1516e::TransportationType theType,
                                        rti1516e::SupplementalReceiveInfo theReceiveInfo)
{
    (void)theUserSuppliedTag;
    (void)sentOrder;
    (void)theType;
    (void)theReceiveInfo;
    handleReceivedInteraction(theInteraction, theParameterValues);
}

void SoldierFederate::receiveInteraction(rti1516e::InteractionClassHandle theInteraction,
                                        const rti1516e::ParameterHandleValueMap& theParameterValues,
                                        const rti1516e::VariableLengthData& theUserSuppliedTag,
                                        rti1516e::OrderType sentOrder,
                                        rti1516e::TransportationType theType,
                                        const rti1516e::LogicalTime& theTime,
                                        rti1516e::OrderType receivedOrder,
                                        rti1516e::SupplementalReceiveInfo theReceiveInfo)
{
    (void)theUserSuppliedTag;
    (void)sentOrder;
    (void)theType;
    (void)theTime;
    (void)receivedOrder;
    (void)theReceiveInfo;
    handleReceivedInteraction(theInteraction, theParameterValues);
}

void SoldierFederate::receiveInteraction(rti1516e::InteractionClassHandle theInteraction,
                                        const rti1516e::ParameterHandleValueMap& theParameterValues,
                                        const rti1516e::VariableLengthData& theUserSuppliedTag,
                                        rti1516e::OrderType sentOrder,
                                        rti1516e::TransportationType theType,
                                        const rti1516e::LogicalTime& theTime,
                                        rti1516e::OrderType receivedOrder,
                                        rti1516e::MessageRetractionHandle theHandle,
                                        rti1516e::SupplementalReceiveInfo theReceiveInfo)
{
    (void)theUserSuppliedTag;
    (void)sentOrder;
    (void)theType;
    (void)theTime;
    (void)receivedOrder;
    (void)theHandle;
    (void)theReceiveInfo;
    handleReceivedInteraction(theInteraction, theParameterValues);
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
    {
        SoldierState state;
        state.objectName = toNarrow(theObject.toString());
        it = knownSoldiers_.insert(std::make_pair(theObject, state)).first;
    }

    SoldierState& state = it->second;

    for (const auto& kv : theAttributeValues)
    {
        try
        {
            if (kv.first == entityIdentifierHandle_)
            {
                const EntityIdentifierData id = decodeEntityIdentifier(kv.second);
                state.siteId = id.siteId;
                state.applicationId = id.applicationId;
                state.entityNumber = id.entityNumber;
            }
            else if (kv.first == forceIdentifierHandle_)
            {
                state.forceIdentifier = decodeForceIdentifier(kv.second);
            }
            else if (kv.first == markingHandle_)
            {
                const std::string decodedMarking = decodeMarking(kv.second);
                if (!decodedMarking.empty())
                {
                    state.marking = decodedMarking;
                }
            }
            else if (kv.first == spatialHandle_)
            {
                const bool firstSpatialForObject = !state.hasSpatial;
                const SpatialData spatial = decodeSpatial(kv.second);
                state.x = spatial.x;
                state.y = spatial.y;
                state.z = spatial.z;
                state.psi = spatial.psi;
                state.theta = spatial.theta;
                state.phi = spatial.phi;
                state.hasSpatial = true;

                if (firstSpatialForObject)
                {
                    logMessage(
                        "INFO",
                        "Received first Spatial for " + state.objectName +
                            " at x=" + std::to_string(state.x) +
                            " y=" + std::to_string(state.y) +
                            " z=" + std::to_string(state.z) + ".");
                }
            }
            else if (kv.first == entityTypeHandle_)
            {
                // Decode for validation; fields are not currently used for behavior.
                (void)decodeEntityType(kv.second);
            }
        }
        catch (const std::exception& ex)
        {
            logMessage("WARN", "Failed to decode reflected attribute for object " + state.objectName + ": " + ex.what());
        }
        catch (...)
        {
            logMessage("WARN", "Failed to decode reflected attribute for object " + state.objectName + ": unknown decode error");
        }
    }

    if (theObject == localSoldierHandle_)
    {
        localSoldier_.x = state.x;
        localSoldier_.y = state.y;
        localSoldier_.z = state.z;
        localSoldier_.psi = state.psi;
        localSoldier_.theta = state.theta;
        localSoldier_.phi = state.phi;
        localSoldier_.hasSpatial = state.hasSpatial;
    }
}
