
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include "pannan.h"
#include "names.h"

#define ONE_WIRE_BUS 2
#define TEMPERATURE_PRECISION 9

#define ERROR_LED_PIN 4

#define READ_DELAY 5000

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

Context ctx;

void print_sensor(int i, TempSensor *sensor, byte newline = 1)
{
    Serial.print(i);
    Serial.print(": ");
    print_address(ctx.temps[i].addr);
    Serial.print(" ");
    Serial.print(ctx.temps[i].name);
    if (newline) Serial.println();
}

void prepare_sensors()
{
    TempSensor names[MAX_TEMP_SENSORS];
    int name_count = 0;
    int i;

    // Locate devices on the bus
    sensors.begin();

    ctx.count = sensors.getDeviceCount();

    Serial.print("Locating devices...");
    Serial.print("Found ");
    Serial.print(ctx.count, DEC);
    Serial.println(" devices.");

    Serial.println("Get device names from EEPROM...");
    eeprom_read_temp_sensors(names, &name_count);

    for (i = 0; i < ctx.count; i++)
    {
        if (!sensors.getAddress(ctx.temps[i].addr, i))
        {
            Serial.print("ERROR getting address for sensor at index ");
            Serial.println(i);
        }
        else
        {
            sensors.setResolution(ctx.temps[i].addr, TEMPERATURE_PRECISION);

            if (eeprom_find_address(names, name_count,
                                    ctx.temps[i].addr, // Search for this
                                    ctx.temps[i].name, // Put name here if found
                                    sizeof(ctx.temps[i].name)))
            {
                // No name found in EEPROM
                strcpy(ctx.temps[i].name, "unknown");
            }

            print_sensor(i, &ctx.temps[i]);
        }
    }
}

void setup()
{
    int i;
    Serial.begin(9600);

    while (!Serial)
    {
        ; // wait for serial port to connect. Needed for native USB
    }

    Serial.println("Serial port active...");

    prepare_sensors();
}

void loop()
{
    int i;

    sensors.requestTemperatures();

    for (i = 0; i < ctx.count; i++)
    {
        ctx.temps[i].temp = sensors.getTempC(ctx.temps[i].addr);
    }

    delay(READ_DELAY);
}
