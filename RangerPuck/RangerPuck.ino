// ============================================================================
//  RangerPuck — step 4: Wi-Fi + HTTP state receiver
//
//  The board is DELIBERATELY DUMB. It holds no logic and no decisions: it
//  listens for a small JSON state over HTTP and renders it. Everything that
//  decides *what* to show lives on M3. That keeps the firmware reflashable
//  without re-entering credentials, and a board on the desk leaks nothing.
//
//  Board : Waveshare ESP32-C6-LCD-1.47 (ST7789 172x320)
//  Pins  : docs/PINOUT.md — confirmed from the Waveshare wiki, do not guess
//  Reach : http://rangerpuck.local  (mDNS — never a hard-coded IP, DHCP moves them)
//
//  POST /state  {"state":"APPROVE","line1":"needs a yes","line2":"0m 31s"}
//  GET  /       plain-text status, handy from curl
// ============================================================================

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "secrets.h"
#include "ranger_logo.h"

#define LCD_SCK   7
#define LCD_MOSI  6
#define LCD_RST  21
#define LCD_DC   15
#define LCD_CS   14
#define LCD_BL   22
#define RGB_PIN   8
#define LCD_W   172
#define LCD_H   320

// ---------------------------------------------------------------------------
// Types must be declared ABOVE the first function definition: arduino-cli
// auto-inserts function prototypes at that point, and a prototype returning
// an undeclared type fails with "'Look' does not name a type".
// ---------------------------------------------------------------------------
struct Look { uint16_t fg; uint8_t r, g, b; };
struct Machine { String name; bool up; uint8_t st; String detail; };  // st: 0 down 1 awake 2 asleep

Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);
Adafruit_NeoPixel led(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
WebServer server(80);

String curState = "BOOT", curL1 = "", curL2 = "";
String curWho = "";

// ─── ONE BOARD, SEVERAL AGENTS ───────────────────────────────────────────────
// The board used to hold a single state, so whoever wrote last won. Claude has
// automatic hooks firing every turn; Gemini writes only when it remembers. The
// result was Claude silently clobbering Gemini's APPROVE while Gemini sat there
// genuinely blocked — David never saw the request.
//
// So the board now keeps a SLOT PER AGENT and renders the most URGENT one, not
// the most recent. A request for a human always beats a progress update, no
// matter who spoke last or how loudly.
#define MAX_AGENTS 4
struct AgentState {
  String who, state, l1, l2;
  uint32_t ms = 0;
  bool used = false;
};
AgentState agents[MAX_AGENTS];
// Which slot is currently on screen. Expiry must age THAT agent's state by ITS
// OWN clock: a global "last update" timestamp is reset by every other agent's
// traffic, so a WAITING that nothing has refreshed sat on screen indefinitely
// while a busier agent kept restarting its timer underneath.
int curSlot = -1;

int urgency(const String& st) {
  if (st == "APPROVE") return 100;   // a human is BLOCKING — always wins
  if (st == "ERROR")   return 80;
  if (st == "PIGS")    return 75;    // welfare (temperature)
  if (st == "PIGSOON") return 60;    // rain within 10min while a run is due — the loud flash
  if (st == "PIGWAIT") return 48;    // no dry window now — steady red info
  if (st == "PIGGO")   return 50;    // dry window open — green, go feed them
  if (st == "SKY")     return 45;   // go and look up — but nothing is blocked
  if (st == "WAITING") return 40;
  if (st == "RADAR")   return 18;   // ambient: beats a finished job, never live work
  if (st == "SYNC" || st == "RUNNING" || st == "THINKING") return 20;
  if (st == "DONE")    return 15;   // below RADAR on purpose — see above
  return 5;                          // IDLE and anything unknown
}

// Store this agent's state, then decide what the screen should show.
void recordAndPick(const String& who, const String& st, const String& l1, const String& l2) {
  String key = who.length() ? who : "UNKNOWN";
  int slot = -1, oldest = 0;
  for (int i = 0; i < MAX_AGENTS; i++) {
    if (agents[i].used && agents[i].who == key) { slot = i; break; }
    if (!agents[i].used && slot < 0) slot = i;
    // evict the LEAST URGENT, then the oldest — never drop a blocked agent to
    // make room for a progress update
    if (urgency(agents[i].state) < urgency(agents[oldest].state) ||
        (urgency(agents[i].state) == urgency(agents[oldest].state) &&
         agents[i].ms < agents[oldest].ms)) oldest = i;
  }
  if (slot < 0) slot = oldest;
  agents[slot] = {key, st, l1, l2, millis(), true};

  // forget an agent that has gone quiet, so a dead session cannot hold the screen
  // NEVER forget a BLOCKED agent. One waiting for a human is silent BY
  // DEFINITION — that is what waiting means. Pruning it broke the single promise
  // this device makes, in exactly the case it exists for.
  for (int i = 0; i < MAX_AGENTS; i++)
    if (agents[i].used && urgency(agents[i].state) < 100
        && millis() - agents[i].ms > 600000UL) agents[i].used = false;

  int best = -1;
  for (int i = 0; i < MAX_AGENTS; i++) {
    if (!agents[i].used) continue;
    if (best < 0) { best = i; continue; }
    int u = urgency(agents[i].state), ub = urgency(agents[best].state);
    if (u > ub || (u == ub && agents[i].ms > agents[best].ms)) best = i;
  }
  if (best >= 0) {
    curState = agents[best].state; curL1 = agents[best].l1;
    curL2 = agents[best].l2;       curWho = agents[best].who;
    curSlot = best;
  }
}
// When did the last state arrive? An agent that crashes, is killed, or simply
// forgets to say it finished must not leave the board asserting something stale.
// The board expires its own state rather than trusting every agent to tidy up.
uint32_t lastStateMs = 0;

// --- live aircraft, for the radar screen ----------------------------------
// The API is polled every ~20s, but the plane should MOVE, not jump. So the
// board dead-reckons between updates: it advances the aircraft along its own
// heading at its own speed, several times a second, and snaps to truth when a
// fresh position arrives. Same maths a real radar display uses.
struct Plane {
  float east, north;     // km east / north of the house
  float track, gs;       // degrees true, knots
  int32_t alt;
  // "TFS>EDI", then the same route spelled out at two widths:
  //   routefull  <=52 chars for landscape — "YUL Montreal, CA > CDG Paris, FR"
  //   routemid   <=28 chars for portrait  — "YUL Montreal > CDG Paris"
  // M3 picks the wording for each budget and DEGRADES (drops the country, then a
  // city) rather than letting the board chop a name mid-word.
  String callsign, type, airline, route, routefull, routemid;
  float dist = 0, cpa = 0;
  int eta = -1;             // minutes to closest approach, -1 if receding
  int vs = 0;               // ft/min: + climbing, - descending
  bool approaching = true;
  bool valid = false;
  uint32_t lastMs = 0;
} plane;

#define MAX_PLANES 10
Plane planes[MAX_PLANES];
int planeCount = 0;
float radarRangeKm = 40.0;

// --- ambient rotation -------------------------------------------------------
// Adding the radar silently hid the fleet view: RADAR outranks IDLE, so once
// aircraft were being tracked the machine list never appeared again. Both are
// ambient — neither is urgent — so when nothing needs a human they take turns.
uint32_t ambientSwapMs = 0;
const uint32_t AMBIENT_SWAP = 12000UL;      // 12s each — long enough to read
uint8_t ambientScreen = 0;                  // 0 planes · 1 fleet · 2 weather · 3 clock
const uint8_t AMBIENT_COUNT = 4;
bool timeReady = false;
uint32_t lastNtpMs = 0;
// Was the clock ever set? NTP used to be attempted ONCE in setup(), and the
// periodic re-sync in loop() was gated on `timeReady` — which only syncTime()
// itself sets. So a board that booted before the router was up never tried
// again: the clock screen read "no time" for the WHOLE uptime and draw() dropped
// it from the rotation permanently. setup()'s own comment calls a slow router the
// NORMAL case after a power cut, so this was the common path, not an edge case.
const uint32_t NTP_RETRY = 60UL * 1000UL;          // keep asking while unset
const uint32_t NTP_REFRESH = 6UL * 3600UL * 1000UL;// and top up every 6h once set
bool mdnsUp = false;        // mDNS is registered once and never re-announced;
                            // after a reconnect or an IP change the name goes
                            // dead until a power cycle. Re-announce instead.
bool wasConnected = false;

// --- weather, held for the ambient rotation ---------------------------------
// Weather states were being PUSHED and losing every time: they scored 5 against
// the radar's 18, so the screen with a guinea pig depending on it never showed.
// Weather is not urgent, it is ambient — so it takes its turn instead of
// competing. PIGS stays a real state at urgency 75 for when it is actually cold.
struct Wx { String temp, cond, rain, pigs, place; bool valid=false; uint32_t ms=0; } wx;
const uint32_t EXPIRE_WORKING = 8UL * 60UL * 1000UL;   // THINKING/RUNNING — work can be slow
const uint32_t EXPIRE_SETTLED = 60UL * 1000UL;         // DONE/WAITING/ERROR/SKY — brief
const uint32_t EXPIRE_NUDGE   = 22UL * 1000UL;         // WAITING — a nudge, not a status
// WAITING says "your move". Once seen it has done its whole job, and leaving it
// up just blocks the ambient screens behind it. Claude Code also re-fires its
// idle notification periodically, which kept refreshing the timer and held the
// screen for minutes.
// APPROVE never expires: it means someone is genuinely blocked, and David may be
// out of the room for an hour. That one has to keep asking.        // CLAUDE / GEMINI / OLLAMA / WEATHER — any agent can drive this

// Each agent gets its own colour so David can tell at a glance WHO is talking.
uint16_t whoColour(const String& w) {
  if (w == "CLAUDE") return 0xFD20;   // orange
  if (w == "GEMINI") return 0x055F;   // blue
  if (w == "OLLAMA") return 0x07E0;   // green
  if (w == "QWEN")   return 0xF81F;   // magenta
  if (w == "WEATHER")return 0x07FF;   // cyan
  if (w == "KALI")   return 0xFCE0;   // amber — Claude running on the Kali box
  if (w == "PLANE")  return 0x8410;   // dim grey — ambient radar, not an agent
  return 0xC618;                      // grey — unknown agent
}

// --- fleet view: shown whenever Claude is IDLE, so the screen is never idle ---
#define MAX_MACHINES 6
Machine fleet[MAX_MACHINES];
int fleetCount = 0;
int fleetDropped = 0;          // machines the board could not fit — shown, not hidden
String fleetStamp = "";

// --- orientation -----------------------------------------------------------
// 0 = portrait (172x320), 1 = landscape (320x172). Switch at runtime with
//   curl -X POST -d '{"rotation":1}' http://<puck>/rotate
// Stored in NVS so it survives a power cut.
Preferences prefs;
uint8_t rot = 1;
uint8_t mascot = 0;              // 0 = Ranger helmet, 1 = cat                 // landscape by default — suits the cat+text layout
int W() { return (rot % 2) ? LCD_H : LCD_W; }   // usable width  for current rotation
int H() { return (rot % 2) ? LCD_W : LCD_H; }   // usable height for current rotation

Look lookFor(const String& s) {
  if (s == "APPROVE")  return {ST77XX_YELLOW, 255, 140,   0};  // amber — waiting on you
  if (s == "THINKING") return {ST77XX_CYAN,     0,  80, 255};  // blue  — working
  if (s == "RUNNING")  return {ST77XX_CYAN,     0,  80, 255};
  if (s == "DONE")     return {ST77XX_GREEN,    0, 255,   0};  // green — finished
  if (s == "ERROR")    return {ST77XX_RED,    255,   0,   0};  // red   — broken
  if (s == "SKY")      return {0xFD7F,        180,  80, 255};  // violet — something overhead
  if (s == "SUN")      return {ST77XX_YELLOW, 255, 170,   0};  // yellow — actual sunshine
  if (s == "DRY")      return {ST77XX_GREEN,   0, 140,  40};  // green — go outside
  if (s == "RAIN")     return {0x04FF,        0,  40, 200};  // deep blue — showers
  if (s == "PIGS")     return {ST77XX_RED,   255,  40,   0};  // red — hutch needs you
  if (s == "WAITING")  return {0x07FF,          0,  90,  90};  // teal — idle, your move
  if (s == "SYNC")     return {ST77XX_MAGENTA, 180,  0, 180};  // purple — fleet sync
  if (s == "IDLE")     return {ST77XX_WHITE,    8,   8,   8};  // near-off
  return {ST77XX_WHITE, 40, 40, 40};
}


// ============================================================================
//  The mascot — a cat, because David has three (Mammy, Kitty, and the tom who
//  answers to "meow"). Drawn with primitives rather than a bitmap array so the
//  expression can change per state without carrying six images in flash.
//  Layout borrowed from the CrabPuck idea; the artwork is ours.
// ============================================================================
// The Ranger helmet, drawn from a 1-bit bitmap so it takes the state colour.
// Amber helmet = Claude is waiting on you. Visible across a room.
// A plane, rotated to the aircraft's ACTUAL heading, so the icon on the desk
// points where the real one over the house is going. Drawn from rotated points
// rather than a bitmap because a bitmap cannot rotate.
void drawPlane(int cx, int cy, float headingDeg, uint16_t col, float scale) {
  // screen Y grows downward, and 0 deg heading is north = up, hence -90
  float a = (headingDeg - 90.0f) * 3.14159265f / 180.0f;
  float ca = cosf(a), sa = sinf(a);
  auto X = [&](float px, float py) { return (int)(cx + (px * ca - py * sa) * scale); };
  auto Y = [&](float px, float py) { return (int)(cy + (px * sa + py * ca) * scale); };

  // fuselage nose -> tail, wings, tailplane (model-space, nose at +x)
  tft.fillTriangle(X(16,0), Y(16,0),  X(-6,-4), Y(-6,-4),  X(-6,4), Y(-6,4), col);
  tft.fillTriangle(X(2,0),  Y(2,0),   X(-8,-13),Y(-8,-13), X(-4,0), Y(-4,0), col);
  tft.fillTriangle(X(2,0),  Y(2,0),   X(-8,13), Y(-8,13),  X(-4,0), Y(-4,0), col);
  tft.fillTriangle(X(-12,0),Y(-12,0), X(-16,-6),Y(-16,-6), X(-13,0),Y(-13,0), col);
  tft.fillTriangle(X(-12,0),Y(-12,0), X(-16,6), Y(-16,6),  X(-13,0),Y(-13,0), col);
}

void drawLogo(int x, int y, const String& st, uint16_t accent) {
  tft.drawBitmap(x, y, RANGER_LOGO, LOGO_W, LOGO_H, accent);
  if (st == "APPROVE") {                       // a pulse bar under it when waiting
    tft.fillRoundRect(x + 8, y + LOGO_H + 6, LOGO_W - 16, 5, 2, accent);
  }
}

void drawMascot(int x, int y, const String& st, uint16_t accent);

void drawCat(int x, int y, const String& st, uint16_t accent) {
  const uint16_t FUR   = 0xFD59;   // warm ginger
  const uint16_t DARK  = 0xC2A6;
  const uint16_t PINK  = 0xFBAE;
  const uint16_t WHITE = ST77XX_WHITE;
  const uint16_t BLACK = ST77XX_BLACK;

  // ears
  tft.fillTriangle(x+4,  y+14, x+12, y-2,  x+22, y+12, FUR);
  tft.fillTriangle(x+48, y+14, x+40, y-2,  x+30, y+12, FUR);
  tft.fillTriangle(x+9,  y+13, x+13, y+4,  x+19, y+12, PINK);
  tft.fillTriangle(x+43, y+13, x+39, y+4,  x+33, y+12, PINK);

  // head
  tft.fillRoundRect(x+2, y+8, 48, 40, 16, FUR);

  // eyes — the expression lives here
  if (st == "IDLE") {                       // asleep
    tft.drawFastHLine(x+12, y+26, 11, DARK);
    tft.drawFastHLine(x+29, y+26, 11, DARK);
  } else if (st == "DONE") {                // happy squint ^  ^
    tft.drawLine(x+12, y+28, x+17, y+22, DARK);
    tft.drawLine(x+17, y+22, x+22, y+28, DARK);
    tft.drawLine(x+30, y+28, x+35, y+22, DARK);
    tft.drawLine(x+35, y+22, x+40, y+28, DARK);
  } else if (st == "ERROR") {               // X  X
    tft.drawLine(x+12, y+22, x+21, y+31, DARK); tft.drawLine(x+21, y+22, x+12, y+31, DARK);
    tft.drawLine(x+31, y+22, x+40, y+31, DARK); tft.drawLine(x+40, y+22, x+31, y+31, DARK);
  } else {                                  // open eyes
    tft.fillCircle(x+17, y+26, 7, WHITE);
    tft.fillCircle(x+35, y+26, 7, WHITE);
    int dx = 0, dy = 0;
    if (st == "THINKING") { dx = 2; dy = -3; }        // glancing up, pondering
    if (st == "APPROVE")  { dx = 0; dy =  0; }        // dead ahead — LOOKING AT YOU
    if (st == "RUNNING")  { dx = -2; dy = 1; }
    tft.fillCircle(x+17+dx, y+26+dy, 4, BLACK);
    tft.fillCircle(x+35+dx, y+26+dy, 4, BLACK);
    tft.fillCircle(x+19+dx, y+24+dy, 1, WHITE);
    tft.fillCircle(x+37+dx, y+24+dy, 1, WHITE);
    if (st == "APPROVE") {                            // eyebrows — expectant
      tft.drawFastHLine(x+11, y+17, 10, DARK);
      tft.drawFastHLine(x+31, y+17, 10, DARK);
    }
  }

  // nose + mouth + whiskers
  tft.fillTriangle(x+23, y+33, x+29, y+33, x+26, y+37, PINK);
  tft.drawLine(x+26, y+37, x+21, y+41, DARK);
  tft.drawLine(x+26, y+37, x+31, y+41, DARK);
  for (int i = 0; i < 2; i++) {
    tft.drawFastHLine(x-6,  y+32 + i*5, 11, DARK);
    tft.drawFastHLine(x+47, y+32 + i*5, 11, DARK);
  }

  // a little accent collar in the state colour
  tft.fillRoundRect(x+14, y+46, 24, 5, 2, accent);
}

void drawMascot(int x, int y, const String& st, uint16_t accent) {
  if (mascot == 1) drawCat(x, y, st, accent);
  else             drawLogo(x, y, st, accent);
}

// line1 = "CALLSIGN TYPE", line2 = "alt|dist|heading" e.g. "24000ft|6km SW|255"
void drawRadar() {
  bool land = (rot % 2);
  int cx = W() / 2, cy = H() / 2 - (land ? 6 : 20);
  int r  = (land ? H() : W()) / 2 - 14;

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  // range rings + your house at the centre
  tft.drawCircle(cx, cy, r,        0x1082);
  tft.drawCircle(cx, cy, r * 2/3,  0x1082);
  tft.drawCircle(cx, cy, r / 3,    0x1082);
  tft.drawFastHLine(cx - r, cy, r * 2, 0x1082);
  tft.drawFastVLine(cx, cy - r, r * 2, 0x1082);
  tft.setTextSize(1); tft.setTextColor(0x4208);
  tft.setCursor(cx - 3, cy - r - 9); tft.print("N");

  if (!planeCount) {
    tft.setTextColor(0x7BEF); tft.setTextSize(1);
    tft.setCursor(4, H() - 12); tft.print("no aircraft in range");
    return;
  }

  float pxPerKm = (float)r / radarRangeKm;

  // ---- every contact, furthest first so the primary lands on top ----------
  for (int i = planeCount - 1; i >= 0; i--) {
    Plane &q = planes[i];
    float px = q.east * pxPerKm, py = -q.north * pxPerKm;
    float mag = sqrtf(px*px + py*py);
    bool off = mag > r;
    if (off && mag > 0) { px = px / mag * r; py = py / mag * r; }

    bool primary = (i == 0);
    uint16_t col = off      ? 0x2965                       // clipped to the ring
                 : primary  ? (q.alt < 3000 ? ST77XX_YELLOW : ST77XX_WHITE)
                            : (q.alt < 3000 ? 0x9B60 : 0x3D9F);   // others, dimmer
    drawPlane(cx + (int)px, cy + (int)py, q.track, col, primary ? (land ? 1.2f : 1.0f) : 0.65f);

    if (primary) {                                          // trail on the primary only
      float a = (q.track - 90.0f) * 3.14159265f / 180.0f;
      for (int k = 1; k <= 3; k++)
        tft.drawPixel(cx + (int)(px - cosf(a)*k*6), cy + (int)(py - sinf(a)*k*6), 0x2945);
    } else {                                                // tiny tag on the rest
      tft.setTextSize(1); tft.setTextColor(0x4208);
      tft.setCursor(cx + (int)px + 7, cy + (int)py - 3);
      tft.print(q.callsign.substring(0, 4));
    }
  }

  // home LAST, so it is never buried under an aircraft sitting almost overhead
  tft.fillCircle(cx, cy, 4, ST77XX_BLACK);
  tft.fillCircle(cx, cy, 2, ST77XX_GREEN);
  tft.drawCircle(cx, cy, 5, 0x03E0);

  // contact count, and a stale marker when the data has stopped arriving
  tft.setTextSize(1); tft.setTextColor(0x4208);
  tft.setCursor(3, 3); tft.printf("%d", planeCount);
  if (millis() - planes[0].lastMs > 45000UL) {
    tft.setTextColor(0x8410);
    tft.setCursor(14, 3); tft.print("stale");
  }

  // ---- distance gauge: a bar that fills as it gets closer ----------------
  float live = sqrtf(plane.east*plane.east + plane.north*plane.north);
  // gauge lifted 8px in landscape: the callsign row moved up to 130 and at
  // text size 2 it is 16px tall, so the old bar at 126..132 ran into it.
  int gw = W() - 12, gx = 6, gy = H() - (land ? 54 : 56);
  float frac = 1.0f - fminf(1.0f, live / radarRangeKm);      // 1.0 = overhead
  uint16_t gcol = live < 5 ? ST77XX_RED : live < 15 ? ST77XX_YELLOW : 0x07FF;
  tft.drawRect(gx, gy, gw, 7, 0x2945);
  tft.fillRect(gx + 1, gy + 1, (int)((gw - 2) * frac), 5, gcol);
  tft.setTextSize(1); tft.setTextColor(0x4208);
  tft.setCursor(gx, gy - 9);            tft.print("0");
  tft.setCursor(gx + gw - 14, gy - 9);  tft.printf("%.0fkm", radarRangeKm);

  // big distance, right-aligned over the bar
  tft.setTextSize(2); tft.setTextColor(gcol);
  char dbuf[12]; snprintf(dbuf, sizeof dbuf, "%.1fkm", live);
  tft.setCursor(W() - strlen(dbuf) * 12 - 6, gy - 26); tft.print(dbuf);

  // ---- identity and state, bottom-left -----------------------------------
  // Callsign and route SIDE BY SIDE on one line. Stacking them pushed the
  // callsign up into the colour bar; they belong together anyway — "who" and
  // "where to" are one thought.
  // Landscape bottom stack, 8px-aligned and all ON SCREEN (H()=172, rows 0..171):
  //   callsign  H()-42 = 130  size2 -> 130..145
  //   routefull H()-24 = 148  size1 -> 148..155
  //   airline   H()-16 = 156  size1 -> 156..163
  //   alt/trend H()-8  = 164  size1 -> 164..171
  // The old stack was 34/18/8/0, so the alt/trend line sat at y=172 and every
  // glyph was silently discarded — that line had never once rendered.
  int ts = land ? 2 : 1, cw = land ? 12 : 6;
  tft.setTextSize(ts); tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(4, H() - (land ? 42 : 42)); tft.print(plane.callsign);
  if (plane.route.length()) {
    tft.setTextColor(0x07E0);                      // green, same line, one gap
    tft.setCursor(4 + (plane.callsign.length() + 1) * cw, H() - (land ? 42 : 42));
    tft.print(plane.route);
  }

  // The route spelled out underneath — code, city AND country, so a bare "CFU"
  // becomes somewhere you can picture. Pick the variant sized for this rotation;
  // M3 has already degraded it to fit, so no truncation is needed here.
  {
    const String& rf = land ? plane.routefull : plane.routemid;
    if (rf.length()) {
      tft.setTextSize(1); tft.setTextColor(0x07E0);
      tft.setCursor(4, H() - (land ? 24 : 30));
      tft.print(rf);
    } else if (plane.routefull.length()) {
      tft.setTextSize(1); tft.setTextColor(0x07E0);   // older M3 sending only the wide one
      tft.setCursor(4, H() - (land ? 24 : 30));
      tft.print(plane.routefull.substring(0, land ? 52 : 28));
    }
  }

  tft.setTextSize(1); tft.setTextColor(0x7BEF);
  tft.setCursor(4, H() - (land ? 16 : 20));
  tft.printf("%s %s", plane.airline.length() ? plane.airline.c_str() : plane.type.c_str(),
             plane.airline.length() ? plane.type.c_str() : "");

  tft.setCursor(4, H() - (land ? 8 : 10));
  const char* trend = plane.vs > 200 ? "climbing" : plane.vs < -200 ? "descending" : "level";
  tft.setTextColor(plane.vs < -200 ? ST77XX_YELLOW : 0x7BEF);
  tft.printf("%ldft %s %.0fkt", (long)plane.alt, trend, plane.gs);

  // ---- closest approach: the question you actually have -------------------
  if (plane.approaching && plane.eta >= 0) {
    tft.setTextColor(plane.cpa < 3 ? ST77XX_GREEN : 0x7BEF);
    // was H()-16 in landscape = the routefull row; a 52-char route reaches
    // x=316 and the right third was being overwritten. The airline line is
    // short and left-aligned, so this row is free.
    tft.setCursor(W() - 96, H() - (land ? 16 : 8));
    tft.printf("CPA %.1fkm %dmin", plane.cpa, plane.eta);
  } else {
    tft.setTextColor(0x4208);
    tft.setCursor(W() - 60, H() - (land ? 16 : 8));
    tft.print("outbound");
  }

  led.setPixelColor(0, plane.alt < 3000 ? led.Color(120, 90, 0) : led.Color(0, 40, 70));
  led.show();
}

void startMdns() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mdnsUp) MDNS.end();
  mdnsUp = MDNS.begin(PUCK_HOSTNAME);
  if (mdnsUp) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS up: http://%s.local\n", PUCK_HOSTNAME);
  } else {
    Serial.println("mDNS failed — reach the board by IP until it retries");
  }
}

