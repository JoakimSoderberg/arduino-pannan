
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>
#include <Ethernet.h>
#include <SoftwareSerial.h>
#include <Button.h>
//#include <HTTPClient.h>

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
#define UP_PIN 5
Button up_button = Button(UP_PIN, PULLUP);
#define DOWN_PIN 6
Button down_button = Button(DOWN_PIN, PULLUP);
unsigned long lcd_last_page_switch = 0;
unsigned long lcd_both_buttons_start = 0;
int lcd_start_index = 0;
int lcd_scroll_enabled = 1;

// Time between reading the temperature sensors.
#define READ_DELAY 5000

// Attach the serial display's RX line to digital pin 3
SoftwareSerial lcd(4,3); // pin 3 = TX, pin 4 = RX (unused)

byte mac[] = { 0xDE, 0x01, 0xBE, 0x3E, 0x21, 0xED };

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#ifdef PANNAN_CLIENT

#define HTTP_REQUEST_PORT_DEFAULT 9000
#define HTTP_REQUEST_DELAY_DEFAULT 5000

// This is used to do HTTP PUT of the json to a specified server.
EthernetClient client;
char server_hostname[32] = "higgs";
IPAddress server_addr = IPAddress(192, 168, 0, 146);
int server_port = HTTP_REQUEST_PORT_DEFAULT;
int http_request_delay = HTTP_REQUEST_DELAY_DEFAULT;

#endif // PANNAN_CLIENT

#ifdef PANNAN_SERVER
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

    delay(5000);

    ctx.count = sensors.getDeviceCount();

    Serial.print(F("Locating devices..."));
    Serial.print("Found ");
    Serial.print(ctx.count, DEC);
    Serial.println(" devices.");

    Serial.println(F("Get device names from EEPROM..."));
    eeprom_read_temp_sensors(names, &name_count);
    delay(5000);

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

