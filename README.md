# ESP Power Consumption Example

## Introduction
This example shows presents various configurations under which power consumption can for a SoC can be measured. This example assumes the following:
- ESP needs to connect to Wi-Fi in STA mode
- ESP has access to a Wi-Fi 6 AP

## Example Configuration
- This example can be configured using the menuconfig by typing `idf.py menuconfig` and then selecting the `Example Configuration` menu option.
- The Wi-Fi SSID and pasphrase must be set before running the application
- Various Power Save configuration are supported in this example such as:
    - Sleep Modes: Light Sleep, Deep Sleep and MODEM sleep
    - Target Wake Time (TWT): Only available if a Wi-Fi 6 AP is used
    - Power Management via Dynamic Frequency Selection (DFS)

## Compiling and running the example
- ESP-IDF v5.1 is a prerequisite to run this demo

## Measuring Power consumption
TBD
