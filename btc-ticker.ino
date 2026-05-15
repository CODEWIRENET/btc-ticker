#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

constexpr int8_t PIN_TFT_MOSI = 6;
constexpr int8_t PIN_TFT_SCLK = 7;
constexpr int8_t PIN_TFT_CS   = 14;
constexpr int8_t PIN_TFT_DC   = 15;
constexpr int8_t PIN_TFT_RST  = 21;
constexpr int8_t PIN_TFT_BL   = 22;
constexpr int8_t PIN_BOOT_BTN = 9;
constexpr unsigned long BOOT_HOLD_MS = 3000;

constexpr int TFT_W = 172;
constexpr int TFT_H = 320;

constexpr unsigned long FETCH_INTERVAL_MS  = 60UL * 1000UL;
constexpr unsigned long RENDER_INTERVAL_MS = 500UL;

constexpr int           TREND_SAMPLES            = 13;
constexpr unsigned long TREND_SAMPLE_INTERVAL_MS = 10UL * 60UL * 1000UL;
constexpr double        TREND_FLAT_EPSILON_USD   = 0.50;

constexpr const char* AP_SSID                = "btc-ticker";
constexpr const char* AP_PASS                = nullptr;
constexpr int         CONFIG_PORTAL_TIMEOUT_S = 300;
constexpr int         CONNECT_TIMEOUT_S       = 30;

constexpr uint16_t COL_BG       = 0x0000;
constexpr uint16_t COL_FG       = 0xFFFF;
constexpr uint16_t COL_DIM      = 0x7BEF;
constexpr uint16_t COL_BTC      = 0xFCA0;
constexpr uint16_t COL_UP       = 0x07E0;
constexpr uint16_t COL_DOWN     = 0xF800;
constexpr uint16_t COL_FLAT     = 0xFFE0;
constexpr uint16_t COL_BLUE     = 0x049F;

enum LogoKind : uint8_t { LOGO_BITCOIN, LOGO_SYMBOL };

struct Coin {
  const char* geckoId;
  const char* symbol;
  const char* name;
  uint16_t    color;
  LogoKind    logo;
};

static const Coin COINS[] = {
  {"bitcoin",     "BTC",  "Bitcoin",    0xFCA0, LOGO_BITCOIN},
  {"ethereum",    "ETH",  "Ethereum",   0x73BE, LOGO_SYMBOL },
  {"solana",      "SOL",  "Solana",     0xA57F, LOGO_SYMBOL },
  {"binancecoin", "BNB",  "BNB",        0xFDE0, LOGO_SYMBOL },
  {"ripple",      "XRP",  "XRP",        0xC618, LOGO_SYMBOL },
  {"cardano",     "ADA",  "Cardano",    0x0AFF, LOGO_SYMBOL },
  {"dogecoin",    "DOGE", "Dogecoin",   0xFE60, LOGO_SYMBOL },
  {"polkadot",    "DOT",  "Polkadot",   0xF8BE, LOGO_SYMBOL },
  {"avalanche-2", "AVAX", "Avalanche",  0xF800, LOGO_SYMBOL },
  {"chainlink",   "LINK", "Chainlink",  0x32BE, LOGO_SYMBOL },
};
constexpr int COIN_COUNT = sizeof(COINS) / sizeof(COINS[0]);
constexpr const char* PREFS_NAMESPACE = "btc-ticker";
constexpr const char* PREFS_KEY_COIN  = "coin";

enum Trend : uint8_t { TREND_UNKNOWN, TREND_UP, TREND_DOWN, TREND_FLAT };
enum Mode  : uint8_t { MODE_BOOT, MODE_CONFIG, MODE_CONNECTING, MODE_LIVE };

Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_MOSI, PIN_TFT_SCLK, PIN_TFT_RST);
WiFiManager     wm;
Preferences     prefs;

static int g_coinIndex = 0;
static const Coin& coin() { return COINS[g_coinIndex]; }

static Mode          g_mode         = MODE_BOOT;
static Mode          g_renderedMode = MODE_BOOT;
static double        g_lastPrice    = 0.0;
static double        g_prevPrice    = 0.0;
static unsigned long g_lastFetchMs  = 0;
static bool          g_priceValid   = false;
static unsigned long g_lastRenderMs = 0;

constexpr int LIVE_PRICE_Y  = 178;
constexpr int LIVE_PRICE_H  = 30;
constexpr int LIVE_TREND_Y  = 220;
constexpr int LIVE_TREND_H  = 22;
constexpr int LIVE_STATUS_Y = 266;
constexpr int LIVE_STATUS_H = 14;
constexpr int LIVE_IP_Y     = TFT_H - 14;
constexpr int LIVE_IP_H     = 12;

