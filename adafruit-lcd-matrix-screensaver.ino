/*
  Matrix-style falling character screensaver for Adafruit I2C LCD backpack.
  https://learn.adafruit.com/i2c-spi-lcd-backpack

  Tested with a 20x4 character LCD on Arduino Leonardo.

  The circuit:
   * 5V to Arduino 5V pin
   * GND to Arduino GND pin
   * CLK to Analog #5 (SCL)
   * DAT to Analog #4 (SDA)
*/

#include "Adafruit_LiquidCrystal.h"

Adafruit_LiquidCrystal lcd(0);

static const uint8_t LCD_COLS = 20;
static const uint8_t LCD_ROWS = 4;

// Custom glyph indices (HD44780 allows 8 user-defined characters).
static const uint8_t GLYPH_HEAD = 0;
static const uint8_t GLYPH_FADE = 1;

struct RainStream {
  bool active;
  int8_t headRow;
  uint8_t tailLen;
  uint8_t stepTimer;
  uint8_t stepDelay;
  char tailChars[LCD_ROWS];
};

RainStream streams[LCD_COLS];
uint8_t screen[LCD_ROWS][LCD_COLS];

// Characters that evoke the Matrix code rain on a Latin-display LCD.
static const char MATRIX_CHARS[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "@#$%^&*<>[]{}|/\\+=?~";

static const unsigned long FRAME_MS = 45;
static const uint8_t SPAWN_CHANCE = 30;  // Per-column chance out of 1000 each frame.

static char randomMatrixChar() {
  return MATRIX_CHARS[random(sizeof(MATRIX_CHARS) - 1)];
}

static void defineMatrixGlyphs() {
  // Bright leading character.
  const uint8_t head[8] = {
      0b01110,
      0b11111,
      0b11111,
      0b11111,
      0b11111,
      0b11111,
      0b01110,
      0b00000,
  };

  // Softer character directly behind the head.
  const uint8_t fade[8] = {
      0b00000,
      0b00100,
      0b01110,
      0b01110,
      0b01110,
      0b00100,
      0b00000,
      0b00000,
  };

  lcd.createChar(GLYPH_HEAD, head);
  lcd.createChar(GLYPH_FADE, fade);
}

static void resetStream(RainStream &stream) {
  stream.active = false;
  stream.headRow = 0;
  stream.tailLen = 0;
  stream.stepTimer = 0;
  stream.stepDelay = 0;
}

static void spawnStream(RainStream &stream) {
  stream.active = true;
  stream.tailLen = random(3, LCD_ROWS + 1);
  stream.headRow = -(int8_t)random(1, stream.tailLen + 1);
  stream.stepTimer = 0;
  stream.stepDelay = random(1, 5);

  for (uint8_t i = 1; i < stream.tailLen; i++) {
    stream.tailChars[i] = randomMatrixChar();
  }
}

static void stepStream(RainStream &stream) {
  if (!stream.active) {
    return;
  }

  stream.stepTimer++;
  if (stream.stepTimer < stream.stepDelay) {
    return;
  }

  stream.stepTimer = 0;
  stream.headRow++;

  if (stream.headRow - (int8_t)stream.tailLen >= LCD_ROWS) {
    resetStream(stream);
    return;
  }

  // Flicker a random trailing character now and then.
  if (stream.tailLen > 1 && random(3) == 0) {
    uint8_t idx = random(1, stream.tailLen);
    stream.tailChars[idx] = randomMatrixChar();
  }
}

static void updateStreams() {
  for (uint8_t col = 0; col < LCD_COLS; col++) {
    RainStream &stream = streams[col];

    if (!stream.active) {
      if (random(1000) < SPAWN_CHANCE) {
        spawnStream(stream);
      }
      continue;
    }

    stepStream(stream);
  }
}

static void composeFrame() {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    for (uint8_t col = 0; col < LCD_COLS; col++) {
      screen[row][col] = ' ';
    }
  }

  for (uint8_t col = 0; col < LCD_COLS; col++) {
    RainStream &stream = streams[col];
    if (!stream.active) {
      continue;
    }

    for (uint8_t segment = 0; segment < stream.tailLen; segment++) {
      int16_t row = stream.headRow - segment;
      if (row < 0 || row >= LCD_ROWS) {
        continue;
      }

      if (segment == 0) {
        screen[row][col] = GLYPH_HEAD;
      } else if (segment == 1) {
        screen[row][col] = GLYPH_FADE;
      } else {
        screen[row][col] = stream.tailChars[segment];
      }
    }
  }
}

static void renderFrame() {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    lcd.setCursor(0, row);
    for (uint8_t col = 0; col < LCD_COLS; col++) {
      uint8_t cell = screen[row][col];
      if (cell < 8) {
        lcd.write(cell);
      } else {
        lcd.write((char)cell);
      }
    }
  }
}

void setup() {
  randomSeed(analogRead(0));

  if (!lcd.begin(LCD_COLS, LCD_ROWS)) {
    while (1) {
      ;  // Halt if the backpack is not detected.
    }
  }

  lcd.setBacklight(HIGH);
  defineMatrixGlyphs();

  for (uint8_t col = 0; col < LCD_COLS; col++) {
    resetStream(streams[col]);
  }

  // Start with several visible streams so the screen is not blank at boot.
  for (uint8_t col = 0; col < LCD_COLS; col += 2) {
    spawnStream(streams[col]);
    streams[col].headRow = random(0, LCD_ROWS);
  }

  composeFrame();
  renderFrame();
}

void loop() {
  static unsigned long lastFrameMs = 0;
  unsigned long now = millis();

  if (now - lastFrameMs < FRAME_MS) {
    return;
  }
  lastFrameMs = now;

  updateStreams();
  composeFrame();
  renderFrame();
}