void syncTime() {
  lastNtpMs = millis();                       // stamp the ATTEMPT, not the success,
                                              // so a failure backs off too
  if (WiFi.status() != WL_CONNECTED) { Serial.println("NTP skipped — no wifi"); return; }
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  setenv("TZ", "GMT0IST,M3.5.0/1,M10.5.0", 1);   // Europe/Dublin, DST handled
  tzset();
  struct tm t;
  for (int i = 0; i < 20 && !getLocalTime(&t, 500); i++) delay(100);
  timeReady = getLocalTime(&t, 100);
  if (timeReady) Serial.printf("time synced: %02d:%02d\n", t.tm_hour, t.tm_min);
  else           Serial.println("NTP failed — clock will be wrong");
}

void drawClock() {
  struct tm t;
  tft.fillScreen(ST77XX_BLACK);
  if (!timeReady || !getLocalTime(&t, 50)) {
    tft.setTextColor(0x7BEF); tft.setTextSize(2);
    tft.setCursor(8, H()/2 - 8); tft.println("no time");
    return;
  }
  bool land = (rot % 2);
  int cx = land ? 84 : W()/2;
  int cy = land ? H()/2 : 104;
  int r  = (land ? H()/2 : 72) - 10;

  // face
  tft.drawCircle(cx, cy, r,     0x39C7);
  tft.drawCircle(cx, cy, r - 1, 0x18E3);
  for (int i = 0; i < 12; i++) {
    float a = i * 30.0f * 3.14159265f / 180.0f;
    int x1 = cx + sinf(a) * (r - 4),  y1 = cy - cosf(a) * (r - 4);
    int x2 = cx + sinf(a) * (r - (i % 3 == 0 ? 10 : 6));
    int y2 = cy - cosf(a) * (r - (i % 3 == 0 ? 10 : 6));
    tft.drawLine(x1, y1, x2, y2, i % 3 == 0 ? ST77XX_WHITE : 0x5AEB);
  }

  auto hand = [&](float deg, int len, uint16_t col, int w) {
    float a = deg * 3.14159265f / 180.0f;
    int x = cx + sinf(a) * len, y = cy - cosf(a) * len;
    tft.drawLine(cx, cy, x, y, col);
    if (w > 1) {                       // thicken by drawing neighbours
      tft.drawLine(cx + 1, cy, x, y, col);
      tft.drawLine(cx, cy + 1, x, y, col);
    }
  };
  float hd = (t.tm_hour % 12) * 30.0f + t.tm_min * 0.5f;
  float md = t.tm_min * 6.0f + t.tm_sec * 0.1f;
  float sd = t.tm_sec * 6.0f;
  hand(hd, r * 0.50f, ST77XX_WHITE,  2);
  hand(md, r * 0.78f, 0xC618,        2);
  hand(sd, r * 0.88f, ST77XX_RED,    1);
  tft.fillCircle(cx, cy, 3, ST77XX_RED);

  // digital, beside it in landscape and beneath in portrait
  char hhmm[6], dayname[12], datestr[20];
  strftime(hhmm,    sizeof hhmm,    "%H:%M",    &t);
  strftime(dayname, sizeof dayname, "%A",       &t);   // full day — Saturday, not Sat
  strftime(datestr, sizeof datestr, "%d %B %Y", &t);   // 13 September 2026
  tft.setTextColor(ST77XX_WHITE);
  if (land) {
    tft.setTextSize(4); tft.setCursor(172, 34);  tft.print(hhmm);
    tft.setTextSize(2); tft.setTextColor(0x07FF);
    tft.setCursor(174, 76);  tft.print(dayname);
    tft.setTextSize(1); tft.setTextColor(0xC618);
    tft.setCursor(174, 98);  tft.print(datestr);
    tft.setTextColor(0x7BEF);
    tft.setCursor(174, 112); tft.printf(":%02d", t.tm_sec);
  } else {
    tft.setTextSize(4); tft.setCursor(14, 200); tft.print(hhmm);
    tft.setTextSize(2); tft.setTextColor(0x07FF);
    tft.setCursor(14, 242); tft.print(dayname);
    tft.setTextSize(1); tft.setTextColor(0xC618);
    tft.setCursor(14, 266); tft.print(datestr);
  }
  led.setPixelColor(0, led.Color(4, 4, 8)); led.show();
}

