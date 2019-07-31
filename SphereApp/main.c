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

// Azure IoT SDK
#include <azureiot/iothub_client_core_common.h>
#include <azureiot/iothub_device_client_ll.h>
#include <azureiot/iothub_client_options.h>
#include <azureiot/iothubtransportmqtt.h>
#include <azureiot/iothub.h>
#include <azureiot/azure_sphere_provisioning.h>

#include "parson.h" // used to parse Device Twin messages.

#define SCOPEID_LENGTH 20

// Termination state
static volatile sig_atomic_t terminationRequired = false;
static char scopeId[SCOPEID_LENGTH]; // ScopeId for the Azure IoT Central application, set in
									 // app_manifest.json, CmdArgs

static IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;
static const int keepalivePeriodSeconds = 20;
static bool iothubAuthenticated = false;
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context);
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
	size_t payloadSize, void* userContextCallback);
static void TwinReportBoolState(const char* propertyName, bool propertyValue);
static void ReportStatusCallback(int result, void* context);
static const char* GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason);
static const char* getAzureSphereProvisioningResultString(
	AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult);
static void SendTelemetry(const unsigned char* key, const unsigned char* value);
static void SetupAzureClient(void);

// Function to generate simulated Temperature data/telemetry
static void SendSimulatedTemperature(void);

// Initialization/Cleanup
static int InitPeripheralsAndHandlers(void);
//static void ClosePeripheralsAndHandlers(void);

// Azure IoT poll periods
static const int AzureIoTDefaultPollPeriodSeconds = 5;
static const int AzureIoTMinReconnectPeriodSeconds = 60;
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60;

static void AzureTimerEventHandler(EventData* eventData);

static int azureTimerFd = -1;
static int epollFd = -1;
static EventData azureEventData = { .eventHandler = &AzureTimerEventHandler };

static int azureIoTPollPeriodSeconds = -1;

static const int SAMPLE_BUTTON_1 = 12;
static const int SAMPLE_BUTTON_2 = 13;
static int LeftButtonGpioFd = -1;
static int RightButtonGpioFd = -1;
static int buttonPollTimerFd = -1;
static int leds[4];
static int ledsindex = 0;

static int redleds[4];
static int redledsindex = 0;
static int score = 0;

// Button state variables
static GPIO_Value_Type leftButtonState = GPIO_Value_High;
static GPIO_Value_Type rightButtonState = GPIO_Value_High;

/// <summary>
///     Send Status to IoTHub
/// </summary>
static void SendMessage()
{
	char buffer[1];
	sprintf(buffer, "%i", ledsindex);
	SendTelemetry("blue", buffer);
}

