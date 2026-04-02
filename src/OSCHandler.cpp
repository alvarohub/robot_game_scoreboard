#include "OSCHandler.h"
#include <cstdio>
#include <cstring>

#ifdef USE_M5UNIFIED
  #include <M5Unified.h>
#endif

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
//    /display/<N>/clearqueue   — discard pending scroll queue for one display
//    /brightness               — global brightness (int arg 0-255)
//    /scroll                   — set scroll mode for ALL displays (int 0-2)
//    /scrollspeed              — scroll speed in ms per pixel step (int, default 25)
//    /scrollblank              — blank frame between scroll items: 0=off, 1=on
//    /clearqueue               — discard scroll queues on all displays
//    /clearall  or  /clear     — clear every display (also flushes queues)
//    /status                   — replies "ANIMATING 0" or "ANIMATING 1"
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
        else if (strcmp(subCmd, "clearqueue") == 0) {
            _display.clearQueue(idx);
            Serial.printf("D%d queue cleared\n", displayNum);
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
    // ── /scrollspeed — scroll animation speed (ms per pixel) ─
    else if (strcmp(address, "/scrollspeed") == 0) {
        if (msg.isInt(0)) {
            _display.setScrollSpeed(msg.getInt(0));
            Serial.printf("Scroll speed → %ld ms\n", (long)msg.getInt(0));
        }
    }
    // ── /scrollblank — blank frame between scroll items ──────
    else if (strcmp(address, "/scrollblank") == 0) {
        if (msg.isInt(0)) {
            _display.setScrollBlank(msg.getInt(0) != 0);
            Serial.printf("Scroll blank → %s\n", msg.getInt(0) ? "ON" : "OFF");
        }
    }
    // ── /clearqueue — flush all scroll queues ────────────────
    else if (strcmp(address, "/clearqueue") == 0) {
        _display.clearQueueAll();
        Serial.println("All queues cleared");
    }
    // ── /clearall  |  /clear ─────────────────────────────────
    else if (strcmp(address, "/clearall") == 0 ||
             strcmp(address, "/clear")    == 0) {
        _display.clearAll();
        Serial.println("All displays cleared");
    }
    // ── /status — query animation state ──────────────────────
    else if (strcmp(address, "/status") == 0) {
        Serial.printf("ANIMATING %d\n", _display.isAnimating() ? 1 : 0);
    }
    // ── /rasterscan — light each LED in sequence ─────────────
    else if (strcmp(address, "/rasterscan") == 0) {
        uint16_t ms = (msg.size() >= 1 && msg.isInt(0)) ? msg.getInt(0) : 30;
        _display.showRasterScan(ms);
    }
    else {
        Serial.printf("Unknown OSC: %s\n", address);
    }
}

// ══════════════════════════════════════════════════════════════
//  Serial command interface
// ══════════════════════════════════════════════════════════════
//
//  Accepts newline-terminated text commands over USB-Serial that
//  mirror the OSC address scheme.  Each line is parsed into an
//  address and arguments, packaged as a proxy OSCMessage, and
//  routed through _processMessage() — zero duplicated logic.
//
//  Format:   /address [arg1] [arg2] …
//
//    - First token must start with '/'
//    - Tokens in quotes are string arguments:  "hello world"
//    - Otherwise parsed as integer
//
//  Examples:
//    /display/1/text "HELLO"
//    /display/2 42
//    /display/1/color 255 0 0
//    /brightness 128
//    /clearall
//
// ══════════════════════════════════════════════════════════════

#if SERIAL_CMD_ENABLED

void OSCHandler::processSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        Serial.write(c);  // echo back for user feedback
        if (c == '\n' || c == '\r') {
            if (_serialPos > 0) {
                _serialBuf[_serialPos] = '\0';
                _handleSerialLine(_serialBuf);
                _serialPos = 0;
            }
        } else if (_serialPos < SERIAL_BUF_SIZE - 1) {
            _serialBuf[_serialPos++] = c;
        }
        // else: overflow — silently drop until newline
    }
}

#endif  // SERIAL_CMD_ENABLED

// ── RS485 command interface (same text protocol as USB Serial) ─
#ifdef USE_RS485

void OSCHandler::processRS485() {
    while (Serial2.available()) {
        char c = Serial2.read();
        if (c == '\n' || c == '\r') {
            if (_rs485Pos > 0) {
                _rs485Buf[_rs485Pos] = '\0';
                Serial.printf("RS485 rx: %s\n", _rs485Buf);
                _handleSerialLine(_rs485Buf);
                _rs485Pos = 0;
            }
        } else if (_rs485Pos < RS485_BUF_SIZE - 1) {
            _rs485Buf[_rs485Pos++] = c;
        }
    }
}

#endif  // USE_RS485

// ── Shared text-line command parser (used by USB Serial & RS485) ──
#if SERIAL_CMD_ENABLED || defined(USE_RS485)

#ifdef USE_M5UNIFIED
static void _lcdSerialDebug(const char* line) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.setTextSize(2);          // 12×16 px — readable on 128×128
    M5.Display.setTextDatum(TL_DATUM);  // top-left
    M5.Display.drawString("Serial rx:", 2, 2);
    // Show the received command (wraps on 128px-wide screen)
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextWrap(true);
    M5.Display.setCursor(2, 24);
    M5.Display.print(line);
}
#endif

void OSCHandler::_handleSerialLine(const char* line) {
    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') line++;

    // Ignore empty lines and comments
    if (*line == '\0' || *line == '#') return;

    if (*line != '/') {
        Serial.println("Serial cmd must start with '/'");
        return;
    }

    // Extract address (first token)
    char address[64];
    const char* p = line;
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\t' && i < sizeof(address) - 1) {
        address[i++] = *p++;
    }
    address[i] = '\0';

    // Build a proxy OSCMessage with the parsed address
    OSCMessage msg(address);

    // Parse remaining tokens as arguments
    while (*p) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        if (*p == '"') {
            // Quoted string argument
            p++;  // skip opening quote
            char strArg[64];
            size_t si = 0;
            while (*p && *p != '"' && si < sizeof(strArg) - 1) {
                strArg[si++] = *p++;
            }
            strArg[si] = '\0';
            if (*p == '"') p++;  // skip closing quote
            msg.add(strArg);
        } else {
            // Try integer
            char* end = nullptr;
            long val = strtol(p, &end, 10);
            if (end != p) {
                msg.add((int32_t)val);
                p = end;
            } else {
                // Unquoted string token (until next space)
                char tok[64];
                size_t ti = 0;
                while (*p && *p != ' ' && *p != '\t' && ti < sizeof(tok) - 1) {
                    tok[ti++] = *p++;
                }
                tok[ti] = '\0';
                msg.add(tok);
            }
        }
    }

    Serial.printf("Serial → %s", address);
    for (int a = 0; a < msg.size(); a++) {
        if (msg.isString(a)) {
            char tmp[64];
            msg.getString(a, tmp, sizeof(tmp));
            Serial.printf(" \"%s\"", tmp);
        } else if (msg.isInt(a)) {
            Serial.printf(" %ld", (long)msg.getInt(a));
        }
    }
    Serial.println();

#ifdef USE_M5UNIFIED
    _lcdSerialDebug(line);
#endif

    _processMessage(msg);
}

#endif  // SERIAL_CMD_ENABLED || USE_RS485
