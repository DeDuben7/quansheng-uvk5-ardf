/* V3 (PY32F071) audio path enable/disable.
 *
 * V1 reference: Inc/peripherals/audio_path.h defines AUDIO_AudioPathOn/Off
 * as static inline functions toggling GPIOC pin "GPIOC_PIN_AUDIO_PATH" (DP32
 * GPIOC4) directly through GPIOC->DATA. This header keeps V1's function
 * NAMES and signatures exactly (bk4819.c calls them un-namespaced from
 * BK4819_PlaySingleTone()), so the two drop-in unchanged, but re-targets the
 * pin to V3 hardware and moves the body to a .c file (Src/peripherals/
 * audio_path.c) instead of inlining it here.
 *
 * V3 pin: GPIOA pin 8 — matches armel's App/driver/gpio.h
 * `GPIO_PIN_AUDIO_PATH = GPIO_MAKE_PIN(GPIOA, LL_GPIO_PIN_8)` and its
 * GPIO_EnableAudioPath()/GPIO_DisableAudioPath() helpers. High = audio path
 * enabled (speaker/mic routed), matching armel's SetOutputPin==enable
 * polarity.
 *
 * Only the two functions bk4819.c actually calls are ported here. V1's
 * audio_path.h also carries BEEP_Type_t/VOICE_ID_t and gEeprom-backed voice
 * prompt declarations that belong to radio.c/audio.c, which are out of
 * scope for this task (Phase 2 Task 1 only builds bk4819.c +
 * audio_path.c) — those get added when that file is ported.
 */
#ifndef PERIPHERALS_AUDIO_PATH_H
#define PERIPHERALS_AUDIO_PATH_H

void AUDIO_AudioPathOn(void);
void AUDIO_AudioPathOff(void);

#endif /* PERIPHERALS_AUDIO_PATH_H */
