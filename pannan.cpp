
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include "pannan.h"

#define ONE_WIRE_BUS 2
#define TEMPERATURE_PRECISION 9

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

Context ctx;

void setup()
{
    int i;
    Serial.begin(9600);

    while (!Serial)
    {
        ; // wait for serial port to connect. Needed for native USB
    }

    // Locate devices on the bus
    sensors.begin();

    ctx.count = sensors.getDeviceCount();
    
    Serial.print("Locating devices...");
    Serial.print("Found ");
    Serial.print(ctx.count, DEC);
    Serial.println(" devices.");

    //temps = malloc(sizeof(TempSensor) * temp_count);

    for (i = 0; i < ctx.count; i++)
    {
        sensors.getAddress(ctx.temps[i].addr, i);
    }
}

void loop()
{

}
