# Wiring: ESP8266 + 16x2 LCD only

No buttons, no resistors, no potentiometer, no extra parts. Six signal
wires plus power. This is the whole bill of materials:

- 1x ESP8266 board (NodeMCU v2 / ESP-12E, USB powered)
- 1x HD44780 16x2 LCD (5 V module)
- Jumper wires

## Pin map (NodeMCU labels and GPIO numbers)

| LCD pin | LCD function | NodeMCU | GPIO |
| --- | --- | --- | --- |
| 1 | VSS (GND) | GND | - |
| 2 | VDD (5 V) | VIN (5 V from USB) | - |
| 3 | V0 (contrast) | GND | - |
| 4 | RS | D1 | GPIO5 |
| 5 | RW | GND | - |
| 6 | E | D2 | GPIO4 |
| 7-10 | D0-D3 | not connected | - |
| 11 | D4 | D5 | GPIO14 |
| 12 | D5 | D6 | GPIO12 |
| 13 | D6 | D7 | GPIO13 |
| 14 | D7 | D0 | GPIO16 |
| 15 | A (backlight +) | VIN (5 V) | - |
| 16 | K (backlight -) | GND | - |

The firmware uses exactly this map (see `src/main.cpp`):

```cpp
LiquidCrystal lcd(5, 4, 14, 12, 13, 16); // RS, E, D4, D5, D6, D7
```

## Why these pins

GPIO 0, 2 and 15 are boot-strapping pins on the ESP8266. Pulling them
the wrong way at power-on stops the board booting, and an LCD data pin
is not a reliable pull-down. The map above avoids all three, so the
board boots every time with nothing but wires attached.

GPIO 16 (D0) is output-only, which is fine here: the LCD is only ever
written to, never read back.

## Contrast without a potentiometer

V0 tied to GND gives maximum contrast. On most 5 V HD44780 modules
this is readable and is the standard no-parts workaround. If your
module shows solid blocks on both rows, it is powered but not
initialised: check RS/E/D4-D7 wiring and that the firmware actually
uploaded, before suspecting contrast. If the text is faint on your
particular module, that module needs a contrast voltage divider, which
is outside the no-extra-parts constraint of this project.

## Power and logic levels

- Power the NodeMCU from USB. VIN then supplies about 5 V to the LCD.
- The ESP8266 drives 3.3 V logic. A 5 V HD44780 recognises 3.3 V as
  HIGH (its input threshold is roughly 2.2 V), so no level shifter is
  needed for this write-only connection.
- Do not power the LCD from the 3.3 V pin: most 16x2 modules are dim
  or blank at 3.3 V.
- Keep the wires short. Long, loose D4-D7 wires are the usual cause
  of garbled characters.

## First power-on

1. Flash the firmware (see README).
2. On first boot with no WiFi saved, the LCD shows the setup
   access point name and then its IP address, alternating.
3. Join that access point from a phone, open `http://192.168.4.1`,
   and enter your home WiFi details.
4. After it connects, the LCD shows its home-network IP for a few
   seconds. Open that IP in a browser for the management page.