void drawPigRun() {
  bool go   = (curState == "PIGGO");
  bool soon = (curState == "PIGSOON");
  uint16_t fg = go ? 0x07E0 : 0xF800;
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  tft.setTextColor(0xFFE0); tft.setTextSize(2);
  tft.setCursor(6, 6); tft.print("PIG RUN");
  tft.setTextColor(fg); tft.setTextSize(6);
  tft.setCursor(6, 40); tft.print(go ? "GO" : "WAIT");
  tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2);
  if (curL1.length()) { tft.setCursor(6, H() - 44); tft.println(curL1.substring(0, W()/12)); }
  tft.setTextColor(0xC618); tft.setTextSize(1);
  if (curL2.length()) { tft.setCursor(6, H() - 20); tft.println(curL2.substring(0, W()/6)); }
  if (go)        led.setPixelColor(0, led.Color(0, 90, 0));
  else if (soon) led.setPixelColor(0, led.Color(120, 0, 0));
  else           led.setPixelColor(0, led.Color(60, 0, 0));
  led.show();
}

void drawWeather() {
  bool land = (rot % 2);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  tft.setTextColor(0x07FF); tft.setTextSize(2);
  tft.setCursor(6, 6); tft.println(wx.place.length() ? wx.place : "WEATHER");
  tft.drawFastHLine(0, 28, W(), 0x07FF);

  tft.setTextColor(ST77XX_WHITE); tft.setTextSize(land ? 5 : 4);
  tft.setCursor(8, land ? 44 : 48); tft.print(wx.temp);

  tft.setTextSize(2); tft.setTextColor(0xC618);
  tft.setCursor(land ? 150 : 8, land ? 56 : 100); tft.println(wx.cond.substring(0, land ? 9 : 13));

  // rain chance — the number you actually want before going out
  tft.setTextSize(2); tft.setTextColor(0x5D9F);
  tft.setCursor(land ? 150 : 8, land ? 84 : 128); tft.print(wx.rain);

  // the pigs, if they need anything
  if (wx.pigs.length()) {
    bool bad = wx.pigs.indexOf("TOO") >= 0 || wx.pigs.indexOf("cold") >= 0;
    tft.fillRect(0, H() - (land ? 30 : 44), W(), land ? 22 : 24, bad ? ST77XX_RED : 0x0320);
    tft.setTextSize(land ? 2 : 1); tft.setTextColor(ST77XX_BLACK);
    tft.setCursor(6, H() - (land ? 26 : 40)); tft.print(wx.pigs);
  }
  led.setPixelColor(0, led.Color(0, 30, 40)); led.show();
}

