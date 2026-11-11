/**
 * @file lcd.cpp
 * @brief LCD driver implementation for the ST7735 0.96" 160x80 display
 *        driven by the TFT_eSPI library.
 *
 * All TFT_eSPI configuration is supplied via build flags in platformio.ini:
 *   USER_SETUP_LOADED=1   – tells the lib to skip its own User_Setup.h
 *   ST7735_DRIVER=1       – selects the ST7735 driver
 *   ST7735_GREENTAB160x80 – 160×80 variant (BGR, inverted, 26 px offset)
 *   TFT_WIDTH=80          – logical width  (portrait origin; lib transposes)
 *   TFT_HEIGHT=160        – logical height
 *   TFT_MOSI=3            – SDA
 *   TFT_SCLK=5            – SCL
 *   TFT_CS=4              – chip select
 *   TFT_DC=2              – data/command
 *   TFT_RST=1             – hardware reset
 *   TFT_BL=38             – backlight enable (active LOW)
 *   LOAD_GFXFF=1          – enable GFX free-font support (FreeSans9pt7b)
 *   SPI_FREQUENCY=27000000
 */

#include "lcd.h"
#include "local_dashboard.h"
#include "pro_display.h"
#include "pro_lcd_v2.h"
#include "pro_setup_text.h"
#include "usb_storage.h"
#include <WiFi.h>
#include <algorithm>
#include <ctime>
#include "setup_network.h"
#include "setup_access.h"

#include <Arduino.h>
#include <TFT_eSPI.h> // Bodmer's TFT_eSPI library

// FreeSans9pt7b is already included transitively by TFT_eSPI.h via
// Fonts/GFXFF/gfxfont.h (line 52) when LOAD_GFXFF=1 is defined.
// Do NOT include it again here — that causes redefinition errors.

static const unsigned char PROGMEM image_hourglass1_bits[] = {
    0x00, 0x00, 0x00, 0x07, 0xff, 0xf0, 0x04, 0x00, 0x10, 0x03, 0xff, 0xe0,
    0x01, 0x00, 0x40, 0x01, 0x00, 0x40, 0x01, 0x7f, 0x40, 0x01, 0x3e, 0x40,
    0x00, 0x9c, 0x80, 0x00, 0x49, 0x00, 0x00, 0x22, 0x00, 0x00, 0x14, 0x00,
    0x00, 0x14, 0x00, 0x00, 0x22, 0x00, 0x00, 0x49, 0x00, 0x00, 0x80, 0x80,
    0x01, 0x08, 0x40, 0x01, 0x3e, 0x40, 0x01, 0x7f, 0x40, 0x01, 0x00, 0x40,
    0x03, 0xff, 0xe0, 0x04, 0x00, 0x10, 0x07, 0xff, 0xf0, 0x00, 0x00, 0x00};
static const unsigned char PROGMEM image_check_contour_bits[] = {
    0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x33, 0x00,
    0x00, 0x00, 0x33, 0x00, 0x0c, 0x00, 0xc0, 0xc0, 0x0c, 0x00, 0xc0, 0xc0,
    0x33, 0x03, 0x03, 0x00, 0x33, 0x03, 0x03, 0x00, 0xc0, 0xcc, 0x0c, 0x00,
    0xc0, 0xcc, 0x0c, 0x00, 0x30, 0x30, 0x30, 0x00, 0x30, 0x30, 0x30, 0x00,
    0x0c, 0x00, 0xc0, 0x00, 0x0c, 0x00, 0xc0, 0x00, 0x03, 0x03, 0x00, 0x00,
    0x03, 0x03, 0x00, 0x00, 0x00, 0xcc, 0x00, 0x00, 0x00, 0xcc, 0x00, 0x00,
    0x00, 0x30, 0x00, 0x00, 0x00, 0x30, 0x00, 0x00};
static const unsigned char PROGMEM image_cross_contour_bits[] = {
    0x0c, 0x00, 0xc0, 0x0c, 0x00, 0xc0, 0x33, 0x03, 0x30, 0x33, 0x03,
    0x30, 0xc0, 0xcc, 0x0c, 0xc0, 0xcc, 0x0c, 0x30, 0x30, 0x30, 0x30,
    0x30, 0x30, 0x0c, 0x00, 0xc0, 0x0c, 0x00, 0xc0, 0x03, 0x03, 0x00,
    0x03, 0x03, 0x00, 0x0c, 0x00, 0xc0, 0x0c, 0x00, 0xc0, 0x30, 0x30,
    0x30, 0x30, 0x30, 0x30, 0xc0, 0xcc, 0x0c, 0xc0, 0xcc, 0x0c, 0x33,
    0x03, 0x30, 0x33, 0x03, 0x30, 0x0c, 0x00, 0xc0, 0x0c, 0x00, 0xc0};
