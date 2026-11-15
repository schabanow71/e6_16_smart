# E6-16 Smart

**BETA2 — 15 November 2026**

An experimental ESP32-C3-based threshold indicator for the analog E6-16 insulation resistance tester. It adds a two-color LED indication, an audible warning for low insulation resistance, and Bluetooth Low Energy (BLE) configuration. The original analog meter remains the primary measuring instrument.

## Features

- Configurable insulation-resistance threshold: **0–200 MΩ**, in **10 MΩ** steps; default **180 MΩ**.
- **Green** steady indication above the threshold; **red** steady indication below it.
- Alternating red/green indication when the decision remains ambiguous after the maximum waiting time.
- Adaptive decision timing, approximately **0.8–3 seconds** after the measurement decision window begins.
- Repeating triple-beep warning for a calculated resistance of **0–10 MΩ** after the decision process completes.
- BLE threshold setting and zero-point calibration, saved in ESP32 nonvolatile storage (NVS).
- Automatic ADC attenuation selection and power-hold control.

## Hardware

The design uses an **ESP32-C3 Super Mini**, an **OPA333** signal amplifier, a two-wire antiparallel red/green LED pair, an active buzzer, and a regulated **3.3 V** supply. The ESP32-C3 reads an amplified low-voltage measurement signal; it must not be connected directly to the megohmmeter's high-voltage output.

### ESP32-C3 pin assignments

| Pin | Connection / role |
| --- | --- |
| GPIO0 | ADC1 channel 0: amplified signal from OPA333 |
| GPIO4 | One terminal of the antiparallel red/green LED pair |
| GPIO5 | Other terminal of the antiparallel red/green LED pair |
| GPIO6 | Self-hold control for the power-enable circuit |
| GPIO7 | Active buzzer positive terminal; buzzer negative terminal to GND |
| GND | Common low-voltage circuit reference |

LED color is selected by driving GPIO4/GPIO5 with opposite polarity. The GPIO7 output is initialized low and has a software-enabled pull-down; an occasional short chirp during startup remains a known limitation. A proposed external pull-down has **not** yet been validated.

**Analog circuit note:** The complete amplifier schematic, component values, power-enable wiring, and physical construction drawings are not included in this release document and should be independently verified before reproducing the hardware.

## Measurement and decision logic

The firmware samples the **12-bit ESP32-C3 ADC**, averaging **64 samples** per acquisition cycle. It uses the ESP-IDF ADC curve-fitting calibration and switches between **0 dB and 11 dB attenuation** according to signal level.

Resistance is calculated using a **21-point calibration table** at 0, 10, ... 200 MΩ, with linear interpolation between points. An optional saved zero-point measurement scales subsequent voltage readings.

The adaptive decision algorithm considers the last three voltage readings and their distance from the threshold voltage:

| Earliest elapsed time | Conditions for a confirmed result |
| --- | --- |
| 800 ms | Three-reading spread ≤10 mV; all readings >30 mV away from the threshold |
| 2000 ms | Three-reading spread ≤3 mV; all readings >2 mV away from the threshold |
| 3000 ms | Deadline: confirm if sufficiently stable and separated; otherwise alternate red and green |

While awaiting confirmation, the LED emits short green flashes. The algorithm uses the calibrated threshold voltage, adjusted for the saved zero reference where available.

### Audible indication

When the decision process has completed and calculated resistance is **0–10 MΩ**, the active buzzer repeats three approximately **30 ms** beeps separated by **30 ms** gaps, followed by a **100 ms** pause. In the current implementation, this condition also includes the ambiguous-decision state: the beep is not an independent guarantee of a stable threshold decision.

## Bluetooth Low Energy configuration

Holding the **Measurement** button for approximately **8 seconds** enables the ESP32-C3 power hold and starts BLE configuration mode. Advertising name: **`E6-16`**. BLE stack: **NimBLE**.

**Primary GATT service:** `0xFFF0`

| Characteristic | Access | Value |
| --- | --- | --- |
| `0xFFF1` | Read | Current threshold: 4-byte signed integer, little-endian, in MΩ |
| `0xFFF2` | Write | New threshold: 4-byte signed integer, little-endian, 0–200 in steps of 10 |
| `0xFFF3` | Write | Save current zero reference: single byte `0x01` |

Threshold and zero reference are stored in NVS. A successful threshold write requests power-off after a short delay (approximately one second). BLE mode also requests power-off after **120 seconds**. Saving the zero reference alone does not request immediate power-off.

## Firmware and build

- **Target:** ESP32-C3
- **Framework:** ESP-IDF **5.2.2**
- **Language:** C
- **Runtime:** FreeRTOS
- **BLE:** NimBLE
- **Main source files:** `main/main.c`, `main/ble_config.c`, `main/ble_config.h`
- **Partition table:** `partitions.csv` (custom)

Activate the ESP-IDF 5.2.2 environment and run from the project root:

```bash
idf.py build
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor
```

Replace `/dev/ttyACM0` with the serial port for your board. The published source copy is named `e6_16_smart`; its firmware build should be tested independently before distribution as a precompiled binary.

## Release status and known limitations

**BETA2** was functionally exercised on a physical E6-16 for approximately **30 minutes** without reported functional problems.

- A short buzzer chirp can occur during power-up.
- The amplifier schematic and Android configuration app are not yet included in this source release.
- This is a prototype modification, not a certified insulation tester or a substitute for the original meter's safety and measurement specifications.

## Safety

The E6-16 is a high-voltage insulation tester. Work on its internal circuitry must be performed only by qualified personnel using appropriate safety procedures. Do not connect ESP32-C3 pins directly to high-voltage measurement nodes.

## Firmware installation

See [Flashing Instructions](FLASHING.md) for firmware download, installation and building from source.

## Hardware documentation

See [Hardware Documentation](HARDWARE.md) for components, GPIO assignments, amplifier and power supply.

## Android application

See [Android Application](ANDROID_APP.md) for BLE connection, threshold configuration and zero calibration.
