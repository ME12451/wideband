#include "port.h"
#include "shared/flash.h"
#include "can_bitrate_storage.h"
#include "shared/strap_pin.h"

#include "wideband_config.h"
#include "../../../for_rusefi/wideband_bitrate.h"

wbo::CanBitrate ReadCanBitrate();

#include "ch.hpp"
#include "hal.h"

#define ADC_CHANNEL_COUNT 3

void PortPrepareAnalogSampling()
{
    adcStart(&ADCD1, nullptr);
}

static adcsample_t adcBuffer[ADC_CHANNEL_COUNT * ADC_OVERSAMPLE];

static chibios_rt::BinarySemaphore adcDoneSemaphore(/* taken =*/ true);

static void adcDoneCallback(ADCDriver*)
{
    chSysLockFromISR();
    adcDoneSemaphore.signalI();
    chSysUnlockFromISR();
}

const ADCConversionGroup convGroup =
{
    false,
    ADC_CHANNEL_COUNT,
    adcDoneCallback,
    nullptr,
    ADC_CFGR1_CONT | ADC_CFGR1_RES_12BIT,                  // CFGR1
    ADC_TR(0, 0),       // TR
    ADC_SMPR_SMP_7P5,      // SMPR
    ADC_CHSELR_CHSEL0 | ADC_CHSELR_CHSEL2 | ADC_CHSELR_CHSEL3
};

static float AverageSamples(adcsample_t* buffer, size_t idx)
{
    uint32_t sum = 0;

    for (size_t i = 0; i < ADC_OVERSAMPLE; i++)
    {
        sum += buffer[idx];
        idx += ADC_CHANNEL_COUNT;
    }

    constexpr float scale = VCC_VOLTS / (ADC_MAX_COUNT * ADC_OVERSAMPLE);

    return (float)sum * scale;
}

void AnalogSampleStart()
{
    adcStartConversion(&ADCD1, &convGroup, adcBuffer, ADC_OVERSAMPLE);
}

AnalogResult AnalogSampleFinish()
{
    adcDoneSemaphore.wait(TIME_INFINITE);

    return
    {
        .ch =
        {
            {
                .NernstVoltage = AverageSamples(adcBuffer, 0) * (1.0 / NERNST_INPUT_GAIN),
                .PumpCurrentVoltage = AverageSamples(adcBuffer, 1),
                .HeaterSupplyVoltage = 0,
            },
        },
        .VirtualGroundVoltageInt = AverageSamples(adcBuffer, 2),

        // TODO!
        .McuTemp = 0,
    };
}

static size_t boardHwId = 0;

size_t BoardGetHwId()
{
    return boardHwId;
}

extern uint8_t __configflash__start__[];

int InitConfiguration()
{
    // See https://github.com/mck1117/wideband/issues/11 to explain this madness
    auto sel1 = readSelPin(ID_SEL1_PORT, ID_SEL1_PIN);
    auto sel2 = readSelPin(ID_SEL2_PORT, ID_SEL2_PIN);

    boardHwId = (3 * sel1 + sel2);
    GetConfiguration(); // load calibration before starting sampling/heater threads

    return 0;
}

static Configuration config;
static float softwareCalibrationGain = 1.0f;

Configuration* GetConfiguration()
{
    const auto& cfg = *reinterpret_cast<const Configuration*>(__configflash__start__);

    // If config has been written before, use the stored configuration
    if (cfg.IsValid())
    {
        // If we have valid config in flash - do not read ID pins, use ID from settings
        config = cfg;
    }
    else
    {
        config.LoadDefaults();

        // Now, override the index with a hardware-strapped option (if present)
        switch (BoardGetHwId()) {
            case 0: config.afr[0].RusEfiIdx = 2; break;
            case 1: config.afr[0].RusEfiIdx = 0; break;
            case 2: config.afr[0].RusEfiIdx = 3; break;
            case 3: config.afr[0].RusEfiIdx = 4; break;
            case 4: /* both floating, do nothing */ break;
            case 5: config.afr[0].RusEfiIdx = 1; break;
            case 6: config.afr[0].RusEfiIdx = 5; break;
            case 7: config.afr[0].RusEfiIdx = 6; break;
            case 8: config.afr[0].RusEfiIdx = 7; break;
            default: break;
        }
    }

    softwareCalibrationGain = GetSoftwareCalibration() ? wbo::CalibrationGain(GetSoftwareCalibration()) : 0.0f;
    return &config;
}