static const unsigned char PROGMEM image_wifi_bits[] = {
    0x01, 0xf0, 0x00, 0x06, 0x0c, 0x00, 0x18, 0x03, 0x00, 0x21, 0xf0, 0x80,
    0x46, 0x0c, 0x40, 0x88, 0x02, 0x20, 0x10, 0xe1, 0x00, 0x23, 0x18, 0x80,
    0x04, 0x04, 0x00, 0x08, 0x42, 0x00, 0x01, 0xb0, 0x00, 0x02, 0x08, 0x00,
    0x00, 0x40, 0x00, 0x00, 0xa0, 0x00, 0x00, 0x40, 0x00};

// Dashboard
static const unsigned char PROGMEM image_cloud_1_bits[] = {
    0x07, 0xc0, 0x00, 0x08, 0x20, 0x00, 0x10, 0x10, 0x00,
    0x30, 0x08, 0x00, 0x40, 0x0e, 0x00, 0x80, 0x01, 0x00,
    0x80, 0x00, 0x80, 0x40, 0x00, 0x80, 0x3f, 0xff, 0x00};
static const unsigned char PROGMEM image_cloud_sync_bits[] = {
    0x00, 0x00, 0x00, 0x07, 0xc0, 0x00, 0x08, 0x20, 0x00, 0x10, 0x10, 0x00,
    0x30, 0x08, 0x00, 0x40, 0x0e, 0x00, 0x80, 0x01, 0x00, 0x82, 0x20, 0x80,
    0x47, 0x20, 0x80, 0x2f, 0xaf, 0x00, 0x02, 0x20, 0x00, 0x02, 0xf8, 0x00,
    0x02, 0x70, 0x00, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char PROGMEM image_folder_explorer_bits[] = {
    0x3c, 0x00, 0x00, 0x42, 0x00, 0x00, 0x81, 0xff, 0x00, 0x80, 0x00, 0x80,
    0xbf, 0xfe, 0x80, 0xc0, 0x01, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00, 0x80,
    0x80, 0x00, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00, 0x80,
    0x80, 0x00, 0x80, 0x80, 0x00, 0x80, 0x80, 0x00, 0x80, 0x7f, 0xff, 0x00};
static const unsigned char PROGMEM image_internet_bits[] = {
    0x03, 0xc0, 0x0d, 0xb0, 0x32, 0x4c, 0x24, 0x24, 0x44, 0x22, 0x7f,
    0xfe, 0x88, 0x11, 0x88, 0x11, 0x88, 0x11, 0x88, 0x11, 0x7f, 0xfe,
    0x44, 0x22, 0x24, 0x24, 0x32, 0x4c, 0x0d, 0xb0, 0x03, 0xc0};
static const unsigned char PROGMEM image_menu_information_sign_white_bits[] = {
    0x07, 0xc0, 0x18, 0x30, 0x23, 0x08, 0x42, 0x84, 0x43, 0x04, 0x80,
    0x02, 0x83, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x82, 0x42, 0x84,
    0x43, 0x84, 0x20, 0x08, 0x18, 0x30, 0x07, 0xc0, 0x00, 0x00};
static const unsigned char PROGMEM image_micro_sd_bits[] = {
    0x3f, 0xc0, 0x60, 0x20, 0x40, 0x20, 0x40, 0x20, 0x47, 0x30, 0x48,
    0x90, 0x4f, 0x88, 0x40, 0x08, 0x49, 0x10, 0x4a, 0x90, 0x44, 0x88,
    0x40, 0x08, 0x47, 0x88, 0x58, 0x68, 0x37, 0xb0, 0x00, 0x00};
static const unsigned char PROGMEM image_refresh_1_bits[] = {
    0x01, 0xe0, 0x7c, 0x38, 0x1c, 0x0c, 0x3c, 0x0c, 0x74,
    0x06, 0x64, 0x06, 0x60, 0x06, 0x60, 0x26, 0x60, 0x2e,
    0x30, 0x3c, 0x30, 0x38, 0x18, 0x3e, 0x07, 0x00};
static const unsigned char PROGMEM image_refresh_bits[] = {
    0x00, 0x00, 0x0f, 0x90, 0x3f, 0xd0, 0x70, 0xf0, 0x40, 0x70, 0xc1,
    0xf0, 0x80, 0x00, 0x80, 0x00, 0x80, 0x08, 0x00, 0x08, 0x7c, 0x08,
    0x70, 0x10, 0x78, 0x70, 0x5f, 0xe0, 0x4f, 0x80, 0x00, 0x00};
static const unsigned char PROGMEM image_wifi_2_bars_bits[] = {
    0x01, 0xf0, 0x00, 0x06, 0x0c, 0x00, 0x18, 0x03, 0x00, 0x21, 0xf0, 0x80,
    0x46, 0x0c, 0x40, 0x88, 0x02, 0x20, 0x10, 0xe1, 0x00, 0x23, 0x18, 0x80,
    0x04, 0x04, 0x00, 0x08, 0x42, 0x00, 0x01, 0xf0, 0x00, 0x03, 0xb8, 0x00,
    0x01, 0x50, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00};
static const unsigned char PROGMEM image_wifi_50_bits[] = {
    0x01, 0xf0, 0x00, 0x06, 0x0c, 0x00, 0x18, 0x03, 0x00, 0x21, 0xf0, 0x80,
    0x46, 0x0c, 0x40, 0x88, 0x02, 0x20, 0x50, 0xe1, 0x40, 0x23, 0xf8, 0x80,
    0x17, 0x1d, 0x00, 0x0e, 0xee, 0x00, 0x05, 0xf4, 0x00, 0x03, 0xb8, 0x00,
    0x01, 0x50, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00};
static const unsigned char PROGMEM image_wifi_full_bits[] = {
    0x01, 0xf0, 0x00, 0x07, 0xfc, 0x00, 0x1e, 0x0f, 0x00, 0x39, 0xf3, 0x80,
    0x77, 0xfd, 0xc0, 0xef, 0x1e, 0xe0, 0x5c, 0xe7, 0x40, 0x3b, 0xfb, 0x80,
    0x17, 0x1d, 0x00, 0x0e, 0xee, 0x00, 0x05, 0xf4, 0x00, 0x03, 0xb8, 0x00,
    0x01, 0x50, 0x00, 0x00, 0xe0, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00};

// ---------------------------------------------------------------------------
// TFT object — constructed once, zero-argument (all config via build flags)
// ---------------------------------------------------------------------------
static TFT_eSPI tft;
// Any other screen invalidates the cached pro frame, including a plain clear.
static bool proRedraw=true;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void lcd_init() {
  proRedraw=true;
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW); // Active-low backlight on T-Dongle-S3

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
}

