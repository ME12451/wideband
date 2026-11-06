#!/usr/bin/env python3
"""Exercise the actual F0 configuration setters and flash failure rollback on host."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class SelectionTests(unittest.TestCase):
    def test_actual_f0_setters(self):
        with tempfile.TemporaryDirectory() as directory:
            tmp=Path(directory)
            (tmp/'hal.h').write_text(r'''#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
using ioportid_t = int;
using iopadid_t = int;
using adcsample_t = uint16_t;
struct ADCDriver {};
inline ADCDriver ADCD1;
struct ADCConversionGroup {
    bool circular; int channels; void (*end)(ADCDriver*); void (*error)(ADCDriver*);
    uint32_t cfgr, tr, smpr, chsel;
};
inline int lockDepth = 0;
inline void chSysLock() { assert(lockDepth == 0); ++lockDepth; }
inline void chSysUnlock() { assert(lockDepth == 1); --lockDepth; }
inline void chSysLockFromISR() {}
inline void chSysUnlockFromISR() {}
inline void adcStart(ADCDriver*, void*) {}
inline void adcStartConversion(ADCDriver*, const ADCConversionGroup*, adcsample_t*, int) {}
inline void palTogglePad(int, int) {}
struct CANConfig { uint32_t mcr, btr; };
#define ADC_CFGR1_CONT 0
#define ADC_CFGR1_RES_12BIT 0
#define ADC_TR(a,b) 0
#define ADC_SMPR_SMP_7P5 0
#define ADC_CHSELR_CHSEL0 0
#define ADC_CHSELR_CHSEL2 0
#define ADC_CHSELR_CHSEL3 0
#define TIME_INFINITE 0
#define STM32_PCLK1 48000000
#define CAN_MCR_ABOM 0x40
#define CAN_MCR_AWUM 0x20
#define CAN_MCR_TXFP 4
#define CAN_BTR_SJW(n) ((n) << 24)
#define CAN_BTR_BRP(n) (n)
#define CAN_BTR_TS1(n) ((n) << 16)
#define CAN_BTR_TS2(n) ((n) << 20)
''')
            (tmp/'ch.hpp').write_text('''#pragma once
namespace chibios_rt { struct BinarySemaphore {
    explicit BinarySemaphore(bool) {} void signalI() {} void wait(int) {}
}; }
''')
            (tmp/'io_pins.h').write_text('''#pragma once
#define ID_SEL1_PORT 0
#define ID_SEL1_PIN 0
#define ID_SEL2_PORT 0
#define ID_SEL2_PIN 1
#define NERNST_49_ESR_DRIVER_PORT 0
#define NERNST_49_ESR_DRIVER_PIN 2
''')
            (tmp/'test.cpp').write_text(r'''
#include "port.h"
#include "can_bitrate_storage.h"
#include <cstring>
#include <iostream>
using namespace wbo;
alignas(4) uint8_t __configflash__start__[1024];
static bool failWrite;
static int erases;
uint8_t readSelPin(ioportid_t, iopadid_t) { return 1; }
void Flash::ErasePage(uint8_t page) {
    assert(page == 31 && lockDepth == 1);
    ++erases;
    std::memset(__configflash__start__,255,1024);
}
void Flash::Write(flashaddr_t address, const uint8_t* data, size_t size) {
    assert(lockDepth == 1);
    if (!failWrite) std::memcpy(reinterpret_cast<void*>(address),data,size);
}
extern wbo::CanBitrate ReadCanBitrate();
static void checkPersisted(SensorType type, uint16_t value, CanBitrate rate) {
    // Reload via actual firmware getter: this models reboot/config reload.
    GetConfiguration();
    assert(GetSensorType() == type);
    assert(GetSoftwareCalibration() == value);
    assert(ReadCanBitrate() == rate);
    assert(lockDepth == 0);
}
int main() {
    std::memset(__configflash__start__,255,1024);
    InitConfiguration();
    assert(BoardGetHwId() == 4);
    assert(GetSensorType() == SensorType::LSU49);
    assert(GetSoftwareCalibration() == 0 && GetPumpCalibrationGain() == 1);
    for (auto rate : {CanBitrate::Kbit500,CanBitrate::Mbit1}) {
        auto marker = MakeCanBitrateRecord(rate);
        std::memcpy(__configflash__start__+CanBitrateRecordOffset,&marker,sizeof(marker));
        assert(SetSoftwareCalibration(12345,SensorType::LSU49_RTRIM_SW) == CalibrationResult::Saved);
        checkPersisted(SensorType::LSU49_RTRIM_SW,12345,rate);
        assert(IsSensorCalibrationReady());
        assert(GetPumpCalibrationGain() == CalibrationGain(12345));
        const int count = erases;
        assert(SetSoftwareCalibration(12345,SensorType::LSU49_RTRIM_SW) == CalibrationResult::Saved);
        assert(erases == count);
        assert(SetSensorType(SensorType::LSU49));
        checkPersisted(SensorType::LSU49,12345,rate);
        assert(GetPumpCalibrationGain() == 1);
        assert(SetSensorType(SensorType::LSU49_RTRIM_SW));
        checkPersisted(SensorType::LSU49_RTRIM_SW,12345,rate);
        assert(GetPumpCalibrationGain() == CalibrationGain(12345));
        // Every pre-existing sensor type remains selectable and ignores Rtrim.
        for (auto type : {SensorType::LSU49,SensorType::LSU42,SensorType::LSUADV,SensorType::LSU49_FAE}) {
            assert(SetSensorType(type));
            checkPersisted(type,12345,rate);
            assert(GetPumpCalibrationGain() == 1);
        }
        assert(SetSoftwareCalibration(0,SensorType::LSU49_RTRIM_SW) == CalibrationResult::Saved);
        checkPersisted(SensorType::LSU49_RTRIM_SW,0,rate);
        assert(!IsSensorCalibrationReady() && GetPumpCalibrationGain() == 0);
        assert(SetSoftwareCalibration(12345,SensorType::LSU49_RTRIM_SW) == CalibrationResult::Saved);
        uint8_t saved[1024]; std::memcpy(saved,__configflash__start__,1024);
        failWrite = true;
        assert(SetSoftwareCalibration(23456,SensorType::LSU49) == CalibrationResult::FlashError);
        // Failed writes must not publish a different running type/gain/value.
        assert(GetSensorType() == SensorType::LSU49_RTRIM_SW);
        assert(GetSoftwareCalibration() == 12345);
        assert(GetPumpCalibrationGain() == CalibrationGain(12345));
        assert(lockDepth == 0);
        failWrite = false;
        std::memcpy(__configflash__start__,saved,1024);
    }
    const int count = erases;
    assert(!SetSensorType(static_cast<SensorType>(5)));
    assert(SetSoftwareCalibration(2999,SensorType::LSU49_RTRIM_SW) == CalibrationResult::Invalid);
    assert(SetSoftwareCalibration(30001,SensorType::LSU49_RTRIM_SW) == CalibrationResult::Invalid);
    assert(SetSoftwareCalibration(12345,SensorType::LSU42) == CalibrationResult::WrongSensor);
    assert(erases == count);
    // A valid custom-type selection with corrupt trim data remains invalid.
    auto* stored = reinterpret_cast<Configuration*>(__configflash__start__);
    stored->calibration.inverted ^= 1;
    GetConfiguration();
    assert(GetSensorType() == SensorType::LSU49_RTRIM_SW);
    assert(!IsSensorCalibrationReady() && GetPumpCalibrationGain() == 0);
    std::cout << "PASS: real F0 setters, atomic selection, physical-mode isolation, reboot reload, both bitrates, failed-write rollback, missing/corrupt Rtrim\n";
}
''')
            subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsingle-precision-constant',
                '-DWB_PROD=1','-DWB_CAN_BITRATE_CONFIG=1','-I'+str(tmp),'-I'+str(ROOT/'test'),
                '-I'+str(ROOT/'firmware'),'-I'+str(ROOT/'firmware/boards'),'-I'+str(ROOT/'firmware/boards/f0_module'),
                str(tmp/'test.cpp'),str(ROOT/'firmware/boards/f0_module/port.cpp'),
                str(ROOT/'firmware/boards/f0_module/port_shared.cpp'),'-o',str(tmp/'test')],check=True)
            subprocess.run([str(tmp/'test')],check=True)

if __name__ == '__main__': unittest.main()