static double        g_trendRing[TREND_SAMPLES] = {0};
static int           g_trendWriteIdx     = 0;
static int           g_trendCount        = 0;
static unsigned long g_lastTrendSampleMs = 0;
static Trend         g_trend             = TREND_UNKNOWN;

static void drawBitcoinLogo(int cx, int cy, int r, uint16_t bodyCol) {
  tft.fillCircle(cx, cy, r, bodyCol);

  int bodyW = r * 14 / 20;
  int bodyH = r * 6 / 5;
  int sx    = cx - bodyW / 2;
  int sy    = cy - bodyH / 2;
  int half  = bodyH / 2;

  tft.fillRoundRect(sx, sy,        bodyW, half,         3, COL_FG);
  tft.fillRoundRect(sx, sy + half, bodyW, bodyH - half, 3, COL_FG);

  int inset = 3;
  tft.fillRoundRect(sx + inset, sy + inset,
                    bodyW - 2 * inset, half - 2 * inset, 2, bodyCol);
  tft.fillRoundRect(sx + inset, sy + half + inset,
                    bodyW - 2 * inset, bodyH - half - 2 * inset, 2, bodyCol);

  int stemW = bodyW / 3;
  tft.fillRect(sx, sy, stemW, bodyH, COL_FG);

  int tickW = 3;
  int tickH = 6;
  int tick1X = sx + 1;
  int tick2X = sx + stemW - tickW - 1;
  tft.fillRect(tick1X, sy - tickH, tickW, tickH, bodyCol);
  tft.fillRect(tick2X, sy - tickH, tickW, tickH, bodyCol);
  tft.fillRect(tick1X, sy + bodyH, tickW, tickH, bodyCol);
  tft.fillRect(tick2X, sy + bodyH, tickW, tickH, bodyCol);
}

static void drawSymbolLogo(int cx, int cy, int r, uint16_t bodyCol, const char* sym) {
  tft.fillCircle(cx, cy, r, bodyCol);
  tft.drawCircle(cx, cy, r - 2, COL_FG);

  int len = strlen(sym);
  uint8_t size = (len <= 3) ? 3 : 2;
  int charW = 6 * size;
  int charH = 8 * size;
  int textW = charW * len;

  tft.setTextWrap(false);
  tft.setTextSize(size);
  tft.setTextColor(COL_FG);
  tft.setCursor(cx - textW / 2, cy - charH / 2);
  tft.print(sym);
}

static void drawCoinLogo(int cx, int cy, int r) {
  const Coin& c = coin();
  if (c.logo == LOGO_BITCOIN) drawBitcoinLogo(cx, cy, r, c.color);
  else                        drawSymbolLogo (cx, cy, r, c.color, c.symbol);
}

static void pushTrendSample(double price) {
  g_trendRing[g_trendWriteIdx] = price;
  g_trendWriteIdx = (g_trendWriteIdx + 1) % TREND_SAMPLES;
  if (g_trendCount < TREND_SAMPLES) g_trendCount++;
}

static Trend computeTrend(double current) {
  if (g_trendCount < 1) return TREND_UNKNOWN;
  int oldestIdx = (g_trendCount < TREND_SAMPLES) ? 0 : g_trendWriteIdx;
  double oldest = g_trendRing[oldestIdx];
  double delta  = current - oldest;
  if (delta >  TREND_FLAT_EPSILON_USD) return TREND_UP;
  if (delta < -TREND_FLAT_EPSILON_USD) return TREND_DOWN;
  return TREND_FLAT;
}

static void setLED(uint8_t r, uint8_t g, uint8_t b) {
#if defined(RGB_BUILTIN)
  rgbLedWrite(RGB_BUILTIN, r, g, b);
#else
  (void)r; (void)g; (void)b;
#endif
}

static void updateLEDForMode() {
  static bool blink = false;
  blink = !blink;
  switch (g_mode) {
    case MODE_CONFIG:
      setLED(0, 0, 64);
      break;
    case MODE_CONNECTING:
      setLED(0, 0, blink ? 48 : 4);
      break;
    case MODE_LIVE:
      switch (g_trend) {
        case TREND_UP:   setLED( 0, 48,  0); break;
        case TREND_DOWN: setLED(48,  0,  0); break;
        case TREND_FLAT: setLED(24, 16,  0); break;
        default:         setLED( 2,  2,  4); break;
      }
      break;
    case MODE_BOOT:
    default:
      setLED(4, 4, 4);
      break;
  }
}

