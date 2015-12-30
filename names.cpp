#include "pannan.h"
#include <EEPROM.h>

#define member_size(type, member) sizeof(((type *)0)->member)
#define DATA_SIZE (member_size(TempSensor, addr) + member_size(TempSensor, name))

int eeprom_read_temp_sensor_index(int i, TempSensor *sensor)
{
    int j;
    int offset = (i * DATA_SIZE);
    char buf[DATA_SIZE];

    // Read address + name.
    for (j = 0; j < DATA_SIZE; j++)
    {
        buf[j] = EEPROM.read(offset + j);
    }

    memcpy(sensor->addr, buf, sizeof(sensor->addr));
    strncpy(sensor->name, buf + sizeof(sensor->addr), sizeof(sensor->name));

    if (!sensor->addr[0])
        return -1;

    return 0; 
}

void eeprom_read_temp_sensors(TempSensor *temps, int *count)
{
    int i;
    for (i = 0; i < MAX_TEMP_SENSORS; i++)
    {
        if (eeprom_read_temp_sensor_index(i, &temps[i]) < 0)
        {
            return;
        }

        (*count)++;
    }
}

void eeprom_write_temp_sensor(int i, TempSensor *sensor)
{
    int j;
    int offset = (i * DATA_SIZE);

    for (j = 0; j < member_size(TempSensor, addr); j++)
    {
        EEPROM.write(offset + j, sensor->addr[j]);
    }

    for (j = 0; j < member_size(TempSensor, name); j++)
    {
        EEPROM.write(offset + j, sensor->name[j]);
    }
}

void eeprom_write_temp_sensors(TempSensor *temps, int count)
{
    int i;

    for (i = 0; i < count; i++)
    {
        eeprom_write_temp_sensor(i, &temps[i]);
    }
}

int find_address(TempSensor *temps, int temp_count,
                DeviceAddress addr, char *buf, int bufsize)
{
    int i;

    for (i = 0; i < temp_count; i++)
    {
        if (!memcmp(temps[i].addr, addr, sizeof(addr)))
        {
            if (buf)
            {
                strncpy(buf, temps[i].name, bufsize);
            }
            return i;
        }
    }

    buf[0] = '\0';
    return -1;
}

void set_name(int i, DeviceAddress addr, const char *name)
{
    TempSensor sensor;
    memcpy(&sensor.addr, addr, sizeof(sensor.addr));
    strcpy(sensor.name, name);
    eeprom_write_temp_sensor(i, &sensor);
}

void add_name(DeviceAddress addr, const char *name)
{
    int i;
    TempSensor temps[MAX_TEMP_SENSORS];
    int temp_count;

    eeprom_read_temp_sensors(temps, &temp_count);

    if ((i = find_address(temps, temp_count, addr, NULL, 0)) < 0)
    {
        i = temp_count; // Address not found so append.
    }

    set_name(i, addr, name);
}

void clear_names()
{
    for (int i = 0 ; i < EEPROM.length() ; i++)
    {
        EEPROM.write(i, 0);
    }
}
