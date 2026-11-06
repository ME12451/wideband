#pragma once
#include <cstdint>
#include "wideband_can.h"

namespace wbo {
// F0 software emulation of the missing LSU4.9 connector trim resistor.
// Requests use extended DATA frames, never broadcast; resistance is 0.01 ohm LE.
constexpr uint32_t SetCalibrationId = 0x0EF90000;
constexpr uint32_t CalibrationReplyId = 0x0EF90001;
constexpr uint32_t GetCalibrationId = 0x0EF90002;
enum class CalibrationResult : uint8_t { Read = 0, Saved = 1, Invalid = 2, FlashError = 3, WrongSensor = 4 };
struct __attribute__((packed)) CalibrationReply {
    uint8_t hwId;
    uint16_t centiohms;
    CalibrationResult result;
    SensorType sensorType;
};
static_assert(sizeof(CalibrationReply) == 5);
constexpr bool IsCalibrationValueValid(uint16_t value) {
    return value == 0 || (value >= 3000 && value <= 30000);
}
struct CalibrationRecord {
    uint32_t magic;
    uint16_t centiohms;
    uint16_t inverted;
};
static_assert(sizeof(CalibrationRecord) == 8);
constexpr CalibrationRecord MakeCalibrationRecord(uint16_t value) {
    return {0x52434131, value, static_cast<uint16_t>(~value)};
}
constexpr uint16_t DecodeCalibration(const CalibrationRecord& record) {
    return record.magic == 0x52434131 &&
        static_cast<uint16_t>(record.centiohms ^ record.inverted) == 0xffff &&
        IsCalibrationValueValid(record.centiohms) ? record.centiohms : 0;
}
inline float CalibrationGain(uint16_t value) {
    // Without RT, all pump current flows through the 61.9 ohm measuring resistor.
    // With RT, only RT / (RT + 61.9) flows through it. Emulate this before lambda.
    return value && IsCalibrationValueValid(value) ? value / (value + 6190.0f) : 1.0f;
}
constexpr bool HasRequiredCalibration(SensorType type, uint16_t value) {
    return type != SensorType::LSU49_RTRIM_SW || (value != 0 && IsCalibrationValueValid(value));
}
constexpr uint8_t SensorTypeFlags(SensorType type) {
    // Bit 1 remains Bosch LSU49 family; bit 3 identifies software trim explicitly.
    return type == SensorType::LSU49_RTRIM_SW ? 0x0a :
        type == SensorType::LSU49 ? 0x02 : type == SensorType::LSU49_FAE ? 0x04 : 0;
}
} // namespace wbo
