#include <Arduino.h>
#include <U8g2lib.h>
#include "display.h"
#include "logo.h"

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R1);

static constexpr int SCREEN_W = 64;
static constexpr int SCREEN_H = 128;
static constexpr int PROGRESS_H = 5;

#define FONT_BIG    u8g2_font_logisoso28_tr
#define FONT_MEDIUM u8g2_font_logisoso22_tr

void clearDisplay() {
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

void connect_display() {
  u8g2.setBusClock(400000);
  u8g2.begin();
  Serial.println("SH1106 initialized");
}

static void drawProgressBar(int elapsedSeconds) {
  int w = ((elapsedSeconds % 60) * SCREEN_W) / 59;
  if (w > SCREEN_W) w = SCREEN_W;
  u8g2.drawBox(0, 0, w, PROGRESS_H);
}

static void drawCentered(int yBaseline, const char* str, const uint8_t* font) {
  u8g2.setFont(font);
  int w = u8g2.getStrWidth(str);
  int x = (SCREEN_W - w) / 2;
  u8g2.drawStr(x, yBaseline, str);
}

static void drawBatteryIcon(int x, int y) {
  const int bodyW = 9, bodyH = 16, nubW = 5, nubH = 2;
  u8g2.drawBox(x + (bodyW - nubW) / 2, y, nubW, nubH);
  u8g2.drawFrame(x, y + nubH, bodyW, bodyH);
}

void displayTime(String hour, String minute, int elapsedSeconds) {
  static String lastHour = "";
  static String lastMinute = "";
  static int lastElapsedSeconds = -1;

  if (lastHour == hour && lastMinute == minute &&
      lastElapsedSeconds == elapsedSeconds) {
    return;
  }

  u8g2.clearBuffer();
  drawProgressBar(elapsedSeconds);
  drawCentered(60,  hour.c_str(),   FONT_BIG);
  drawCentered(110, minute.c_str(), FONT_BIG);
  u8g2.sendBuffer();

  lastHour = hour;
  lastMinute = minute;
  lastElapsedSeconds = elapsedSeconds;
}

void displayElapsedMinutes(int elapsedSeconds) {
  int elapsedMinutes = elapsedSeconds / 60;
  int progressW = ((elapsedSeconds % 60) * SCREEN_W) / 59;

  static int lastElapsedMinutes = -1;
  static int lastProgressW = -1;

  if (lastElapsedMinutes == elapsedMinutes &&
      lastProgressW == progressW) {
    return;
  }

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", elapsedMinutes);

  const uint8_t* font = (elapsedMinutes >= 100) ? FONT_MEDIUM : FONT_BIG;

  u8g2.clearBuffer();
  drawProgressBar(elapsedSeconds);
  drawCentered(90, buf, font);
  u8g2.sendBuffer();

  lastElapsedMinutes = elapsedMinutes;
  lastProgressW = progressW;
}

void displayBatteryLow() {
  static bool lastShown = false;
  if (lastShown) return;

  u8g2.clearBuffer();
  // Battery body outline (44 wide x 88 tall, centered horizontally)
  u8g2.drawFrame(10, 20, 44, 88);
  // Nub on top
  u8g2.drawBox(24, 14, 16, 6);
  // Low-charge fill at bottom (~⅓ height)
  u8g2.drawBox(14, 74, 36, 30);
  u8g2.sendBuffer();

  lastShown = true;
}

void displaySplash(float batteryPercent, bool syncedRecently, bool forceRedraw) {
  int pct = (int)(batteryPercent + 0.5f);

  static int lastPercent = -1;
  static bool lastSyncedRecently = false;
  if (!forceRedraw && lastPercent == pct && lastSyncedRecently == syncedRecently) {
    return;
  }

  u8g2.clearBuffer();

  // Debug-only signal: a border means the RTC has NOT synced recently, as
  // a stale-clock warning. Deliberately subtle (no text) so it doesn't
  // distract end users, but visible enough to check at a glance during
  // bring-up.
  if (!syncedRecently) {
    u8g2.drawFrame(0, 0, SCREEN_W, SCREEN_H);
  }

  u8g2.drawXBMP((SCREEN_W - LOGO_WIDTH) / 2, 6, LOGO_WIDTH, LOGO_HEIGHT, LOGO_BITS);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", pct);

  u8g2.setFont(FONT_MEDIUM);
  int textW = u8g2.getStrWidth(buf);
  const int iconW = 9, iconGap = 4;
  int startX = (SCREEN_W - (iconW + iconGap + textW)) / 2;

  drawBatteryIcon(startX, 80);
  u8g2.drawStr(startX + iconW + iconGap, 100, buf);

  u8g2.sendBuffer();

  lastPercent = pct;
  lastSyncedRecently = syncedRecently;
}

