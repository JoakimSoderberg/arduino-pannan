
#ifndef __PANNAN_H__
#define __PANNAN_H__

#include <DallasTemperature.h>

#define MAX_TEMP_SENSORS 15 // TODO: To raise this, read from eeprom value by value instead.
#define MAX_NAME_LEN 10

#define member_size(type, member) sizeof(((type *)0)->member)

typedef enum sensor_type_e
{
	SENSOR_DS18B20,
	SENSOR_DS2762
} sensor_type_t;

typedef struct TempSensor
{
    DeviceAddress addr;
    char name[MAX_NAME_LEN];
    float temp;
    sensor_type_t type;
    #ifdef PANNAN_DS2762
    float microvolts;
    float ambient_temp;
    #endif // PANNAN_DS2762
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
	#ifdef PANNAN_CLIENT
	Settings settings;
	#endif
} Context;

#endif // __PANNAN_H__
