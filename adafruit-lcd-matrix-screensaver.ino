/*
  Digital rain screensaver for Adafruit I2C LCD backpack + 20x4 LCD.
  https://learn.adafruit.com/i2c-spi-lcd-backpack
  https://www.adafruit.com/product/198

  Tested target: Arduino Leonardo (ATmega32U4).

  Wiring (Leonardo I2C):
   * 5V  -> 5V
   * GND -> GND
   * DAT -> SDA (digital 2)
   * CLK -> SCL (digital 3)

  Visual approach:
   - Per-column falling heads with decaying trails (phosphor fade).
   - Custom CGRAM glyphs for Matrix-like symbols and trail density.
   - Bright solid-block heads that flicker; trails fall off through
     custom fade levels before clearing.
   - Dirty-row redraws to keep I2C traffic reasonable.
*/

#include "Adafruit_LiquidCrystal.h"

Adafruit_LiquidCrystal lcd(0);

static const uint8_t LCD_COLS = 20;
static const uint8_t LCD_ROWS = 4;

// CGRAM slots
static const uint8_t CG_FADE1 = 0;   // sparsest trail
static const uint8_t CG_FADE2 = 1;
static const uint8_t CG_FADE3 = 2;
static const uint8_t CG_FADE4 = 3;   // densest soft block
static const uint8_t CG_SYM0 = 4;
static const uint8_t CG_SYM1 = 5;
static const uint8_t CG_SYM2 = 6;
static const uint8_t CG_SYM3 = 7;

// Intensity: 0 empty .. HEAD_LEVEL bright head
static const uint8_t HEAD_LEVEL = 7;
static const uint8_t MAX_TRAIL = 6;  // cells of glow behind the head

static const unsigned long FRAME_MS = 55;
static const uint8_t SPAWN_CHANCE = 55;   // out of 1000, per idle column / frame
static const uint8_t GLITCH_CHANCE = 4;   // out of 1000, rare full-column flicker
static const uint8_t FLICKER_CHANCE = 18; // out of 100, trail glyph reshuffle

