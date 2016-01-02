
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

#define ERROR_LED_PIN 13
#define UP_PIN 5
#define DOWN_PIN 6
Button up_button = Button(UP_PIN, PULLUP);
Button down_button = Button(DOWN_PIN, PULLUP);
unsigned long lcd_last_page_switch = 0;
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
int server_port = HTTP_REQUEST_PORT_DEFAULT;
int http_request_delay = HTTP_REQUEST_DELAY_DEFAULT;
unsigned long last_http_request = 0;

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
    print_address(Serial, sensor->addr);

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

int get_http_status_code(char *buf, int bufsize)
{
    if (!strncmp(buf, "HTTP/1.", 7))
    {
        // Skip "HTTP/1.x "
        int start = 7 + 2;
        if (start >= bufsize)
            return 0;

        int end = start;
        while (isdigit(buf[end]) && (end < bufsize)) end++;
        buf[end] = 0;

        return atoi(&buf[start]);
    }

    return 0;
}

int http_request()
{
    int status_code = 0;
    int ret;
    Serial.print(F("HTTP Client request to "));
    Serial.println(server_hostname);
    client.stop();

    if ((ret = client.connect(server_hostname, server_port)))
    {
        int j = 0;
        int end;
        Serial.println(F("  Connected..."));

        // This needs to be allocated so we can calculate
        // the content-length, which is a HTTP PUT requirement.
        char *s = get_http_request_json();

        client.println(F("PUT / HTTP/1.1"));
        client.print(F("Host: "));
        client.println(server_hostname);
        client.println(F("User-Agent: arduino-ethernet"));
        client.println(F("Connection: close"));
        client.println(F("Content-Type: application/json"));
        client.print(F("Content-Length: "));
        client.println(strlen(s));
        client.println(); // End of header.

        client.print(s);
        free(s);

        char buf[16];
        memset(buf, 0, sizeof(buf));

        while (client.connected())
        {
            if (client.available())
            {
                char c = client.read();
                Serial.print(c);

                // Save the first line so we can parse status.
                if (j < sizeof(buf))
                {
                    buf[j++] = c;
                }
            }
        }

        status_code = get_http_status_code(buf, sizeof(buf));

        client.stop();
        Serial.print("Status code: ");
        Serial.println(status_code);
        Serial.println(F("  Disconnected"));
    }
    else
    {
        Serial.print(F("Connection failed: "));
        Serial.println(ret);
        client.stop();
    }

    return status_code;
}
#endif // PANNAN_CLIENT

#ifdef PANNAN_SERVER

#define CHTML(str) client.print(F(str))
#define SHTML(str) c.print(F(str))

#define HTML_OK "200 OK"
#define HTML_CONTENT_TYPE "text/html; charset=utf-8"

void send_http_response_header(Print &c,
                            const char *status = HTML_OK,
                            const char *content_type = HTML_CONTENT_TYPE,
                            int end = 1)
{
  c.print(F("HTTP/1.1 "));
  c.println(status);
  c.print(F("Content-Type: "));
  c.println(content_type);
  if (end) c.println();
}

void server_404_reply(Print &c)
{
    send_http_response_header(c, "404 Not Found");
    SHTML("<html>404 bad url!</html>");
}

void server_unsupported_reply(Print &c)
{
    send_http_response_header(c, "501 Method not supported");
}

void server_json_reply(Print &c, char *url)
{
    char *s = get_http_request_json();

    send_http_response_header(c, HTML_OK, "application/json", 0);
    c.print("Content-Length: ");
    c.println(strlen(s));
    c.println();

    c.print(s);
    free(s);
}

void server_names_form_reply(Print &c, char *url)
{
    TempSensor *s;
    send_http_response_header(c);

    SHTML("<html>"
          "<body>"
          "<h1>Sensor names</h1>"
          "<table>"
          "<tr><th>Index</th><th>Address</th><th>Name</th></tr>");
    
    for (int i = 0; i < ctx.count; i++)
    {
        s = &ctx.temps[i];

        SHTML("<tr><td>");
        c.print(i);
        SHTML("</td><td>");
        print_address(c, s->addr);
        SHTML("</td><td>");
        c.print(s->name);
        SHTML("</td><td>");
        SHTML("<form action='editname' method='get'>"
              "<input type='submit' value='Edit' />"
              "<input type='hidden' name='i' value='");
        c.print(i);
        SHTML("'/></form></td></tr>");
    }

    SHTML("</table>"
          "</form>"
          "</body>"
          "</html>");
}

#define SKIP_UNTIL_NEXT_PARAM(s) \
    while (*s && (*s != '&') && (*s != '\n')) s++;

int parse_url_params(char *url)
{

}

