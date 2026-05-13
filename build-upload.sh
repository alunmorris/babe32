#!/bin/bash
/home/alun/.platformio/penv/bin/platformio run --target upload -e jc3248w535c_battery 2>&1
#or ~/.platformio/penv/bin/pio run -e jc3248w535c_battery --target upload --upload-port /dev/ttyACM0 2>&1 | tail -12