static bool WriteConfigurationUnlocked(wbo::CanBitrate bitrate)
{
    static_assert(sizeof(Configuration) == 256);
    static_assert(sizeof(Configuration) <= wbo::CanBitrateRecordOffset);
    return WriteCanConfiguration(__configflash__start__,
        reinterpret_cast<const uint8_t*>(&config), sizeof(config), bitrate);
}

static bool WriteConfiguration(wbo::CanBitrate bitrate)
{
    chSysLock();
    const bool saved = WriteConfigurationUnlocked(bitrate);
    chSysUnlock();
    return saved;
}

void SetConfiguration()
{
    // A sensor type/index write must preserve a pending bitrate, too.
    WriteConfiguration(ReadCanBitrate());
}

bool SetCanBitrate(uint8_t value)
{
    const auto marker = *reinterpret_cast<const volatile uint32_t*>(wbo::CanBitrateBootloaderMarkerAddress);
    if (!wbo::IsBitrateChangeAllowed(value, marker)) {
        return false;
    }
    auto bitrate = static_cast<wbo::CanBitrate>(value);
    GetConfiguration();
    return WriteConfiguration(bitrate);
}

SensorType GetSensorType()
{
    return config.sensorType;
}

void SetupESRDriver(SensorType)
{
    // NOP
}

int GetESRSupplyR()
{
    // Nernst AC injection resistor value
    return 22000;
}

void ToggleESRDriver(SensorType)
{
    palTogglePad(NERNST_49_ESR_DRIVER_PORT, NERNST_49_ESR_DRIVER_PIN);
}

uint16_t GetSoftwareCalibration()
{
    return wbo::DecodeCalibration(config.calibration);
}

float GetPumpCalibrationGain()
{
    return GetSensorType() == SensorType::LSU49_RTRIM_SW
        ? softwareCalibrationGain : 1.0f;
}

bool IsSensorCalibrationReady()
{
    return wbo::HasRequiredCalibration(GetSensorType(), GetSoftwareCalibration());
}

static bool WriteSensorSettings(uint16_t centiohms, SensorType type)
{
    if (GetSoftwareCalibration() == centiohms && GetSensorType() == type) {
        return true; // avoid unnecessary flash erase
    }
    const auto previous = config.calibration;
    const auto previousType = config.sensorType;
    // Publish type, record and gain together only after verified persistence.
    // Sampling cannot observe intermediate values or a failed write.
    chSysLock();
    config.calibration = wbo::MakeCalibrationRecord(centiohms);
    config.sensorType = type;
    const bool saved = WriteConfigurationUnlocked(ReadCanBitrate());
    if (saved) {
        softwareCalibrationGain = centiohms ? wbo::CalibrationGain(centiohms) : 0.0f;
    } else {
        config.calibration = previous;
        config.sensorType = previousType;
    }
    chSysUnlock();
    return saved;
}

bool SetSensorType(SensorType type)
{
    if (static_cast<uint8_t>(type) > static_cast<uint8_t>(SensorType::LSU49_RTRIM_SW)) {
        return false;
    }
    GetConfiguration();
    return WriteSensorSettings(GetSoftwareCalibration(), type);
}

wbo::CalibrationResult SetSoftwareCalibration(uint16_t centiohms, SensorType type)
{
    if (!wbo::IsCalibrationValueValid(centiohms)) {
        return wbo::CalibrationResult::Invalid;
    }
    if (type != SensorType::LSU49 && type != SensorType::LSU49_RTRIM_SW) {
        return wbo::CalibrationResult::WrongSensor;
    }
    GetConfiguration();
    return WriteSensorSettings(centiohms, type)
        ? wbo::CalibrationResult::Saved : wbo::CalibrationResult::FlashError;
}


// A previous failed write may have erased flash while RAM was rolled back.
// An unchanged request is successful only when flash still matches that RAM.
static bool ConfigurationMatchesFlash(wbo::CanBitrate bitrate) {
    const auto record = wbo::MakeCanBitrateRecord(bitrate);
    return VerifyCanFlash(__configflash__start__, reinterpret_cast<const uint8_t*>(&config), sizeof(config)) &&
        VerifyCanFlash(__configflash__start__ + wbo::CanBitrateRecordOffset,
            reinterpret_cast<const uint8_t*>(&record), sizeof(record));
}