/// <summary>
///     Update status leds on dev board.
/// </summary>
static void UpdateLeds(void)
{
	for (int i = 0; i < 4; i++) {
		if (i != ledsindex)
			GPIO_SetValue(leds[i], GPIO_Value_High);
		else {
			GPIO_SetValue(leds[i], GPIO_Value_Low);
			Log_Debug("Turned on LED %d\r\n", i);
		}
	}

	SendMessage();
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

	// If the button has just been pressed, change the LED blink interval
	// The button has GPIO_Value_Low when pressed and GPIO_Value_High when released
	if (newButtonState != leftButtonState) {
		if (newButtonState == GPIO_Value_Low) {
			//blinkIntervalIndex = (blinkIntervalIndex + 1) % numBlinkIntervals;
			//if (SetTimerFdToPeriod(blinkingLedTimerFd, &blinkIntervals[blinkIntervalIndex]) != 0) {
			//	terminationRequired = true;
			//}
			ledsindex--;
			if (ledsindex < 0) ledsindex += 4;
			UpdateLeds();
		}
		leftButtonState = newButtonState;
	}

	// Check for a button press
	result = GPIO_GetValue(RightButtonGpioFd, &newButtonState);
	if (result != 0) {
		Log_Debug("ERROR: Could not read button GPIO: %s (%d).\n", strerror(errno), errno);
		terminationRequired = true;
		return;
	}

	// If the button has just been pressed, change the LED blink interval
	// The button has GPIO_Value_Low when pressed and GPIO_Value_High when released
	if (newButtonState != rightButtonState) {
		if (newButtonState == GPIO_Value_Low) {
			//blinkIntervalIndex = (blinkIntervalIndex + 1) % numBlinkIntervals;
			//if (SetTimerFdToPeriod(blinkingLedTimerFd, &blinkIntervals[blinkIntervalIndex]) != 0) {
			//	terminationRequired = true;
			//}
			ledsindex++;
			ledsindex %= 4;
			UpdateLeds();
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

	leds[0] = GPIO_OpenAsOutput(10, GPIO_OutputMode_PushPull, GPIO_Value_High);
	leds[1] = GPIO_OpenAsOutput(17, GPIO_OutputMode_PushPull, GPIO_Value_High);
	leds[2] = GPIO_OpenAsOutput(20, GPIO_OutputMode_PushPull, GPIO_Value_High);
	leds[3] = GPIO_OpenAsOutput(23, GPIO_OutputMode_PushPull, GPIO_Value_High);

	redleds[0] = GPIO_OpenAsOutput(8, GPIO_OutputMode_PushPull, GPIO_Value_High);
	redleds[1] = GPIO_OpenAsOutput(15, GPIO_OutputMode_PushPull, GPIO_Value_High);
	redleds[2] = GPIO_OpenAsOutput(18, GPIO_OutputMode_PushPull, GPIO_Value_High);
	redleds[3] = GPIO_OpenAsOutput(21, GPIO_OutputMode_PushPull, GPIO_Value_High);

	Log_Debug("IoT Hub/Central Application starting.\n");

	InitPeripheralsAndHandlers();

	if (argc == 2) {
		Log_Debug("Setting Azure Scope ID %s\n", argv[1]);
		strncpy(scopeId, argv[1], SCOPEID_LENGTH);
	}
	else {
		Log_Debug("ScopeId needs to be set in the app_manifest CmdArgs\n");
		return -1;
	}

    //int fd = GPIO_OpenAsOutput(MT3620_RDB_LED1_BLUE, GPIO_OutputMode_PushPull, GPIO_Value_High);
    //if (fd < 0) {
    //    Log_Debug(
    //        "Error opening GPIO: %s (%d). Check that app_manifest.json includes the GPIO used.\n",
    //        strerror(errno), errno);
    //    return -1;
    //}

	//if (InitPeripheralsAndHandlers() != 0) {
	//	Log_Debug("Could not initialize timers");
	//}

	//SetupAzureClient();

	UpdateLeds();

	while (!terminationRequired) {
		redledsindex++;
		redledsindex %= 400;
		//if ((redledsindex % 100) == 0) {
		//	IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
		//}
		for (int i = 0; i < 4; i++) {
			if (i != redledsindex / 100)
				GPIO_SetValue(redleds[i], GPIO_Value_High);
			else {
				GPIO_SetValue(redleds[i], GPIO_Value_Low);
			}
		}

		if (WaitForEventAndCallHandler(epollFd) != 0) {
			terminationRequired = true;
		}
	}
}

/// <summary>
///     Sets the IoT Hub authentication state for the app
///     The SAS Token expires which will set the authentication state
/// </summary>
static void HubConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result,
	IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
	void* userContextCallback)
{
	iothubAuthenticated = (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED);
	Log_Debug("IoT Hub Authenticated: %s\n", GetReasonString(reason));
}

/// <summary>
/// Azure timer event:  Check connection status and send telemetry
/// </summary>
static void AzureTimerEventHandler(EventData* eventData)
{
	if (ConsumeTimerFdEvent(azureTimerFd) != 0) {
		//terminationRequired = true;
		return;
	}

	//bool isNetworkReady = false;
	if (!iothubAuthenticated) {
		SetupAzureClient();
	}

	if (iothubAuthenticated) {
		//SendSimulatedTemperature();
		IoTHubDeviceClient_LL_DoWork(iothubClientHandle);
	}
}

/// <summary>
///     Sets up the Azure IoT Hub connection (creates the iothubClientHandle)
///     When the SAS Token for a device expires the connection needs to be recreated
///     which is why this is not simply a one time call.
/// </summary>
static void SetupAzureClient(void)
{
	if (iothubClientHandle != NULL)
		IoTHubDeviceClient_LL_Destroy(iothubClientHandle);

	AZURE_SPHERE_PROV_RETURN_VALUE provResult =
		IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning(scopeId, 10000,
			&iothubClientHandle);
	Log_Debug("IoTHubDeviceClient_LL_CreateWithAzureSphereDeviceAuthProvisioning returned '%s'.\n",
		getAzureSphereProvisioningResultString(provResult));

	if (provResult.result != AZURE_SPHERE_PROV_RESULT_OK) {

		// If we fail to connect, reduce the polling frequency, starting at
		// AzureIoTMinReconnectPeriodSeconds and with a backoff up to
		// AzureIoTMaxReconnectPeriodSeconds
		if (azureIoTPollPeriodSeconds == AzureIoTDefaultPollPeriodSeconds) {
			azureIoTPollPeriodSeconds = AzureIoTMinReconnectPeriodSeconds;
		}
		else {
			azureIoTPollPeriodSeconds *= 2;
			if (azureIoTPollPeriodSeconds > AzureIoTMaxReconnectPeriodSeconds) {
				azureIoTPollPeriodSeconds = AzureIoTMaxReconnectPeriodSeconds;
			}
		}

		struct timespec azureTelemetryPeriod = { azureIoTPollPeriodSeconds, 0 };
		SetTimerFdToPeriod(azureTimerFd, &azureTelemetryPeriod);

		Log_Debug("ERROR: failure to create IoTHub Handle - will retry in %i seconds.\n",
			azureIoTPollPeriodSeconds);
		return;
	}

	// Successfully connected, so make sure the polling frequency is back to the default
	//azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
	//struct timespec azureTelemetryPeriod = { azureIoTPollPeriodSeconds, 0 };
	//SetTimerFdToPeriod(azureTimerFd, &azureTelemetryPeriod);

	iothubAuthenticated = true;

	if (IoTHubDeviceClient_LL_SetOption(iothubClientHandle, OPTION_KEEP_ALIVE,
		&keepalivePeriodSeconds) != IOTHUB_CLIENT_OK) {
		Log_Debug("ERROR: failure setting option \"%s\"\n", OPTION_KEEP_ALIVE);
		return;
	}

	IoTHubDeviceClient_LL_SetDeviceTwinCallback(iothubClientHandle, TwinCallback, NULL);
	int i = IoTHubDeviceClient_LL_SetConnectionStatusCallback(iothubClientHandle,
		HubConnectionStatusCallback, NULL);
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

	azureIoTPollPeriodSeconds = AzureIoTDefaultPollPeriodSeconds;
	struct timespec azureTelemetryPeriod = { azureIoTPollPeriodSeconds, 0 };
	azureTimerFd =
		CreateTimerFdAndAddToEpoll(epollFd, &azureTelemetryPeriod, &azureEventData, EPOLLIN);

	// Open button GPIO as input, and set up a timer to poll it
	LeftButtonGpioFd = GPIO_OpenAsInput(SAMPLE_BUTTON_1);
	if (LeftButtonGpioFd < 0) {
		Log_Debug("ERROR: Could not open button GPIO: %s (%d).\n", strerror(errno), errno);
		return -1;
	}

	RightButtonGpioFd = GPIO_OpenAsInput(SAMPLE_BUTTON_2);
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

/// <summary>
///     Callback invoked when a Device Twin update is received from IoT Hub.
///     Updates local state for 'showEvents' (bool).
/// </summary>
/// <param name="payload">contains the Device Twin JSON document (desired and reported)</param>
/// <param name="payloadSize">size of the Device Twin JSON document</param>
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload,
	size_t payloadSize, void* userContextCallback)
{
	size_t nullTerminatedJsonSize = payloadSize + 1;
	char* nullTerminatedJsonString = (char*)malloc(nullTerminatedJsonSize);
	if (nullTerminatedJsonString == NULL) {
		Log_Debug("ERROR: Could not allocate buffer for twin update payload.\n");
		abort();
	}

	// Copy the provided buffer to a null terminated buffer.
	memcpy(nullTerminatedJsonString, payload, payloadSize);
	// Add the null terminator at the end.
	nullTerminatedJsonString[nullTerminatedJsonSize - 1] = 0;

	JSON_Value* rootProperties = NULL;
	rootProperties = json_parse_string(nullTerminatedJsonString);
	if (rootProperties == NULL) {
		Log_Debug("WARNING: Cannot parse the string as JSON content.\n");
		goto cleanup;
	}

	//JSON_Object* rootObject = json_value_get_object(rootProperties);
	//JSON_Object* desiredProperties = json_object_dotget_object(rootObject, "desired");
	//if (desiredProperties == NULL) {
	//	desiredProperties = rootObject;
	//}

	//// Handle the Device Twin Desired Properties here.
	//JSON_Object* LEDState = json_object_dotget_object(desiredProperties, "StatusLED");
	//if (LEDState != NULL) {
	//	statusLedOn = (bool)json_object_get_boolean(LEDState, "value");
	//	GPIO_SetValue(deviceTwinStatusLedGpioFd,
	//		(statusLedOn == true ? GPIO_Value_Low : GPIO_Value_High));
	//	TwinReportBoolState("StatusLED", statusLedOn);
	//}

cleanup:
	// Release the allocated memory.
	json_value_free(rootProperties);
	free(nullTerminatedJsonString);
}

/// <summary>
///     Converts the IoT Hub connection status reason to a string.
/// </summary>
static const char* GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason)
{
	static char* reasonString = "unknown reason";
	switch (reason) {
	case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN:
		reasonString = "IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN";
		break;
	case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED:
		reasonString = "IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED";
		break;
	case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL:
		reasonString = "IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL";
		break;
	case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED:
		reasonString = "IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED";
		break;
	case IOTHUB_CLIENT_CONNECTION_NO_NETWORK:
		reasonString = "IOTHUB_CLIENT_CONNECTION_NO_NETWORK";
		break;
	case IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR:
		reasonString = "IOTHUB_CLIENT_CONNECTION_COMMUNICATION_ERROR";
		break;
	case IOTHUB_CLIENT_CONNECTION_OK:
		reasonString = "IOTHUB_CLIENT_CONNECTION_OK";
		break;
	}
	return reasonString;
}

