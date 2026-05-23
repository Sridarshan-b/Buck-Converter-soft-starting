//#############################################################################
//
// FILE:   buck_softstart_closedloop_epwm2.c
//
// Buck Converter Control using TMS320F280049C
// - Soft start with PWM duty ramp on EPWM2A
// - PI update planned for ADC ISR after soft-start (ADC trigger = EPWM2 SOCA)
// - INA219 telemetry loop
//
//#############################################################################

#include "driverlib.h"
#include "device.h"
#include "board.h"
#include "c2000ware_libraries.h"


#ifndef EPWM_BASE_EP
#define EPWM_BASE_EP    EPWM2_BASE    // using EPWM2 (EPWM2A -> IR2104 IN)
#endif

#define CMPA_TARGET     92U
#define SOFTSTART_STEP  2U
#define SOFT_TIMER_FREQ 1000U   // 1 kHz -> 1 ms per step

/* PWM assumptions */
#define TBPRD_COUNTS    333U    // if your SysConfig TBPRD is different, update this

INA219 sensor;

/* PI Controller Gains (kept here for later use) */
#define Kp  0.30f
#define Ki  0.015f

/* ADC scaling */
#define ADC_BASE_LOCAL        ADCA_BASE
#define ADC_RESOLUTION  4095.0f
#define VREF_ADC        3.3f
#define DIV_RATIO       ( (10.0f) / (10.0f + 4.3f) )  // adjust if resistors differ
#define VOUT_TARGET     3.30f

volatile uint16_t gCurrentCMPA = 0;
volatile bool gSoftStartCompleted = false;
static float integral = 0.0f;

/* Optional debug variable you can watch */
volatile uint16_t ISR_last_cmp_verify = 0;

/* Prototypes */
void EPWM_SafeInit(void);
void ADC_LocalInit(void);
void setupEPWMforADCtrigger(void);
__interrupt void cpuTimer0SoftStartISR(void);
__interrupt void INT_myADCA_1_ISR(void); // match SysConfig symbol

int main(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();
    Board_init();                 // SysConfig generated board and pinmux init
    C2000Ware_libraries_init();

    /* Initialize EPWM/ADC helpers */
    EPWM_SafeInit();
    ADC_LocalInit();
    setupEPWMforADCtrigger();

    /* Ensure EPWM2 timebase / count-mode consistent (optional if SysConfig already sets it) */
    EPWM_setTimeBasePeriod(EPWM_BASE_EP, TBPRD_COUNTS);
    EPWM_setTimeBaseCounterMode(EPWM_BASE_EP, EPWM_COUNTER_MODE_UP);

    /* Register and start soft-start timer (CPU Timer0) */
    Interrupt_register(INT_TIMER0, cpuTimer0SoftStartISR);
    CPUTimer_setPeriod(CPUTIMER0_BASE, DEVICE_SYSCLK_FREQ / SOFT_TIMER_FREQ);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_startTimer(CPUTIMER0_BASE);
    Interrupt_enable(INT_TIMER0);

    /* Register ADC ISR (must match SysConfig symbol) */
    Interrupt_register(INT_myADCA_1, INT_myADCA_1_ISR);
    Interrupt_enable(INT_myADCA_1);

    EINT;
    ERTM;

    /* INA219 init (ensure I2C pins/peripheral configured by Board_init or init I2C before this) */
    INA219_init(&sensor, 0.1f, 2.0f); // Rshunt = 0.1Ω, Imax = 2A

    for(;;)
    {
        /* Telemetry loop (non-blocking) */
        float Vbus = INA219_readBusVoltage(&sensor);
        float Ishunt = INA219_readCurrent(&sensor);
        float P = INA219_readPower(&sensor);

        /* debug breakpoint here if needed */
        DEVICE_DELAY_US(100000); // 100 ms
    }
}

/* EPWM_SafeInit: start CMPA=0 for safe soft-start */
void EPWM_SafeInit(void)
{
    EPWM_setCounterCompareValue(EPWM_BASE_EP, EPWM_COUNTER_COMPARE_A, 0U);
    gCurrentCMPA = 0U;
    gSoftStartCompleted = false;

    /* If dead-band is configured in SysConfig, leave it. No DB configuration here. */
    /* If you want to set DB counts even if disabled, these calls are harmless:
       EPWM_setRisingEdgeDelayCount(EPWM_BASE_EP, DB_COUNTS);
       EPWM_setFallingEdgeDelayCount(EPWM_BASE_EP, DB_COUNTS);
    */
}

