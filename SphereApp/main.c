// Standard & Board Imports
#define I2C_STRUCTS_VERSION 1
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <applibs/rtc.h>
#include <applibs/log.h>
#include <applibs/gpio.h>
#include "mt3620_rdb.h"
#include "epoll_timerfd_utilities.h"
#include "leds.h"
#include <applibs/i2c.h>
#include "lcd.h"


// Azure IoT SDK
#include "iot.h"

// GPIO59 for Reset
static int resetFd = -1;
static int resetCounter = -1;

// LCD Stuff
static int i2cFd = -1;
static bool lcd_enabled = false;

// Globals
static int indexBlue = 0;
static int indexRed = 0;
static char scopeId[SCOPEID_LENGTH];
static volatile sig_atomic_t terminationRequired = false;

// Initialization/Cleanup
static bool firstConnected = true;
static int InitPeripheralsAndHandlers(void);
//static void ClosePeripheralsAndHandlers(void);
static void AzureTimerEventHandler(EventData* eventData);
static EventData azureEventData = { .eventHandler = &AzureTimerEventHandler };

static int azureTimerFd = -1;
static int epollFd = -1;

static int LeftButtonGpioFd = -1;
static int RightButtonGpioFd = -1;
static int buttonPollTimerFd = -1;

// Button state variables
static GPIO_Value_Type leftButtonState = GPIO_Value_High;
static GPIO_Value_Type rightButtonState = GPIO_Value_High;

// I2C Stuff
void LiquidCrystal_I2C_expanderWrite(uint8_t _data);
void LiquidCrystal_I2C_pulseEnable(uint8_t _data);
void LiquidCrystal_I2C_write4bits(uint8_t value);
void LiquidCrystal_I2C_send(uint8_t value, uint8_t mode);


static void hardreset(void) {
	int result = GPIO_SetValue(resetFd, GPIO_Value_Low);
}
/// <summary>
///     Signal handler for termination requests. This handler must be async-signal-safe.
/// </summary>
static void TerminationHandler(int signalNumber)
{
	// Don't use Log_Debug here, as it is not guaranteed to be async-signal-safe.
	terminationRequired = true;
}

