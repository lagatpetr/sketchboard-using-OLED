# Sketch Board

Draw on your phone or laptop and watch it appear live on a small OLED screen.

The ESP32 creates its own WiFi hotspot and hosts a drawing page. Whatever you draw on the page is sent to the 128x64 OLED display. No internet or router is needed.

Before you draw anything, the OLED shows a startup animation: the title **"Sketch your idea"** with a smiley face that bounces around the screen, leaving a fading dotted trail. The animation stops as soon as the first drawing arrives.

## Features

- Standalone WiFi hotspot (no router needed)
- Large drawing board that fills the screen on phones and laptops
- Tools: Pen, Eraser, Line, Rectangle, Circle, Triangle, Text
- Undo (up to 40 steps) and Clear
- Pen size from 1 to 4 px, filled shapes option
- Invert screen option
- Live updates to the OLED while you draw
- Startup animation with squashing smiley, blinking and a dotted trail

## What you need

- An ESP32 development board
- A 128x64 I2C OLED display, either:
  - SH1106 (usually the 1.3" screen), or
  - SSD1306 (usually the 0.96" screen)
- 4 jumper wires
- A USB cable
- Arduino IDE

## Wiring

| OLED pin | ESP32 pin |
|----------|-----------|
| VCC      | 3.3V      |
| GND      | GND       |
| SDA      | GPIO 21   |
| SCL      | GPIO 22   |

If you use different pins, change this line in `setup()`:

```cpp
Wire.begin(21, 22);   // (SDA, SCL)
```

## Software setup

1. Install the **Arduino IDE**.
2. Add ESP32 support: in **Boards Manager**, search for **esp32** by Espressif Systems and install it.
3. Install the display library: in **Library Manager**, search for **U8g2** (by olikraus) and install it.
4. `WiFi.h`, `WebServer.h` and `Wire.h` come with the ESP32 board package, so there is nothing else to install.

## Upload

1. Open `SketchBoard.ino` in the Arduino IDE. The file must stay inside a folder called `SketchBoard`.
2. Select your board under **Tools > Board** (for example *ESP32 Dev Module*).
3. Select the correct port under **Tools > Port**.
4. Click **Upload**. If it hangs on "Connecting...", hold the **BOOT** button on the ESP32 until the upload starts.

## How to use it

1. Power the ESP32. The OLED shows the bouncing smiley.
2. On your phone or laptop, connect to the WiFi network:
   - Name: `SketchBoard`
   - Password: `sketch1234`
3. Open a browser and go to **http://192.168.4.1**
4. Draw on the board. Your drawing shows on the OLED right away.

If your phone says the network has no internet and tries to switch back to mobile data, choose to stay connected.

### Tools

- **Pen / Eraser:** drag to draw or erase. The eraser is 3 times the pen size.
- **Line:** drag from the start point to the end point.
- **Rectangle / Triangle:** drag from corner to corner.
- **Circle:** drag from the centre outwards.
- **Text:** type your text in the Text panel, choose the **Text** tool, then tap the board where the text should start. Pick Small, Medium or Large next to the text box.
- **Undo / Clear:** step back or wipe the board.
- **Options:** pen size, filled shapes, and invert screen.

## Settings you can change

All settings are near the top of `SketchBoard.ino`.

| Setting | What it does |
|---------|--------------|
| `AP_SSID` | WiFi hotspot name |
| `AP_PASS` | WiFi password (must be at least 8 characters) |
| `USE_SH1106` | `true` for SH1106 (1.3"), `false` for SSD1306 (0.96") |
| `SCREEN_ROTATION` | `U8G2_R0` normal, `U8G2_R2` upside down |
| `ANIM_DELAY_MS` | Startup animation delay (lower = faster) |
| `BALL_SPEED` | Smiley speed (0.5 slow, 1.0 normal, 1.5 fast) |
| `TRAIL_LEN` | Length of the dotted trail |
| `TRAIL_EVERY` | Frames between dots (bigger = more spaced out) |
| `TRAIL_BIG` / `TRAIL_MEDIUM` | How quickly the trail fades |
| `BALL_MIN_X`, `BALL_MAX_X`, `BALL_MIN_Y`, `BALL_MAX_Y` | The invisible walls the smiley bounces off |

To change the size of the drawing board on the web page, edit `--boardMax:1600px` in the CSS inside the page. A smaller number (for example `1200px`) makes the board smaller so the tools are easier to reach without scrolling.

## Troubleshooting

**The screen stays blank**
- Check the wiring (SDA to GPIO 21, SCL to GPIO 22, and 3.3V power).
- Try changing `USE_SH1106` to the other value. Using the wrong chip setting gives a blank or shifted picture.
- Some OLED modules use a different I2C address. Run an I2C scanner sketch to check.

**The picture is shifted or has noise on the sides**
- This is the wrong chip setting. Flip `USE_SH1106`.

**The screen shows "WiFi failed"**
- The hotspot did not start. Make sure `AP_PASS` has at least 8 characters.

**I can't see the WiFi network**
- Open the Serial Monitor at 115200 baud. It prints the network name and the address to open.

**The page does not open**
- Make sure you are connected to the `SketchBoard` network, then open `http://192.168.4.1` (use `http`, not `https`).

**The status in the top corner says "Not connected"**
- Your device lost the hotspot connection. Reconnect to the WiFi and try again.

**The drawing feels slow**
- Stay close to the ESP32 and keep only one device connected while drawing.

## How it works

- The web page keeps the drawing as a 128x64 grid of on/off pixels.
- Each change is turned into 2048 hex characters (1024 bytes) and sent to the ESP32 with a `POST /frame` request.
- The ESP32 converts it back to bytes and draws it on the OLED with U8g2.
- Until the first drawing arrives, the ESP32 runs the startup animation instead.

## Project files

```
SketchBoard/
  SketchBoard.ino
  README.md
```
