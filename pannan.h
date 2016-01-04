
#ifndef __PANNAN_H__
#define __PANNAN_H__

#include <DallasTemperature.h>

#define MAX_TEMP_SENSORS 16
#define MAX_NAME_LEN 10

#define member_size(type, member) sizeof(((type *)0)->member)

typedef struct TempSensor
{
    DeviceAddress addr;
    char name[MAX_NAME_LEN];
    float temp;
} TempSensor;

#define ADDR_SIZE member_size(TempSensor, addr)
#define NAME_SIZE member_size(TempSensor, name)
#define DATA_SIZE (1 + ADDR_SIZE + NAME_SIZE) // 1 for "used" status

typedef struct Settings
{
	byte http_client_enabled;
	char server_hostname[16];
	int server_port;
	int http_request_delay;
} Settings;

typedef struct Context
{
	TempSensor temps[MAX_TEMP_SENSORS];
	int count;
	int err;
	Settings settings;
} Context;

#endif // __PANNAN_H__
