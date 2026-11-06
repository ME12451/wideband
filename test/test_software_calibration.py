#!/usr/bin/env python3
"""Host verification of the real config layout, flash writer, sampler and lambda path."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class CalibrationTests(unittest.TestCase):
    def test_flash_compatibility_and_measurement(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp = Path(directory)
            (tmp/'io_pins.h').write_text('#pragma once\n')
            (tmp/'timer_stub.cpp').write_text('#include "timer.h"\nTimer::Timer() {}\nvoid Timer::reset() {}\n')
            (tmp/'test.cpp').write_text(r'''
#include "port.h"
#include "sampling.h"
#include "lambda_conversion.h"
#include "can_bitrate_storage.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
using namespace wbo;
Configuration config;
Sampler sampler;
alignas(4) uint8_t page[1024];
bool fail = false;
void Flash::ErasePage(uint8_t index) { assert(index == 31); std::memset(page, 255, sizeof(page)); }
void Flash::Write(flashaddr_t address, const uint8_t* data, size_t size) {
    if (!fail) std::memcpy(reinterpret_cast<void*>(address), data, size);
}
SensorType GetSensorType() { return config.sensorType; }
int GetESRSupplyR() { return 22000; }
float GetPumpCalibrationGain() {
    return GetSensorType() == SensorType::LSU49_RTRIM_SW ? (DecodeCalibration(config.calibration) ? CalibrationGain(DecodeCalibration(config.calibration)) : 0) : 1;
}
bool IsSensorCalibrationReady() { return HasRequiredCalibration(GetSensorType(), DecodeCalibration(config.calibration)); }
const ISampler& GetSampler(int) { return sampler; }
static void measure(float raw, uint16_t value) {
    config.sensorType = value ? SensorType::LSU49_RTRIM_SW : SensorType::LSU49;
    config.calibration = MakeCalibrationRecord(value);
    // Feed the real ADC filter and current conversion, then compare to a
    // physically trimmed equivalent current passed through the same lambda curve.
    AnalogChannelResult data{0.45f, 1.65f - raw * 0.619f, 0};
    for (int i=0; i<4000; ++i) sampler.ApplySample(data, 1.65f);
    const float corrected = raw * CalibrationGain(value);
    assert(std::fabs(sampler.GetPumpNominalCurrent() - corrected) < 0.0001f);
    const float phi = (corrected < 0 ? -0.28299f : -0.44817f) * corrected + 0.99559f;
    assert(std::fabs(GetLambda(0) - 1/phi) < 0.0001f);
    const float calibratedLambda = GetLambda(0);
    config.sensorType = SensorType::LSU49;
    // The remembered resistance must have NO effect on normal LSU49.
    data.PumpCurrentVoltage = 1.65f - corrected * 0.619f;
    for (int i=0; i<4000; ++i) sampler.ApplySample(data, 1.65f);
    assert(std::fabs(GetLambda(0) - calibratedLambda) < 0.0001f);
}
int main() {
    static_assert(sizeof(Configuration) == 256);
    // Stable original field positions, including packed protocol arrays.
    assert(reinterpret_cast<uint8_t*>(&config.sensorType) - reinterpret_cast<uint8_t*>(&config) == 135);
    assert(reinterpret_cast<uint8_t*>(&config.afr) - reinterpret_cast<uint8_t*>(&config) == 136);
    assert(reinterpret_cast<uint8_t*>(&config.egt) - reinterpret_cast<uint8_t*>(&config) == 168);
    assert(reinterpret_cast<uint8_t*>(&config.calibration) - reinterpret_cast<uint8_t*>(&config) == 200);
    config.LoadDefaults();
    assert(config.IsValid());
    assert(DecodeCalibration(config.calibration) == 0);
    assert(CalibrationGain(0) == 1);
    // Previous padding contents must never activate calibration.
    for (int byte : {0, 255, 66}) {
        std::memset(&config.calibration, byte, sizeof(config.calibration));
        assert(DecodeCalibration(config.calibration) == 0);
    }
    for (unsigned value=0; value<=65535; ++value) {
        const bool valid = value == 0 || (value >= 3000 && value <= 30000);
        assert(IsCalibrationValueValid(value) == valid);
        assert(DecodeCalibration(MakeCalibrationRecord(value)) == (valid ? value : 0));
    }
    const auto record = MakeCalibrationRecord(12345);
    for (int bit=0; bit<64; ++bit) {
        auto corrupted = record;
        reinterpret_cast<uint8_t*>(&corrupted)[bit/8] ^= 1 << (bit%8);
        assert(DecodeCalibration(corrupted) == 0);
    }
    for (auto rate : {CanBitrate::Kbit500, CanBitrate::Mbit1}) {
        config.LoadDefaults();
        config.afr[0].RusEfiIdx = 7;
        config.calibration = record;
        assert(WriteCanConfiguration(page, reinterpret_cast<uint8_t*>(&config), sizeof(config), rate));
        Configuration restored;
        std::memcpy(&restored, page, sizeof(restored));
        assert(restored.IsValid() && restored.afr[0].RusEfiIdx == 7);
        assert(DecodeCalibration(restored.calibration) == 12345);
        CanBitrateRecord bitrate;
        std::memcpy(&bitrate, page+CanBitrateRecordOffset, sizeof(bitrate));
        assert(DecodeCanBitrate(bitrate) == rate);
        fail = true;
        assert(!WriteCanConfiguration(page, reinterpret_cast<uint8_t*>(&config), sizeof(config), rate));
        fail = false;
    }
    for (uint16_t value : {uint16_t(0), uint16_t(3000), uint16_t(6190), uint16_t(12345), uint16_t(30000)}) {
        measure(-1.0f, value);
        measure(0.0f, value);
        measure(0.5f, value);
    }
    config.calibration = record;
    for (auto type : {SensorType::LSU49, SensorType::LSU42, SensorType::LSUADV, SensorType::LSU49_FAE}) {
        config.sensorType = type;
        assert(GetPumpCalibrationGain() == 1);
    }
    static_assert(static_cast<uint8_t>(SensorType::LSU49) == 0);
    static_assert(static_cast<uint8_t>(SensorType::LSU42) == 1);
    static_assert(static_cast<uint8_t>(SensorType::LSUADV) == 2);
    static_assert(static_cast<uint8_t>(SensorType::LSU49_FAE) == 3);
    static_assert(static_cast<uint8_t>(SensorType::LSU49_RTRIM_SW) == 4);
    assert(SensorTypeFlags(SensorType::LSU49) == 2);
    assert(SensorTypeFlags(SensorType::LSU49_RTRIM_SW) == 10);
    assert(SensorTypeFlags(SensorType::LSU49_FAE) == 4);
    config.sensorType = SensorType::LSU49_RTRIM_SW;
    for (auto value : {uint16_t(0), uint16_t(1), uint16_t(2999), uint16_t(30001), uint16_t(65535)}) {
        config.calibration = MakeCalibrationRecord(value);
        assert(!IsSensorCalibrationReady());
        assert(GetLambda(0) == 0); // actual lambda output cannot be flagged valid
    }
    for (int bit=0; bit<64; ++bit) {
        config.calibration = record;
        reinterpret_cast<uint8_t*>(&config.calibration)[bit/8] ^= 1 << (bit%8);
        assert(!IsSensorCalibrationReady());
        assert(GetLambda(0) == 0);
    }
    config.calibration = record;
    assert(IsSensorCalibrationReady());
    // LSU49_RTRIM_SW uses the exact same Nernst temperature characteristic.
    const float customTemperature = sampler.GetSensorTemperature();
    config.sensorType = SensorType::LSU49;
    assert(sampler.GetSensorTemperature() == customTemperature);
    std::cout << "PASS: config migration, all values, 64 corruptions, flash readback/failure, both CAN rates, rich/stoich/lean sampler/lambda\n";
}
''')
            subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=undefined',
                '-DWB_CAN_BITRATE_CONFIG=1','-I'+str(tmp),'-I'+str(ROOT/'test'),
                '-I'+str(ROOT/'firmware'),'-I'+str(ROOT/'firmware/boards'),
                '-I'+str(ROOT/'firmware/boards/f0_module'),'-I'+str(ROOT/'firmware/util'),
                '-I'+str(ROOT/'firmware/libfirmware/util/include'),
                str(tmp/'test.cpp'),str(tmp/'timer_stub.cpp'),str(ROOT/'firmware/sampling.cpp'),
                str(ROOT/'firmware/lambda_conversion.cpp'),'-o',str(tmp/'test')],check=True)
            subprocess.run([str(tmp/'test')],check=True)

if __name__ == '__main__':
    unittest.main()