/* ADC_LocalInit: configure ADCA SOC triggered by EPWM2 SOCA and ADC interrupt */
void ADC_LocalInit(void)
{
    ADC_setVREF(ADC_BASE_LOCAL, ADC_REFERENCE_INTERNAL, ADC_REFERENCE_3_3V);
    ADC_setPrescaler(ADC_BASE_LOCAL, ADC_CLK_DIV_4_0);

    ADC_enableConverter(ADC_BASE_LOCAL);
    DEVICE_DELAY_US(1000);

    /* IMPORTANT: Use EPWM2 SOCA as trigger (adjust macro if your SDK differs) */
    ADC_setupSOC(ADC_BASE_LOCAL, ADC_SOC_NUMBER0, ADC_TRIGGER_EPWM2_SOCA, ADC_CH_ADCIN0, 15U);

    ADC_setInterruptSource(ADC_BASE_LOCAL, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
    ADC_clearInterruptStatus(ADC_BASE_LOCAL, ADC_INT_NUMBER1);
    ADC_enableInterrupt(ADC_BASE_LOCAL, ADC_INT_NUMBER1);
}

/* setupEPWMforADCtrigger: configure EPWM2 SOCA */
void setupEPWMforADCtrigger(void)
{
    EPWM_enableADCTrigger(EPWM_BASE_EP, EPWM_SOC_A);
    EPWM_setADCTriggerSource(EPWM_BASE_EP, EPWM_SOC_A, EPWM_SOC_TBCTR_PERIOD);
    EPWM_setADCTriggerEventPrescale(EPWM_BASE_EP, EPWM_SOC_A, 1);
}

/* cpuTimer0SoftStartISR: soft-start ramp */
__interrupt void cpuTimer0SoftStartISR(void)
{
    CPUTimer_clearOverflowFlag(CPUTIMER0_BASE);

    if(!gSoftStartCompleted)
    {
        uint32_t next = (uint32_t)gCurrentCMPA + SOFTSTART_STEP;

        if(next >= CMPA_TARGET)
        {
            next = CMPA_TARGET;
            gSoftStartCompleted = true;
            integral = 0.0f; /* reset integral when handing control to PI */
        }

        gCurrentCMPA = (uint16_t)next;

        /* write CMPA */
        EPWM_setCounterCompareValue(EPWM_BASE_EP, EPWM_COUNTER_COMPARE_A, gCurrentCMPA);

        /* read-back for debug (watch ISR_last_cmp_verify) */
        ISR_last_cmp_verify = EPWM_getCounterCompareValue(EPWM_BASE_EP, EPWM_COUNTER_COMPARE_A);

        if(gSoftStartCompleted)
        {
            CPUTimer_stopTimer(CPUTIMER0_BASE);
            CPUTimer_disableInterrupt(CPUTIMER0_BASE);
        }
    }

    /* Timer0 is CPU interrupt — no PIE ACK here */
}

/* INT_myADCA_1_ISR: PI update (runs once per PWM period, after SOCA) */
__interrupt void INT_myADCA_1_ISR(void)
{
    /* Read ADC result (replace names if your SysConfig used different symbols) */
    uint16_t rawADC = ADC_readResult(myADCA_RESULT_BASE, myADCA_SOC0);

    float vADC = ((float)rawADC / ADC_RESOLUTION) * VREF_ADC;
    float vOUT = vADC / DIV_RATIO;

    if(gSoftStartCompleted)
    {
        float error = VOUT_TARGET - vOUT;
        integral += error * Ki;

        /* anti-windup */
        if(integral > 0.8f) integral = 0.8f;
        if(integral < -0.8f) integral = -0.8f;

        float duty = (error * Kp) + integral;

        /* clamp duty and convert to CMPA (TBPRD_COUNTS assumed 333) */
        if(duty < 0.0f) duty = 0.0f;
        if(duty > 0.90f) duty = 0.90f;

        uint32_t cmp_tmp = (uint32_t)(duty * (float)TBPRD_COUNTS + 0.5f);
        if(cmp_tmp > TBPRD_COUNTS) cmp_tmp = TBPRD_COUNTS;

        EPWM_setCounterCompareValue(EPWM_BASE_EP, EPWM_COUNTER_COMPARE_A, (uint16_t)cmp_tmp);

        /* optional verify */
        ISR_last_cmp_verify = EPWM_getCounterCompareValue(EPWM_BASE_EP, EPWM_COUNTER_COMPARE_A);
    }

    /* Clear ADC interrupt and acknowledge PIE group */
    ADC_clearInterruptStatus(myADCA_BASE, ADC_INT_NUMBER1);
    Interrupt_clearACKGroup(INT_myADCA_1_INTERRUPT_ACK_GROUP);
}

/* End of File */
