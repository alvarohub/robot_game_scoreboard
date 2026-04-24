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

#if SCOREBOARD_HAS_WIFI
  #include "WebInterface.h"
#endif

#if SCOREBOARD_HAS_M5UNIFIED
  #include <M5Unified.h>
#endif

DisplayManager displayManager;
OSCHandler     osc(displayManager);
#if SCOREBOARD_HAS_WIFI
WebInterface   webUI(osc);
#endif
bool           networkUp = false;

// ── Helper: show status on the AtomS3 built-in LCD ───────────
#if SCOREBOARD_HAS_M5UNIFIED
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
#if SCOREBOARD_HAS_WIFI
static void toggleAP() {
    if (!SCOREBOARD_WIFI_ENABLED) {
        return;
    }

    if (webUI.isRunning()) {
        webUI.stopServer();
        osc.stopWiFi();
        networkUp = false;

#if SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_STATION
        if (osc.begin()) {
            networkUp = true;
            Serial.printf("WiFi STA reconnected — IP %s\n", osc.localIP().toString().c_str());
#if SCOREBOARD_HAS_M5UNIFIED
            lcdStatus("Ready", osc.localIP().toString().c_str(), TFT_GREEN);
            delay(1000);
            lcdShowIdle();
#endif
        }
#else
#if SCOREBOARD_HAS_M5UNIFIED
        lcdShowIdle();
#endif
        Serial.println("WiFi AP disabled");
#endif
    } else {
        if (osc.startAccessPoint()) {
            networkUp = true;
            webUI.startServer();
            IPAddress ip = osc.localIP();
#if SCOREBOARD_HAS_M5UNIFIED
            lcdShowAP(webUI.ssid(), ip.toString().c_str());
#endif
            Serial.printf("WiFi AP active — SSID %s  IP %s\n",
                          webUI.ssid(), ip.toString().c_str());
        } else {
            networkUp = false;
#if SCOREBOARD_HAS_M5UNIFIED
            lcdStatus("AP ERR", "serial only", TFT_RED);
            delay(1000);
            lcdShowIdle();
#endif
            Serial.println("Failed to enable WiFi AP");
        }
    }
}
#endif

// ── Setup ────────────────────────────────────────────────────
void setup() {
#if SCOREBOARD_HAS_M5UNIFIED
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
#if SCOREBOARD_HAS_M5UNIFIED
    Serial.printf("  IMU          : %s\n", M5.Imu.isEnabled() ? "enabled" : "not detected");
#endif

#if SCOREBOARD_RS485_ENABLED
    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    Serial.printf("  RS485        : RX=GPIO%d  TX=GPIO%d  %d baud\n",
                  RS485_RX_PIN, RS485_TX_PIN, RS485_BAUD);
#endif

    // ── Network ────────────────────────────────────────────────

#if SCOREBOARD_HAS_ETHERNET || (SCOREBOARD_HAS_WIFI && SCOREBOARD_WIFI_ENABLED)
#if SCOREBOARD_HAS_WIFI && (SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_AP)
#if SCOREBOARD_HAS_M5UNIFIED
    lcdStatus("Starting", "access point...", TFT_YELLOW);
#endif
    if (osc.startAccessPoint()) {
        networkUp = true;
        webUI.startServer();
        IPAddress ip = osc.localIP();
        Serial.printf("Network up — AP %s @ %s\n", webUI.ssid(), ip.toString().c_str());
#if SCOREBOARD_HAS_M5UNIFIED
        lcdShowAP(webUI.ssid(), ip.toString().c_str());
#endif
    } else {
        Serial.println("*** Access point failed — continuing serial-only ***");
#if SCOREBOARD_HAS_M5UNIFIED
        lcdStatus("NET ERR", "serial only", TFT_RED);
#endif
        delay(2000);
    }
#else
#if SCOREBOARD_HAS_M5UNIFIED
    lcdStatus("Connecting", "network...", TFT_YELLOW);
#endif
    if (osc.begin()) {
        networkUp = true;
        IPAddress ip = osc.localIP();
        Serial.printf("Network up — IP %s\n", ip.toString().c_str());
#if SCOREBOARD_HAS_M5UNIFIED
        lcdStatus("Ready", ip.toString().c_str(), TFT_GREEN);
#endif
        delay(4000);
    } else {
        Serial.println("*** Network failed — continuing serial-only ***");
#if SCOREBOARD_HAS_M5UNIFIED
        lcdStatus("NET ERR", "serial only", TFT_RED);
#endif
        delay(2000);
    }
#endif
#endif

    // Initialise display and storage after the network stack has claimed
    // the internal RAM it needs on the S3.
    displayManager.begin();
    displayManager.showTestPattern();
    displayManager.startDisplay();
    osc.beginRuntimeScripts();

    displayManager.clearAll();
    displayManager.loadStartupParams();
    displayManager.update();
    Serial.println(networkUp
        ? "Ready — waiting for OSC messages + serial commands …"
        : "Ready — serial-only mode (no network) …");
    Serial.println("SCRIPT_READY");
#if SCOREBOARD_HAS_WIFI && SCOREBOARD_WIFI_ENABLED
    Serial.println("Tap AtomS3 button to toggle WiFi AP + web UI");
#endif
#if SCOREBOARD_HAS_M5UNIFIED
#if SCOREBOARD_HAS_WIFI && SCOREBOARD_WIFI_ENABLED
    if (!webUI.isRunning()) {
        lcdShowIdle();
    }
#else
    lcdShowIdle();
#endif
#endif
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
#if SCOREBOARD_HAS_M5UNIFIED
    M5.update();         // poll button, etc.
    // Button A (screen tap on AtomS3) toggles AP on/off
#if SCOREBOARD_HAS_WIFI && SCOREBOARD_WIFI_ENABLED
    if (M5.BtnA.wasPressed()) {
        toggleAP();
    }
#endif
#endif
    if (networkUp) osc.update();  // read & dispatch incoming OSC packets
#if SCOREBOARD_HAS_WIFI && SCOREBOARD_WIFI_ENABLED
    webUI.update();               // handle web clients (no-op when AP is off)
#endif
#if SERIAL_CMD_ENABLED
    osc.processSerial(); // read & dispatch serial text commands
#endif
#if SCOREBOARD_RS485_ENABLED
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