static const char* trendLabel(Trend t) {
  switch (t) {
    case TREND_UP:   return "UP 2h";
    case TREND_DOWN: return "DN 2h";
    case TREND_FLAT: return "FLAT";
    default:         return "WARMING";
  }
}

static uint16_t trendColor(Trend t) {
  switch (t) {
    case TREND_UP:   return COL_UP;
    case TREND_DOWN: return COL_DOWN;
    case TREND_FLAT: return COL_FLAT;
    default:         return COL_DIM;
  }
}

static String formatPrice(double p) {
  long v = (long)(p + 0.5);
  String num = String(v);
  String out = "$";
  int len = num.length();
  for (int i = 0; i < len; i++) {
    if (i > 0 && (len - i) % 3 == 0) out += ",";
    out += num[i];
  }
  return out;
}

static void renderConfig() {
  if (g_renderedMode == MODE_CONFIG) return;
  g_renderedMode = MODE_CONFIG;
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);

  tft.setTextSize(2);
  tft.setTextColor(COL_BLUE);
  tft.setCursor(8, 10);
  tft.print("SETUP MODE");
  tft.drawFastHLine(8, 32, TFT_W - 16, COL_BLUE);

  tft.setTextSize(1);
  tft.setTextColor(COL_FG);
  tft.setCursor(8, 50);
  tft.print("1) Join WiFi:");
  tft.setTextSize(2);
  tft.setTextColor(COL_BTC);
  tft.setCursor(8, 68);
  tft.print(AP_SSID);

  tft.setTextSize(1);
  tft.setTextColor(COL_FG);
  tft.setCursor(8, 110);
  tft.print("2) Open in browser:");
  tft.setTextSize(2);
  tft.setTextColor(COL_BTC);
  tft.setCursor(8, 128);
  tft.print("192.168.4.1");

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.setCursor(8, 180);
  tft.print("3) Enter your WiFi");
  tft.setCursor(8, 192);
  tft.print("   and save.");

  drawCoinLogo(TFT_W / 2, 260, 30);

  tft.setTextSize(1);
  tft.setTextColor(COL_BLUE);
  tft.setCursor(8, TFT_H - 12);
  tft.print("waiting for config...");

  tft.setTextColor(COL_DIM);
  tft.setCursor(8, 300);
  tft.print("Configure WiFi -> pick coin");
}

static void renderConnecting(const char* ssid) {
  if (g_renderedMode == MODE_CONNECTING) return;
  g_renderedMode = MODE_CONNECTING;
  tft.fillScreen(COL_BG);
  drawCoinLogo(TFT_W / 2, 90, 50);

  tft.setTextWrap(false);
  tft.setTextSize(2);
  tft.setTextColor(COL_BLUE);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds("Connecting", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_W - (int)w) / 2, 170);
  tft.print("Connecting");

  tft.setTextSize(1);
  tft.setTextColor(COL_DIM);
  tft.getTextBounds("to WiFi...", 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_W - (int)w) / 2, 200);
  tft.print("to WiFi...");

  if (ssid && *ssid) {
    tft.setTextColor(COL_FG);
    tft.getTextBounds(ssid, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_W - (int)w) / 2, 230);
    tft.print(ssid);
  }
}

