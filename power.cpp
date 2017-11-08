#include <stdint.h>
#include <Arduino.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#include "power.h"

bool FlagShutdown;

void WakeupISR() {
}

void InitPowerSwitch() {
  FlagShutdown = false;
  pinMode(PIN_POWER_SWITCH, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_POWER_SWITCH), WakeupISR, LOW);
}

void CheckPowerSwitch() {
  uint8_t i = 0;
  if (FlagShutdown) {
    return;
  }
  while (digitalRead(PIN_POWER_SWITCH) == 0) {
    ++i;
    if (i >= 100) {
      // Power switch has been held for at least 1 second, shut down
      FlagShutdown = true;
      return;
    }
    delay(10);
  }
  // Power switch was released, don't shut down
}

void PowerOff() {
  uint8_t i = 0;
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  Serial.println("powering off");
  Serial.flush();
  // Wait for power switch to be released for 100ms before sleeping
  while (i < 10) {
    if (digitalRead(PIN_POWER_SWITCH) == 0) {
      i = 0;
    }
    delay(10);
    ++i;
  }
  while (1) {
    attachInterrupt(digitalPinToInterrupt(PIN_POWER_SWITCH), WakeupISR, LOW);
    sleep_mode();
    // wake up here
    detachInterrupt(digitalPinToInterrupt(PIN_POWER_SWITCH));
    // wait for power switch to be held for 0.5s, or else go back to sleep
    i = 0;
    while (digitalRead(PIN_POWER_SWITCH) == 0 && i < 50) {
      delay(10);
      ++i;
    }
    if (i >= 50) {
      break;
    }
    // switch released too soon. ignore and go back to sleep.
  }
  // wake up and reboot
  sleep_disable();
  // Wait for power switch to be released for 100ms before rebooting
  // TODO combine with above?
  while (i < 10) {
    if (digitalRead(PIN_POWER_SWITCH) == 0) {
      i = 0;
    }
    delay(10);
    ++i;
  }
  Serial.println("reboot");
  Serial.flush();
  wdt_enable(WDTO_15MS);
  while(1);
}