void drawFleet() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);
  bool land = (rot % 2);
  bool anyDown = false;

  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(land ? 2 : 3);
  tft.setCursor(6, 6);
  tft.println("FLEET");
  tft.drawFastHLine(0, land ? 28 : 40, W(), ST77XX_CYAN);

  int perCol = land ? 3 : 6;
  int colW   = land ? W()/2 : W();
  int rowH   = land ? 40 : 38;
  int y0     = land ? 38 : 54;

  for (int i = 0; i < fleetCount && i < MAX_MACHINES; i++) {
    int col = i / perCol, row = i % perCol;
    int x = col * colW + 6;
    int y = y0 + row * rowH;
    if (!fleet[i].up) anyDown = true;

    tft.fillCircle(x + 8, y + 8, 6, fleet[i].st == 1 ? ST77XX_GREEN
                                  : fleet[i].st == 2 ? 0xFD20 : ST77XX_RED);
    tft.setTextSize(2); tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(x + 22, y);
    tft.println(fleet[i].name.substring(0, land ? 6 : 5));
    tft.setTextSize(1); tft.setTextColor(fleet[i].up ? 0xC618 : 0x7BEF);
    tft.setCursor(x + 22, y + 18);
    tft.println(fleet[i].detail.substring(0, land ? 16 : 22));
  }

  int barY = land ? 150 : 286;
  tft.drawFastHLine(0, barY, W(), anyDown ? ST77XX_RED : ST77XX_GREEN);
  tft.setTextSize(1); tft.setTextColor(0x7BEF);
  tft.setCursor(6, barY + 8);
  tft.print(fleetStamp);
  // Say so when the list does not fit. handleFleet() breaks at MAX_MACHINES, and
  // a silently dropped machine reads as "no such machine" — the opposite of what
  // a fleet monitor is for.
  if (fleetDropped > 0) {
    tft.setTextColor(0xFD20);
    tft.setCursor(W() - 52, barY + 8);
    tft.printf("+%d more", fleetDropped);
  }

  led.setPixelColor(0, anyDown ? led.Color(120,0,0) : led.Color(0,18,0));
  led.show();
}