void lcd_show_success(const char *message) {
  proRedraw=true;

  tft.fillScreen(0x0);
  // Loading copy 1
  tft.setTextColor(0xFFFF);
  tft.setTextSize(1);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString(message, 11, 38);
  // check_contour
  tft.drawBitmap(67, 12, image_check_contour_bits, 26, 20, 0x4D6A);
}

void lcd_show_error(const char *message) {
  proRedraw=true;

  tft.fillScreen(0x0);
  // Loading copy 1
  tft.setTextColor(0xFFFF);
  tft.setTextSize(1);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString(message, 31, 38);
  // cross_contour
  tft.drawBitmap(69, 12, image_cross_contour_bits, 22, 22, 0xF206);
}

void lcd_show_state(const char *state) {
  proRedraw=true;
  tft.fillScreen(0x0000);

  if (strcmp(state, "loading") == 0) {
    tft.drawBitmap(15, 28, image_hourglass1_bits, 24, 24, 0xFFFF);
    tft.setTextColor(0xFFFF);
    tft.setTextSize(1);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString("Starting...", 50, 33);
  } else if (strcmp(state, "wifi_connecting") == 0) {
    tft.setTextColor(0xFFFF);
    tft.setTextSize(1);
    tft.setFreeFont(&FreeSans9pt7b);
    tft.drawString("Connecting...", 27, 38);
    // wifi
    tft.drawBitmap(71, 16, image_wifi_bits, 19, 15, 0xFFE7);
  } else {
    lcd_clear();
  }
}

static DashboardState prev_state = {-1, false, false, false, false, 0, 0, 0, 0, 0, 0, -1, {0}};

