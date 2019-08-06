#pragma once
// Azure IoT SDK
#include <azureiot/iothub_client_core_common.h>
#include <azureiot/iothub_device_client_ll.h>
#include <azureiot/iothub_client_options.h>
#include <azureiot/iothubtransportmqtt.h>
#include <azureiot/iothub.h>
#include <azureiot/azure_sphere_provisioning.h>
#include "stdbool.h"
#include "epoll_timerfd_utilities.h"
#include "parson.h" // used to parse Device Twin messages.

#define SCOPEID_LENGTH 20
#define LOGLINE_LENGTH 1024

// Azure IoT poll periods

static const int AzureIoTDefaultPollPeriodSeconds = 1;
static const int AzureIoTMinReconnectPeriodSeconds = 60;
static const int AzureIoTMaxReconnectPeriodSeconds = 10 * 60;
static int azureIoTPollPeriodSeconds = -1;

static IOTHUB_DEVICE_CLIENT_LL_HANDLE iothubClientHandle = NULL;

static const int keepalivePeriodSeconds = 20;

void SetupAzureClient(int timerFd, char _scopeId[SCOPEID_LENGTH]);
bool isIoTHubAuthenticated(void);
static void SendMessageCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* context);
static void TwinCallback(DEVICE_TWIN_UPDATE_STATE updateState, const unsigned char* payload, size_t payloadSize, void* userContextCallback);
void TwinReportStringState(const char* propertyName, const char* propertyValue);
void TwinReportIntState(const char* propertyName, unsigned int propertyValue);
static void ReportStatusCallback(int result, void* context);
static int DeviceMethodCallback(const char* method_name, const unsigned char* payload, size_t size, unsigned char** response, size_t* resp_size, void* userContextCallback);
static const char* GetReasonString(IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason);
static const char* getAzureSphereProvisioningResultString(AZURE_SPHERE_PROV_RETURN_VALUE provisioningResult);
static void SendTelemetry(const unsigned char* key, const unsigned char* value);
void HubConnectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* userContextCallback);
IOTHUB_DEVICE_CLIENT_LL_HANDLE getIoTHubClientHandle(void);
static IOTHUBMESSAGE_DISPOSITION_RESULT ReceiveMessageCallback(IOTHUB_MESSAGE_HANDLE message, void* user_context);
