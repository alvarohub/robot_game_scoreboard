#include "OSCHandler.h"
#include <cstdio>
#include <cstring>

#ifdef USE_WIFI
  #include "credentials.h"        // defines WIFI_SSID, WIFI_PASSWORD
#endif

// ── Constructor ──────────────────────────────────────────────
OSCHandler::OSCHandler(DisplayManager& display)
    : _display(display) {}

// ── Network init + UDP listener ──────────────────────────────
bool OSCHandler::begin() {

#ifdef USE_WIFI
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
        if (millis() - t0 > 15000) {
            Serial.println("\nWiFi connection timed out");
            return false;
        }
    }
    Serial.printf("\nConnected — IP %s\n", WiFi.localIP().toString().c_str());

#elif defined(USE_ETHERNET_W5500)
    // Reset W5500
    pinMode(ETH_RST_PIN, OUTPUT);
    digitalWrite(ETH_RST_PIN, LOW);
    delay(100);
    digitalWrite(ETH_RST_PIN, HIGH);
    delay(200);

    Ethernet.init(ETH_CS_PIN);
    byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

    Serial.print("Obtaining IP via DHCP");
    if (Ethernet.begin(mac) == 0) {
        Serial.println("\nDHCP failed");
        return false;
    }
    Serial.printf("\nEthernet IP %s\n", Ethernet.localIP().toString().c_str());
#endif

    _udp.begin(OSC_PORT);
    Serial.printf("OSC listening on UDP port %d\n", OSC_PORT);
    return true;
}

// ── Poll for incoming packets ────────────────────────────────
void OSCHandler::update() {
    int size;
    while ((size = _udp.parsePacket()) > 0) {
        OSCMessage msg;
        while (size--) {
            msg.fill(_udp.read());
        }
        if (!msg.hasError()) {
            _processMessage(msg);
        } else {
            Serial.println("OSC parse error");
        }
    }
}

// ── Resolve our IP ───────────────────────────────────────────
IPAddress OSCHandler::localIP() {
#ifdef USE_WIFI
    return WiFi.localIP();
#elif defined(USE_ETHERNET_W5500)
    return Ethernet.localIP();
#else
    return IPAddress(0, 0, 0, 0);
#endif
}

// ══════════════════════════════════════════════════════════════
//  OSC message routing
// ══════════════════════════════════════════════════════════════
//
//  Supported addresses:
//
//    /display/<N>              — set text (string or int arg)
//    /display/<N>/text         — set text (string arg)
//    /display/<N>/color        — set colour (3 int args: R G B)
//    /display/<N>/clear        — clear one display
//    /display/<N>/brightness   — per-display brightness (global for now)
//    /display/<N>/scroll       — scroll mode: 0=instant, 1=up, 2=down
//    /brightness               — global brightness (int arg 0-255)
//    /scroll                   — set scroll mode for ALL displays (int 0-2)
//    /clearall  or  /clear     — clear every display
//
//  Display numbers are 1-based in OSC (mapped to 0-based internally).
// ══════════════════════════════════════════════════════════════

void OSCHandler::_processMessage(OSCMessage& msg) {
    char address[64];
    msg.getAddress(address, 0);

    int  displayNum   = 0;
    char subCmd[32]   = "";

    // ── /display/<N>[/subcommand] ────────────────────────────
    if (sscanf(address, "/display/%d/%31s", &displayNum, subCmd) >= 1) {
        if (displayNum < 1 || displayNum > NUM_DISPLAYS) {
            Serial.printf("Display %d out of range\n", displayNum);
            return;
        }
        uint8_t idx = displayNum - 1;

        if (subCmd[0] == '\0' || strcmp(subCmd, "text") == 0) {
            // Text — accept string or int
            if (msg.isString(0)) {
                char text[32];
                msg.getString(0, text, sizeof(text));
                _display.setText(idx, text);
                Serial.printf("D%d ← \"%s\"\n", displayNum, text);
            } else if (msg.isInt(0)) {
                char text[16];
                snprintf(text, sizeof(text), "%ld", (long)msg.getInt(0));
                _display.setText(idx, text);
                Serial.printf("D%d ← %s\n", displayNum, text);
            }
        }
        else if (strcmp(subCmd, "color") == 0) {
            if (msg.size() >= 3 && msg.isInt(0)) {
                uint8_t r = msg.getInt(0);
                uint8_t g = msg.getInt(1);
                uint8_t b = msg.getInt(2);
                _display.setColor(idx, r, g, b);
                Serial.printf("D%d color (%d,%d,%d)\n", displayNum, r, g, b);
            }
        }
        else if (strcmp(subCmd, "clear") == 0) {
            _display.clear(idx);
            Serial.printf("D%d cleared\n", displayNum);
        }
        else if (strcmp(subCmd, "brightness") == 0) {
            if (msg.isInt(0)) {
                _display.setBrightness(msg.getInt(0));
                Serial.printf("Brightness → %ld\n", (long)msg.getInt(0));
            }
        }
        else if (strcmp(subCmd, "scroll") == 0) {
            if (msg.isInt(0)) {
                int mode = msg.getInt(0);
                _display.setScrollMode(idx, mode);
                Serial.printf("D%d scroll → %d\n", displayNum, mode);
            }
        }
    }
    // ── /brightness ──────────────────────────────────────────
    else if (strcmp(address, "/brightness") == 0) {
        if (msg.isInt(0)) {
            _display.setBrightness(msg.getInt(0));
            Serial.printf("Brightness → %ld\n", (long)msg.getInt(0));
        }
    }
    // ── /scroll — set scroll mode for all displays ──────────
    else if (strcmp(address, "/scroll") == 0) {
        if (msg.isInt(0)) {
            int mode = msg.getInt(0);
            _display.setScrollModeAll(mode);
            Serial.printf("All scroll → %d\n", mode);
        }
    }
    // ── /clearall  |  /clear ─────────────────────────────────
    else if (strcmp(address, "/clearall") == 0 ||
             strcmp(address, "/clear")    == 0) {
        _display.clearAll();
        Serial.println("All displays cleared");
    }
    else {
        Serial.printf("Unknown OSC: %s\n", address);
    }
}
