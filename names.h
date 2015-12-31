
#ifndef __NAMES_H__
#define __NAMES_H__

#include <DallasTemperature.h>

void print_address(DeviceAddress address);

void eeprom_add_name(DeviceAddress addr, const char *name);
void eeprom_clear_names();
void eeprom_list_names();
void eeprom_read_temp_sensors(TempSensor *temps, int *count);
int eeprom_find_address(TempSensor *temps, int temp_count,
                DeviceAddress addr, char *buf, int bufsize);

#endif // __NAMES_H__
