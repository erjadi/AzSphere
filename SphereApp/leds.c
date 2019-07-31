#include <stdbool.h>
#include <applibs/gpio.h>
#include "leds.h"
#include "mt3620.h"
#include "mt3620_rdb.h"
#include <applibs/log.h>

static int ledsRed[4];
static int ledsGreen[4];
static int ledsBlue[4];

static int statusLedRed = -1;
static int statusLedGreen = -1;

/// <summary>
///     Update status leds on dev board.
/// </summary>
void SetStatusLed(bool status)
{
	GPIO_SetValue(statusLedRed, status ? GPIO_Value_High : GPIO_Value_Low);
	GPIO_SetValue(statusLedGreen, status ? GPIO_Value_Low : GPIO_Value_High);
}

/// <summary>
///     Update status leds on dev board.
/// </summary>
void UpdateLeds(int r,int g,int b)
{
	UpdateRedLed(r);
	UpdateGreenLed(g);
	UpdateBlueLed(b);
}

void InitLeds(void)
{
	statusLedRed = GPIO_OpenAsOutput(MT3620_RDB_STATUS_LED_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	statusLedGreen = GPIO_OpenAsOutput(MT3620_RDB_STATUS_LED_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);

	ledsRed[0] = GPIO_OpenAsOutput(MT3620_RDB_LED1_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsRed[1] = GPIO_OpenAsOutput(MT3620_RDB_LED2_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsRed[2] = GPIO_OpenAsOutput(MT3620_RDB_LED3_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsRed[3] = GPIO_OpenAsOutput(MT3620_RDB_LED4_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);

	ledsGreen[0] = GPIO_OpenAsOutput(MT3620_RDB_LED1_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsGreen[1] = GPIO_OpenAsOutput(MT3620_RDB_LED2_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsGreen[2] = GPIO_OpenAsOutput(MT3620_RDB_LED3_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsGreen[3] = GPIO_OpenAsOutput(MT3620_RDB_LED4_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);

	ledsBlue[0] = GPIO_OpenAsOutput(MT3620_RDB_LED1_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsBlue[1] = GPIO_OpenAsOutput(MT3620_RDB_LED2_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsBlue[2] = GPIO_OpenAsOutput(MT3620_RDB_LED3_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsBlue[3] = GPIO_OpenAsOutput(MT3620_RDB_LED4_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);

}

void UpdateRedLed(int index)
{
	for (int i = 0; i < 4; i++) {
		if (i != index)
			GPIO_SetValue(ledsRed[i], GPIO_Value_High);
		else {
			GPIO_SetValue(ledsRed[i], GPIO_Value_Low);
		}
	}
}

void UpdateGreenLed(int index)
{
	for (int i = 0; i < 4; i++) {
		if (i != index)
			GPIO_SetValue(ledsGreen[i], GPIO_Value_High);
		else {
			GPIO_SetValue(ledsGreen[i], GPIO_Value_Low);
		}
	}
}

void UpdateBlueLed(int index)
{
	for (int i = 0; i < 4; i++) {
		if (i != index)
			GPIO_SetValue(ledsBlue[i], GPIO_Value_High);
		else {
			GPIO_SetValue(ledsBlue[i], GPIO_Value_Low);
		}
	}
}

