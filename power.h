#ifndef POWER_H
#define POWER_H

const uint8_t PIN_POWER_SWITCH = 2; // Must be 2 or 3 for ISR to work

extern bool FlagShutdown;

void InitPowerSwitch();
void CheckPowerSwitch();
void PowerOff();

#endif // POWER_H