/// <summary>
///     Handle button timer event: if the button is pressed, change the LED blink rate.
/// </summary>
static void ButtonTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(buttonPollTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	// Check for a button press (Left button)
	GPIO_Value_Type newButtonState;
	int result = GPIO_GetValue(LeftButtonGpioFd, &newButtonState);
	if (result != 0) {
		Log_Debug("ERROR: Could not read button GPIO: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
		return;
	}

	if (newButtonState != leftButtonState) {
		if (newButtonState == GPIO_Value_Low) {
			indexBlue--;
			if (indexBlue < 0) indexBlue += 4;
			UpdateBlueLed(indexBlue);
		}
		leftButtonState = newButtonState;
	}

	// Check for a button press (Right button)
	result = GPIO_GetValue(RightButtonGpioFd, &newButtonState);
	if (result != 0) {
		Log_Debug("ERROR: Could not read button GPIO: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
		return;
	}

	if (newButtonState != rightButtonState) {
		if (newButtonState == GPIO_Value_Low) {

			indexBlue++;
			indexBlue %= 4;
			UpdateBlueLed(indexBlue);
		}
		rightButtonState = newButtonState;
	}
}

static EventData buttonEventData = { .eventHandler = &ButtonTimerEventHandler };

int main(int argc, char* argv[])
{
    // This minimal Azure Sphere app repeatedly toggles GPIO 9, which is the green channel of RGB
    // LED 1 on the MT3620 RDB.
    // Use this app to test that device and SDK installation succeeded that you can build,
    // deploy, and debug an app with Visual Studio, and that you can deploy an app over the air,
    // per the instructions here: https://docs.microsoft.com/azure-sphere/quickstarts/qs-overview
    //
    // It is NOT recommended to use this as a starting point for developing apps; instead use
    // the extensible samples here: https://github.com/Azure/azure-sphere-samples

	clock_systohc();

	lcd_enabled = lcd_init(MT3620_RDB_HEADER4_ISU2_I2C);

	if (lcd_enabled) {
		lcd_command(LCD_DISPLAYON | LCD_CURSORON | LCD_BLINKINGON);
		lcd_command(LCD_CLEAR);
		lcd_light(true);
		lcd_gotolc(1, 1);
		lcd_print("SphereApp v0.1.2");
	}

	// I2C Scratch

	Log_Debug("AzSphere Application starting.\n");

	InitLeds();
	InitPeripheralsAndHandlers();

	if (argc == 2) {
		Log_Debug("Setting Azure Scope ID %s\n", argv[1]);
		strncpy(scopeId, argv[1], SCOPEID_LENGTH);
		if (lcd_enabled) {
			lcd_gotolc(3, 1);
			lcd_print("ScopeId");
			lcd_gotolc(4, 1);
			lcd_print(scopeId);
			lcd_gotolc(2, 1);
			lcd_print("Connecting...");
		}
	}
	else {
		Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs\n");
		return -1;
	}

	UpdateLeds(-1,-1,0); // Clear all status leds

	int cycle = 0;

	while (!terminationRequired) {

		if (resetCounter > 0) {
			resetCounter--;
		}

		if (resetCounter == 0) hardreset();

		cycle++;
		cycle %= 400;

		if (cycle == 0) {
			indexRed = (indexRed + 1) % 4;
			UpdateRedLed(indexRed);
		}

		if (WaitForEventAndCallHandler(epollFd) != 0) {
			terminationRequired = true;
		}

		//if ((cycle % 25) == 0) IoTHubDeviceClient_LL_DoWork(getIoTHubClientHandle());
	}

	AllLedsOff();
}


/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
static void AzureTimerEventHandler(EventData* eventData)
{
	SetStatusLed(isIoTHubAuthenticated());

	if (ConsumeTimerFdEvent(azureTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	//bool isNetworkReady = false;
	if (!isIoTHubAuthenticated()) {
		SetupAzureClient(azureTimerFd, scopeId);
	}

	if (isIoTHubAuthenticated()) {
		IoTHubDeviceClient_LL_DoWork(getIoTHubClientHandle());

		if (firstConnected) {
			firstConnected = false;
			lcd_command(LCD_CLEAR);
		}
		else {
			lcd_gotolc(1, 1);
			time_t rawtime;
			struct tm* timeinfo;
			time(&rawtime);
			timeinfo = localtime(&rawtime);
			char timebuf[18];
			sprintf(timebuf, "Connected %02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
			lcd_print(timebuf);
		}
	}
}

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int InitPeripheralsAndHandlers(void)
{
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = TerminationHandler;
	sigaction(SIGTERM, &action, NULL);

	resetFd = GPIO_OpenAsOutput(MT3620_RDB_HEADER1_PIN3_GPIO, GPIO_OutputMode_OpenDrain, GPIO_Value_High);

	epollFd = CreateEpollFd();
	if (epollFd < 0) {
		return -1;
	}

	SetStatusLed(false);

	azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
	struct timespec azureTelemetryPeriod = { azureIoTPollPeriodSeconds, 0 };
	azureTimerFd =
		CreateTimerFdAndAddToEpoll(epollFd, &azureTelemetryPeriod, &azureEventData, EPOLLIN);

	// Open button GPIO as input, and set up a timer to poll it
	LeftButtonGpioFd = GPIO_OpenAsInput(MT3620_RDB_BUTTON_A);
	if (LeftButtonGpioFd < 0) {
		Log_Debug("ERROR: Could not open button GPIO: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	RightButtonGpioFd = GPIO_OpenAsInput(MT3620_RDB_BUTTON_B);
	if (RightButtonGpioFd < 0) {
		Log_Debug("ERROR: Could not open button GPIO: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	struct timespec buttonPressCheckPeriod = { 0, 1000000 };
	buttonPollTimerFd =
		CreateTimerFdAndAddToEpoll(epollFd, &buttonPressCheckPeriod, &buttonEventData, EPOLLIN);
	if (buttonPollTimerFd < 0) {
		return -1;
	}

	return 0;
}

void processMessage(unsigned char* message, int length) {
	unsigned char fixedstring[length];
	strcpy(fixedstring, message);
	fixedstring[length] = 0;
	if (strcmp(fixedstring, "reset_LCD") == 0)
	{
		lcd_enabled = lcd_init(MT3620_RDB_HEADER4_ISU2_I2C);

		if (lcd_enabled) {
			lcd_command(LCD_DISPLAYON | LCD_CURSORON | LCD_BLINKINGON);
			lcd_command(LCD_CLEAR);
			lcd_light(true);
			lcd_gotolc(1, 1);
		}
	}
	else if (strcmp(fixedstring, "terminate") == 0) {
		terminationRequired = true;
	}
	else if (strcmp(fixedstring, "reboot") == 0) {
		TerminationHandler(0);
	}
	else if (strcmp(fixedstring, "hardreset") == 0) {
		resetCounter = 10000;
	}
	else if (lcd_enabled) {
		lcd_gotolc(3, 1);
		lcd_print("                    ");
		lcd_gotolc(3, 1);
		lcd_printlen(message, length);
	}
}

