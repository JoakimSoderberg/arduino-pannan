
#include "pannan.h"
#include "names.h"

int hex2bin(const char *s)
{
    int ret = 0;
    int i;
    for (i = 0; i < 2; i++)
    {
        char c = *s++;
        int n = 0;
        if ('0' <= c && c <= '9')
            n = c - '0';
        else if ('a' <= c && c <= 'f')
            n = 10 + c - 'a';
        else if ('A' <= c && c <= 'F')
            n = 10 + c - 'A';
        ret = n + (ret * 16);
    }
    return ret;
}

int parse_1wire_address(const char *addr_str, DeviceAddress addr)
{
    const char *s = addr_str;
    int i = 0;

    if (strlen(s) != ADDR_SIZE * 2)
    {
        Serial.print(F("\n\nAddress "));
        Serial.print(s);
        Serial.print(F(" must be "));
        Serial.print(ADDR_SIZE * 2);
        Serial.println(F(" characters long"));
        goto fail;
    }

    while (*s && (i < ADDR_SIZE))
    {
        addr[i++] = hex2bin(s);

        if (!s[0] || !s[1])
            goto fail;

        s += 2;
    }

    if (i != ADDR_SIZE)
        goto fail;

    return 0;
fail:
    return -1;
}

void parse_set_cmd()
{
    DeviceAddress addr;
    //String addr_str = Serial.readStringUntil(' ');
    char *addr_str = strtok(NULL, " ");

    if (parse_1wire_address(addr_str, addr) < 0)
    {
        Serial.print("\nERROR Could not parse address: ");
        Serial.println(addr_str);
        //Serial.readStringUntil('\n');
        return;
    }

    //String name = Serial.readStringUntil('\n');
    char *name = strtok(NULL, " ");

    eeprom_add_name(addr, name);
}

void parse_clear_cmd()
{
    eeprom_clear_names();
    Serial.println("OK Cleared EEPROM");
}

void parse_help_cmd()
{
    Serial.println("Commands:");
    Serial.println(" SET <addr> <name>");
    Serial.println(" LIST");
    Serial.println(" CLEAR");
    Serial.println(" HELP\n");
}

void parse_list_cmd()
{
    eeprom_list_names();
}

//SET 1234567891234567 abadb

void parse_serial()
{
    if (Serial.available() > 0)
    {
        String cmd = Serial.readStringUntil(' ');
        cmd.replace("\r", "");

        if (cmd == "SET") parse_set_cmd();
        else if (cmd == "LIST") parse_list_cmd();
        else if (cmd == "CLEAR") parse_clear_cmd();
        else if (cmd == "HELP") parse_help_cmd();
        else if (cmd == " ") ;
        else
        {
            Serial.print("ERROR Unknown cmd: '");
            Serial.print(cmd);
            Serial.print("'");
            Serial.println("");
            Serial.readStringUntil('\n');
            return;
        }
    }
}

void parse_serial2()
{
    #define LINE_MAX 32
    char line[LINE_MAX];

    if (Serial.available() <= 0)
        return;

    int size = Serial.readBytesUntil('\n', line, LINE_MAX);
    line[size] = 0;

    String cmd = strtok(line, " ");
    cmd.replace("\r", "");
    cmd.trim();

    if (cmd == "SET") parse_set_cmd();
    else if (cmd == "LIST") parse_list_cmd();
    else if (cmd == "CLEAR") parse_clear_cmd();
    else if (cmd == "HELP") parse_help_cmd();
    else
    {
        Serial.print("ERROR Unknown cmd: '");
        Serial.print(cmd);
        Serial.print("'");
        Serial.println("");
        Serial.readStringUntil('\n');
        return;
    }
}

void setup()
{
    Serial.begin(9600);

    while (!Serial)
    {
        ; // wait for serial port to connect. Needed for native USB
    }

    Serial.println("Started");
}

void loop()
{
    parse_serial2();
}

