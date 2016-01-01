
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <Ethernet.h>

#include "pannan.h"
#include "names.h"

//
// Not sure why, but we need to declare this for the Ethernet lib
// to work with Arduino-cmake :/
//
// Since this works with the normal Arduino environment it should be possible
// to fix this in arduino-cmake instead.
//
__extension__ typedef int __guard __attribute__((mode (__DI__)));
extern "C" int __cxa_guard_acquire(__guard *);
extern "C" void __cxa_guard_release (__guard *);
extern "C" void __cxa_guard_abort (__guard *);
int __cxa_guard_acquire(__guard *g)
{
    return !*(char *)(g);
}
void __cxa_guard_release (__guard *g)
{
    *(char *)g = 1;
}
void __cxa_guard_abort (__guard *)
{
}
extern "C" void __cxa_pure_virtual(void);
void __cxa_pure_virtual(void)
{
}

#define ONE_WIRE_BUS 2
#define TEMPERATURE_PRECISION 12

#define ERROR_LED_PIN 4

#define READ_DELAY 5000

byte mac[] = { 0xDE, 0x01, 0xBE, 0xEF, 0xFE, 0xED };

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

#ifdef PANNAN_CLIENT
EthernetClient client;
char server[] = "192.168.0.230";
IPAddress server_addr = IPAddress(192, 168, 0, 230);
int server_port = 9000;
#else // PANNAN_CLIENT

EthernetServer server(80);

#endif

Context ctx;
unsigned long last_temp_read;

void print_sensor(int i, TempSensor *sensor,
                  byte newline = 1, byte show_temperature = 0)
{
    Serial.print(i);
    Serial.print(": ");
    print_address(sensor->addr);

    if (show_temperature)
    {
        Serial.print(" ");
        Serial.print(sensor->temp);
        Serial.print("C");
    }

    Serial.print(" ");
    Serial.print(sensor->name);
    Serial.println(" ");

    if (newline) Serial.println();
}

void prepare_sensors()
{
    TempSensor names[MAX_TEMP_SENSORS];
    int name_count = 0;
    int i;

    // Locate devices on the bus
    sensors.begin();

    delay(100);

    ctx.count = sensors.getDeviceCount();

    Serial.print(F("Locating devices..."));
    Serial.print("Found ");
    Serial.print(ctx.count, DEC);
    Serial.println(" devices.");

    Serial.println(F("Get device names from EEPROM..."));
    eeprom_read_temp_sensors(names, &name_count);
    delay(100);

    for (i = 0; i < ctx.count; i++)
    {
        if (!sensors.getAddress(ctx.temps[i].addr, i))
        {
            Serial.print(F("ERROR getting address for sensor at index "));
            Serial.println(i);
        }
        else
        {
            sensors.setResolution(ctx.temps[i].addr, TEMPERATURE_PRECISION);

            if (eeprom_find_address(names, name_count,
                                    ctx.temps[i].addr, // Search for this
                                    ctx.temps[i].name, // Put name here if found
                                    sizeof(ctx.temps[i].name)) < 0)
            {
                // No name found in EEPROM
                strcpy(ctx.temps[i].name, "unknown");
            }

            print_sensor(i, &ctx.temps[i]);
        }
    }
}

void print_ip()
{
    for (byte i = 0; i < 4; i++)
    {
        Serial.print(Ethernet.localIP()[i], DEC);
        if (i != 3) Serial.print(".");
    }

    Serial.println();
}

void feed_dhcp()
{
    switch (Ethernet.maintain())
    {
        case 1:
            Serial.println(F("ERROR: DHCP renewed fail"));
            break;
        case 2:
            Serial.println(F("Renew success"));
            print_ip();
            break;
        case 3:
            Serial.println(F("Error: rebind fail"));
            break;
        case 4:
            Serial.println(F("Rebind success"));
            print_ip();
            break;

        default:
            break;
    }
}

void print_address_json(EthernetClient *c, TempSensor *s)
{
    for (uint8_t i = 0; i < ADDR_SIZE; i++)
    {
        // zero pad the address if necessary
        if (s->addr[i] < 16) c->print("0");
        c->print(s->addr[i], HEX);
    }
}

