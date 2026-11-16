# HomeMesh

A desk unit for text messaging over a [MeshCore](https://github.com/meshcore-dev/MeshCore) LoRa mesh, without internet or mobile network.

HomeMesh is built for people who are not into radio technology: a clock, incoming messages in full screen, large buttons and an on-screen keyboard. The device works on its own, and the regular MeshCore app can still be connected over Bluetooth.

> **Status:** planning. There is no HomeMesh code in this repository yet.
> This is a personal project. There is no prebuilt firmware.

## Hardware

- Seeed SenseCAP Indicator D1L (ESP32-S3 + RP2040, 3.95" 480×480 touch display, SX1262, USB-C power)

## Planned features

- Home screen with clock, signal status and shortcuts to favourite contacts
- Full-screen notification for messages from favourites, quiet counters for everything else
- Channels to read and write, without notifications
- Night mode with dark theme and dimmed backlight
- Remote commands from favourites (status, pinned note, appointments)
- Wi-Fi firmware update

Setup (contacts, favourites, channels, radio settings) is done in the MeshCore app. The firmware does not create contacts or channels on its own.

## Repository layout

`main` follows upstream MeshCore unchanged. HomeMesh work happens on `dev`.

## License

MIT, see [license.txt](license.txt). Based on MeshCore by Scott Powell and contributors.