void format_speed(uint32_t speed_kbps, char *buf, size_t buf_size) {
  if (speed_kbps < 1000) {
    snprintf(buf, buf_size, "%lu kb/s", speed_kbps);
  } else {
    snprintf(buf, buf_size, "%lu mb/s", speed_kbps / 1000);
  }
}

void lcd_dashboard_init() {
  lcd_clear();

  // Static elements
  tft.drawBitmap(3, 24, image_cloud_sync_bits, 17, 16, 0xFFFF);
  tft.drawBitmap(3, 43, image_folder_explorer_bits, 17, 16, 0xFFFF);
  tft.drawBitmap(4, 63, image_menu_information_sign_white_bits, 15, 16, 0xFFFF);

  tft.setTextColor(0x9936);
  tft.setFreeFont(); // use default font
  tft.drawString("Cloud Stick", 90, 6);
  tft.drawLine(89, 14, 154, 14, 0x9936);

  // Reset previous state so everything draws on first update
  prev_state.wifi_signal = -1;
  prev_state.internet_connected = !prev_state.internet_connected;
  prev_state.server_connected = !prev_state.server_connected;
  prev_state.sd_mounted = !prev_state.sd_mounted;
  prev_state.is_syncing = !prev_state.is_syncing;
  prev_state.upload_speed_kbps = 0xFFFFFFFF;
  prev_state.download_speed_kbps = 0xFFFFFFFF;
  prev_state.read_speed_kbps = 0xFFFFFFFF;
  prev_state.write_speed_kbps = 0xFFFFFFFF;
  prev_state.sync_progress_percent = -1;
  prev_state.last_action[0] = '\0';
}

