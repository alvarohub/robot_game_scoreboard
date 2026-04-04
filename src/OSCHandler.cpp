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

// ── Execute a command string (reuses the serial-line parser) ─
void OSCHandler::executeCommand(const char* line) {
    _handleSerialLine(line);
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
//    /display/<N>/mode         — display mode: 0=text, 1=scroll up, 2=scroll down
//    /display/<N>/text/enable  — text layer: 0=off, 1=on
//    /display/<N>/particles/enable — particles layer: 0=off, 1=on
//    /display/<N>/text/brightness  — text layer brightness (int 0-255)
//    /display/<N>/particles/brightness — particle layer brightness (int 0-255)
//    /display/<N>/particles/color — particle colour (3 int args: R G B)
//    /display/<N>/color        — text colour (3 int args: R G B)
//    /display/<N>/clear        — clear one display
//    /display/<N>/brightness   — per-display brightness (global for now)
//    /display/<N>/scroll       — scroll mode: 0=instant, 1=up, 2=down
//    /display/<N>/clearqueue   — discard pending scroll queue for one display
//    /display/<N>/text/push    — push text to display's text stack
//    /display/<N>/text/pop     — pop last text stack entry
//    /display/<N>/text/set     — set text stack entry at index (int, string)
//    /display/<N>/text/clear   — clear text stack
//    /display/<N>/text/list    — print text stack to serial
//    /brightness               — global brightness (int arg 0-255)
//    /mode                     — set mode for ALL displays (int 0-2)
//    /text/enable              — text layer on all displays: 0=off, 1=on
//    /particles/enable         — particles layer on all displays: 0=off, 1=on
//    /text/brightness          — text layer brightness (all displays, int 0-255)
//    /particles/brightness     — particle layer brightness (all displays, int 0-255)
//    /particles/color          — particle colour (all displays, 3 int args: R G B)
//    /scroll                   — set scroll mode for ALL displays (int 0-2)
//    /scrollspeed              — scroll speed in ms per pixel step (int, default 25)
//    /scrollcontinuous         — auto-cycle textStack in scroll mode: 0=off, 1=on
//    /text/push "STR"          — push to text stack (all displays)
//    /text/pop                 — pop text stack (all displays)
//    /text/set N "STR"         — set text stack entry N (all displays)
//    /text/clear               — clear text stack (all displays)
//    /text/list                — print text stacks to serial
//    /defaults                 — reset all params to compiled defaults
//    /clearqueue               — discard scroll queues on all displays
//    /clearall  or  /clear     — clear every display (also flushes queues)
//    /status                   — replies "ANIMATING 0" or "ANIMATING 1"
//
//  Display numbers are 1-based in OSC (mapped to 0-based internally).
// ══════════════════════════════════════════════════════════════

static bool _parseDisplayModeArg(OSCMessage& msg, int argIndex, DisplayMode* outMode) {
    if (msg.isInt(argIndex)) {
        int mode = msg.getInt(argIndex);
        if (mode < DISPLAY_MODE_TEXT || mode > DISPLAY_MODE_SCROLL_DOWN) {
            return false;
        }
        *outMode = (DisplayMode)mode;
        return true;
    }

    if (msg.isString(argIndex)) {
        char modeName[24];
        msg.getString(argIndex, modeName, sizeof(modeName));
        for (char* p = modeName; *p; ++p) {
            if (*p >= 'A' && *p <= 'Z') *p = *p - 'A' + 'a';
        }

        if (strcmp(modeName, "text") == 0 || strcmp(modeName, "immediate") == 0) {
            *outMode = DISPLAY_MODE_TEXT;
            return true;
        }
        if (strcmp(modeName, "scrollup") == 0 || strcmp(modeName, "scroll_up") == 0) {
            *outMode = DISPLAY_MODE_SCROLL_UP;
            return true;
        }
        if (strcmp(modeName, "scrolldown") == 0 || strcmp(modeName, "scroll_down") == 0) {
            *outMode = DISPLAY_MODE_SCROLL_DOWN;
            return true;
        }
    }

    return false;
}

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
        else if (strcmp(subCmd, "mode") == 0) {
            DisplayMode mode;
            if (_parseDisplayModeArg(msg, 0, &mode)) {
                _display.setMode(idx, mode);
                Serial.printf("D%d mode → %d\n", displayNum, (int)mode);
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
        else if (strcmp(subCmd, "particles/enable") == 0) {
            if (msg.isInt(0)) {
                _display.setParticlesEnabled(idx, msg.getInt(0) != 0);
                Serial.printf("D%d particles → %s\n", displayNum, msg.getInt(0) ? "ON" : "OFF");
            }
        }
        else if (strcmp(subCmd, "particles/brightness") == 0) {
            if (msg.isInt(0)) {
                _display.setParticleBrightness(idx, msg.getInt(0));
                Serial.printf("D%d particle brightness → %ld\n", displayNum, (long)msg.getInt(0));
            }
        }
        else if (strcmp(subCmd, "particles/color") == 0) {
            if (msg.size() >= 3 && msg.isInt(0)) {
                _display.setParticleColor(idx, msg.getInt(0), msg.getInt(1), msg.getInt(2));
                Serial.printf("D%d particle color (%ld,%ld,%ld)\n", displayNum,
                              (long)msg.getInt(0), (long)msg.getInt(1), (long)msg.getInt(2));
            }
        }
        else if (strcmp(subCmd, "text/enable") == 0) {
            if (msg.isInt(0)) {
                _display.setTextEnabled(idx, msg.getInt(0) != 0);
                Serial.printf("D%d text → %s\n", displayNum, msg.getInt(0) ? "ON" : "OFF");
            }
        }
        else if (strcmp(subCmd, "text/brightness") == 0) {
            if (msg.isInt(0)) {
                _display.setTextBrightness(idx, msg.getInt(0));
                Serial.printf("D%d text brightness → %ld\n", displayNum, (long)msg.getInt(0));
            }
        }
        else if (strcmp(subCmd, "particles") == 0) {
            // /display/<N>/particles count renderMs gravityScale elasticity wallElasticity
            //   radius renderStyle glowSigma temperature attractStrength attractRange
            //   gravityEnabled substepMs damping glowWavelength
            // All args optional — missing args keep current values
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                ParticleModeConfig cfg = vd->modeConfig().particles;
                if (msg.size() >= 1 && msg.isInt(0))   cfg.count          = msg.getInt(0);
                if (msg.size() >= 2 && msg.isInt(1))   cfg.renderMs       = msg.getInt(1);
                if (msg.size() >= 3 && msg.isFloat(2)) cfg.gravityScale   = msg.getFloat(2);
                if (msg.size() >= 4 && msg.isFloat(3)) cfg.elasticity     = msg.getFloat(3);
                if (msg.size() >= 5 && msg.isFloat(4)) cfg.wallElasticity = msg.getFloat(4);
                if (msg.size() >= 6 && msg.isFloat(5)) cfg.radius         = msg.getFloat(5);
                if (msg.size() >= 7 && msg.isInt(6))   cfg.renderStyle    = (ParticleModeConfig::RenderStyle)msg.getInt(6);
                if (msg.size() >= 8 && msg.isFloat(7)) cfg.glowSigma      = msg.getFloat(7);
                if (msg.size() >= 9 && msg.isFloat(8)) cfg.temperature    = msg.getFloat(8);
                if (msg.size() >= 10 && msg.isFloat(9))  cfg.attractStrength = msg.getFloat(9);
                if (msg.size() >= 11 && msg.isFloat(10)) cfg.attractRange   = msg.getFloat(10);
                if (msg.size() >= 12 && msg.isInt(11))   cfg.gravityEnabled = (msg.getInt(11) != 0);
                if (msg.size() >= 13 && msg.isInt(12))   cfg.substepMs      = msg.getInt(12);
                if (msg.size() >= 14 && msg.isFloat(13)) cfg.damping        = msg.getFloat(13);
                if (msg.size() >= 15 && msg.isFloat(14)) cfg.glowWavelength = msg.getFloat(14);
                if (msg.size() >= 16 && msg.isInt(15))   cfg.speedColor     = (msg.getInt(15) != 0);
                _display.setParticleConfig(idx, cfg);
                Serial.printf("D%d particles: n=%d grav=%.1f(%s) el=%.2f att=%.2f@%.1f temp=%.2f\n",
                              displayNum, cfg.count,
                              cfg.gravityScale, cfg.gravityEnabled ? "on" : "off",
                              cfg.elasticity, cfg.attractStrength, cfg.attractRange,
                              cfg.temperature);
            }
        }
        // ── /display/<N>/text/push "STR" — per-display text stack ─
        else if (strcmp(subCmd, "text/push") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd && msg.isString(0)) {
                char text[TEXT_MAX_LEN];
                msg.getString(0, text, sizeof(text));
                vd->textPush(text);
                Serial.printf("D%d text push → \"%s\"\n", displayNum, text);
            }
        }
        else if (strcmp(subCmd, "text/pop") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) { vd->textPop(); Serial.printf("D%d text pop\n", displayNum); }
        }
        else if (strcmp(subCmd, "text/set") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd && msg.size() >= 2 && msg.isInt(0) && msg.isString(1)) {
                uint8_t ti = msg.getInt(0);
                char text[TEXT_MAX_LEN];
                msg.getString(1, text, sizeof(text));
                vd->textSet(ti, text);
                Serial.printf("D%d text set [%d] → \"%s\"\n", displayNum, ti, text);
            }
        }
        else if (strcmp(subCmd, "text/clear") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) { vd->textClear(); Serial.printf("D%d text stack cleared\n", displayNum); }
        }
        else if (strcmp(subCmd, "text/list") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                Serial.printf("D%d text stack (%d entries):\n", displayNum, vd->textCount());
                for (uint8_t j = 0; j < vd->textCount(); j++) {
                    Serial.printf("  [%d] \"%s\"\n", j, vd->textGet(j));
                }
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
    // ── /mode — set display mode for all displays ────────────
    else if (strcmp(address, "/mode") == 0) {
        DisplayMode mode;
        if (_parseDisplayModeArg(msg, 0, &mode)) {
            _display.setModeAll(mode);
            Serial.printf("All mode → %d\n", (int)mode);
        }
    }
    // ── /particles/enable — toggle particles overlay (all) ──
    else if (strcmp(address, "/particles/enable") == 0) {
        if (msg.isInt(0)) {
            _display.setParticlesEnabled(msg.getInt(0) != 0);
            Serial.printf("All particles → %s\n", msg.getInt(0) ? "ON" : "OFF");
        }
    }
    // ── /particles/brightness — particle layer brightness (all) ─
    else if (strcmp(address, "/particles/brightness") == 0) {
        if (msg.isInt(0)) {
            _display.setParticleBrightness(msg.getInt(0));
            Serial.printf("All particle brightness → %ld\n", (long)msg.getInt(0));
        }
    }
    // ── /particles/color — particle colour (all, R G B) ─────
    else if (strcmp(address, "/particles/color") == 0) {
        if (msg.size() >= 3 && msg.isInt(0)) {
            _display.setParticleColor(msg.getInt(0), msg.getInt(1), msg.getInt(2));
            Serial.printf("All particle color (%ld,%ld,%ld)\n",
                          (long)msg.getInt(0), (long)msg.getInt(1), (long)msg.getInt(2));
        }
    }
    // ── /text/enable — toggle text layer (all) ──────────────
    else if (strcmp(address, "/text/enable") == 0) {
        if (msg.isInt(0)) {
            _display.setTextEnabled(msg.getInt(0) != 0);
            Serial.printf("All text → %s\n", msg.getInt(0) ? "ON" : "OFF");
        }
    }
    // ── /text/brightness — text layer brightness (all) ──────
    else if (strcmp(address, "/text/brightness") == 0) {
        if (msg.isInt(0)) {
            _display.setTextBrightness(msg.getInt(0));
            Serial.printf("All text brightness → %ld\n", (long)msg.getInt(0));
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
    // ── /scrollcontinuous — auto-cycle textStack in scroll mode ─
    else if (strcmp(address, "/scrollcontinuous") == 0) {
        if (msg.isInt(0)) {
            _display.setScrollContinuous(msg.getInt(0) != 0);
            Serial.printf("Scroll continuous → %s\n", msg.getInt(0) ? "ON" : "OFF");
        }
    }
    // ── /display/<N>/text/push "STRING" — push to text stack ─
    // ── /text/push "STRING" ────── push to all displays ──────
    // (handled via display-specific routing above, plus global aliases below)

    // ── Text stack commands (global — applies to all displays) ─
    else if (strcmp(address, "/text/push") == 0) {
        if (msg.isString(0)) {
            char text[TEXT_MAX_LEN];
            msg.getString(0, text, sizeof(text));
            for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
                VirtualDisplay* vd = _display.getDisplay(i);
                if (vd) vd->textPush(text);
            }
            Serial.printf("Text push → \"%s\" (all displays)\n", text);
        }
    }
    else if (strcmp(address, "/text/pop") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->textPop();
        }
        Serial.println("Text pop (all displays)");
    }
    else if (strcmp(address, "/text/set") == 0) {
        if (msg.size() >= 2 && msg.isInt(0) && msg.isString(1)) {
            uint8_t idx = msg.getInt(0);
            char text[TEXT_MAX_LEN];
            msg.getString(1, text, sizeof(text));
            for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
                VirtualDisplay* vd = _display.getDisplay(i);
                if (vd) vd->textSet(idx, text);
            }
            Serial.printf("Text set [%d] → \"%s\" (all displays)\n", idx, text);
        }
    }
    else if (strcmp(address, "/text/clear") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->textClear();
        }
        Serial.println("Text stack cleared (all displays)");
    }
    else if (strcmp(address, "/text/list") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (!vd) continue;
            Serial.printf("D%d text stack (%d entries):\n", i + 1, vd->textCount());
            for (uint8_t j = 0; j < vd->textCount(); j++) {
                Serial.printf("  [%d] \"%s\"\n", j, vd->textGet(j));
            }
        }
    }
    // ── /defaults — reset all params to compiled defaults ────
    else if (strcmp(address, "/defaults") == 0) {
        _display.clearAll();
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) {
                vd->textClear();
                DisplayModeConfig mc;  // default-constructed = all defaults
                vd->setMode(mc);
            }
        }
        _display.setBrightness(DEFAULT_BRIGHTNESS);
        Serial.println("All params reset to defaults");
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
    // ── /saveparams — persist to ESP32 NVS ───────────────────
    else if (strcmp(address, "/saveparams") == 0) {
        _display.saveParams();
    }
    // ── /loadparams — restore from ESP32 NVS ─────────────────
    else if (strcmp(address, "/loadparams") == 0) {
        _display.loadParams();
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
 //       Serial.write(c);  // echo back for user feedback
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

// ── Shared text-line command parser (used by USB Serial, RS485 & Web) ──

#ifdef USE_M5UNIFIED
static void _lcdSerialDebug(const char* line) {
    // Show received command at the bottom of the LCD (non-destructive)
    int16_t h = M5.Display.height();
    M5.Display.fillRect(0, h - 12, M5.Display.width(), 12, TFT_BLACK);
    M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.setTextDatum(BC_DATUM);  // bottom-centre
    // Truncate to fit screen width (~21 chars at size 1)
    char buf[22];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    M5.Display.drawString(buf, M5.Display.width() / 2, h - 1);
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
            // Try number: float if it contains '.', otherwise integer
            char* end = nullptr;
            bool parsed = false;

            // Peek ahead to decide float vs int
            bool hasDecimal = false;
            for (const char* q = p; *q && *q != ' ' && *q != '\t'; q++) {
                if (*q == '.') { hasDecimal = true; break; }
            }

            if (hasDecimal) {
                float fval = strtof(p, &end);
                if (end != p) { msg.add(fval); p = end; parsed = true; }
            } else {
                long val = strtol(p, &end, 10);
                if (end != p) { msg.add((int32_t)val); p = end; parsed = true; }
            }

            if (!parsed) {
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
        } else if (msg.isFloat(a)) {
            Serial.printf(" %.3f", msg.getFloat(a));
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