void draw() {
  if (curState == "PIGGO" || curState == "PIGWAIT" || curState == "PIGSOON") {
    drawPigRun();
    return;
  }
  // AMBIENT ROTATION — the four screens that show when nothing needs a human.
  // This dispatch is what makes the rotation real: for three releases the swap
  // timer incremented ambientScreen while draw() ignored it entirely, so the
  // weather screen was DEAD CODE and the fleet view was unreachable whenever
  // any aircraft was in range. Both were reported as working.
  if (curState == "RADAR" || curState == "IDLE") {
    bool have[AMBIENT_COUNT] = { planeCount > 0, fleetCount > 0, wx.valid, timeReady };
    for (int i = 0; i < AMBIENT_COUNT; i++) {      // skip screens with no data
      uint8_t k = (ambientScreen + i) % AMBIENT_COUNT;
      if (!have[k]) continue;
      ambientScreen = k;
      if      (k == 0) drawRadar();
      else if (k == 1) drawFleet();
      else if (k == 2) drawWeather();
      else             drawClock();
      return;
    }
  }

  Look k = lookFor(curState);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextWrap(false);

  if (rot % 2) {
    // ---------- LANDSCAPE 320x172: cat left, big text right ----------
    drawMascot(14, mascot ? 52 : 44, curState, k.fg);

    tft.setTextColor(k.fg);
    tft.setTextSize(curState.length() > 7 ? 3 : 4);
    tft.setCursor(96, 44);
    tft.println(curState);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    if (curL1.length()) { tft.setCursor(98, 90);  tft.println(curL1.substring(0, 18)); }
    if (curL2.length()) { tft.setCursor(98, 116); tft.println(curL2.substring(0, 18)); }

    tft.fillRect(0, 150, W(), 10, k.fg);
    // anyone else also wanting attention?
    for (int i = 0; i < MAX_AGENTS; i++) {
      if (!agents[i].used || agents[i].who == curWho) continue;
      if (urgency(agents[i].state) < 40) continue;
      tft.setTextSize(1); tft.setTextColor(whoColour(agents[i].who));
      tft.setCursor(4, H() - 26);
      tft.printf("+ %s %s", agents[i].who.c_str(), agents[i].state.c_str());
      break;
    }
    if (curWho.length()) {                       // who is speaking, top-right
      uint16_t wc = whoColour(curWho);
      int tw = curWho.length() * 6 + 8;
      tft.fillRoundRect(W() - tw - 4, 4, tw, 14, 3, wc);
      tft.setTextSize(1); tft.setTextColor(ST77XX_BLACK);
      tft.setCursor(W() - tw, 8); tft.print(curWho);
    }
    tft.setTextSize(1); tft.setTextColor(0x7BEF);
    tft.setCursor(4, 163); tft.print(WiFi.localIP());
  } else {
    // ---------- PORTRAIT 172x320: cat on top, text below ----------
    drawMascot(mascot ? 56 : 50, 16, curState, k.fg);

    tft.setTextColor(k.fg);
    tft.setTextSize(curState.length() > 7 ? 2 : 3);
    tft.setCursor(6, 92);
    tft.println(curState);

    tft.setTextSize(2);
    tft.setTextColor(ST77XX_WHITE);
    if (curL1.length()) { tft.setCursor(6, 130); tft.println(curL1.substring(0, 14)); }
    if (curL2.length()) { tft.setCursor(6, 158); tft.println(curL2.substring(0, 14)); }

    tft.fillRect(0, 196, W(), 12, k.fg);
    if (curWho.length()) {
      uint16_t wc = whoColour(curWho);
      int tw = curWho.length() * 6 + 8;
      tft.fillRoundRect(W() - tw - 4, 4, tw, 14, 3, wc);
      tft.setTextSize(1); tft.setTextColor(ST77XX_BLACK);
      tft.setCursor(W() - tw, 8); tft.print(curWho);
    }
    tft.setTextSize(1); tft.setTextColor(0x7BEF);
    tft.setCursor(6, 300); tft.print(WiFi.localIP());
  }

  led.setPixelColor(0, led.Color(k.r, k.g, k.b));
  led.show();
}

