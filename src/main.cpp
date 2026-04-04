// ═══════════════════════════════════════════════════════════════
//  Robot Game Scoreboard — main firmware
// ═══════════════════════════════════════════════════════════════
//
//  Receives OSC-over-UDP commands and renders text/scores on
//  six daisy-chained 32×8 NeoPixel matrix panels.
//
//  Build environments are defined in platformio.ini.
// ═══════════════════════════════════════════════════════════════

#include <Arduino.h>
#include "config.h"
#include "DisplayManager.h"
#include "OSCHandler.h"

#ifdef USE_WIFI
  #include "WebInterface.h"
#endif

#ifdef USE_M5UNIFIED
  #include <M5Unified.h>
#endif

DisplayManager displayManager;
OSCHandler     osc(displayManager);
#ifdef USE_WIFI
WebInterface   webUI(osc);
#endif
bool           networkUp = false;

// ── Helper: show status on the AtomS3 built-in LCD ───────────
#ifdef USE_M5UNIFIED
static void lcdStatus(const char* line1, const char* line2 = nullptr,
                      uint32_t color = TFT_WHITE) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.setTextSize(2);          // 12×16 px chars — readable on 128×128
    M5.Display.setTextDatum(MC_DATUM);  // middle-centre
    M5.Display.drawString(line1, M5.Display.width() / 2,
                          line2 ? M5.Display.height() / 2 - 14
                                : M5.Display.height() / 2);
    if (line2) {
        M5.Display.setTextSize(1);
        M5.Display.drawString(line2, M5.Display.width() / 2,
                              M5.Display.height() / 2 + 14);
    }
}

/// Show AP info (SSID + IP) on the top, and optional small status at bottom.
static void lcdShowAP(const char* ssid, const char* ip) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextDatum(MC_DATUM);
    // SSID
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString(ssid, M5.Display.width() / 2, 24);
    // IP
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString(ip, M5.Display.width() / 2, 52);
    // Hint
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.drawString("tap to stop AP", M5.Display.width() / 2, 78);
}

/// Show "AP off" idle screen.
static void lcdShowIdle() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString("Scoreboard", M5.Display.width() / 2, M5.Display.height() / 2 - 10);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.drawString("tap to start AP", M5.Display.width() / 2, M5.Display.height() / 2 + 16);
}

/// Show small data line at the very bottom of the LCD.
static void lcdBottomLine(const char* text) {
    // Clear only the bottom strip
    M5.Display.fillRect(0, M5.Display.height() - 12, M5.Display.width(), 12, TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(BC_DATUM);  // bottom-centre
    M5.Display.drawString(text, M5.Display.width() / 2, M5.Display.height() - 1);
}
#endif

// ── AP toggle ────────────────────────────────────────────────
#ifdef USE_WIFI
static void toggleAP() {
    if (webUI.isRunning()) {
        webUI.stopAP();
#ifdef USE_M5UNIFIED
        lcdShowIdle();
#endif
        // Re-establish STA WiFi for OSC
        if (osc.begin()) {
            networkUp = true;
            Serial.printf("WiFi STA reconnected — IP %s\n", osc.localIP().toString().c_str());
        }
    } else {
        // Disconnect STA WiFi before starting AP
        WiFi.disconnect(true);
        networkUp = false;
        IPAddress ip = webUI.startAP();
#ifdef USE_M5UNIFIED
        lcdShowAP(webUI.ssid(), ip.toString().c_str());
#endif
    }
}
#endif

// ── Setup ────────────────────────────────────────────────────
void setup() {
#ifdef USE_M5UNIFIED
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);
    Serial.begin(115200);            // explicit init for USB-CDC on ESP32-S3
    delay(1000);                     // let USB-CDC settle
    M5.Display.setRotation(0);
    lcdStatus("Scoreboard", "v0.1", TFT_CYAN);
#else
    Serial.begin(115200);
    delay(1000);   // let USB-CDC settle on S3 chips
#endif

    Serial.println();
    Serial.println("╔══════════════════════════════════╗");
    Serial.println("║   Robot Game Scoreboard v0.1     ║");
    Serial.println("╚══════════════════════════════════╝");
    Serial.printf("  NeoPixel pin : GPIO %d\n", NEOPIXEL_PIN);
    Serial.printf("  Displays     : %d × %d×%d\n",
                  NUM_DISPLAYS, MATRIX_TILE_WIDTH, MATRIX_TILE_HEIGHT);
    Serial.printf("  OSC port     : %d\n", OSC_PORT);
#ifdef USE_M5UNIFIED
    Serial.printf("  IMU          : %s\n", M5.Imu.isEnabled() ? "enabled" : "not detected");
#endif

#ifdef USE_RS485
    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    Serial.printf("  RS485        : RX=GPIO%d  TX=GPIO%d  %d baud\n",
                  RS485_RX_PIN, RS485_TX_PIN, RS485_BAUD);
#endif

    // Initialise LED matrices and run a quick self-test
    displayManager.begin();
    displayManager.showTestPattern();
    displayManager.startDisplay();

    // ── Network ────────────────────────────────────────────────

#if defined(USE_WIFI) || defined(USE_ETHERNET_W5500)
#ifdef USE_M5UNIFIED
    lcdStatus("Connecting", "network...", TFT_YELLOW);
#endif
    if (osc.begin()) {
        networkUp = true;
        IPAddress ip = osc.localIP();
        Serial.printf("Network up — IP %s\n", ip.toString().c_str());
#ifdef USE_M5UNIFIED
        lcdStatus("Ready", ip.toString().c_str(), TFT_GREEN);
#endif
        delay(4000);
    } else {
        Serial.println("*** Network failed — continuing serial-only ***");
#ifdef USE_M5UNIFIED
        lcdStatus("NET ERR", "serial only", TFT_RED);
#endif
        delay(2000);
    }
#endif

    displayManager.clearAll();
    displayManager.update();
    Serial.println(networkUp
        ? "Ready — waiting for OSC messages + serial commands …"
        : "Ready — serial-only mode (no network) …");
    Serial.println("Tap AtomS3 button to toggle WiFi AP + web UI");
#ifdef USE_M5UNIFIED
    lcdShowIdle();
#endif
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
#ifdef USE_M5UNIFIED
    M5.update();         // poll button, etc.
    // Button A (screen tap on AtomS3) toggles AP on/off
#ifdef USE_WIFI
    if (M5.BtnA.wasPressed()) {
        toggleAP();
    }
#endif
#endif
    if (networkUp) osc.update();  // read & dispatch incoming OSC packets
#ifdef USE_WIFI
    webUI.update();               // handle web clients (no-op when AP is off)
#endif
#if SERIAL_CMD_ENABLED
    osc.processSerial(); // read & dispatch serial text commands
#endif
#ifdef USE_RS485
    osc.processRS485();  // read & dispatch RS485 text commands
#endif
    displayManager.update();    // push pixel changes to the strip (no-op when idle)

    // Notify over serial when a scroll animation finishes
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        if (displayManager.scrollFinished(i)) {
            Serial.printf("SCROLL_DONE %d\n", i + 1);   // 1-based
        }
    }
}