void lcd_dashboard_update(const DashboardState &state) {
  proRedraw=true;
  tft.setFreeFont(); // default font for stats

  // WiFi Icon
  if (state.wifi_signal != prev_state.wifi_signal) {
    tft.fillRect(2, 1, 19, 18, TFT_BLACK); // clear old icon
    if (state.wifi_signal == 3)
      tft.drawBitmap(2, 2, image_wifi_full_bits, 19, 16, 0xFFFF);
    else if (state.wifi_signal == 2)
      tft.drawBitmap(2, 2, image_wifi_50_bits, 19, 16, 0xFFFF);
    else if (state.wifi_signal == 1)
      tft.drawBitmap(2, 2, image_wifi_2_bars_bits, 19, 16, 0xFFFF);
    else if (state.wifi_signal == 0) {
      tft.drawBitmap(2, 2, image_wifi_full_bits, 19, 16, 0x7BEF); // dim
      tft.drawLine(2, 1, 19, 18, 0xFFFF);                         // strike
    }
    prev_state.wifi_signal = state.wifi_signal;
  }

  // Internet Icon
  if (state.internet_connected != prev_state.internet_connected) {
    tft.fillRect(26, 1, 18, 18, TFT_BLACK);
    if (state.internet_connected) {
      tft.drawBitmap(27, 2, image_internet_bits, 16, 16, 0xFFFF);
    } else {
      tft.drawBitmap(27, 2, image_internet_bits, 16, 16, 0x7BEF);
      tft.drawLine(26, 1, 43, 18, 0xFFFF);
    }
    prev_state.internet_connected = state.internet_connected;
  }

  // Server Icon (Cloud)
  if (state.server_connected != prev_state.server_connected) {
    tft.fillRect(47, 1, 18, 18, TFT_BLACK);
    if (state.server_connected) {
      tft.drawBitmap(48, 5, image_cloud_1_bits, 17, 9, 0xFFFF);
    } else {
      tft.drawBitmap(48, 5, image_cloud_1_bits, 17, 9, 0x7BEF);
      tft.drawLine(47, 1, 64, 18, 0xFFFF);
    }
    prev_state.server_connected = state.server_connected;
  }

  // SD Card Icon
  if (state.sd_mounted != prev_state.sd_mounted) {
    tft.fillRect(48, 2, 16, 18, TFT_BLACK);
    if (state.sd_mounted) {
      tft.drawBitmap(49, 3, image_micro_sd_bits, 14, 16, 0xFFFF);
    } else {
      tft.drawBitmap(49, 3, image_micro_sd_bits, 14, 16, 0x7BEF);
      tft.drawLine(48, 2, 63, 18, 0xFFFF);
    }
    prev_state.sd_mounted = state.sd_mounted;
  }

  // Sync Icon
  if (state.is_syncing != prev_state.is_syncing) {
    tft.fillRect(67, 1, 18, 18, TFT_BLACK);
    if (state.is_syncing) {
      tft.drawBitmap(69, 2, image_refresh_bits, 13, 16, 0xFFFF);
    }
    prev_state.is_syncing = state.is_syncing;
  }

  char buf[16];

  // Upload Speed
  if (state.upload_speed_kbps != prev_state.upload_speed_kbps) {
    format_speed(state.upload_speed_kbps, buf, sizeof(buf));
    tft.setTextColor(0x24BE, TFT_BLACK); // fg, bg to overwrite old text
    tft.drawString("U:         ", 26,
                   24); // clear old text by padding with spaces
    tft.drawString(String("U: ") + buf, 26, 24);
    prev_state.upload_speed_kbps = state.upload_speed_kbps;
  }

  // Download Speed
  if (state.download_speed_kbps != prev_state.download_speed_kbps) {
    format_speed(state.download_speed_kbps, buf, sizeof(buf));
    tft.setTextColor(0x4D6A, TFT_BLACK);
    tft.drawString("D:         ", 26, 32);
    tft.drawString(String("D: ") + buf, 26, 32);
    prev_state.download_speed_kbps = state.download_speed_kbps;
  }

  // Read Speed
  if (state.read_speed_kbps != prev_state.read_speed_kbps) {
    format_speed(state.read_speed_kbps, buf, sizeof(buf));
    tft.setTextColor(0xFFE0, TFT_BLACK);
    tft.drawString("R:         ", 26, 43);
    tft.drawString(String("R: ") + buf, 26, 43);
    prev_state.read_speed_kbps = state.read_speed_kbps;
  }

  // Write Speed
  if (state.write_speed_kbps != prev_state.write_speed_kbps) {
    format_speed(state.write_speed_kbps, buf, sizeof(buf));
    tft.setTextColor(0xF810, TFT_BLACK);
    tft.drawString("W:         ", 26, 52);
    tft.drawString(String("W: ") + buf, 26, 52);
    prev_state.write_speed_kbps = state.write_speed_kbps;
  }

  // Last Action
  if (strcmp(state.last_action, prev_state.last_action) != 0) {
    tft.fillRect(27, 68, 130, 10, TFT_BLACK); // clear old text
    tft.setTextColor(0xFC00); // no background so it looks cleaner
    tft.drawString(state.last_action, 27, 68);
    strncpy(prev_state.last_action, state.last_action,
            sizeof(prev_state.last_action));
  }

  // Progress Bar
  if (state.sync_progress_percent != prev_state.sync_progress_percent || state.is_syncing != prev_state.is_syncing) {
      if (state.is_syncing && state.sync_progress_percent >= 0 && state.sync_progress_percent <= 100) {
          int bar_width = (160 * state.sync_progress_percent) / 100;
          if (bar_width > 0) tft.fillRect(0, 78, bar_width, 2, 0xFC00);
          if (bar_width < 160) tft.fillRect(bar_width, 78, 160 - bar_width, 2, TFT_BLACK);
      } else {
          tft.fillRect(0, 78, 160, 2, TFT_BLACK);
      }
      prev_state.sync_progress_percent = state.sync_progress_percent;
  }
}

void lcd_show_loading() { lcd_show_state("loading"); }

void lcd_clear() { proRedraw=true;tft.fillScreen(TFT_BLACK); }

void lcd_show_text(const char *message) {
  proRedraw=true;
  constexpr uint16_t bg=pro::lcd_v2::Background,muted=pro::lcd_v2::Muted,accent=pro::lcd_v2::Good;
  tft.fillScreen(bg);tft.setFreeFont();tft.setTextSize(1);tft.setTextDatum(TL_DATUM);
  tft.setTextColor(muted,bg);tft.drawString("CLOUD STICK",6,5,1);
  tft.fillRoundRect(6,16,148,2,1,accent);
  const auto frame=pro::setupText(message,[](const char* line){return unsigned(tft.textWidth(line,2));});
  tft.setTextColor(TFT_WHITE,bg);
  for(unsigned i=0;i<frame.count;++i)
    tft.drawString(frame.lines[i].data(),pro::SetupText::X,
                   pro::SetupText::Y+i*pro::SetupText::LineHeight,2);
}