// HD44780 A00 ROM glyphs that survive Latin LCD fonts reasonably well.
// (No backslash/tilde — those remap to yen / arrow on many modules.)
static const char ROM_CHARS[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "@#$%&*<>[]{}|/+=?";

struct Column {
  bool raining;
  int8_t headRow;      // row of the bright head; may be < 0 while entering
  uint8_t stepTimer;
  uint8_t stepDelay;   // frames between advances (1 = fastest)
  uint8_t trailLen;    // how far intensity is stamped behind the head
  uint8_t cooldown;    // frames to wait after a stream exits
};

Column columns[LCD_COLS];
uint8_t intensity[LCD_ROWS][LCD_COLS];
char glyphs[LCD_ROWS][LCD_COLS];
char displayBuf[LCD_ROWS][LCD_COLS];
bool rowDirty[LCD_ROWS];

// --- custom glyphs (5x8) -------------------------------------------------

static const uint8_t GLYPH_FADE1[8] = {
    0b00000,
    0b00000,
    0b00100,
    0b00000,
    0b00000,
    0b00100,
    0b00000,
    0b00000,
};

static const uint8_t GLYPH_FADE2[8] = {
    0b00000,
    0b01010,
    0b00000,
    0b00100,
    0b00000,
    0b01010,
    0b00000,
    0b00000,
};

static const uint8_t GLYPH_FADE3[8] = {
    0b00100,
    0b01010,
    0b10101,
    0b01010,
    0b00100,
    0b01010,
    0b10101,
    0b01010,
};

static const uint8_t GLYPH_FADE4[8] = {
    0b01110,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b11111,
    0b01110,
};

// Abstract "code" marks — denser, more digital-rain than Latin letters.
static const uint8_t GLYPH_SYM0[8] = {
    0b10001,
    0b01010,
    0b00100,
    0b01010,
    0b10001,
    0b00100,
    0b01010,
    0b10001,
};

static const uint8_t GLYPH_SYM1[8] = {
    0b00100,
    0b00100,
    0b11111,
    0b00100,
    0b00100,
    0b01110,
    0b10101,
    0b00100,
};

static const uint8_t GLYPH_SYM2[8] = {
    0b11111,
    0b10001,
    0b10101,
    0b10001,
    0b11111,
    0b00100,
    0b01010,
    0b10001,
};

static const uint8_t GLYPH_SYM3[8] = {
    0b00000,
    0b01110,
    0b10001,
    0b10101,
    0b10001,
    0b01110,
    0b00100,
    0b00100,
};

static void loadCustomChars() {
  lcd.createChar(CG_FADE1, const_cast<uint8_t *>(GLYPH_FADE1));
  lcd.createChar(CG_FADE2, const_cast<uint8_t *>(GLYPH_FADE2));
  lcd.createChar(CG_FADE3, const_cast<uint8_t *>(GLYPH_FADE3));
  lcd.createChar(CG_FADE4, const_cast<uint8_t *>(GLYPH_FADE4));
  lcd.createChar(CG_SYM0, const_cast<uint8_t *>(GLYPH_SYM0));
  lcd.createChar(CG_SYM1, const_cast<uint8_t *>(GLYPH_SYM1));
  lcd.createChar(CG_SYM2, const_cast<uint8_t *>(GLYPH_SYM2));
  lcd.createChar(CG_SYM3, const_cast<uint8_t *>(GLYPH_SYM3));
}

static char randomRainGlyph() {
  // Bias toward custom symbols so the rain feels less like plain text.
  uint8_t roll = random(100);
  if (roll < 45) {
    return (char)random(CG_SYM0, CG_SYM3 + 1);
  }
  return ROM_CHARS[random(sizeof(ROM_CHARS) - 1)];
}

static char glyphForIntensity(uint8_t level, char stored) {
  switch (level) {
    case 0:
      return ' ';
    case 1:
      return (char)CG_FADE1;
    case 2:
      return (char)CG_FADE2;
    case 3:
      return (char)CG_FADE3;
    case 4:
      return (char)CG_FADE4;
    case 5:
    case 6:
      return stored;
    default:
      // Head: solid block (0xFF on HD44780) reads as the bright lead.
      return (char)0xFF;
  }
}

static void markRowDirty(uint8_t row) {
  rowDirty[row] = true;
}

static void setCell(uint8_t row, uint8_t col, uint8_t level, char glyph) {
  if (intensity[row][col] == level && glyphs[row][col] == glyph) {
    return;
  }
  intensity[row][col] = level;
  glyphs[row][col] = glyph;
  markRowDirty(row);
}

static void clearColumnState(uint8_t col) {
  columns[col].raining = false;
  columns[col].headRow = -1;
  columns[col].stepTimer = 0;
  columns[col].stepDelay = 1;
  columns[col].trailLen = 3;
  columns[col].cooldown = 0;
}

static void spawnColumn(uint8_t col) {
  Column &c = columns[col];
  c.raining = true;
  c.trailLen = random(3, MAX_TRAIL + 1);
  c.headRow = -(int8_t)random(0, 2);  // sometimes starts just above
  c.stepTimer = 0;
  // Weighted toward medium/fast so a 4-row panel stays lively.
  c.stepDelay = random(1, 4);
  c.cooldown = 0;
}

static void decayColumn(uint8_t col) {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    uint8_t level = intensity[row][col];
    if (level == 0) {
      continue;
    }

    // Soft phosphor decay — not a hard clear behind the head.
    uint8_t next = level - 1;
    if (next == 0) {
      setCell(row, col, 0, ' ');
    } else if (next <= 4) {
      // Fade stages ignore letter identity.
      setCell(row, col, next, (char)(CG_FADE1 + (next - 1)));
    } else if (level == HEAD_LEVEL || random(100) < FLICKER_CHANCE) {
      // Leaving the solid head (or flickering) picks a rain glyph.
      setCell(row, col, next, randomRainGlyph());
    } else {
      setCell(row, col, next, glyphs[row][col]);
    }
  }
}

static void stampHead(uint8_t col, int8_t headRow) {
  if (headRow < 0 || headRow >= LCD_ROWS) {
    return;
  }

  // Bright leading edge; trail is whatever remains above after decay.
  setCell((uint8_t)headRow, col, HEAD_LEVEL, (char)0xFF);
}

static void advanceColumn(uint8_t col) {
  Column &c = columns[col];

  decayColumn(col);

  c.headRow++;
  stampHead(col, c.headRow);

  // Stream is done once the head and its trail have left the bottom.
  if (c.headRow - (int8_t)c.trailLen >= LCD_ROWS) {
    c.raining = false;
    c.cooldown = random(2, 12);
  }
}

