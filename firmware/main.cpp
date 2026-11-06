#include "ch.h"
#include "hal.h"

#include "can.h"
#include "manager_service.h"
extern void ManagerInit();
#include "status.h"
#include "heater_control.h"
#include "pump_control.h"
#include "pump_dac.h"
#include "sampling.h"
#include "uart.h"
#include "io_pins.h"
#include "auxout.h"
#include "max3185x.h"
#include "port.h"
#include "tunerstudio.h"
#include "indication.h"

#include "wideband_config.h"

#include <cstring>

using namespace wbo;

/*
 * Application entry point.
 */
int main() {
    halInit();
    chSysInit();

    // Load configuration
    InitConfiguration();

    // Fire up all of our threads
    StartSampling();
    InitPumpDac();
    StartHeaterControl();
    StartPumpControl();
    // F0 has no external analogue outputs.

#if defined(TS_ENABLED) && TS_ENABLED != 0
    startTunerStudioConnectivity();
#endif

    ManagerInit();
    InitCan();
    // CAN-only F0 manager build: no serial transport.
    InitIndication();

#if (EGT_CHANNELS > 0)
    StartEgt();
#endif

    // F0-only manager indication. No extra thread; bounded 50 ms response.
    unsigned tick=0;
    while(true)
    {
        const auto status=GetCurrentStatus(0);
        if(ManagerIdentifyActive()){
            palWritePad(LED_BLUE_PORT,LED_BLUE_PIN,(tick/2)%2);
            palWritePad(LED_GREEN_PORT,LED_GREEN_PIN,!((tick/2)%2));
        }else{
            const bool fault=status>=Status::FirstError&&status<=Status::LastError;
            const unsigned period=GetHeaterAllowed()!=HeaterAllow::Allowed?40:status==Status::RunningClosedLoop?14:2;
            palWritePad(LED_GREEN_PORT,LED_GREEN_PIN,!fault&&((tick/period)%2));
            // Preserve counted error indication (300ms on/off, then 2s pause).
            unsigned phase=tick%(12*static_cast<unsigned>(status)+40);
            palWritePad(LED_BLUE_PORT,LED_BLUE_PIN,fault&&phase<12*static_cast<unsigned>(status)&&((phase/6)%2==0));
        }
        tick++;
        chThdSleepMilliseconds(50);
    }
}

typedef enum  {
	Reset = 1,
	NMI = 2,
	HardFault = 3,
	MemManage = 4,
	BusFault = 5,
	UsageFault = 6,
} FaultType;

#define bkpt() __asm volatile("BKPT #0\n")

extern "C" void HardFault_Handler_C(void* sp) {
	//Copy to local variables (not pointers) to allow GDB "i loc" to directly show the info
	//Get thread context. Contains main registers including PC and LR
	struct port_extctx ctx;
	memcpy(&ctx, sp, sizeof(struct port_extctx));

	//Interrupt status register: Which interrupt have we encountered, e.g. HardFault?
	FaultType faultType = (FaultType)__get_IPSR();
	(void)faultType;
#if (__CORTEX_M > 0)
	//For HardFault/BusFault this is the address that was accessed causing the error
	uint32_t faultAddress = SCB->BFAR;

	//Flags about hardfault / busfault
	//See http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Cihdjcfc.html for reference
	bool isFaultPrecise = ((SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 1) ? true : false);
	bool isFaultImprecise = ((SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 2) ? true : false);
	bool isFaultOnUnstacking = ((SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 3) ? true : false);
	bool isFaultOnStacking = ((SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 4) ? true : false);
	bool isFaultAddressValid = ((SCB->CFSR >> SCB_CFSR_BUSFAULTSR_Pos) & (1 << 7) ? true : false);
	(void)faultAddress;
	(void)isFaultPrecise;
	(void)isFaultImprecise;
	(void)isFaultOnUnstacking;
	(void)isFaultOnStacking;
	(void)isFaultAddressValid;
#endif

	//Cause debugger to stop. Ignored if no debugger is attached
	bkpt();
	NVIC_SystemReset();
}

extern "C" void UsageFault_Handler_C(void* sp) {
	//Copy to local variables (not pointers) to allow GDB "i loc" to directly show the info
	//Get thread context. Contains main registers including PC and LR
	struct port_extctx ctx;
	memcpy(&ctx, sp, sizeof(struct port_extctx));

	//Interrupt status register: Which interrupt have we encountered, e.g. HardFault?
	FaultType faultType = (FaultType)__get_IPSR();
	(void)faultType;
#if (__CORTEX_M > 0)
	//Flags about hardfault / busfault
	//See http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Cihdjcfc.html for reference
	bool isUndefinedInstructionFault = ((SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 0) ? true : false);
	bool isEPSRUsageFault = ((SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 1) ? true : false);
	bool isInvalidPCFault = ((SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 2) ? true : false);
	bool isNoCoprocessorFault = ((SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 3) ? true : false);
	bool isUnalignedAccessFault = ((SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 8) ? true : false);
	bool isDivideByZeroFault = ((SCB->CFSR >> SCB_CFSR_USGFAULTSR_Pos) & (1 << 9) ? true : false);
	(void)isUndefinedInstructionFault;
	(void)isEPSRUsageFault;
	(void)isInvalidPCFault;
	(void)isNoCoprocessorFault;
	(void)isUnalignedAccessFault;
	(void)isDivideByZeroFault;
#endif

	bkpt();
	NVIC_SystemReset();
}

extern "C" void MemManage_Handler_C(void* sp) {
	//Copy to local variables (not pointers) to allow GDB "i loc" to directly show the info
	//Get thread context. Contains main registers including PC and LR
	struct port_extctx ctx;
	memcpy(&ctx, sp, sizeof(struct port_extctx));

	//Interrupt status register: Which interrupt have we encountered, e.g. HardFault?
	FaultType faultType = (FaultType)__get_IPSR();
	(void)faultType;
#if (__CORTEX_M > 0)
	//For HardFault/BusFault this is the address that was accessed causing the error
	uint32_t faultAddress = SCB->MMFAR;
	(void)faultAddress;

	//Flags about hardfault / busfault
	//See http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.dui0552a/Cihdjcfc.html for reference
	bool isInstructionAccessViolation = ((SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 0) ? true : false);
	bool isDataAccessViolation = ((SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 1) ? true : false);
	bool isExceptionUnstackingFault = ((SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 3) ? true : false);
	bool isExceptionStackingFault = ((SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 4) ? true : false);
	bool isFaultAddressValid = ((SCB->CFSR >> SCB_CFSR_MEMFAULTSR_Pos) & (1 << 7) ? true : false);
	(void)isInstructionAccessViolation;
	(void)isDataAccessViolation;
	(void)isExceptionUnstackingFault;
	(void)isExceptionStackingFault;
	(void)isFaultAddressValid;
#endif

	bkpt();
	NVIC_SystemReset();
}
