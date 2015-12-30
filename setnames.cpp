
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

int parse_1wire_address(String addr_str, DeviceAddress addr)
{
    const char *s = addr_str.c_str();
    int i = 0;

    while (*s && (i < sizeof(addr)))
    {
        addr[i++] = hex2bin(s);

        if (!s[0] || !s[1])
            goto fail;

        s += 2;
    }

    if (i != sizeof(addr))
        goto fail;

    return 0;
fail:
    return -1;
}

int cmd_is_valid(String cmd)
{
    int i;
    const char *valid_cmds[] = {
        "SET",
        "LIST",
        "CLEAR",
        "HELP"
    };

    for (i = 0; i < sizeof(valid_cmds) / sizeof(valid_cmds[0]); i++)
    {
        if (cmd == valid_cmds[i])
            return 1;
    }

    return 0;
}

void parse_set_cmd()
{
    DeviceAddress addr;
    String addr_str = Serial.readStringUntil(' ');

    if (parse_1wire_address(addr_str, addr) < 0)
    {
        Serial.print("ERROR Could not parse address:");
        Serial.println(addr_str);
        Serial.readStringUntil('\n');
        return;
    }

    String name = Serial.readStringUntil('\n');

    add_name(addr, name.c_str());
}

void parse_serial()
{
    if (Serial.available() > 0)
    {
        String cmd = Serial.readStringUntil(' ');

        if (!cmd_is_valid(cmd))
        {
            Serial.print("ERROR Unknown cmd: ");
            Serial.println(cmd);
            Serial.readStringUntil('\n');
            return;
        }

        parse_set_cmd();
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

}


//
// Callback for when serial data is available. 
//
/*void serialEvent()
{
    Serial.read()
}*/