void server_editname_form_reply(Print &c, char *url)
{
    int i = 0;
    {
        char *start = url;
        char *s = start;
        send_http_response_header(c);

        Serial.println(url);

        while (*s)
        {
            if (!strncmp(s, "i=", 2))
            {
                s += 2;
                start = s;
                //SKIP_UNTIL_NEXT_PARAM(s);
                while (*s && (*s != '&') && (*s != '\n')) s++;
                *s = 0;
                i = atoi(start);
                //while (*s && (*s != '&') && (*s != '\n')) s++;
            }
            s++;
        }
    }

    if ((i < 0) || (i >= ctx.count))
    {
        server_404_reply(c);
        return;
    }

    TempSensor *s = &ctx.temps[i];

    SHTML("<html>"
          "<body>"
          "<h1>Edit sensor name</h1>"
          "<table>");

    //      "<tr><th>Index</th><th>Address</th><th>Name</th></tr>");
    SHTML("<tr><th>Index:</th><td>");
    c.print(i);
    SHTML("</td></tr>");

    SHTML("<tr><th>Address:</th><td>");
    print_address(c, s->addr);
    SHTML("</td></tr>");

    SHTML("<tr><th>Name:</th><td>"
          "<form action='/setname'>"
          "<input type='text' value='");
    c.print(s->name);
    SHTML("'/></td></tr>");

    SHTML("</table>"
          "</form>"
          "</body>"
          "</html>");
}

void server_setname_reply(Print &c, char *url)
{
    server_unsupported_reply(c);
}

char *server_get_url(int i, char *buf, int bufsize)
{
    char *url = &buf[i];
    while ((i < (bufsize - 1)) && buf[i] != ' ')
        i++;
    buf[i] = 0;

    return url;
}

void feed_server()
{
    EthernetClient sclient = server.available();

    if (sclient)
    {
        char buf[64];
        char *url = NULL;
        int j = 0;
        typedef enum method_type_e
        {
            UNSUPPORTED,
            GET,
            POST
        } method_type_t;
        method_type_t method = UNSUPPORTED;
        Serial.println(F("New client"));

        // A HTTP request ends with a blank line.
        boolean blank_line = true;

        while (sclient.connected())
        {
            if (sclient.available())
            {
                char c = sclient.read();
                Serial.write(c);

                if (j < sizeof(buf))
                    buf[j++] = c;

                // if we've gotten to the end of the line (received a newline
                // character) and the line is blank, the http request has ended,
                // so we can send a reply
                if (c == '\n' && blank_line)
                {
                    // Find the URL.
                    for (int i = 0; i < sizeof(buf); i++)
                    {
                        if (!strncmp(&buf[i], "GET ", 4))
                        {
                            i += 4;
                            url = server_get_url(i, buf, sizeof(buf));
                            method = GET;
                        }
                        else if (!strncmp(&buf[i], "POST ", 5))
                        {
                            i += 5;
                            url = server_get_url(i, buf, sizeof(buf));
                            method = POST;
                        }
                    }

                    if (method == GET)
                    {
                        if (!strcmp(url, "/") || !strncmp(url, "/json", 5))
                        {
                            server_json_reply(sclient, url);
                        }
                        else if (!strncmp(url, "/names", 6))
                        {
                            server_names_form_reply(sclient, url + 6);
                        }
                        else if (!strncmp(url, "/editname?", 10))
                        {
                            server_editname_form_reply(sclient, url + 10);
                        }
                        else if (!strncmp(url, "/setname", 8))
                        {
                            server_setname_reply(sclient, url);
                        }
                        else
                        {
                            server_404_reply(sclient);
                        }
                    }
                    else if (method == POST)
                    {
                        char *post;
                        j = 0;
                        while (sclient.available())
                        {
                            c = sclient.read();
                            Serial.write(c);

                            if (j < sizeof(buf))
                                buf[j++] = c;
                        }

                        post = server_get_url(0, buf, sizeof(buf));
                        Serial.print(F("Post:"));
                        Serial.println(post);
                        goto end;
                    }
                    else
                    {
                        Serial.println(F("Unsupported method"));
                        server_unsupported_reply(sclient);
                        goto end;
                    }

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
    end:
        // Give the web browser time to receive the data
        delay(1);
        sclient.stop();
        Serial.println(F("Client disconnected"));
    }
}

#endif // PANNAN_SERVER

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
    lcd.write(254); // move cursor to beginning of first line
    lcd.write(128);
    // TODO: Print IP.
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

        print_lcd_temperatures();
    }
}

void set_error(const char *error)
{
    // TODO: Set error stuff. Turn on LED, show LCD message.
}

void read_temp_sensors()
{
    sensors.requestTemperatures();
    Serial.println();

    for (int i = 0; i < ctx.count; i++)
    {
        ctx.temps[i].temp = sensors.getTempC(ctx.temps[i].addr);
        print_sensor(i, &ctx.temps[i], 0, 1);
    }

    last_temp_read = millis();

    print_lcd_temperatures();
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
        read_temp_sensors();
        last_temp_read = millis();
    }

    #ifdef PANNAN_CLIENT
    if ((millis() - last_http_request) > http_request_delay)
    {
        if (http_request() != 200)
        {
            set_error("HTTP client fail");
        }

        last_http_request = millis(); 
    }
    #endif // PANNAN_CLIENT

    // Server.
    #ifdef PANNAN_SERVER
    feed_server();
    #endif // PANNAN_SERVER

    feed_dhcp();
}
