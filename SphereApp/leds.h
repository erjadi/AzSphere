#pragma once
#include <applibs/gpio.h>

void SetStatusLed(bool status);
void UpdateLeds(int r, int g, int b);
void InitLeds(void);

void UpdateRedLed(int index);
void UpdateGreenLed(int index);
void UpdateBlueLed(int index);

void SaveLedState(GPIO_Value backup[12]);
void RestoreLedState(GPIO_Value backup[12]);
void Blink(void);
void AllLedsOff(void);