void showBoot(const char* msg) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_GREEN); tft.setTextSize(3);
  tft.setCursor(6, 24); tft.println("RANGER");
  tft.setCursor(6, 56); tft.println("PUCK");
  tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2);
  tft.setCursor(6, 110); tft.println(msg);
}

// A body too large to be a legitimate state. There is no authentication on this
// board (deliberate — see README), so the one thing worth defending is the heap:
// a 200KB POST on a part with 320KB of RAM is a reset, not a bug report.
const size_t MAX_BODY = 4096;
bool bodyTooBig() {
  if (server.arg("plain").length() <= MAX_BODY) return false;
  server.send(413, "text/plain", "body too large\n");
  return true;
}

void handleState() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only\n"); return; }
  if (bodyTooBig()) return;
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "bad json\n"); return; }
  recordAndPick((const char*)(doc["who"]   | ""),
                (const char*)(doc["state"] | "IDLE"),
                (const char*)(doc["line1"] | ""),
                (const char*)(doc["line2"] | ""));
  lastStateMs = millis();
  Serial.printf("state <- %s | %s | %s | %s\n", curState.c_str(), curL1.c_str(),
                curL2.c_str(), curWho.c_str());
  draw();
  server.send(200, "text/plain", "ok\n");
}

void handleWeather() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only\n"); return; }
  if (bodyTooBig()) return;
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "bad json\n"); return; }
  wx.temp  = (const char*)(doc["temp"]  | "--");
  wx.cond  = (const char*)(doc["cond"]  | "");
  wx.rain  = (const char*)(doc["rain"]  | "");
  wx.pigs  = (const char*)(doc["pigs"]  | "");
  wx.place = (const char*)(doc["place"] | "");
  wx.valid = true; wx.ms = millis();
  if (curState == "RADAR" || curState == "IDLE") draw();
  server.send(200, "text/plain", "ok\n");
}

