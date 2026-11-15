# E6-16 Smart BETA2 — Flashing Instructions

Target: ESP32-C3

## Firmware files

Download `E6-16_Smart_BETA2_Firmware.zip` from the GitHub release and extract it.

The archive contains:
- bootloader.bin
- partition-table.bin
- e6_16_smart.bin

## Flash using esptool

Install esptool if necessary:

```bash
python3 -m pip install esptool
```


## Flash firmware

Extract E6-16_Smart_BETA2_Firmware.zip.

Run from the directory containing the extracted files:

    python3 -m esptool --chip esp32c3 -p /dev/ttyACM0 write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 e6_16_smart.bin

Replace /dev/ttyACM0 with your serial port.

## Build from source

Requires ESP-IDF 5.2.2.

    idf.py build
    idf.py -p /dev/ttyACM0 flash

## Safety

Use only with the documented E6-16 Smart hardware.
Never connect ESP32-C3 GPIO directly to the high-voltage measurement circuit.
