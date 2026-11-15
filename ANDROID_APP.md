# E6-16 Smart — Android Application

## Purpose
Android application for configuring the E6-16 Smart via Bluetooth Low Energy (BLE).

## Features
- BLE connection to ESP32-C3.
- Single threshold slider: 0–200 MOhm, step 10 MOhm.
- Green and red threshold regions.
- Threshold storage in nonvolatile memory (NVS).
- Zero-reference calibration.
- Connection and configuration status indicators.

## Connection
1. Enable Bluetooth and, if required, location services.
2. Hold the Measurement button for approximately 8 seconds to enter BLE mode.
3. Open the Android application and connect to E6-16.

## Configuration
Select the threshold using the slider and save it. The instrument powers off automatically after successful threshold recording.

## Zero calibration
Use the separate zero-calibration command to store the current zero reference.

## Availability
A working Android APK has been tested with the E6-16 Smart prototype. The APK is not yet included in the public GitHub release.
