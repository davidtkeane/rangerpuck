// Diagnostic: attempt the connection and report the EXACT reason it fails.
// Distinguishes "wrong password" from "AP not found" from "timeout" — no guessing.
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "secrets.h"
#define LCD_SCK 7
#define LCD_MOSI 6
#define LCD_RST 21
#define LCD_DC 15
#define LCD_CS 14
#define LCD_BL 22
Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);
volatile int lastReason = 0; volatile bool got = false;

const char* reasonText(int r) {
  switch (r) {
    case 2:   return "AUTH EXPIRE";
    case 4:   return "ASSOC EXPIRE";
    case 15:  return "WRONG PASSWORD";   // 4-way handshake timeout
    case 201: return "AP NOT FOUND";
    case 202: return "AUTH FAIL";
    case 203: return "ASSOC FAIL";
    case 204: return "HANDSHAKE TIMEOUT";
    case 205: return "CONNECTION FAIL";
    default:  return "see code";
  }
}
void onEvent(WiFiEvent_t e, WiFiEventInfo_t info) {
  if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    lastReason = info.wifi_sta_disconnected.reason; got = true;
  }
}
void line(int y, const char* s, uint16_t c) { tft.setTextColor(c); tft.setCursor(4,y); tft.println(s); }

void setup() {
  Serial.begin(115200); delay(400);
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH);
  SPI.begin(LCD_SCK,-1,LCD_MOSI,LCD_CS);
  tft.init(172,320,SPI_MODE0); tft.setSPISpeed(40000000); tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK); tft.setTextSize(1);

  line(4, "WIFI DIAGNOSTIC", ST77XX_CYAN);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4,22); tft.printf("ssid: %.20s", WIFI_SSID);
  tft.setCursor(4,34); tft.printf("pw len: %d chars", (int)strlen(WIFI_PASS));
  Serial.printf("SSID='%s'  password length=%d\n", WIFI_SSID, (int)strlen(WIFI_PASS));

  WiFi.onEvent(onEvent);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  line(56, "connecting...", ST77XX_YELLOW);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis()-t0 < 20000) { delay(300); Serial.print("."); }
  Serial.println();

  tft.fillRect(0,50,172,120,ST77XX_BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    line(56, "CONNECTED", ST77XX_GREEN);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(4,74); tft.print(WiFi.localIP());
    tft.setCursor(4,88); tft.printf("rssi %d dBm", WiFi.RSSI());
    Serial.print("CONNECTED ip="); Serial.println(WiFi.localIP());
  } else {
    line(56, "FAILED", ST77XX_RED);
    tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2);
    tft.setCursor(4,76); tft.println(reasonText(lastReason));
    tft.setTextSize(1); tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(4,104); tft.printf("reason code %d", lastReason);
    Serial.printf("FAILED reason=%d (%s)\n", lastReason, reasonText(lastReason));
  }
}
void loop(){ delay(1000); }
