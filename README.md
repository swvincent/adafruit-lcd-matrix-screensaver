# Adafruit LCD Matrix Screensaver

Digital-rain style screensaver for a **20×4 character LCD** driven over **I2C** with an [Adafruit LCD backpack](https://learn.adafruit.com/i2c-spi-lcd-backpack). Falling “code” streams use bright heads, phosphor-style fading trails, custom CGRAM glyphs, and occasional flicker/glitches.

Built and tested on an **Arduino Leonardo**.

Created with **Cursor** and **Grok** AI as a learning exercise.

## Hardware

| Part | Notes |
|------|--------|
| [Standard LCD 20×4 (white on blue)](https://www.adafruit.com/product/198) | HD44780-compatible |
| [I2C/SPI character LCD backpack](https://www.adafruit.com/product/292) | STEMMA QT / Qwiic friendly |
| Arduino Leonardo | Other AVR boards with I2C should work; update wiring if pins differ |

### Wiring (Leonardo)

| Backpack | Leonardo |
|----------|----------|
| 5V | 5V |
| GND | GND |
| DAT (SDA) | Digital **2** (SDA) |
| CLK (SCL) | Digital **3** (SCL) |

Default I2C address is `0` (backpack jumpers unset), matching `Adafruit_LiquidCrystal lcd(0)` in the sketch.

## Software

1. Install the **Adafruit LiquidCrystal** library (Library Manager → search “Adafruit LiquidCrystal”, or [GitHub](https://github.com/adafruit/Adafruit_LiquidCrystal)).
2. Open `adafruit-lcd-matrix-screensaver.ino` in the Arduino IDE or Arduino CLI.
3. Select **Arduino Leonardo** as the board, pick the correct port, and upload.

## How it looks

- Per-column rain with a solid-block head and decaying trail
- Mix of HD44780-safe ROM characters and custom Matrix-like symbols
- Trail brightness falls through custom fade glyphs before clearing
- Only changed rows are rewritten over I2C to keep updates snappy

## Tuning

Constants near the top of the sketch:

| Constant | Role |
|----------|------|
| `FRAME_MS` | Frame period (lower = faster) |
| `SPAWN_CHANCE` | How often idle columns start raining (out of 1000) |
| `FLICKER_CHANCE` | How often trail glyphs reshuffle (out of 100) |
| `GLITCH_CHANCE` | Rare full-column flash (out of 1000) |
| `MAX_TRAIL` | Soft limit on trail glow length |

For a different panel size, change `LCD_COLS` / `LCD_ROWS` and expect to retune spawn rate and timing—four rows fill quickly, so density is tuned for 20×4.

## References

- [Adafruit I2C/SPI LCD backpack guide](https://learn.adafruit.com/i2c-spi-lcd-backpack)
- [Adafruit 20×4 LCD product page](https://www.adafruit.com/product/198)