static void drawCentered(const String& s, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t x1, y1; uint16_t w, h;
  tft.getTextBounds(s.c_str(), 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((TFT_W - (int)w) / 2, y);
  tft.print(s);
}

static void renderLive() {
  bool firstTime = (g_renderedMode != MODE_LIVE);

  if (firstTime) {
    tft.fillScreen(COL_BG);
    drawCoinLogo(TFT_W / 2, 70, 52);
    String label = String(coin().symbol) + " / USD";
    drawCentered(label, 148, 2, COL_DIM);
    g_renderedMode = MODE_LIVE;
  }

  static double      lastPrice  = -1.0;
  static bool        lastValid  = false;
  static Trend       lastTrend  = (Trend)255;
  static String      lastStatus = "";
  static String      lastIp     = "";

  bool priceChanged = firstTime || lastValid != g_priceValid || lastPrice != g_lastPrice;
  bool trendChanged = firstTime || lastTrend != g_trend;

  if (priceChanged || trendChanged) {
    tft.fillRect(0, LIVE_PRICE_Y, TFT_W, LIVE_PRICE_H, COL_BG);
    String priceStr = g_priceValid ? formatPrice(g_lastPrice) : String("----");
    drawCentered(priceStr, LIVE_PRICE_Y, 3, g_priceValid ? trendColor(g_trend) : COL_DIM);
    lastPrice = g_lastPrice;
    lastValid = g_priceValid;
  }

  if (trendChanged) {
    tft.fillRect(0, LIVE_TREND_Y, TFT_W, LIVE_TREND_H, COL_BG);
    const char* arrow;
    switch (g_trend) {
      case TREND_UP:   arrow = "\x18 "; break;
      case TREND_DOWN: arrow = "\x19 "; break;
      case TREND_FLAT: arrow = "- ";    break;
      default:         arrow = ".. ";   break;
    }
    drawCentered(String(arrow) + trendLabel(g_trend), LIVE_TREND_Y, 2, trendColor(g_trend));
    lastTrend = g_trend;
  }

  String statusStr;
  if (WiFi.status() != WL_CONNECTED) {
    statusStr = "offline";
  } else if (!g_priceValid) {
    statusStr = "fetching price...";
  } else {
    char buf[24];
    snprintf(buf, sizeof(buf), "samples %d/13", g_trendCount);
    statusStr = buf;
  }
  if (firstTime || statusStr != lastStatus) {
    tft.fillRect(0, LIVE_STATUS_Y, TFT_W, LIVE_STATUS_H, COL_BG);
    drawCentered(statusStr, LIVE_STATUS_Y + 2, 1, COL_DIM);
    lastStatus = statusStr;
  }

  String ipStr = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("");
  if (firstTime || ipStr != lastIp) {
    tft.fillRect(0, LIVE_IP_Y - 2, TFT_W, LIVE_IP_H + 2, COL_BG);
    if (ipStr.length()) drawCentered(ipStr, LIVE_IP_Y, 1, COL_DIM);
    lastIp = ipStr;
  }
}

static void render() {
  switch (g_mode) {
    case MODE_CONFIG:     renderConfig();                              break;
    case MODE_CONNECTING: renderConnecting(WiFi.SSID().c_str());       break;
    case MODE_LIVE:       renderLive();                                break;
    case MODE_BOOT:
    default:
      tft.fillScreen(COL_BG);
      tft.setTextSize(2);
      tft.setTextColor(COL_FG);
      tft.setCursor(8, 8);
      tft.print("btc-ticker");
      tft.setTextSize(1);
      tft.setTextColor(COL_DIM);
      tft.setCursor(8, 32);
      tft.print("booting...");
      break;
  }
}

static bool fetchPrice(double& out) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  String url = String("https://api.coingecko.com/api/v3/simple/price?ids=")
             + coin().geckoId + "&vs_currencies=usd";
  if (!http.begin(client, url)) {
    Serial.println(F("http.begin failed"));
    return false;
  }
  http.setTimeout(10000);
  http.setUserAgent("btc-ticker/1.0");
  int code = http.GET();
  if (code != 200) {
    Serial.printf("HTTP %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("JSON err: %s\n", err.c_str());
    return false;
  }
  JsonVariant v = doc[coin().geckoId]["usd"];
  if (v.isNull()) return false;
  out = v.as<double>();
  return true;
}

static void loadCoinFromPrefs() {
  prefs.begin(PREFS_NAMESPACE, true);
  String saved = prefs.getString(PREFS_KEY_COIN, "bitcoin");
  prefs.end();
  for (int i = 0; i < COIN_COUNT; i++) {
    if (saved.equals(COINS[i].geckoId)) { g_coinIndex = i; return; }
  }
  g_coinIndex = 0;
}

static void saveCoinToPrefs(const char* geckoId) {
  prefs.begin(PREFS_NAMESPACE, false);
  prefs.putString(PREFS_KEY_COIN, geckoId);
  prefs.end();
}

static String buildCoinDatalist() {
  String s = "<datalist id='coins'>";
  for (int i = 0; i < COIN_COUNT; i++) {
    s += "<option value='";
    s += COINS[i].geckoId;
    s += "'>";
    s += COINS[i].name;
    s += " (";
    s += COINS[i].symbol;
    s += ")";
  }
  s += "</datalist>";
  return s;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println(F("btc-ticker boot"));

  setLED(4, 4, 4);

  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
  pinMode(PIN_BOOT_BTN, INPUT_PULLUP);

  tft.init(TFT_W, TFT_H);
  tft.setRotation(0);
  tft.fillScreen(COL_BG);
  Serial.println(F("ST7789 init done"));
  render();

  loadCoinFromPrefs();
  Serial.printf("loaded coin: %s (%s)\n", coin().geckoId, coin().symbol);

  static String coinDatalist = buildCoinDatalist();
  static WiFiManagerParameter paramDatalist(coinDatalist.c_str());
  static WiFiManagerParameter paramCoin("coin", "Coin (tap to choose)",
                                        coin().geckoId, 24, "list='coins'");
  wm.addParameter(&paramDatalist);
  wm.addParameter(&paramCoin);

  wm.setConnectTimeout(CONNECT_TIMEOUT_S);
  wm.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT_S);
  static std::vector<const char*> wmMenu = {"wifi", "info", "exit"};
  wm.setMenu(wmMenu);
  wm.setShowPassword(false);
  wm.setRemoveDuplicateAPs(true);
  wm.setMinimumSignalQuality(-1);
  wm.setScanDispPerc(true);
  wm.setAPCallback([](WiFiManager*) {
    Serial.printf("config AP up: SSID=%s  open http://192.168.4.1\n", AP_SSID);
    g_mode = MODE_CONFIG;
    render();
    updateLEDForMode();
  });
  wm.setSaveConfigCallback([]() {
    Serial.println(F("config saved, connecting"));
    const char* picked = paramCoin.getValue();
    if (picked && *picked) {
      for (int i = 0; i < COIN_COUNT; i++) {
        if (strcmp(picked, COINS[i].geckoId) == 0) {
          if (i != g_coinIndex) {
            g_coinIndex = i;
            saveCoinToPrefs(picked);
            g_trendCount    = 0;
            g_trendWriteIdx = 0;
            g_priceValid    = false;
            g_lastPrice     = 0;
            g_trend         = TREND_UNKNOWN;
          }
          Serial.printf("selected coin: %s (%s)\n", COINS[i].geckoId, COINS[i].symbol);
          break;
        }
      }
    }
    g_mode = MODE_CONNECTING;
    g_renderedMode = MODE_BOOT;
    render();
    updateLEDForMode();
  });

  g_mode = MODE_CONNECTING;
  render();
  updateLEDForMode();

  bool ok = wm.autoConnect(AP_SSID, AP_PASS);
  if (!ok) {
    Serial.println(F("WiFi config failed — restarting"));
    delay(1000);
    ESP.restart();
  }

  Serial.print(F("connected, IP="));
  Serial.println(WiFi.localIP());
  g_mode = MODE_LIVE;
  updateLEDForMode();
  render();
}

