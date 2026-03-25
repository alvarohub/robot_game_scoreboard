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

#ifdef USE_M5UNIFIED
    lcdStatus("Connecting", "network...", TFT_YELLOW);
#endif

    // Bring up the network
    if (!osc.begin()) {
        Serial.println("*** Network failed — halting ***");
        display.setText(0, "NET");
        display.setText(1, "ERR");
        display.update();
#ifdef USE_M5UNIFIED
        lcdStatus("NET ERR", "no link", TFT_RED);
#endif
        while (true) { delay(1000); }
    }

    // Flash the IP address on the first two displays for convenience
    IPAddress ip = osc.localIP();
    display.setText(0, "IP:");
    display.setText(1, ip.toString().c_str());
    display.update();
    Serial.printf("Showing IP for 4 s: %s\n", ip.toString().c_str());

#ifdef USE_M5UNIFIED
    lcdStatus("Ready", ip.toString().c_str(), TFT_GREEN);
#endif
    delay(4000);

    display.clearAll();
    display.update();
    Serial.println("Ready — waiting for OSC messages …");
}

// ── Loop ─────────────────────────────────────────────────────
void loop() {
#ifdef USE_M5UNIFIED
    M5.update();         // poll button, IMU, etc.
#endif
    osc.update();        // read & dispatch incoming OSC packets
    display.update();    // push pixel changes to the strip (no-op when idle)
}