/// <summary>
///     Converts AZURE_SPHERE_PROV_RETURN_VALUE to a string.
/// </summary>
static const char* getAzureSphereProvisioningResultString(
	AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult)
{
	switch (provisioningResult.result) {
	case AZURE_SPHERE_PROV_RESULT_OK:
		return "AZURE_SPHERE_PROV_RESULT_OK";
	case AZURE_SPHERE_PROV_RESULT_INVALID_PARAM:
		return "AZURE_SPHERE_PROV_RESULT_INVALID_PARAM";
	case AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY:
		return "AZURE_SPHERE_PROV_RESULT_NETWORK_NOT_READY";
	case AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY:
		return "AZURE_SPHERE_PROV_RESULT_DEVICEAUTH_NOT_READY";
	case AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR:
		return "AZURE_SPHERE_PROV_RESULT_PROV_DEVICE_ERROR";
	case AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR:
		return "AZURE_SPHERE_PROV_RESULT_GENERIC_ERROR";
	default:
		return "UNKNOWN_RETURN_VALUE";
	}
}

/// <summary>
///     Callback confirming message delivered to IoT Hub.
/// </summary>
/// <param name="result">Message delivery status</param>
/// <param name="context">User specified context</param>
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context)
{
	Log_Debug("INFO: Message received by IoT Hub. Result is: %d\n", result);
}

