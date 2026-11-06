#!/usr/bin/env python3
"""Exercise the actual F0 configuration setters and flash failure rollback on host."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]

class ManagerSnapshotTests(unittest.TestCase):
    def test_actual_manager_settings(self):
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
#include <sys/mman.h>
extern void ManagerInit();
extern uint8_t ManagerApplyConfig(const uint8_t*,bool);
extern void ManagerReadConfig(uint8_t*);
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
int main() {
    void* page=mmap(reinterpret_cast<void*>(0x08000000),4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(page!=MAP_FAILED);
    *reinterpret_cast<uint32_t*>(CanBitrateBootloaderMarkerAddress)=CanBitrateBootloaderMarker;
    std::memset(__configflash__start__,255,1024);
    InitConfiguration();
    for(auto rate:{CanBitrate::Kbit500,CanBitrate::Mbit1}) {
        auto record=MakeCanBitrateRecord(rate);
        std::memcpy(__configflash__start__+CanBitrateRecordOffset,&record,sizeof(record));
        assert(SetSoftwareCalibration(12345,SensorType::LSU49_RTRIM_SW)==CalibrationResult::Saved);
        auto* cfg=GetConfiguration();
        cfg->afr[0].RusEfiIdx=42;cfg->afr[0].RusEfiTx=true;cfg->afr[0].RusEfiTxDiag=true;
        cfg->afr[1].RusEfiIdx=201;cfg->egt[0].RusEfiIdx=211;cfg->auxOutBins[0][0]=12.25f;
        SetConfiguration();ManagerInit();
        uint8_t request[]={43,0,static_cast<uint8_t>(rate),1,0xa5};
        int before=erases;
        assert(ManagerApplyConfig(request,false)==0&&erases==before+1);
        assert(cfg->afr[0].RusEfiIdx==43&&GetSensorType()==SensorType::LSU49&&GetSoftwareCalibration()==12345&&GetPumpCalibrationGain()==1);
        assert(cfg->afr[1].RusEfiIdx==201&&cfg->egt[0].RusEfiIdx==211&&cfg->auxOutBins[0][0]==12.25f);
        before=erases;assert(ManagerApplyConfig(request,false)==0&&erases==before);
        Configuration expected=*cfg;
        uint8_t flash[1024];std::memcpy(flash,__configflash__start__,1024);
        request[0]=44;request[1]=4;request[3]=2;
        failWrite=true;
        assert(ManagerApplyConfig(request,false)==2);
        assert(std::memcmp(cfg,&expected,sizeof(expected))==0&&GetPumpCalibrationGain()==1&&lockDepth==0);
        assert(ReadCanBitrate()==CanBitrate::Kbit500); // RAM rollback is not a flash backup
        failWrite=false;std::memcpy(__configflash__start__,flash,1024);
        failWrite=true;
        assert(ManagerApplyConfig(nullptr,true)==2);
        assert(std::memcmp(cfg,&expected,sizeof(expected))==0&&lockDepth==0);
        // Restore the old settings after an erase failure. Even though RAM equals
        // the requested data, the damaged flash must be reprogrammed/verified.
        failWrite=false;request[0]=43;request[1]=0;request[3]=1;
        before=erases;assert(ManagerApplyConfig(request,false)==0&&erases==before+1);
        assert(std::memcmp(cfg,__configflash__start__,sizeof(*cfg))==0&&ReadCanBitrate()==rate);
        assert(ManagerApplyConfig(nullptr,true)==0);
        assert(cfg->afr[0].RusEfiIdx==0&&GetSensorType()==BOARD_DEFAULT_SENSOR_TYPE);
        assert(cfg->afr[0].RusEfiTx&&cfg->afr[0].RusEfiTxDiag&&GetSoftwareCalibration()==0&&GetPumpCalibrationGain()==1);
        assert(cfg->afr[1].RusEfiIdx==201&&cfg->egt[0].RusEfiIdx==211&&cfg->auxOutBins[0][0]==12.25f);
        assert(ReadCanBitrate()==rate);
        before=erases;assert(ManagerApplyConfig(nullptr,true)==0&&erases==before);
    }
    munmap(page,4096);
    std::cout<<"PASS: real 256-byte configuration, compact rollback, unchanged legacy bytes, defaults, no-op writes and repair after erase failure, both bitrates\n";

}
''')
            subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fsingle-precision-constant',
                '-DWB_PROD=1','-DWB_CAN_BITRATE_CONFIG=1','-I'+str(tmp),'-I'+str(ROOT/'test'),
                '-I'+str(ROOT/'firmware'),'-I'+str(ROOT/'firmware/boards'),'-I'+str(ROOT/'firmware/boards/f0_module'),
                str(tmp/'test.cpp'),str(ROOT/'firmware/boards/f0_module/port.cpp'),
                str(ROOT/'firmware/boards/f0_module/port_shared.cpp'),'-o',str(tmp/'test')],check=True)
            subprocess.run([str(tmp/'test')],check=True)

if __name__ == '__main__': unittest.main()
