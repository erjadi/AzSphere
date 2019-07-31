// Standard & Board Imports
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <applibs/log.h>
#include <applibs/gpio.h>
#include "mt3620_rdb.h"
#include "epoll_timerfd_utilities.h"
#include "leds.h"

// Azure IoT SDK
#include "iot.h"
//#include <azureiot/iothub_client_core_common.h>
//#include <azureiot/iothub_device_client_ll.h>
//#include <azureiot/iothub_client_options.h>
//#include <azureiot/iothubtransportmqtt.h>
//#include <azureiot/iothub.h>
//#include <azureiot/azure_sphere_provisioning.h>
//#include "parson.h" // used to parse Device Twin messages.

static int indexBlue = 0;
static int indexRed = 0;
static char scopeId[SCOPEID_LENGTH];
static volatile sig_atomic_t terminationRequired = false;

// Initialization/Cleanup
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

	Log_Debug("AzSphere Application starting.\n");

	InitLeds();
	InitPeripheralsAndHandlers();

	if (argc == 2) {
		Log_Debug("Setting Azure Scope ID %s\n", argv[1]);
		strncpy(scopeId, argv[1], SCOPEID_LENGTH);
	}
	else {
		Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs\n");
		return -1;
	}

	UpdateLeds(-1,-1,0); // Clear all status leds

	int cycle = 0;

	while (!terminationRequired) {

		cycle++;
		cycle %= 400;

		if (cycle == 0) {
			indexRed = (indexRed + 1) % 4;
			UpdateRedLed(indexRed);
		}

		if (WaitForEventAndCallHandler(epollFd) != 0) {
			terminationRequired = true;
		}

		IoTHubDeviceClient_LL_DoWork(getIoTHubClientHandle());
	}
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
		//SendSimulatedTemperature();
		IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
	}
}

/// <summary>
///     Set up SIGTERM termination handler, initialize peripherals, and set up event handlers.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
static int InitPeripheralsAndHandlers(void)
{
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