/// <summary>
///     Sends telemetry to IoT Hub
/// </summary>
/// <param name="key">The telemetry item to update</param>
/// <param name="value">new telemetry value</param>
static void SendTelemetry(const unsigned char* key, const unsigned char* value)
{
	static char eventBuffer[100] = { 0 };
	static const char* EventMsgTemplate = "{ \"%s\": %s }";
	int len = snprintf(eventBuffer, sizeof(eventBuffer), EventMsgTemplate, key, value);
	if (len < 0)
		return;

	Log_Debug("Sending IoT Hub Message: %s\n", eventBuffer);

	IOTHUB_MESSAGE_HANDLE messageHandle = IoTHubMessage_CreateFromString(eventBuffer);

	if (messageHandle == 0) {
		Log_Debug("WARNING: unable to create a new IoTHubMessage\n");
		return;
	}

	if (IoTHubDeviceClient_LL_SendEventAsync(iothubClientHandle, messageHandle, SendMessageCallback,
		/*&callback_param*/ 0) != IOTHUB_CLIENT_OK) {
		Log_Debug("WARNING: failed to hand over the message to IoTHubClient\n");
	}
	else {
		Log_Debug("INFO: IoTHubClient accepted the message for delivery\n");
	}

	IoTHubMessage_Destroy(messageHandle);
}