void handlePlane() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only\n"); return; }
  if (bodyTooBig()) return;
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "bad json\n"); return; }
  if (doc["clear"] | false) {
    planeCount = 0; plane.valid = false;
    curState = "IDLE"; draw();
    server.send(200, "text/plain", "cleared\n"); return;
  }
  radarRangeKm = doc["range"] | 40.0f;
  if (radarRangeKm < 1.0f) radarRangeKm = 40.0f;   // guard: /0 in drawRadar
  planeCount = 0;
  for (JsonObject o : doc["planes"].as<JsonArray>()) {
    if (planeCount >= MAX_PLANES) break;
    Plane &q = planes[planeCount];
    q.east  = o["east"]  | 0.0f;   q.north = o["north"] | 0.0f;
    q.track = o["track"] | 0.0f;   q.gs    = o["gs"]    | 0.0f;
    q.alt   = o["alt"]   | 0;      q.vs    = o["vs"]    | 0;
    q.dist  = o["dist"]  | 0.0f;   q.cpa   = o["cpa"]   | 0.0f;
    q.eta   = o["eta"]   | -1;
    q.approaching = o["approaching"] | true;
    q.callsign = (const char*)(o["cs"]      | "?");
    q.type     = (const char*)(o["type"]    | "");
    q.airline  = (const char*)(o["airline"] | "");
    q.route    = (const char*)(o["route"]   | "");
    // These two were the missing link. The struct had `routefull`, drawRadar()
    // already printed it — but nothing ever parsed it out of the JSON, so it was
    // permanently "" and the expanded route silently never appeared. spotter.py
    // had been sending it the whole time.
    q.routefull = (const char*)(o["routefull"] | "");
    q.routemid  = (const char*)(o["routemid"]  | "");
    q.valid = true; q.lastMs = millis();
    planeCount++;
  }
  if (planeCount) { plane = planes[0]; plane.valid = true; }   // [0] is the primary
  else plane.valid = false;

  // The radar is ambient information. It must never push aside an agent that is
  // actually blocked waiting for David — the same clobbering fault the per-agent
  // slots fixed, and it would have come straight back in through this endpoint.
  recordAndPick("PLANE", "RADAR", "", "");
  lastStateMs = millis();
  if (curState == "RADAR") drawRadar(); else draw();
  server.send(200, "text/plain", String(planeCount) + " contacts, showing " + curState + "\n");
}

void handleFleet() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only\n"); return; }
  if (bodyTooBig()) return;
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) { server.send(400, "text/plain", "bad json\n"); return; }

  fleetStamp = doc["stamp"] | "";
  JsonArray arr = doc["machines"];
  fleetCount = 0; fleetDropped = 0;
  for (JsonObject m : arr) {
    if (fleetCount >= MAX_MACHINES) { fleetDropped++; continue; }
    fleet[fleetCount].name   = (const char*)(m["name"]   | "?");
    fleet[fleetCount].up     = m["up"] | false;
    fleet[fleetCount].st     = m["state"] | (m["up"] ? 1 : 0);
    fleet[fleetCount].detail = (const char*)(m["detail"] | "");
    fleetCount++;
  }
  Serial.printf("fleet <- %d machines\n", fleetCount);
  if (curState == "IDLE") drawFleet();
  server.send(200, "text/plain", "ok\n");
}

void handleMascot() {
  if (server.method() == HTTP_POST) {
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain"))) mascot = doc["mascot"] | mascot;
  } else mascot = (mascot + 1) % 2;
  prefs.begin("puck", false); prefs.putUChar("mascot", mascot); prefs.end();
  draw();
  server.send(200, "text/plain", String(mascot ? "cat" : "helmet") + "\n");
}

void handleRotate() {
  uint8_t want = rot;
  if (server.method() == HTTP_POST) {
    JsonDocument doc;
    if (!deserializeJson(doc, server.arg("plain"))) want = doc["rotation"] | rot;
  } else {
    want = (rot + 1) % 4;          // GET /rotate just cycles — handy from a phone
  }
  rot = want % 4;
  prefs.begin("puck", false); prefs.putUChar("rot", rot); prefs.end();
  tft.setRotation(rot);
  Serial.printf("rotation -> %d (%dx%d)\n", rot, W(), H());
  draw();
  server.send(200, "text/plain", "rotation " + String(rot) + "\n");
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nRangerPuck — step 4: wifi + http");

  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, HIGH);
  led.begin(); led.setBrightness(40);
  led.setPixelColor(0, led.Color(255, 140, 0)); led.show();

  SPI.begin(LCD_SCK, -1, LCD_MOSI, LCD_CS);
  tft.init(LCD_W, LCD_H, SPI_MODE0);
  tft.setSPISpeed(40000000);
  prefs.begin("puck", true); rot = prefs.getUChar("rot", 1); mascot = prefs.getUChar("mascot", 0); prefs.end();
  tft.setRotation(rot);
  Serial.printf("rotation %d (%dx%d)\n", rot, W(), H());

  showBoot("connecting wifi...");
  Serial.printf("connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(PUCK_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 30000) { delay(400); Serial.print("."); }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WIFI FAILED — check SSID/password in secrets.h, and that it is 2.4GHz");
    showBoot("wifi FAILED");
    tft.setTextSize(2); tft.setTextColor(ST77XX_RED);
    tft.setCursor(6, 150); tft.println("check");
    tft.setCursor(6, 175); tft.println("secrets.h");
    led.setPixelColor(0, led.Color(255, 0, 0)); led.show();
    // do NOT return — loop() retries Wi-Fi, but if the server was never started
    // the board rejoins the network deaf until someone power-cycles it. After a
    // power cut the router usually boots slower than the puck, so this is the
    // NORMAL case, not an edge case.
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Only claim this when it is true. The old code printed "connected, IP
    // 0.0.0.0" immediately after announcing WIFI FAILED — actively misleading at
    // exactly the moment you are staring at the serial monitor.
    Serial.print("connected, IP "); Serial.println(WiFi.localIP());
    Serial.printf("RSSI %d dBm\n", WiFi.RSSI());
    wasConnected = true;
  }
  startMdns();

  server.on("/state", handleState);
  server.on("/fleet", handleFleet);
  server.on("/plane", handlePlane);
  server.on("/weather", handleWeather);
  server.on("/rotate", handleRotate);
  server.on("/mascot", handleMascot);
  server.on("/", []() {
    // Report what the board BELIEVES it is showing, not just that it is alive.
    // Without this, "did the expanded route arrive?" could only be answered by
    // walking over and looking at a 1.47" screen — which is how routefull sat
    // unparsed and unnoticed in the first place. A status endpoint that cannot be
    // used to diagnose the thing it reports on is decoration.
    String s = "RangerPuck\nstate: " + curState +
      "\nip: " + WiFi.localIP().toString() +
      "\nrssi: " + String(WiFi.RSSI()) + " dBm" +
      "\nuptime: " + String(millis()/1000) + "s" +
      "\nwho: " + (curWho.length() ? curWho : String("-")) +
      "\nline1: " + curL1 + "\nline2: " + curL2 +
      "\nrot: " + String(rot) + (rot % 2 ? " (landscape)" : " (portrait)") +
      "\nscreen: " + String(ambientScreen) + "\ntime: " + (timeReady ? "synced" : "UNSET") +
      "\ncontacts: " + String(planeCount) + "\n";
    for (int i = 0; i < MAX_AGENTS; i++) {
      if (!agents[i].used) continue;
      s += "agent[" + String(i) + "]: " + agents[i].who + " " + agents[i].state +
           " (u" + String(urgency(agents[i].state)) + ", " +
           String((millis() - agents[i].ms) / 1000) + "s ago)" +
           (i == curSlot ? "  <- on screen" : "") + "\n";
    }
    if (plane.valid) {
      s += "primary: " + plane.callsign + " " + plane.type + " " + plane.airline +
           "\n  route:     " + plane.route +
           "\n  routefull: " + plane.routefull +
           "\n  routemid:  " + plane.routemid +
           "\n  showing:   " + ((rot % 2) ? plane.routefull : plane.routemid) + "\n";
    }
    server.send(200, "text/plain", s);
  });
  server.begin();
  Serial.println("http server up on :80");
  syncTime();

  curState = "IDLE"; curL1 = "ready"; curL2 = "rangerpuck";
  draw();
}