void print_sensor_json(EthernetClient *c, int i, TempSensor *s)
{
    c->println("    {");
    c->print("      \"name\": \""); c->print(s->name); c->println("\",");
    c->print("      \"index\": "); c->print(i); c->println(",");
    c->print("      \"addr\": \""); print_address_json(c, s); c->println("\",");
    c->print("      \"temp\": "); c->println(s->temp);
    c->print("    }");
}

void print_http_request_body(EthernetClient *c)
{
    c->println("{");
    c->println("  \"sensors\":");
    c->println("  [");
    
    for (int i = 0; i < ctx.count; i++)
    {
        print_sensor_json(c, i, &ctx.temps[i]);
        
        if (i != (ctx.count - 1))
        {
            c->println(",");
        }
        else
        {
            c->println();
        }
    }

    c->println("  ]");
    c->println("}");
}

#ifdef PANNAN_CLIENT
void print_http_request_header()
{
    client.println("PUT / HTTP/1.1");
    client.print("Host: ");
    client.println(server);
    client.println("User-Agent: arduino-ethernet");
    client.println("Connection: close");
    client.println("Content-Type: application/json");
    client.println();
}

void http_request()
{
    int ret;
    Serial.println("Making HTTP request...");
    client.stop();

    if ((ret = client.connect(server_addr, server_port)))
    {
        Serial.println("Connected...");

        print_http_request_header();
        print_http_request_body(&client);

        while (client.connected())
        {
            if (client.available())
            {
                char c = client.read();
                Serial.print(c);
            }      
        }

        Serial.println();
        client.stop();
    }
    else
    {
        Serial.print("Connection failed: ");
        Serial.println(ret);
        client.stop();
    }
}
#endif // PANNAN_CLIENT

void setup()
{
    int i;
    Serial.begin(9600);

    while (!Serial)
    {
        ; // wait for serial port to connect. Needed for native USB
    }

    Serial.println(F("Serial port active..."));

    prepare_sensors();

    delay(5000);
    Serial.println("Start Ethernet...");

    // Initiate DHCP request for IP.
    if (Ethernet.begin(mac) == 0)
    {
        Serial.println(F("Failed to configure Ethernet using DHCP..."));
        while (Ethernet.begin(mac) == 0)
        {
            delay(READ_DELAY);
        }
    }

    Serial.println(F("DHCP request successful"));
    print_ip();

    server.begin();
    delay(5000);
}

void loop()
{
    if ((millis() - last_temp_read) > READ_DELAY)
    {
        sensors.requestTemperatures();
        Serial.println();

        for (int i = 0; i < ctx.count; i++)
        {
            ctx.temps[i].temp = sensors.getTempC(ctx.temps[i].addr);
            print_sensor(i, &ctx.temps[i], 0, 1);
        }

        #ifdef PANNAN_CLIENT
        http_request();
        #endif

        last_temp_read = millis();
    }

    // Server.
    #ifndef PANNAN_CLIENT

    EthernetClient client = server.available();

    if (client)
    {
        Serial.println(F("New client"));

        // A HTTP request ends with a blank line
        boolean blank_line = true;

        while (client.connected())
        {
            if (client.available())
            {
                char c = client.read();
                Serial.write(c);
                
                // Look for end of request.
                if (c == '\n' && blank_line)
                {
                    // send a standard http response header
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: text/html");
                    client.println("Connection: close");  // the connection will be closed after completion of the response
                    //client.println("Refresh: 5");  // refresh the page automatically every 5 sec
                    client.println("Content-Type: application/json");
                    client.println();
                    
                    print_http_request_body(&client);
                    break;
                }

                if (c == '\n')
                {
                    // you're starting a new line
                    blank_line = true;
                }
                else if (c != '\r')
                {
                    // you've gotten a character on the current line
                    blank_line = false;
                }
            }
        }

        // Give the web browser time to receive the data
        delay(1);
        // close the connection:
        client.stop();
        Serial.println("Client disconnected");
    }
    #endif // !PANNAN_CLIENT

    feed_dhcp();
}