static void enterConfigPortalRuntime() {
  Serial.println(F("BOOT held — re-opening config portal"));
  g_mode = MODE_CONFIG;
  g_renderedMode = MODE_BOOT;
  render();
  updateLEDForMode();
  wm.startConfigPortal(AP_SSID, AP_PASS);
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("no WiFi after portal — restart"));
    delay(500);
    ESP.restart();
  }
  g_mode = MODE_LIVE;
  g_renderedMode = MODE_BOOT;
  g_lastFetchMs  = 0;
  updateLEDForMode();
}

void loop() {
  unsigned long now = millis();

  static unsigned long bootHeldSince = 0;
  if (digitalRead(PIN_BOOT_BTN) == LOW) {
    if (bootHeldSince == 0) bootHeldSince = now;
    else if (now - bootHeldSince >= BOOT_HOLD_MS) {
      bootHeldSince = 0;
      enterConfigPortalRuntime();
      return;
    }
  } else {
    bootHeldSince = 0;
  }

  if (g_mode == MODE_LIVE && WiFi.status() == WL_CONNECTED &&
      (g_lastFetchMs == 0 || now - g_lastFetchMs >= FETCH_INTERVAL_MS)) {
    double p;
    if (fetchPrice(p)) {
      g_prevPrice  = g_lastPrice;
      g_lastPrice  = p;
      g_priceValid = true;
      Serial.printf("BTC = $%.2f\n", p);

      if (g_trendCount == 0 ||
          now - g_lastTrendSampleMs >= TREND_SAMPLE_INTERVAL_MS) {
        pushTrendSample(p);
        g_lastTrendSampleMs = now;
      }
      g_trend = computeTrend(p);
      updateLEDForMode();
      Serial.printf("trend=%s samples=%d\n", trendLabel(g_trend), g_trendCount);
    } else {
      Serial.println(F("fetchPrice failed"));
    }
    g_lastFetchMs = now;
  }

  if (now - g_lastRenderMs >= RENDER_INTERVAL_MS) {
    render();
    if (g_mode == MODE_CONNECTING) updateLEDForMode();
    g_lastRenderMs = now;
  }
  delay(20);
}
