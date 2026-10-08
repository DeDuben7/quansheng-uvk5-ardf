/* Host emulator: speaker amplifier HAL. Records the state so test scenes
 * can check the squelch audio gate (expect audio on|off). */
#ifndef PERIPHERALS_AUDIO_PATH_H
#define PERIPHERALS_AUDIO_PATH_H

void AUDIO_AudioPathOn(void);
void AUDIO_AudioPathOff(void);

#endif /* PERIPHERALS_AUDIO_PATH_H */