void print_ip(IPAddress ip)
{
    ip.printTo(Serial);
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
            print_ip(Ethernet.localIP());
            break;
        case 3:
            Serial.println(F("Error: rebind fail"));
            break;
        case 4:
            Serial.println(F("Rebind success"));
            print_ip(Ethernet.localIP());
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

char *get_address_str(char *buf, DeviceAddress *addr)
{
    uint8_t j = 0;
    uint8_t step;

    for (uint8_t i = 0; i < ADDR_SIZE; i++)
    {
        step = 2;

        // zero pad the address if necessary
        if ((*addr)[i] < 16)
        {
            buf[j++] = '0';
            step--;
        }

        // avr version does not have 0 padding...
        sprintf(&buf[j], "%X", (*addr)[i]);

        j += step;
    }

    return buf;
}

#define SENSOR_JSON_FMT         \
    "    {\n"                   \
    "      \"name\": \"%s\",\n" \
    "      \"index\": %d,\n"    \
    "      \"addr\": \"%s\",\n" \
    "      \"temp\": %s\n"      \
    "    }"

char *get_sensor_json(char *buf, int i, TempSensor *s)
{
    char addrbuf[ADDR_SIZE * 2 + 1];
    char str_temp[6];
    dtostrf(s->temp, 2, 2, str_temp);

    sprintf(buf, SENSOR_JSON_FMT,
            s->name, i, get_address_str(addrbuf, &s->addr), str_temp);

    return buf;
}

char *get_http_request_json()
{
    int offset = 0;
    #define REQ_JSON_HEADER \
        "{\n"               \
        "  \"sensors\":\n"  \
        "  [\n"

    #define REQ_JSON_FOOTER \
        "  ]\n"             \
        "}\n"

    // NOTE: If ctx.count > 12 this will be too much memory.
    int alloc_size = (ctx.count * 
        (MAX_NAME_LEN + (ADDR_SIZE * 2) + sizeof("-##.##")))
        + sizeof(SENSOR_JSON_FMT)
        + sizeof(REQ_JSON_HEADER) + 32 + sizeof(REQ_JSON_FOOTER);

    Serial.print("Alloc size: ");
    Serial.println(alloc_size);

    // Guestimate the needed buffer size.
    char *s = (char *)malloc(alloc_size);

    if (!s)
    {
        s = (char *)malloc(32);
        strcpy(s, "{\n\"error\": \"Out of memory\"\n}");
        return s;
    }

    strcpy(s, REQ_JSON_HEADER);
    offset += strlen(s);

    for (int i = 0; i < ctx.count; i++)
    {
        get_sensor_json(&s[offset], i, &ctx.temps[i]);
        offset += strlen(&s[offset]);

        if (i != (ctx.count - 1))
        {
            strcat(&s[offset], ",\n");
            offset += 2;
        }
        else
        {
            strcat(&s[offset], "\n");
            offset++;
        }
    }

    strcat(s, REQ_JSON_FOOTER);
}

#ifdef PANNAN_CLIENT
void print_http_request_header()
{
    client.println(F("PUT / HTTP/1.1"));
    client.print(F("Host: "));
    server_addr.printTo(client);
    client.println();
    client.println(F("User-Agent: arduino-ethernet"));
    client.println(F("Connection: close"));
    client.println(F("Content-Type: application/json"));
    client.println();
}

void http_request()
{
    int ret;
    Serial.print(F("HTTP Client request to "));
    print_ip(server_addr);
    client.stop();

    if ((ret = client.connect(server_addr, server_port)))
    {
        Serial.println(F("  Connected..."));

        // This needs to be allocated so we can calculate
        // the content-length, which is a HTTP PUT requirement.
        char *s = get_http_request_json();

        client.println(F("PUT / HTTP/1.1"));
        client.print(F("Host: "));
        server_addr.printTo(client);
        client.println();
        client.println(F("User-Agent: arduino-ethernet"));
        client.println(F("Connection: close"));
        client.println(F("Content-Type: application/json"));
        client.print(F("Content-Length: "));
        client.println(strlen(s));
        client.println(); // End of header.

        client.print(s);
        free(s);

        while (client.connected())
        {
            if (client.available())
            {
                char c = client.read();
                Serial.print(c);
            }
        }

        client.stop();
        Serial.println(F("  Disconnected"));
    }
    else
    {
        Serial.print(F("Connection failed: "));
        Serial.println(ret);
        client.stop();
    }
}
#endif // PANNAN_CLIENT

//
// position 1   2   3   4   5   6   7   8   9   10  11  12  13  14  15  16
// line 1  128 129 130 131 132 133 134 135 136 137 138 139 140 141 142 143
// line 2  192 193 194 195 196 197 198 199 200 201 202 203 204 205 206 207

void lcd_clear()
{
    lcd.write(254); // Move cursor to beginning of first line.
    lcd.write(128);
    lcd.write("                "); // Clear display.

    lcd.write(254); 
    lcd.write(192);
    lcd.write("                ");

    lcd.write(254); // move cursor to beginning of first line
    lcd.write(128);
}

void print_lcd_started()
{
    lcd_clear();
    lcd.write("Started!");
}

void print_lcd_temperature_buf(int i)
{
    char buf[16];
    char str_temp[6];
    dtostrf(ctx.temps[i].temp, 2, 2, str_temp);
    sprintf(buf, "%-10s%4sC", ctx.temps[i].name, str_temp);
    lcd.write(buf);
}

void print_lcd_temperatures()
{
    int i = lcd_start_index;
    char str_temp[6];

    // Move cursor to beginning of first line.
    lcd.write(254);
    lcd.write(128);

    print_lcd_temperature_buf(i);
    i++;

    // Move cursor to beginning of second line.
    lcd.write(254); 
    lcd.write(192);

    if (i < ctx.count)
    {
        print_lcd_temperature_buf(i);
    }
    else
    {
        lcd.write("                ");
    }
}

void lcd_do_scroll()
{
    // Switch page if it's time.
    if ((millis() - lcd_last_page_switch) > READ_DELAY)
    {
        lcd_start_index += 2;
        if (lcd_start_index >= ctx.count)
            lcd_start_index = 0;
        lcd_last_page_switch = millis();
    }
}

void lcd_button_state_change()
{
    if (up_button.isPressed() && down_button.isPressed())
    {
        if (!lcd_scroll_enabled)
        {
            lcd_scroll_enabled = 1;
            lcd_clear();
            lcd.println(F("Scroll enabled"));
            delay(1000);
        }    
        return;
    }

    lcd_both_buttons_start = millis();

    if (millis() - lcd_last_page_switch > 500)
    {
        if (up_button.isPressed())
        {
            Serial.println("UP");
            lcd_start_index -= 2;
        }
        else if (down_button.isPressed())
        {
            Serial.println("DOWN");

            if ((lcd_start_index + 1) < ctx.count)
                lcd_start_index += 2;
        }

        if (lcd_scroll_enabled)
        {
            lcd.println(F("Scroll disabled"));
            delay(1000);
            lcd_scroll_enabled = 0;
        }

        lcd_start_index = max(0, lcd_start_index);
        lcd_start_index %= ctx.count;

        lcd_last_page_switch = millis();

        print_lcd_temperatures();
    }
}

#define CHTML(str) client.print(F(str))
#define SHTML(str) sclient.print(F(str))

void print_html_form()
{

}

void setup()
{
    //prepare_sensors();

    Serial.begin(9600);

    while (!Serial)
    {
        ; // wait for serial port to connect. Needed for native USB
    }

    Serial.println(F("Serial port active..."));

    lcd.begin(9600);
    delay(500);
    Serial.println(F("LCD serial active..."));

    prepare_sensors();

    delay(5000);
    Serial.println(F("Start Ethernet..."));

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
    print_ip(Ethernet.localIP());

    #ifdef PANNAN_SERVER
    server.begin();
    delay(5000);
    #endif

    print_lcd_started();
    delay(1000);
}

void loop()
{
    if (lcd_scroll_enabled)
    {
        lcd_do_scroll();
    }

    if (up_button.isPressed() || down_button.isPressed())
    {
        lcd_button_state_change();
    }

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

        print_lcd_temperatures();
    }

    // Server.
    #ifdef PANNAN_SERVER

    EthernetClient sclient = server.available();

    if (sclient)
    {
        Serial.println(F("New client"));

        // A HTTP request ends with a blank line.
        boolean blank_line = true;

        while (sclient.connected())
        {
            if (sclient.available())
            {
                char c = sclient.read();
                Serial.write(c);

                if (c == '\n' && blank_line)
                {
                    char *s = get_http_request_json();
                    
                    sclient.println("HTTP/1.1 200 OK");
                    sclient.println("Connection: close");
                    sclient.println("Content-Type: application/json");
                    sclient.print("Content-Length: ");
                    sclient.println(strlen(s));
                    sclient.println();

                    sclient.print(s);
                    free(s);

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
        sclient.stop();
        Serial.println("Client disconnected");
    }
    #endif // !PANNAN_CLIENT

    feed_dhcp();
}
