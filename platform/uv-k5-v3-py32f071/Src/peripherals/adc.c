/* UV-K5 V3 (PY32F071) battery-voltage ADC front end.
 *
 * PY32F071's ADC1 block is register-compatible with the classic
 * STM32F1-style SAR ADC (SR/CR1/CR2/SMPRx/SQRx/DR — not the newer
 * STM32F0 ISR/CFGR1/CHSELR layout), confirmed against
 * bsp/py32f071/Include/py32f071xB.h's ADC_TypeDef (SR, CR1, CR2, SMPR1-3,
 * JOFR1-4, HTR, LTR, SQR1-3, JSQR, JDR1-4, DR, CCSR).
 *
 * This file transcribes armel-k5v3-src/App/board.c's BOARD_ADC_Init()
 * (lines 138-164) and BOARD_ADC_GetBatteryInfo() (lines 166-175), which
 * call the PY32 LL driver (Drivers/PY32F071_HAL_Driver/Inc/py32f071_ll_adc.h
 * and py32f071_ll_rcc.h) — every LL call below is transcribed against that
 * LL header's __STATIC_INLINE implementation to get the exact register/bit
 * operation (cited per line). No behaviour is invented.
 *
 * armel board.c:138-142 (GPIO + ADC1 clocks):
 *   LL_IOP_GRP1_EnableClock(GPIOB)                  -> RCC->IOPENR |= GPIOBEN
 *   LL_GPIO_SetPinMode(GPIOB, PIN_0|PIN_1, ANALOG)   -> GPIOB->MODER analog
 *   LL_APB1_GRP2_EnableClock(ADC1)                   -> RCC->APBENR2 |= ADCEN
 *   LL_RCC_SetADCClockSource(PCLK_DIV4)              -> RCC->CR.ADC_DIV = 01
 *     (py32f071_ll_rcc.h:1545-1548: MODIFY_REG(RCC->CR, RCC_CR_ADC_DIV,
 *      Source); LL_RCC_ADC_CLKSOURCE_PCLK_DIV4 = RCC_CR_ADC_DIV_0)
 *
 * armel board.c:146-157 (ADC1 mode + channel 8 config), each LL call
 * resolved against py32f071_ll_adc.h:
 *   SetCommonPathInternalCh(NONE)   -> ADC1_COMMON->CR2 &= ~TSVREFE (none)
 *   SetResolution(12B)              -> CR1.RESSEL = 00   (0x0000_0000U)
 *   SetDataAlignment(RIGHT)         -> CR2.ALIGN  = 0     (0x0000_0000U)
 *   SetSequencersScanMode(DISABLE)  -> CR1.SCAN   = 0
 *   SetTriggerSource(SOFTWARE)      -> CR2.EXTSEL = 0b111 (all 3 bits set;
 *                                        ll_adc.h:548 EXTSEL_2|1|0 — this is
 *                                        only a mux *selection* value, the
 *                                        actual SW-start pulse is EXTTRIG+
 *                                        SWSTART, set in GetBatteryInfo)
 *   SetContinuousMode(SINGLE)       -> CR2.CONT   = 0
 *   SetDMATransfer(NONE)            -> CR2.DMA    = 0
 *   SetSequencerLength(SCAN_DISABLE)-> SQR1.L      = 0000 (1 conversion)
 *   SetSequencerDiscont(DISABLE) x2 -> CR1.DISCEN/DISCNUM = 0 (board.c calls
 *                                        this twice back-to-back; redundant,
 *                                        both write the same 0 -- collapsed
 *                                        to a single write here)
 *   SetSequencerRanks(RANK_1, CH8)  -> SQR3.SQ1   = 8  (channel number)
 *                                        (ll_adc.h:2065 rank1 -> SQR3, and
 *                                        ADC_SQR3_SQ1 is bits[4:0] at offset0)
 *   SetChannelSamplingTime(CH8,
 *     41.5 cycles)                  -> SMPR3.SMP8 = 0b101
 *                                        (ll_adc.h:1004-1009 SMP8 is
 *                                        bits[26:24] of SMPR3;
 *                                        LL_ADC_SAMPLINGTIME_41CYCLES_5 =
 *                                        SMP0_2|SMP0_0 = 0b101, ll_adc.h:717)
 *
 * armel board.c:159-163 (self-calibration, then enable):
 *   LL_ADC_StartCalibration(ADC1)     -> SET_BIT(CR2, CAL)   (ll_adc.h:3295-3298)
 *   while(LL_ADC_IsCalibrationOnGoing)-> poll CR2.CAL until hw clears it
 *                                        (ll_adc.h:3324-3331)
 *   LL_ADC_Enable(ADC1)                -> SET_BIT(CR2, ADON) (ll_adc.h:3241-3248)
 *   NOTE: board.c runs calibration *before* ADON is set -- transcribed
 *   verbatim even though that ordering differs from some other STM32-family
 *   app notes; see brief: "do NOT invent the sequence".
 *
 * armel board.c:166-175 (BOARD_ADC_GetBatteryInfo):
 *   LL_ADC_REG_StartConversionSWStart -> SET_BIT(CR2, SWSTART | EXTTRIG)
 *                                        (ll_adc.h:3357-3360)
 *   while(!LL_ADC_IsActiveFlag_EOS)   -> poll SR.EOC (LL_ADC_FLAG_EOS is an
 *                                        alias for ADC_SR_EOC, ll_adc.h:403)
 *   LL_ADC_ClearFlag_JEOS             -> SR = ~JEOC (clears the *injected*
 *                                        EOC flag only; regular EOC self-
 *                                        clears when DR is read below --
 *                                        transcribed as-is from board.c:171,
 *                                        which never explicitly clears SR.EOC)
 *   LL_ADC_REG_ReadConversionData12   -> return DR & DATA (ll_adc.h:3423-3426)
 *   *pCurrent = 0                     -> V3 has no current-sense channel;
 *                                        armel board.c:174 always returns 0.
 *
 * PB0 is also set analog above (armel board.c:141 sets PIN_0|PIN_1
 * together) -- PB0 would be ADC1 channel 9, the current-sense input on
 * other PY32 boards, but board.c's GetBatteryInfo never reads it (current
 * is hardcoded 0), and this file likewise never converts on channel 9. Kept
 * analog only to match the reference GPIO config faithfully; unused by
 * ADC_GetBatteryRaw().
 *
 * A3.6: the battery divider/scaling network feeding PB1 is carried over
 * unverified from the V3 reference -- not re-tuned here.
 */
