#pragma once

static int indexBlue = 0;
static int indexRed = 0;

static int ledsBlue[4];
static int ledsRed[4];

static int statusLedRed = -1;
static int statusLedGreen = -1;

void SetStatusLed(bool status);
void UpdateLeds(void);
void InitLeds(void);

