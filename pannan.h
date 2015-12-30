
#ifndef __PANNAN_H__
#define __PANNAN_H__

#include <DallasTemperature.h>

#define MAX_TEMP_SENSORS 16

typedef struct TempSensor {
    DeviceAddress addr;
    char name[16];
} TempSensor;

typedef struct Context {
	TempSensor temps[MAX_TEMP_SENSORS];
	int count;
} Context;

#endif // __PANNAN_H__
