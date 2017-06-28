# ESP8266 AC Controller

This project is an internet connected controller for my home AC which uses a Netatmo thermostat as temperature input.

## Getting Started

I am using an ESP8266 Relay board (the one here: https://ucexperiment.wordpress.com/2016/12/18/yunshan-esp8266-250v-15a-acdc-network-wifi-relay-module/), but any ESP8266 board which has a relay could do, you will only need to make sure you change the RELAY_PIN define to match your boards pin to control the relay.

## Acknowledgments

All my work is created using the following tools:

* ESP8266 Arduino code
* Arduino IDE
* Netatmo SDK
* Wifi Manager from tzapu, great work!!! heavily recommended for any ESP8266 project (https://github.com/tzapu/WiFiManager)
* Arduino Json from bblanchon, good work also (https://github.com/bblanchon/ArduinoJson)
