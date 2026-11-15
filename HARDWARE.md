# E6-16 Smart BETA2 — Hardware

## Main components

- ESP32-C3 Super Mini
- OPA333 measurement amplifier
- TPS63020 3.3 V power supply
- Antiparallel red/green LED pair
- Active buzzer

## ESP32-C3 connections

| GPIO | Function |
| --- | --- |
| GPIO0 | ADC input from OPA333 |
| GPIO4 | LED terminal 1 |
| GPIO5 | LED terminal 2 |
| GPIO6 | Power-enable hold |
| GPIO7 | Active buzzer positive terminal |

## Analog measurement circuit

OPA333 is configured as a non-inverting amplifier with an approximate gain of 40.

- Feedback resistor: 39 kOhm
- Gain-setting resistor: 1 kOhm
- Input low-pass RC filter: 10 kOhm series resistor; 1 uF and 220 nF capacitors in parallel to GND (total 1.22 uF)
- Amplifier supply: 3.3 V
- Amplifier output: ESP32-C3 GPIO0 (ADC)

These values describe the tested prototype configuration, not a complete construction schematic.

### Connection to the E6-16 meter movement

The OPA333 measurement input is connected across the two terminals of the E6-16 analog meter movement, in parallel with the meter movement.

In the tested prototype, the negative terminal of the meter movement is connected to the OPA333 circuit GND. The positive terminal provides the measurement signal through the input filter. Verify polarity and electrical isolation before connecting the amplifier; do not reverse connections experimentally.

## Power supply and enable control

- DC/DC converter: TPS63020
- Battery voltage range: approximately 2.9-4.2 V
- Regulated output: 3.3 V
- Measurement button activates EN through approximately 1 kOhm
- EN pull-down to GND: approximately 10 kOhm
- ESP32-C3 GPIO6 provides power-enable hold for BLE mode

The complete power-enable wiring has not yet been independently verified.

## Documentation status

Complete circuit schematic and component values require verification before publication.

