
#ifndef __PANNAN_H__
#define __PANNAN_H__

#include <DallasTemperature.h>

#define MAX_TEMP_SENSORS 16

#define member_size(type, member) sizeof(((type *)0)->member)

typedef struct TempSensor {
    DeviceAddress addr;
    char name[16];
} TempSensor;

#define ADDR_SIZE member_size(TempSensor, addr)
#define NAME_SIZE member_size(TempSensor, name)
#define DATA_SIZE (ADDR_SIZE + NAME_SIZE)

typedef struct Context {
	TempSensor temps[MAX_TEMP_SENSORS];
	int count;
} Context;

#endif // __PANNAN_H__
