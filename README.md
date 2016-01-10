Arduino Furnace Temperature
===========================

Introduction
------------

This is an Arduino project designed to run on an Arduino Ethernet
used by me to measure the temperatures on my furnace using a 1-wire
network.

The 1-wire network is primarily made up of DS18B20 temperature
sensors. But to sense the temperature of the flue gas I'm using
a k-type thermocouple connected to a DS2762 1-wire sensor.

Building
--------

Since I'm not a fan of the arduino IDE and the way it handles libraries,
this project is done using Arduino-cmake (with some fixes missing from upstream)

```bash
git submodule update --init --recursive # Get all libs
mkdir build
cd build
cmake ..
make                # Build all.
make pannan-upload  # Upload firmware.
make pannan-serial  # Open serial port.
```

Note that the DS2762 features and the webserver support for setting
the names of the sensors is too big to fit in the firmware at the
same time, by default DS2762 support is enabled. At least once you
probably want to enable setting the names:

```bash
...
cmake -DPANNAN_NAMES=ON -DPANNAN_DS2762=OFF ..
make pannan-upload  # Build and upload in one go.
```

Or if you prefer you can instead use a purpose made sketch with
a serial port protocol to set the names:

```bash
cmake ..
make setnames-upload
make setnames-serial

# In serial session:
HELP
```

Features
--------

**Webserver**

* Home screen with all temperatures
* Edit names of sensors and save them in EEPROM `http://server/names`
* Output JSON with all sensors and values: `http://server/json`

**HTTP Client**

* Periodically connects to a host and does a HTTP PUT with the json values.

**LCD Screen**

* Support for a 2 line Adafruit LCD screen via software serial.

**DS2762**

* 1-Wire sensor with support for k-type thermocouple.

**DS18B20**

* Main temperatur sensors

