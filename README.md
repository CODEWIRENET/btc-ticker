# btc-ticker

ESP32-C6 cryptocurrency price ticker for a 1.47" 172×320 ST7789 colour TFT.
Live price + 2-hour trend indicator, configured entirely from your phone — no
hard-coded WiFi credentials.

## Hardware

- **Board:** LAFVIN ESP32-C6FH4 (RISC-V, WiFi 6, 4 MB flash)
- **Display:** 1.47" 172×320 ST7789 TFT (SPI)
- **Pins:** `MOSI=6 SCK=7 CS=14 DC=15 RST=21 BL=22`, BOOT button `GPIO9`
- **LED:** on-board WS2812 (`RGB_BUILTIN`, GPIO 8)

## Features

- Live price for 10 coins: BTC, ETH, SOL, BNB, XRP, ADA, DOGE, DOT, AVAX, LINK
- Source: CoinGecko public API (HTTPS, 60 s interval)
- **WiFiManager captive portal** — join the `btc-ticker` AP, pick your network
  and coin from a list (HTML5 datalist), saved to NVS
- **2-hour trend RGB LED:** green = up, red = down, yellow = flat, blue = setup
- Differential TFT rendering (only changed regions repaint — no flicker)
- Hold **BOOT 3 s** in live mode to re-open the config portal (change coin/WiFi
  without reflashing)

## Build & flash

Requires [`arduino-cli`](https://arduino.github.io/arduino-cli/) with the
`esp32:esp32` core (≥ 3.3.8) and libraries: Adafruit ST7735/ST7789, Adafruit
GFX, ArduinoJson, WiFiManager.

```
arduino-cli compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota .
arduino-cli upload  --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=no_ota -p COM5 .
```

(Find the port with [CheckESP32](https://github.com/CODEWIRENET/CheckESP32).)

## First run

1. TFT shows **SETUP MODE** (blue LED). Join the `btc-ticker` WiFi AP from your
   phone.
2. Open `http://192.168.4.1` → **Configure WiFi** → pick network + coin → Save.
3. Device reconnects and shows the live price; LED reflects the 2-hour trend.

## License

MIT — see [LICENSE](LICENSE).
