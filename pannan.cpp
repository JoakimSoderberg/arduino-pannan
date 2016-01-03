
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

//
// Helper to reuse the same flash string.
//  const char MyText[] PROGMEM  = { "My flash based text" };
//  Serial.println(FS(MyText));
//
#define FS(x) (__FlashStringHelper*)(x)

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
SoftwareSerial lcd(4,3); // pin 4 = RX (unused), pin 3 = TX

byte mac[] = { 0xDE, 0x01, 0xBE, 0x3E, 0x21, 0xED };

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#ifdef PANNAN_CLIENT

#define HTTP_REQUEST_PORT_DEFAULT 9000
#define HTTP_REQUEST_DELAY_DEFAULT 5000

// This is used to do HTTP PUT of the json to a specified server.
EthernetClient client;
char server_hostname[16] = "higgs";
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

    ctx.count = sensors.getDeviceCount();

    Serial.print(F("Locating devices..."));
    Serial.print("Found ");
    Serial.print(ctx.count, DEC);
    Serial.println(" devices.");

    //Serial.println(F("Get device names from EEPROM..."));
    eeprom_read_temp_sensors(names, &name_count);

    for (i = 0; i < ctx.count; i++)
    {
        if (!sensors.getAddress(ctx.temps[i].addr, i))
        {
            //Serial.print(F("ERROR getting address for sensor at index "));
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
            //Serial.println(F("Renew success"));
            print_ip(Ethernet.localIP());
            break;
        case 3:
            //Serial.println(F("Error: rebind fail"));
            break;
        case 4:
            //Serial.println(F("Rebind success"));
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
#define SENSOR_ADDR_SIZE (ADDR_SIZE * 2 + 1)
#define SENSOR_TEMP_SIZE 6
#define SENSOR_BUF_SIZE (sizeof(SENSOR_JSON_FMT) + SENSOR_ADDR_SIZE + SENSOR_TEMP_SIZE)

char *get_sensor_json(char *buf, int i, TempSensor *s)
{
    char addrbuf[SENSOR_ADDR_SIZE];
    char str_temp[SENSOR_TEMP_SIZE];
    dtostrf(s->temp, 2, 2, str_temp);

    sprintf(buf, SENSOR_JSON_FMT,
            s->name, i, get_address_str(addrbuf, &s->addr), str_temp);

    return buf;
}

char *dec2hex(int a)
{
    static char buf[4];
    sprintf(buf, "%x", a);
    return buf;
}

void print_sensor_json(Print &c, int i, TempSensor *s)
{
    char buf[SENSOR_BUF_SIZE];
    char *str = get_sensor_json(buf, i, s);
    c.println(dec2hex(strlen(str)));
    c.println(str);
}

#define PRINT_CHUNK(str)                        \
    do                                          \
    {                                           \
        c.println(dec2hex(sizeof(str) - 1));    \
        c.println(F(str));                      \
    } while (0)

//
// Note! This must be sent with header Transfer-Encoding: chunked
//
void print_http_request_json(Print &c)
{
    PRINT_CHUNK("{\n" \
                "  \"sensors\":\n"
                "  [\n");
    
    for (int i = 0; i < ctx.count; i++)
    {
        print_sensor_json(c, i, &ctx.temps[i]);
        
        if (i != (ctx.count - 1))
        {
            PRINT_CHUNK(",\n");
        }
        else
        {
            PRINT_CHUNK("\n");
        }
    }

    PRINT_CHUNK("  ]\n"
                "}\n");
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

        client.println(F("PUT / HTTP/1.1\r\n"
                         "User-Agent: arduino-ethernet\r\n"
                         "Connection: close\r\n"
                         "Content-Type: application/json\r\n"
                         "Transfer-Encoding: chunked\r\n"));
        client.print(F("Host: "));
        client.print(server_hostname);
        client.print(":");
        client.print(server_port);
        client.println(); // End of header.

        // This must be sent as chunked!
        print_http_request_json(client);
        client.println("0\r\n"); // End of chunked message.

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
        Serial.print("Status: ");
        Serial.println(status_code);
        //Serial.println(F("  Disconnected"));
    }
    else
    {
        Serial.print(F("Connect fail: "));
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

const char HTML_BODY_START[] PROGMEM = 
    "<html>"
    "<link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.6/css/bootstrap.min.css'>"
    "<body>";

void send_http_response_header(Print &c,
                            const char *status = HTML_OK,
                            const char *content_type = HTML_CONTENT_TYPE,
                            int end = 1)
{
  SHTML("HTTP/1.1 ");
  c.println(status);
  SHTML("Content-Type: ");
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
    send_http_response_header(c, HTML_OK, "application/json", 0);
    c.println(F("Transfer-Encoding: chunked"));
    c.println();
    print_http_request_json(c);
    c.println("0\r\n");
}

void server_home_reply(Print &c, char *url)
{
    TempSensor *s;
    char str_temp[6];

    send_http_response_header(c, HTML_OK, HTML_CONTENT_TYPE, 0);
    c.println(F("Refresh: 10"));    
    c.println();
    c.println(FS(HTML_BODY_START));
    SHTML("<div class='container'>");

    for (int i = 0; i < ctx.count; i++)
    {
        s = &ctx.temps[i];
        SHTML("<div class='row'>");
        SHTML("<div class='col-md-1'><strong>");
        c.print(s->name);
        SHTML("</strong></div>");
        SHTML("<div class='col-md-1'>");
        dtostrf(s->temp, 2, 2, str_temp);
        c.print(str_temp);
        SHTML("C</div>");
        SHTML("</div><br>");
    }

    SHTML("</div>");
    SHTML("</body>"
          "</html>");
}

void server_names_form_reply(Print &c, char *url)
{
    TempSensor *s;
    send_http_response_header(c);

    c.println(FS(HTML_BODY_START));
    SHTML("<h1>Sensor names</h1>"
          "<table class='table'>"
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
              "<input class='btn btn-link' type='submit' value='Edit' />"
              "<input type='hidden' name='i' value='");
        c.print(i);
        SHTML("'/></form></td></tr>");
    }

    SHTML("</table>"
          "</form>"
          "</body>"
          "</html>");
}

#define IF_PARAM(s, param)                      \
    if (!strncmp(s, param, sizeof(param) - 1)   \
        && (s += (sizeof(param) - 1)))

void server_editname_form_reply(Print &c, char *url)
{
    int i = -1;
    {
        char *s;
        char delimit[] = "&\n";
        s = strtok(url, delimit);

        while (s)
        {
            IF_PARAM(s, "i=")
            {
                i = atoi(s);
                break;
            }

            s = strtok(NULL, delimit);
        }
    }

    if ((i < 0) || (i >= ctx.count))
    {
        server_404_reply(c);
        return;
    }

    send_http_response_header(c);

    TempSensor *s = &ctx.temps[i];

    c.println(FS(HTML_BODY_START));
    SHTML("<form action='/setname' method='post'>"
          "<h1>Edit sensor</h1>"
          "<table class='table'>");

    SHTML("<tr><th>Index</th><td>");
    c.print(i);
    SHTML("</td></tr>");

    SHTML("<tr><th>Address</th><td>");
    print_address(c, s->addr);
    SHTML("</td></tr>");

    SHTML("<tr><th>Name</th><td>"
          "<input type='text' name='name' value='");
    c.print(s->name);
    SHTML("'/></td></tr>");

    SHTML("</table>"
          "<br/>"
          "<input class='btn btn-primary' type='submit' value='Save' />"
          "<a class='btn btn-link' href='/names'>Cancel</a>"
          "<input type='hidden' name='i' value='");
    c.print(i);
    SHTML("'/>"
          "</form>"
          "</body>"
          "</html>");
}

void server_setname_reply(Print &c, char *url, char *post)
{
    int i = -1;
    char name[MAX_NAME_LEN] = { 0 };
    {
        char *s;
        char delimit[] = "&\n";
        s = strtok(post, delimit);

        while (s)
        {
            IF_PARAM(s, "i=")
            {
                i = atoi(s);
            }

            IF_PARAM(s, "name=")
            {
                char *it = s;
                while (*it)
                {
                    if (*it == '+')
                        *it = ' ';
                    it++;
                }
                strncpy(name, s, sizeof(name) - 1);
            }

            s = strtok(NULL, delimit);
        }
    }

    if ((i < 0) || (i >= ctx.count) || (strlen(name) <= 0))
    {
        Serial.println(F("Invalid params setname"));
        send_http_response_header(c, "400 Bad request");
        SHTML("<html>400 Bad request</html>");
        return;
    }

    TempSensor *s = &ctx.temps[i];
    strcpy(s->name, name);
    eeprom_add_name(s->addr, name);

    Serial.println(F("Sending response 303"));
    send_http_response_header(c, "303 See other", HTML_CONTENT_TYPE, 0);
    SHTML("Location: /names");
    c.println();
}

char *server_get_query_string(int i, char *buf, int bufsize)
{
    char *url = &buf[i];
    while ((i < (bufsize - 1)) && buf[i] != ' ' && buf[i] != '\n')
        i++;
    buf[i] = 0;

    return url;
}

void feed_server()
{
    EthernetClient sclient = server.available();

    if (sclient)
    {
        char buf[32];
        char *url = NULL;
        int j = 0;
        typedef enum method_type_e
        {
            UNSUPPORTED,
            GET,
            POST
        } method_type_t;
        method_type_t method = UNSUPPORTED;
        //Serial.println(F("New client"));

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
                            url = server_get_query_string(i, buf, sizeof(buf));
                            method = GET;
                        }
                        else if (!strncmp(&buf[i], "POST ", 5))
                        {
                            i += 5;
                            url = server_get_query_string(i, buf, sizeof(buf));
                            method = POST;
                        }
                    }

                    if (method == GET)
                    {
                        if (!strcmp(url, "/"))
                        {
                            server_home_reply(sclient, url + 1);
                        }
                        else if (!strncmp(url, "/json", 5))
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
                        else
                        {
                            server_404_reply(sclient);
                        }
                    }
                    else if (method == POST)
                    {
                        char *post;
                        // TODO: Get rid of this...
                        url = strdup(url);
                        j = 0;
                        while (sclient.available())
                        {
                            c = sclient.read();
                            Serial.write(c);

                            if (j < sizeof(buf))
                                buf[j++] = c;
                        }

                        post = server_get_query_string(0, buf, sizeof(buf));
                        Serial.print(F("\nPost:"));
                        Serial.println(post);

                        if (!strncmp(url, "/setname", 8))
                        {
                            server_setname_reply(sclient, url + 8, post);
                        }
                        free(url);
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
        //Serial.println(F("Client disconnected"));
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

    Serial.println(F("Start Ethernet..."));

    // Initiate DHCP request for IP.
    if (Ethernet.begin(mac) == 0)
    {
        //Serial.println(F("Failed to configure Ethernet using DHCP..."));
        while (Ethernet.begin(mac) == 0)
        {
            delay(READ_DELAY);
        }
    }

    //Serial.println(F("DHCP request successful"));
    print_ip(Ethernet.localIP());

    #ifdef PANNAN_SERVER
    server.begin();
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