/// <summary>
///     Creates and enqueues a report containing the name and value pair of a Device Twin reported
///     property. The report is not sent immediately, but it is sent on the next invocation of
///     IoTHubDeviceClient_LL_DoWork().
/// </summary>
/// <param name="propertyName">the IoT Hub Device Twin property name</param>
/// <param name="propertyValue">the IoT Hub Device Twin property value</param>
static void TwinReportBoolState(const char* propertyName, bool propertyValue)
{
	if (iothubClientHandle == NULL) {
		Log_Debug("ERROR: client not initialized\n");
	}
	else {
		static char reportedPropertiesString[30] = { 0 };
		int len = snprintf(reportedPropertiesString, 30, "{\"%s\":%s}", propertyName,
			(propertyValue == true ? "true" : "false"));
		if (len < 0)
			return;

		if (IoTHubDeviceClient_LL_SendReportedState(
			iothubClientHandle, (unsigned char*)reportedPropertiesString,
			strlen(reportedPropertiesString), ReportStatusCallback, 0) != IOTHUB_CLIENT_OK) {
			Log_Debug("ERROR: failed to set reported state for '%s'.\n", propertyName);
		}
		else {
			Log_Debug("INFO: Reported state for '%s' to value '%s'.\n", propertyName,
				(propertyValue == true ? "true" : "false"));
		}
	}
}

/// <summary>
///     Callback invoked when the Device Twin reported properties are accepted by IoT Hub.
/// </summary>
static void ReportStatusCallback(int result, void* context)
{
	Log_Debug("INFO: Device Twin reported properties update result: HTTP status code %d\n", result);
}

/// <summary>
///     Generates a simulated Temperature and sends to IoT Hub.
/// </summary>
void SendSimulatedTemperature(void)
{
	static float temperature = 30.0;
	float deltaTemp = (float)(rand() % 20) / 20.0f;
	if (rand() % 2 == 0) {
		temperature += deltaTemp;
	}
	else {
		temperature -= deltaTemp;
	}

	char tempBuffer[20];
	int len = snprintf(tempBuffer, 20, "%3.2f", temperature);
	if (len > 0)
		SendTelemetry("Temperature", tempBuffer);
}