// Diagnostic: list every 2.4GHz network the board can see, on screen and serial.
// If your SSID is NOT in this list, the board cannot reach it — almost always
// because it is a 5GHz-only network. The ESP32-C6 is 2.4GHz only.
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#define LCD_SCK 7
#define LCD_MOSI 6
#define LCD_RST 21
#define LCD_DC 15
#define LCD_CS 14
#define LCD_BL 22
Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);

void setup() {
  Serial.begin(115200); delay(400);
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH);
  SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);
  tft.init(172, 320, SPI_MODE0); tft.setSPISpeed(40000000); tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN); tft.setTextSize(2);
  tft.setCursor(4,6); tft.println("SCANNING");
  tft.setTextSize(1);

  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(200);
  int n = WiFi.scanNetworks();
  Serial.printf("\n=== %d networks visible (2.4GHz only) ===\n", n);

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN); tft.setTextSize(1);
  tft.setCursor(2,2); tft.printf("%d nets on 2.4GHz\n", n);
  int y = 16;
  for (int i = 0; i < n && i < 18; i++) {
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    bool open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    Serial.printf("  %-28s ch%-3d %4d dBm %s\n", ssid.c_str(), WiFi.channel(i), rssi, open?"OPEN":"secured");
    tft.setTextColor(rssi > -70 ? ST77XX_WHITE : 0x7BEF);
    tft.setCursor(2, y);
    tft.printf("%.20s", ssid.c_str());
    tft.setCursor(132, y);
    tft.printf("%d", rssi);
    y += 11;
    if (y > 300) break;
  }
  Serial.println("=== end of scan ===");
  Serial.println("If your SSID is missing, it is 5GHz-only and the C6 cannot use it.");
}
void loop() { delay(1000); }