#include "../../../for_rusefi/manager_can.h"
static uint8_t managerActiveRate;
void ManagerInit(){managerActiveRate=static_cast<uint8_t>(ReadCanBitrate());}
uint8_t ManagerCurrentIndex(){return config.afr[0].RusEfiIdx;}
void ManagerReadConfig(uint8_t* p){
    p[0]=config.afr[0].RusEfiIdx;p[1]=static_cast<uint8_t>(config.sensorType);
    p[2]=static_cast<uint8_t>(ReadCanBitrate());
    p[3]=(config.afr[0].RusEfiTx?1:0)|(config.afr[0].RusEfiTxDiag?2:0);p[4]=managerActiveRate;
}
// Only F0-visible settings are changed by this service. Keep a 12-byte
// rollback snapshot, not a second 256-byte Configuration on the CAN stack.
struct F0SettingsSnapshot {
    wbo::CalibrationRecord calibration;
    SensorType sensor;
    uint8_t index;
    uint8_t flags;
};
static_assert(sizeof(F0SettingsSnapshot) <= 12);

uint8_t ManagerApplyConfig(const uint8_t* p, bool defaults) {
    const auto previousBitrate = ReadCanBitrate();
    auto bitrate = previousBitrate;
    if (!defaults) {
        if (!wbm_valid_config(p[1], p[2], p[3])) return WBM_INVALID;
        if (!wbo::HasRequiredCalibration(static_cast<SensorType>(p[1]), GetSoftwareCalibration())) return WBM_INVALID;
        bitrate = static_cast<wbo::CanBitrate>(p[2]);
        const auto marker = *reinterpret_cast<const volatile uint32_t*>(wbo::CanBitrateBootloaderMarkerAddress);
        if (bitrate != previousBitrate && !wbo::IsBitrateChangeAllowed(p[2], marker)) return WBM_BOOTLOADER_REQUIRED;
    }

    chSysLock();
    const F0SettingsSnapshot previous {
        config.calibration, config.sensorType, config.afr[0].RusEfiIdx,
        static_cast<uint8_t>((config.afr[0].RusEfiTx ? 1 : 0) | (config.afr[0].RusEfiTxDiag ? 2 : 0))
    };
    if (defaults) {
        // Preserve unrelated legacy layout fields. This F0 service has no
        // commands for other channels, auxiliary outputs or other protocols.
        const uint8_t strapIndex[] = {2, 0, 3, 4, 0, 1, 5, 6, 7};
        config.afr[0].RusEfiIdx = strapIndex[BoardGetHwId()];
        config.sensorType = BOARD_DEFAULT_SENSOR_TYPE;
        config.afr[0].RusEfiTx = true;
        config.afr[0].RusEfiTxDiag = true;
        config.calibration = wbo::MakeCalibrationRecord(0);
    } else {
        config.afr[0].RusEfiIdx = p[0];
        config.sensorType = static_cast<SensorType>(p[1]);
        config.afr[0].RusEfiTx = (p[3] & 1) != 0;
        config.afr[0].RusEfiTxDiag = (p[3] & 2) != 0;
    }
    const auto& calibration = config.calibration;
    const bool changed = bitrate != previousBitrate || config.afr[0].RusEfiIdx != previous.index ||
        config.sensorType != previous.sensor || config.afr[0].RusEfiTx != ((previous.flags & 1) != 0) ||
        config.afr[0].RusEfiTxDiag != ((previous.flags & 2) != 0) ||
        calibration.magic != previous.calibration.magic || calibration.centiohms != previous.calibration.centiohms ||
        calibration.inverted != previous.calibration.inverted;
    // Idempotent settings do not erase flash, regardless of transaction number.
    const bool saved = (!changed && ConfigurationMatchesFlash(bitrate)) || WriteConfigurationUnlocked(bitrate);
    if (!saved) {
        config.afr[0].RusEfiIdx = previous.index;
        config.sensorType = previous.sensor;
        config.afr[0].RusEfiTx = (previous.flags & 1) != 0;
        config.afr[0].RusEfiTxDiag = (previous.flags & 2) != 0;
        config.calibration = previous.calibration;
    }
    softwareCalibrationGain = GetSoftwareCalibration() ? wbo::CalibrationGain(GetSoftwareCalibration()) : 0.0f;
    chSysUnlock();
    return saved ? WBM_OK : WBM_FLASH_ERROR;
}
