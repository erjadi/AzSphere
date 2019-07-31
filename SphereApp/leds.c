#include <stdbool.h>
#include <applibs/gpio.h>
#include "leds.h"
#include "mt3620.h"
#include "mt3620_rdb.h"
#include <applibs/log.h>

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
void UpdateLeds(void)
{
	for (int i = 0; i < 4; i++) {
		if (i != indexBlue)
			GPIO_SetValue(ledsBlue[i], GPIO_Value_High);
		else {
			GPIO_SetValue(ledsBlue[i], GPIO_Value_Low);
			Log_Debug("Turned on LED %d\r\n", i);
		}
	}

	//SendMessage();
}

void InitLeds(void)
{
	statusLedRed = GPIO_OpenAsOutput(MT3620_RDB_STATUS_LED_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	statusLedGreen = GPIO_OpenAsOutput(MT3620_RDB_STATUS_LED_GREEN, GPIO_OutputMode_PushPull, GPIO_Value_High);

	ledsBlue[0] = GPIO_OpenAsOutput(MT3620_RDB_LED1_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsBlue[1] = GPIO_OpenAsOutput(MT3620_RDB_LED2_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsBlue[2] = GPIO_OpenAsOutput(MT3620_RDB_LED3_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsBlue[3] = GPIO_OpenAsOutput(MT3620_RDB_LED4_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);

	ledsRed[0] = GPIO_OpenAsOutput(MT3620_RDB_LED1_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsRed[1] = GPIO_OpenAsOutput(MT3620_RDB_LED2_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsRed[2] = GPIO_OpenAsOutput(MT3620_RDB_LED3_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
	ledsRed[3] = GPIO_OpenAsOutput(MT3620_RDB_LED4_RED, GPIO_OutputMode_PushPull, GPIO_Value_High);
}