#include "peripherals/adc.h"
#include "py32f0xx.h"

#define ADC_BATTERY_CHANNEL   8u   /* PB1 -- armel board.c:156-157 */

void ADC_Init(void)
{
    /* armel board.c:140-141 -- GPIOB clock + PB0/PB1 analog mode. */
    RCC->IOPENR |= RCC_IOPENR_GPIOBEN;
    (void)RCC->IOPENR; /* read-back: ensure the enable took effect */

    GPIOB->MODER |= (3u << (0u * 2u)) | (3u << (1u * 2u)); /* PB0, PB1 = analog (11) */

    /* armel board.c:143-144 -- ADC1 peripheral clock + PCLK/4 ADC clock. */
    RCC->APBENR2 |= RCC_APBENR2_ADCEN;
    (void)RCC->APBENR2;
    RCC->CR = (RCC->CR & ~RCC_CR_ADC_DIV_Msk) | RCC_CR_ADC_DIV_0; /* PCLK/4 */

    /* armel board.c:146 -- no internal measurement path (Vrefint/temp). */
    ADC1_COMMON->CR2 &= ~ADC_CR2_TSVREFE_Msk;

    /* armel board.c:147-148 -- 12-bit resolution, right-aligned data. */
    ADC1->CR1 &= ~ADC_CR1_RESSEL_Msk;   /* RESSEL = 00 -> 12-bit */
    ADC1->CR2 &= ~ADC_CR2_ALIGN_Msk;    /* ALIGN  = 0  -> right-aligned */

    /* armel board.c:149 -- sequencer scan mode off (single-channel). */
    ADC1->CR1 &= ~ADC_CR1_SCAN_Msk;

    /* armel board.c:150 -- trigger-source mux = software (EXTSEL = 0b111). */
    ADC1->CR2 = (ADC1->CR2 & ~ADC_CR2_EXTSEL_Msk) | ADC_CR2_EXTSEL_Msk;

    /* armel board.c:151-152 -- single conversion per trigger, no DMA. */
    ADC1->CR2 &= ~(ADC_CR2_CONT_Msk | ADC_CR2_DMA_Msk);

    /* armel board.c:153 -- regular sequencer length = 1 conversion (L=0000). */
    ADC1->SQR1 &= ~ADC_SQR1_L_Msk;

    /* armel board.c:154-155 -- discontinuous mode off (called twice in the
     * reference; one write here is equivalent). */
    ADC1->CR1 &= ~(ADC_CR1_DISCEN_Msk | ADC_CR1_DISCNUM_Msk);

    /* armel board.c:156 -- regular sequence rank 1 = channel 8 (PB1). */
    ADC1->SQR3 = (ADC1->SQR3 & ~ADC_SQR3_SQ1_Msk)
               | (ADC_BATTERY_CHANNEL << ADC_SQR3_SQ1_Pos);

    /* armel board.c:157 -- channel 8 sample time = 41.5 ADC clock cycles
     * (SMPR3.SMP8, bits[26:24] = 0b101). */
    ADC1->SMPR3 = (ADC1->SMPR3 & ~ADC_SMPR3_SMP8_Msk)
                | ((ADC_SMPR3_SMP8_2 | ADC_SMPR3_SMP8_0));

    /* armel board.c:159-161 -- self-calibration; hardware clears CR2.CAL
     * when done. Run before enabling the ADC, matching the reference. */
    ADC1->CR2 |= ADC_CR2_CAL_Msk;
    while (ADC1->CR2 & ADC_CR2_CAL_Msk)
    {
    }

    /* armel board.c:163 -- power the ADC on. */
    ADC1->CR2 |= ADC_CR2_ADON_Msk;
}

uint16_t ADC_GetBatteryRaw(void)
{
    /* armel board.c:168 -- software-start a regular conversion. */
    ADC1->CR2 |= (ADC_CR2_SWSTART_Msk | ADC_CR2_EXTTRIG_Msk);

    /* armel board.c:169-170 -- wait for end-of-(regular-)sequence. */
    while (!(ADC1->SR & ADC_SR_EOC_Msk))
    {
    }

    /* armel board.c:171 -- clear the injected-EOC flag (as the reference
     * does; regular EOC self-clears on the DR read below). */
    ADC1->SR = ~ADC_SR_JEOC_Msk;

    /* armel board.c:173 -- 12-bit right-aligned battery voltage reading. */
    return (uint16_t)(ADC1->DR & 0x0FFFu);
}