void lcd_show_setup(const char* ssid,const char* password) {
  proRedraw=true;
  tft.fillScreen(TFT_BLACK);tft.setFreeFont();tft.setTextSize(1);tft.setTextColor(TFT_WHITE,TFT_BLACK);
  tft.drawString("Cloud Stick Pro setup",0,0);tft.drawString(ssid,0,14);
  tft.drawString(setup_network_ip().toString(),0,28);
  tft.drawString(String("Wi-Fi: ")+password,0,42);
  tft.drawString(String("Web: ")+setup_access::Username+" / "+setup_access::WebPassword,0,56);
  tft.drawString("Idle timeout: 10 minutes",0,70);
}
#if CSP_ENABLE_LCD_V2
static void lcd_v2_line(const char* text,int x,int y,int font,int pixels){
  char line[25]{};std::snprintf(line,sizeof line,"%s",text);
  size_t length=std::strlen(line);
  if(tft.textWidth(line,font)>pixels){
    while(length&&tft.textWidth(line,font)>pixels){line[--length]=0;}
    if(length>=3){line[length-3]='.';line[length-2]='.';line[length-1]='.';}
  }
  tft.drawString(line,x,y,font);
}
static void lcd_v2_render(const pro::lcd_v2::Frame& frame,const char* connectedSsid=nullptr){
  using namespace pro::lcd_v2;
  static Frame previous{};static uint32_t previousTick=UINT32_MAX;
  static char previousLabel[15]{};
  char label[15]{};wifiLabel(label,frame.wifi,connectedSsid);
  const uint32_t tick=millis()/250;
  const bool changed=proRedraw||!equal(previous,frame)||std::strcmp(previousLabel,label);
  const bool animate=frame.media&&frame.state==State::Active&&frame.busy&&frame.percent<0;
  if(!changed&&(!animate||tick==previousTick))return;
  if(changed){
    tft.fillScreen(Background);tft.setFreeFont();tft.setTextSize(1);tft.setTextDatum(TL_DATUM);
    // Fixed160x80 rows:16px header,16px phase,8px detail, one5px bar and
    // two bounded footer fields. No sprite, dynamic font or per-frame String.
    tft.fillRect(0,0,160,16,Surface);
    tft.setTextColor(TFT_WHITE,Surface);tft.drawString(label,6,4,1);
    tft.fillCircle(99,8,2,frame.wifi?Good:Muted);
    tft.setTextColor(frame.wifi?TFT_WHITE:Muted,Surface);tft.drawString("WiFi",105,4,1);
    tft.fillRoundRect(135,2,20,12,3,Track);
    tft.setTextColor(frame.media?TFT_WHITE:Warning,Track);tft.drawString("SD",139,4,1);
    const auto state=frame.media?frame.state:State::Attention;
    char badge[6]{};
    if(state==State::Active&&frame.percent>=0)std::snprintf(badge,sizeof badge,"%d%%",frame.percent);
    else text(badge,state==State::Active?"BUSY":state==State::Waiting?"WAIT":state==State::Offline?"OFF":state==State::Attention?"CHECK":"IDLE");
    const uint16_t badgeColor=state==State::Attention?Warning:state==State::Active?Good:Track;
    tft.fillRoundRect(120,21,34,14,4,badgeColor);
    tft.setTextColor(state==State::Attention||state==State::Active?Background:TFT_WHITE,badgeColor);
    tft.setTextDatum(TR_DATUM);tft.drawString(badge,151,24,1);tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE,Background);lcd_v2_line(frame.title,6,20,2,110);
    tft.setTextColor(Muted,Background);lcd_v2_line(frame.detail,6,40,1,148);
    tft.setTextColor(TFT_WHITE,Background);lcd_v2_line(frame.counts,6,66,1,90);
    tft.setTextColor(Muted,Background);tft.setTextDatum(TR_DATUM);
    lcd_v2_line(frame.rate,154,66,1,54);tft.setTextDatum(TL_DATUM);
  }
  tft.fillRoundRect(6,54,148,5,2,Track);
  if(frame.percent>=0){const int width=148*frame.percent/100;if(width)tft.fillRect(6,54,width,5,frame.accent);}
  else if(animate){const int position=int(tick%25)*5;tft.fillRect(6+position,54,24,5,frame.accent);}
  previous=frame;std::memcpy(previousLabel,label,sizeof label);previousTick=tick;proRedraw=false;
}
#endif
void lcd_pro_update(const BackupStatus& s,pro::Owner owner,bool wifi,const char* connectedSsid) {
  pro::DisplayInput input;input.wifi=wifi;input.active=s.active;input.scanning=s.scanning;
  input.complete=s.scanComplete;input.hasScan=s.hasScan;input.windowOpen=backup_window_open();
  input.quiet=storage_quiet_remaining(backup_policy().idleSeconds);input.verified=s.verified;input.pending=s.pending;input.failed=s.failed;
  input.speed=s.transferSpeed;input.done=s.transferDone;input.total=s.transferTotal;input.error=s.error;input.percent=s.progress;input.phase=s.phase;
#if CSP_ENABLE_LCD_V2
  auto frame=pro::lcd_v2::make(input,storage_media_detected(),s.lastBackup,uint64_t(time(nullptr)));
  if(owner==pro::Owner::Fault){pro::lcd_v2::text(frame.title,"Check SD card");pro::lcd_v2::text(frame.detail,"Storage needs attention");frame.accent=pro::lcd_v2::Warning;frame.media=false;frame.percent=-1;}
  if(owner!=pro::Owner::Fault&&frame.media&&s.scanComplete&&!s.active&&wifi&&!s.error&&!s.failed&&!s.pending&&!input.quiet){
    const uint32_t pane=(millis()/7000)%3;
#if CSP_ENABLE_LOCAL_DASHBOARD
    if(pane==1&&pro::local_dashboard::listening()){
      const auto address=WiFi.localIP();std::snprintf(frame.detail,sizeof frame.detail,"Status: %u.%u.%u.%u",unsigned(address[0]),unsigned(address[1]),unsigned(address[2]),unsigned(address[3]));
    }else
#endif
    if(pane==2)pro::lcd_v2::text(frame.detail,"Hold BOOT 3s for setup");
  }
  lcd_v2_render(frame,connectedSsid);return;
#else
  (void)connectedSsid;
  auto frame=pro::displayFrame(input);
  if(owner==pro::Owner::Fault){snprintf(frame.title,sizeof frame.title,"CHECK SD CARD");snprintf(frame.detail,sizeof frame.detail,"Storage needs attention");frame.accent=0xFD20;}
  if(!s.active&&wifi&&!s.error&&!s.failed&&millis()%12000>=7000){
#if CSP_ENABLE_BACKUP_SETUP
#if CSP_ENABLE_LOCAL_DASHBOARD
    if(pro::local_dashboard::listening()&&millis()%18000>=12000){
      snprintf(frame.detail,sizeof frame.detail,"Dashboard on your Wi-Fi");
      snprintf(frame.footer,sizeof frame.footer,"%s",WiFi.localIP().toString().c_str());
    }else{
#endif
    snprintf(frame.detail,sizeof frame.detail,"Phone setup: hold BOOT");
    snprintf(frame.footer,sizeof frame.footer,"Bluetooth + owner key");
#if CSP_ENABLE_LOCAL_DASHBOARD
    }
#endif
#else
    snprintf(frame.detail,sizeof frame.detail,"Dashboard on your Wi-Fi");
    snprintf(frame.footer,sizeof frame.footer,"%s",WiFi.localIP().toString().c_str());
#endif
  }
  static pro::DisplayFrame previous;
  if(!proRedraw&&strcmp(frame.title,previous.title)==0&&strcmp(frame.detail,previous.detail)==0&&strcmp(frame.footer,previous.footer)==0&&frame.percent==previous.percent&&frame.accent==previous.accent&&frame.wifi==previous.wifi)return;
  constexpr uint16_t bg=0x0844,muted=0x9D78,track=0x19C8;
  tft.fillScreen(bg);tft.setFreeFont();tft.setTextSize(1);tft.setTextDatum(TL_DATUM);
  tft.setTextColor(muted,bg);tft.drawString("CLOUD STICK",6,4,1);tft.fillCircle(150,7,3,wifi?0x47D0:0xFD20);
  tft.setTextColor(frame.accent,bg);tft.drawString(frame.title,6,17,2);
  tft.setTextColor(TFT_WHITE,bg);tft.drawString(frame.detail,6,38,1);
  tft.fillRoundRect(6,51,148,5,2,track);
  if(frame.percent>=0){int width=148*std::min(100,std::max(0,frame.percent))/100;if(width)tft.fillRect(6,51,width,5,frame.accent);}
  tft.setTextColor(muted,bg);tft.drawString(frame.footer,6,65,1);
  previous=frame;proRedraw=false;
#endif
}
