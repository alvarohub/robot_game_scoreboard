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

#ifdef USE_M5UNIFIED
  #include <M5Unified.h>
#endif

DisplayManager display;
OSCHandler     osc(display);
bool           networkUp = false;

// ── Helper: show a status line on the AtomS3 built-in LCD ────
#ifdef USE_M5UNIFIED
static void lcdStatus(const char* line1, const char* line2 = nullptr,
                      uint32_t color = TFT_WHITE) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(MC_DATUM);  // middle-centre
    M5.Display.drawString(line1, M5.Display.width() / 2,
                          line2 ? M5.Display.height() / 2 - 10
                                : M5.Display.height() / 2);
    if (line2) {
        M5.Display.drawString(line2, M5.Display.width() / 2,
                              M5.Display.height() / 2 + 10);
    }
}
#endif

// ── Setup ────────────────────────────────────────────────────
void setup() {
#ifdef USE_M5UNIFIED
    auto cfg = M5.config();
    // cfg.serial_baudrate = 115200;   // default
    M5.begin(cfg);
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

    // Initialise LED matrices and run a quick self-test
    display.begin();
    display.showTestPattern();
    display.startDisplay();

    // ── WiFi prompt: press button to connect, or wait to skip ──

#if defined(USE_WIFI) && defined(USE_M5UNIFIED) && (WIFI_PROMPT_SECONDS > 0)
    lcdStatus("WiFi?", "Press to connect", TFT_YELLOW);
    Serial.printf("Press button within %d s to connect WiFi…\n",
                  WIFI_PROMPT_SECONDS);

    bool buttonPressed = false;
    unsigned long promptStart = millis();
    while (millis() - promptStart < (unsigned long)WIFI_PROMPT_SECONDS * 1000) {
        M5.update();
        if (M5.BtnA.wasPressed()) {
            buttonPressed = true;
            break;
        }
        delay(50);
    }

    if (buttonPressed) {
        lcdStatus("Connecting", "WiFi...", TFT_YELLOW);

        if (osc.begin()) {
            networkUp = true;
            IPAddress ip = osc.localIP();
            Serial.printf("Connected — IP %s\n", ip.toString().c_str());
            lcdStatus("Ready", ip.toString().c_str(), TFT_GREEN);
            delay(4000);
        } else {
            Serial.println("*** WiFi failed — continuing serial-only ***");
            lcdStatus("WiFi FAIL", "serial only", TFT_RED);
            delay(2000);
        }
    } else {
        Serial.println("WiFi skipped — serial-only mode");
        lcdStatus("Serial", "mode", TFT_CYAN);
        delay(2000);
    }

#elif defined(USE_WIFI) || defined(USE_ETHERNET_W5500)
    // No button prompt — always connect (non-M5 boards or prompt disabled)
#ifdef USE_M5UNIFIED
    lcdStatus("Connecting", "network...", TFT_YELLOW);
#endif
    if (osc.begin()) {
        networkUp = true;
        IPAddress ip = osc.localIP();
        Serial.printf("Showing IP for 4 s: %s\n", ip.toString().c_str());
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

    display.clearAll();
    display.update();
    Serial.println(networkUp
        ? "Ready — waiting for OSC messages + serial commands …"
        : "Ready — serial-only mode (no network) …");
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
#ifdef USE_M5UNIFIED
    M5.update();         // poll button, IMU, etc.
#endif
    if (networkUp) osc.update();  // read & dispatch incoming OSC packets
#if SERIAL_CMD_ENABLED
    osc.processSerial(); // read & dispatch serial text commands
#endif
    display.update();    // push pixel changes to the strip (no-op when idle)
}