static void glitchColumn(uint8_t col) {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    setCell(row, col, (uint8_t)random(4, HEAD_LEVEL + 1), randomRainGlyph());
  }
}

static void updateRain() {
  for (uint8_t col = 0; col < LCD_COLS; col++) {
    Column &c = columns[col];

    if (c.cooldown > 0) {
      c.cooldown--;
      if ((c.cooldown & 1) == 0) {
        decayColumn(col);
      }
      continue;
    }

    if (!c.raining) {
      // Idle columns still let leftover phosphor fade out.
      if (c.stepTimer++ >= 2) {
        c.stepTimer = 0;
        decayColumn(col);
      }
      if (random(1000) < SPAWN_CHANCE) {
        spawnColumn(col);
      }
      continue;
    }

    c.stepTimer++;
    if (c.stepTimer >= c.stepDelay) {
      c.stepTimer = 0;
      advanceColumn(col);
    } else if (c.headRow >= 0 && c.headRow < LCD_ROWS) {
      // Between steps: hold intensity, flicker glyphs for life.
      setCell((uint8_t)c.headRow, col, HEAD_LEVEL, (char)0xFF);
      if (c.headRow > 0 && random(3) == 0) {
        uint8_t trailRow = (uint8_t)(c.headRow - 1);
        if (intensity[trailRow][col] >= 5) {
          setCell(trailRow, col, intensity[trailRow][col], randomRainGlyph());
        }
      }
    }

    if (random(1000) < GLITCH_CHANCE) {
      glitchColumn(col);
    }
  }
}

static void composeDisplay() {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    for (uint8_t col = 0; col < LCD_COLS; col++) {
      char ch = glyphForIntensity(intensity[row][col], glyphs[row][col]);
      if (displayBuf[row][col] != ch) {
        displayBuf[row][col] = ch;
        rowDirty[row] = true;
      }
    }
  }
}

static void renderDirtyRows() {
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    if (!rowDirty[row]) {
      continue;
    }
    rowDirty[row] = false;

    lcd.setCursor(0, row);
    for (uint8_t col = 0; col < LCD_COLS; col++) {
      lcd.write(displayBuf[row][col]);
    }
  }
}

static void seedOpeningBurst() {
  // Immediate activity so boot does not look empty.
  for (uint8_t col = 0; col < LCD_COLS; col += 2) {
    spawnColumn(col);
    columns[col].headRow = (int8_t)random(0, LCD_ROWS);
    columns[col].stepDelay = random(1, 3);
    stampHead(col, columns[col].headRow);

    // Prefill a short decaying trail above the head.
    for (int8_t t = 1; t <= (int8_t)columns[col].trailLen; t++) {
      int8_t row = columns[col].headRow - t;
      if (row < 0) {
        break;
      }
      uint8_t level = (uint8_t)constrain(HEAD_LEVEL - t, 1, 6);
      setCell((uint8_t)row, col, level, randomRainGlyph());
    }
  }
}

void setup() {
  // Mix analog noise with a timer crumb; Leonardo's A0 floats enough to help.
  randomSeed(analogRead(A0) ^ (micros() * 2654435761UL));

  if (!lcd.begin(LCD_COLS, LCD_ROWS)) {
    while (1) {
      ;  // Backpack not found — halt.
    }
  }

  lcd.setBacklight(HIGH);
  lcd.clear();
  loadCustomChars();

  for (uint8_t col = 0; col < LCD_COLS; col++) {
    clearColumnState(col);
  }
  for (uint8_t row = 0; row < LCD_ROWS; row++) {
    rowDirty[row] = true;
    for (uint8_t col = 0; col < LCD_COLS; col++) {
      intensity[row][col] = 0;
      glyphs[row][col] = ' ';
      displayBuf[row][col] = ' ';
    }
  }

  seedOpeningBurst();
  composeDisplay();
  renderDirtyRows();
}

void loop() {
  static unsigned long lastFrameMs = 0;
  unsigned long now = millis();

  if (now - lastFrameMs < FRAME_MS) {
    return;
  }
  lastFrameMs = now;

  updateRain();
  composeDisplay();
  renderDirtyRows();
}