void loop() {
  server.handleClient();

  // --- alternate the two ambient screens ----------------------------------
  if (curState == "RADAR" || curState == "IDLE") {
    if (millis() - ambientSwapMs > AMBIENT_SWAP) {
      ambientSwapMs = millis();
      ambientScreen = (ambientScreen + 1) % AMBIENT_COUNT;
      draw();
    }
  }

  if (curState == "PIGSOON") {
    static uint32_t lastPulse = 0; static bool on = false;
    if (millis() - lastPulse > 450) {
      lastPulse = millis(); on = !on;
      led.setPixelColor(0, on ? led.Color(200, 0, 0) : led.Color(10, 0, 0));
      led.show();
    }
  }

  // --- second hand, and a periodic NTP re-sync -----------------------------
  if ((curState == "RADAR" || curState == "IDLE") && ambientScreen == 3 && timeReady) {
    static uint32_t lastSec = 0;
    if (millis() - lastSec >= 1000) { lastSec = millis(); drawClock(); }
  }
  // Keep asking until the clock is set, then just top it up. The old condition
  // was `timeReady && ...`, which could only ever run AFTER a success.
  if (millis() - lastNtpMs > (timeReady ? NTP_REFRESH : NTP_RETRY)) syncTime();

  // --- move the aircraft between API updates so it CRAWLS, never jumps -----
  if (curState == "RADAR" && planeCount && ambientScreen == 0) {
    // 400ms meant a full-screen repaint two and a half times a second, which
    // reads as flashing from across a room. At 20km an aircraft moves about 200m
    // a second — a pixel or two — so redrawing that often showed nothing new and
    // cost a flicker. One second is smooth and calm.
    static uint32_t lastTick = 0;
    // lastTick started at 0, so the FIRST tick computed dt = the board's entire
    // uptime in seconds. An hour of uptime gave dt ~3600: at 400kt that flung the
    // icon thousands of km on one frame. The `fresh` check below bounds how STALE
    // the fix may be, not how big dt may be — so it did not help here.
    if (!lastTick) lastTick = millis();
    if (millis() - lastTick > 1000) {
      float dt = (millis() - lastTick) / 1000.0f;
      if (dt > 5.0f) dt = 5.0f;              // belt and braces after any stall
      lastTick = millis();
      // Extrapolate only as far as the data can justify. A jet at 400kt covers
      // 7km a minute, so running dead reckoning until the 3-minute drop-out
      // puts the icon 20km from where the aircraft actually is — the screen was
      // reading 4.4km while the API said 17.3km. Freeze after 45 seconds and
      // let the display show the last KNOWN position instead of a guess.
      bool fresh = (millis() - planes[0].lastMs) < 45000UL;
      if (fresh) {
        for (int i = 0; i < planeCount; i++) {
          float kmps = planes[i].gs * 1.852f / 3600.0f;    // knots -> km/s
          float a = planes[i].track * 3.14159265f / 180.0f;
          planes[i].east  += sinf(a) * kmps * dt;
          planes[i].north += cosf(a) * kmps * dt;
        }
      }
      if (planeCount) { float e=planes[0].east, n=planes[0].north;
                        plane.east=e; plane.north=n; }

      // Skip the repaint when nothing has moved far enough to see. A parked or
      // slow contact used to repaint the whole screen every tick for no visible
      // change at all — pure flicker, no information.
      static float lastE = 1e9, lastN = 1e9;
      float pxPerKm = ((rot % 2 ? LCD_W : LCD_H) / 2 - 14) / radarRangeKm;
      if (fabsf(planes[0].east - lastE) * pxPerKm > 1.0f ||
          fabsf(planes[0].north - lastN) * pxPerKm > 1.0f) {
        lastE = planes[0].east; lastN = planes[0].north;
        drawRadar();
      }
    }
    // no contact for 3 minutes: it has gone out of range
    if (millis() - planes[0].lastMs > 180000UL) {
      planeCount = 0; plane.valid = false; curState = "IDLE"; draw();
    }
  }

  // --- expire a stale state and fall back to the fleet view -----------------
  if (curSlot >= 0 && agents[curSlot].used
      && curState != "IDLE" && curState != "APPROVE") {
    bool working = (curState == "THINKING" || curState == "RUNNING" || curState == "SYNC");
    uint32_t limit = working            ? EXPIRE_WORKING
                   : curState == "WAITING" ? EXPIRE_NUDGE
                   : curState == "SKY"     ? EXPIRE_NUDGE * 3   // an ISS pass is ~6 min
                                           : EXPIRE_SETTLED;
    if (millis() - agents[curSlot].ms > limit) {
      Serial.printf("expired '%s' (%s)\n", curState.c_str(), curWho.c_str());
      // "What is shown" and "what is known" were mutated separately: an expired
      // state stayed in its slot and was re-picked by the next POST from anyone,
      // flashing back every 20s and eating fresher lower-urgency states.
      agents[curSlot].used = false;
      curSlot = -1;
      int nx = -1;
      for (int i = 0; i < MAX_AGENTS; i++) {
        if (!agents[i].used) continue;
        if (nx < 0 || urgency(agents[i].state) > urgency(agents[nx].state)) nx = i;
      }
      if (nx >= 0) {
        curState = agents[nx].state; curL1 = agents[nx].l1;
        curL2 = agents[nx].l2; curWho = agents[nx].who; curSlot = nx;
      } else {
        curState = "IDLE"; curL1 = "ready"; curL2 = ""; curWho = "";
      }
      draw();
    }
  }
  if (WiFi.status() != WL_CONNECTED) {   // reconnect quietly if the router blips
    static uint32_t last = 0;
    if (millis() - last > 10000) { last = millis(); WiFi.reconnect(); }
    wasConnected = false;
  } else if (!wasConnected) {
    // Just (re)joined. Re-announce mDNS — the record is tied to the address we
    // had, so after a lease change rangerpuck.local pointed at nothing. And take
    // the chance to set the clock if it is still unset.
    wasConnected = true;
    Serial.print("wifi up, IP "); Serial.println(WiFi.localIP());
    startMdns();
    if (!timeReady) syncTime();
  }
}
