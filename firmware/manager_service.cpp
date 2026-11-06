#include "manager_service.h"
#include "port.h"
#include "sampling.h"
#include "heater_control.h"
#include "pump_dac.h"
#include "can.h"
#include "../for_rusefi/manager_can.h"
extern void ManagerReadConfig(uint8_t* out);
extern uint8_t ManagerCurrentIndex();
extern uint8_t ManagerApplyConfig(const uint8_t* data,bool defaults);
static systime_t identifyStart;
static uint8_t identifySeconds;
/* Write helpers compare settings before erasing flash. No RAM reply cache. */
static void Put16(uint8_t* p,int value){p[0]=value;p[1]=value>>8;}
static int16_t SignedMicroamps(float ma){float ua=ma*1000;return ua>32767?32767:ua< -32768?-32768:static_cast<int16_t>(ua);}
bool ManagerIdentifyActive(){return identifySeconds && chVTTimeElapsedSinceX(identifyStart)<TIME_S2I(identifySeconds);}
bool ManagerRequest(const CANRxFrame& f){
 if(f.IDE!=CAN_IDE_EXT||f.RTR!=CAN_RTR_DATA||f.DLC!=8||f.data8[1]!=BoardGetHwId())return false;
 const bool indexed=(f.EID&0xffff0000u)==WBM_INDEX_REQUEST_BASE;
 if(indexed){if(f.EID!=wbm_index_request(BoardGetHwId(),ManagerCurrentIndex()))return false;}
 else if(f.EID!=WBM_REQUEST)return false;
 CANTxFrame r{};r.IDE=CAN_IDE_EXT;r.RTR=CAN_RTR_DATA;
 r.EID=indexed?WBM_INDEX_REPLY_BASE|(f.EID&0xffffu):WBM_REPLY_BASE|BoardGetHwId();r.DLC=8;
 auto p=r.data8;p[0]=f.data8[0];p[1]=f.data8[2];
 switch(p[0]){
 case WBM_INFO:p[3]=WBM_BOARD_F0;p[4]=indexed?WBM_INDEX_VERSION:WBM_VERSION;p[5]=WBM_CAPS;p[6]=0;p[7]=1;break;
 case WBM_CONFIG:ManagerReadConfig(p+3);break;
 case WBM_APPLY:case WBM_DEFAULTS:
  if((p[0]==WBM_APPLY&&f.data8[7]!=WBM_CONFIRM)||(p[0]==WBM_DEFAULTS&&f.data8[3]!=WBM_CONFIRM))p[2]=WBM_INVALID;
  else p[2]=ManagerApplyConfig(f.data8+3,p[0]==WBM_DEFAULTS);
  ManagerReadConfig(p+3);break;
 case WBM_RSET:{
  uint16_t value=f.data8[3]|(uint16_t(f.data8[4])<<8);auto type=static_cast<wbo::SensorType>(f.data8[5]);
  if(f.data8[6]!=WBM_CONFIRM||!wbo::IsCalibrationValueValid(value)||!wbo::HasRequiredCalibration(type,value))p[2]=WBM_INVALID;
  else{auto result=SetSoftwareCalibration(value,type);p[2]=result==wbo::CalibrationResult::Saved?WBM_OK:result==wbo::CalibrationResult::FlashError?WBM_FLASH_ERROR:WBM_INVALID;}
  [[fallthrough]];
 }
 case WBM_RGET:{uint16_t value=GetSoftwareCalibration();Put16(p+3,value);p[5]=static_cast<uint8_t>(GetSensorType());p[6]=IsSensorCalibrationReady()?1:0;break;}
 case WBM_IDENTIFY:
  if(f.data8[3]>30)p[2]=WBM_INVALID;
  else{identifyStart=chVTGetSystemTimeX();identifySeconds=f.data8[3];p[3]=identifySeconds;}break;
 case WBM_RESET:if(f.data8[3]!=WBM_CONFIRM)p[2]=WBM_INVALID;break;
 case WBM_DIAG1:{const auto& s=GetSampler(0);Put16(p+3,static_cast<int>(s.GetNernstAc()*1000));Put16(p+5,SignedMicroamps(s.GetPumpNominalCurrent()));p[7]=static_cast<uint8_t>(GetHeaterState(0));break;}
 case WBM_DIAG2:Put16(p+3,SignedMicroamps(GetPumpCurrent(0)));Put16(p+5,static_cast<int>(GetHeaterController(0).GetHeaterEffectiveVoltage()*1000));p[7]=static_cast<uint8_t>(GetRemoteBatteryVoltage()*10);break;
 default:p[2]=WBM_UNSUPPORTED;break;
 }
 if(canTransmitTimeout(&CAND1,CAN_ANY_MAILBOX,&r,TIME_MS2I(100))==MSG_OK&&p[0]==WBM_RESET&&p[2]==WBM_OK){chThdSleepMilliseconds(50);NVIC_SystemReset();}
 return true;
}
