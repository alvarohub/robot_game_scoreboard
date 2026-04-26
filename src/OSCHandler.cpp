#include "OSCHandler.h"
#include <cstdio>
#include <cstring>

#if SCOREBOARD_HAS_M5UNIFIED
  #include <M5Unified.h>
#endif

#if SCOREBOARD_HAS_WIFI && (SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_STATION)
  #include "credentials.h"        // defines WIFI_SSID, WIFI_PASSWORD
#endif

// ── Constructor ──────────────────────────────────────────────
OSCHandler::OSCHandler(DisplayManager& display)
    : _display(display) {}

String OSCHandler::bankSlotName(uint8_t slot) const {
    return _runtimeScripts.bankSlotName(slot);
}

const char* OSCHandler::configuredWiFiModeName() const {
#if SCOREBOARD_HAS_WIFI
#if SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_AP
    return "ap";
#elif SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_STATION
    return "station";
#else
    return "off";
#endif
#else
    return "off";
#endif
}

bool OSCHandler::accessPointActive() const {
#if SCOREBOARD_HAS_WIFI
    wifi_mode_t currentMode = WiFi.getMode();
    return (currentMode & WIFI_AP) != 0;
#else
    return false;
#endif
}

uint8_t OSCHandler::connectedClientCount() const {
#if SCOREBOARD_HAS_WIFI
    return accessPointActive() ? (uint8_t)WiFi.softAPgetStationNum() : 0;
#else
    return 0;
#endif
}

void OSCHandler::printWiFiState(Print& out) const {
    IPAddress ip(0, 0, 0, 0);
#if SCOREBOARD_HAS_WIFI
    if (SCOREBOARD_WIFI_ENABLED) {
        ip = accessPointActive() ? WiFi.softAPIP() : WiFi.localIP();
    }
#endif
    out.printf("WIFI_STATE enabled=%d mode=%s active=%d ip=%s clients=%u\n",
               SCOREBOARD_WIFI_ENABLED ? 1 : 0,
               configuredWiFiModeName(),
               accessPointActive() ? 1 : 0,
               ip.toString().c_str(),
               connectedClientCount());
}

void OSCHandler::printDisplayState(uint8_t displayIndex, Print& out) const {
    _display.printDisplayState(displayIndex, out);
}

bool OSCHandler::_startUdpListener() {
    _udp.stop();
    _udp.begin(OSC_PORT);
    Serial.printf("OSC listening on UDP port %d\n", OSC_PORT);
    return true;
}

#if SCOREBOARD_HAS_WIFI
bool OSCHandler::startAccessPoint() {
    if (!SCOREBOARD_WIFI_ENABLED || SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_OFF) {
        Serial.println("WiFi disabled in config");
        return false;
    }

    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_AP);
    delay(100);

    if (!WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS)) {
        Serial.println("Failed to start WiFi AP");
        return false;
    }

    delay(100);
    Serial.printf("WiFi AP started — SSID: %s  IP: %s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
    return _startUdpListener();
}

void OSCHandler::stopWiFi() {
    _udp.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
}
#endif

// ── Network init + UDP listener ──────────────────────────────
bool OSCHandler::begin() {

#if SCOREBOARD_HAS_WIFI
    if (!SCOREBOARD_WIFI_ENABLED || SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_OFF) {
        Serial.println("WiFi disabled in config");
        return false;
    }
#if SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_AP
    return startAccessPoint();
#elif SCOREBOARD_WIFI_MODE == SCOREBOARD_WIFI_MODE_STATION
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
        if (millis() - t0 > WIFI_CONNECT_TIMEOUT_MS) {
            Serial.println("\nWiFi connection timed out");
            return false;
        }
    }
    Serial.printf("\nConnected — IP %s\n", WiFi.localIP().toString().c_str());
#else
    Serial.println("Unsupported WiFi mode in config");
    return false;
#endif

#elif SCOREBOARD_HAS_ETHERNET
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
#else
    return false;
#endif

    return _startUdpListener();
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

void OSCHandler::beginRuntimeScripts() {
    _runtimeScripts.begin();
    String startup = _runtimeScripts.startupScriptPath();
    if (startup.length() == 0) {
        return;
    }

    String err;
    if (_runtimeScripts.loadStartupScript(&err)) {
        Serial.printf("SCRIPT_STARTUP_LOADED %u %s\n",
            _runtimeScripts.lastInstalledId(),
            _runtimeScripts.lastInstalledName().c_str());
    } else {
        Serial.printf("SCRIPT_ERROR startup %s\n", err.c_str());
    }
}

// ── Resolve our IP ───────────────────────────────────────────
IPAddress OSCHandler::localIP() const {
#if SCOREBOARD_HAS_WIFI
    if (accessPointActive()) {
        return WiFi.softAPIP();
    }
    return WiFi.localIP();
#elif SCOREBOARD_HAS_ETHERNET
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
//    /display/<N>/text2particles — convert rendered text to frozen particles
//    /display/<N>/screen2particles — capture current screen to particles (with colours)
//    /display/<N>/particles    — particle config (up to 26 positional args):
//                                count renderMs gravityScale elasticity wallElasticity
//                                radius renderStyle glowSigma temperature attractStrength attractRange
//                                gravityEnabled substepMs damping glowWavelength
//                                speedColor springStrength springRange springEnabled
//                                coulombStrength coulombRange coulombEnabled
//                                scaffoldStrength scaffoldRange scaffoldEnabled
//                                collisionEnabled
//    /display/<N>/particles/pause — pause/resume physics: 0=run, 1=pause
//    /display/<N>/particles/restore — restore scaffold positions (pauses physics)
//    /display/<N>/particles/restorecolors — restore scaffold colours
//    /display/<N>/particles/transform — view transform: angleDeg scaleX scaleY tx ty
//    /display/<N>/particles/rotate   — rotation only (float degrees)
//    /display/<N>/particles/scale    — scale only (float sx [sy])
//    /display/<N>/particles/translate — translate only (float tx ty)
//    /display/<N>/particles/resettransform — reset view to identity
//    /display/<N>/animation N — select animation N for this display (0=off)
//    /display/<N>/animation/start — start selected animation immediately
//    /display/<N>/animation/stop — stop current animation on this display
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
//    /text2particles           — text-to-particles on all displays
//    /screen2particles         — screen-to-particles on all displays (with colours)
//    /particles/pause          — pause/resume physics (all displays): 0/1
//    /particles/restore        — restore scaffold positions (all, pauses physics)
//    /particles/restorecolors  — restore scaffold colours (all)
//    /particles/rotate         — rotation (all, float degrees)
//    /particles/scale          — scale (all, float sx [sy])
//    /particles/translate      — translate (all, float tx ty)
//    /particles/resettransform — reset all view transforms
//    /defaults                 — reset all params to compiled defaults
//    /saveparams [bank]        — save params to NVS bank (default=startup)
//    /loadparams [bank]        — load params from NVS bank (default=startup)
//    /startupbank [bank]       — get/set startup bank [1..5]
//    /save                     — alias of /saveparams
//    /load                     — alias of /loadparams
//    /animation N              — select animation N for all displays (0=off)
//    /animation/stop           — stop running animations on all displays
//    /script/begin             — start staged runtime script upload
//    /script/append "line"     — append one source line to staged script text
//    /script/commit            — parse/install staged runtime script
//    /script/cancel            — discard staged script text
//    /script/save "file"       — save staged script text to SPIFFS
//    /script/load "file"       — load + install script text from SPIFFS
//    /script/delete "file"     — delete stored script from SPIFFS
//    /script/files             — list stored .anim files in SPIFFS
//    /script/list              — list currently loaded runtime scripts
//    /script/unload N          — remove one runtime script by id
//    /script/status            — show staged upload + storage status
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
                if (msg.size() >= 17 && msg.isFloat(16)) cfg.springStrength  = msg.getFloat(16);
                if (msg.size() >= 18 && msg.isFloat(17)) cfg.springRange     = msg.getFloat(17);
                if (msg.size() >= 19 && msg.isInt(18))   cfg.springEnabled   = (msg.getInt(18) != 0);
                if (msg.size() >= 20 && msg.isFloat(19)) cfg.coulombStrength  = msg.getFloat(19);
                if (msg.size() >= 21 && msg.isFloat(20)) cfg.coulombRange     = msg.getFloat(20);
                if (msg.size() >= 22 && msg.isInt(21))   cfg.coulombEnabled   = (msg.getInt(21) != 0);
                if (msg.size() >= 23 && msg.isFloat(22)) cfg.scaffoldStrength = msg.getFloat(22);
                if (msg.size() >= 24 && msg.isFloat(23)) cfg.scaffoldRange    = msg.getFloat(23);
                if (msg.size() >= 25 && msg.isInt(24))   cfg.scaffoldEnabled  = (msg.getInt(24) != 0);
                if (msg.size() >= 26 && msg.isInt(25))   cfg.collisionEnabled = (msg.getInt(25) != 0);
                if (msg.size() >= 27 && msg.isInt(26))   cfg.attractEnabled   = (msg.getInt(26) != 0);
                _display.setParticleConfig(idx, cfg);
                Serial.printf("D%d particles: n=%d grav=%.1f(%s) att=%s %.2f@%.1f temp=%.2f spr=%.2f@%.1f(%s) coul=%.2f@%.1f(%s) scf=%.2f@%.1f(%s) col=%s\n",
                              displayNum, cfg.count,
                              cfg.gravityScale, cfg.gravityEnabled ? "on" : "off",
                              cfg.attractEnabled ? "on" : "off", cfg.attractStrength, cfg.attractRange,
                              cfg.temperature,
                              cfg.springStrength, cfg.springRange, cfg.springEnabled ? "on" : "off",
                              cfg.coulombStrength, cfg.coulombRange, cfg.coulombEnabled ? "on" : "off",
                              cfg.scaffoldStrength, cfg.scaffoldRange, cfg.scaffoldEnabled ? "on" : "off",
                              cfg.collisionEnabled ? "on" : "off");
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
        else if (strcmp(subCmd, "text/stack") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd && msg.isString(0)) {
                char text[512];
                msg.getString(0, text, sizeof(text));
                vd->replaceTextStack(text);
                Serial.printf("D%d text stack replaced (%d entries)\n", displayNum, vd->textCount());
            }
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
        // ── /display/<N>/text2particles — convert text to frozen particles ─
        else if (strcmp(subCmd, "text2particles") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                vd->textToParticles();
                Serial.printf("D%d text→particles\n", displayNum);
            }
        }
        // ── /display/<N>/screen2particles — capture screen to particles ──
        else if (strcmp(subCmd, "screen2particles") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                vd->screenToParticles();
                Serial.printf("D%d screen→particles\n", displayNum);
            }
        }
        else if (strcmp(subCmd, "particles/add") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                uint8_t amount = 1;
                if (msg.isInt(0) && msg.getInt(0) > 0) {
                    amount = (uint8_t)msg.getInt(0);
                }
                uint16_t count = vd->addRandomParticle(amount);
                Serial.printf("D%d particles add → %u\n", displayNum, count);
            }
        }
        else if (strcmp(subCmd, "particles/clear") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                vd->clearParticles();
                Serial.printf("D%d particles cleared\n", displayNum);
            }
        }
        // ── /display/<N>/particles/pause — pause/resume physics ─
        else if (strcmp(subCmd, "particles/pause") == 0) {
            if (msg.isInt(0)) {
                VirtualDisplay* vd = _display.getDisplay(idx);
                if (vd) {
                    vd->setPhysicsPaused(msg.getInt(0) != 0);
                    Serial.printf("D%d physics %s\n", displayNum, msg.getInt(0) ? "PAUSED" : "RUNNING");
                }
            }
        }
        // ── /display/<N>/particles/restore — restore scaffold positions ─
        else if (strcmp(subCmd, "particles/restore") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                vd->restoreScaffoldPositions();
                vd->setPhysicsPaused(true);
                Serial.printf("D%d scaffold positions restored\n", displayNum);
            }
        }
        // ── /display/<N>/particles/restorecolors — restore scaffold colors ─
        else if (strcmp(subCmd, "particles/restorecolors") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                vd->restoreScaffoldColors();
                Serial.printf("D%d scaffold colours restored\n", displayNum);
            }
        }
        // ── /display/<N>/particles/transform — set view transform ─
        //   args: angleDeg scaleX scaleY tx ty  (all float, all optional)
        else if (strcmp(subCmd, "particles/transform") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                ParticleTransform2D t = vd->particleTransform();
                if (msg.size() >= 1 && msg.isFloat(0)) vd->setParticleRotation(msg.getFloat(0));
                if (msg.size() >= 3 && msg.isFloat(1) && msg.isFloat(2))
                    vd->setParticleScale(msg.getFloat(1), msg.getFloat(2));
                if (msg.size() >= 5 && msg.isFloat(3) && msg.isFloat(4))
                    vd->setParticleTranslation(msg.getFloat(3), msg.getFloat(4));
                Serial.printf("D%d transform: rot=%.1f° s=(%.2f,%.2f) t=(%.1f,%.1f)\n",
                              displayNum, msg.size() >= 1 ? msg.getFloat(0) : 0.0f,
                              vd->particleTransform().scaleX, vd->particleTransform().scaleY,
                              vd->particleTransform().tx, vd->particleTransform().ty);
            }
        }
        // ── /display/<N>/particles/rotate — set rotation only ─
        else if (strcmp(subCmd, "particles/rotate") == 0) {
            if (msg.isFloat(0)) {
                VirtualDisplay* vd = _display.getDisplay(idx);
                if (vd) {
                    vd->setParticleRotation(msg.getFloat(0));
                    Serial.printf("D%d rotate %.1f°\n", displayNum, msg.getFloat(0));
                }
            }
        }
        // ── /display/<N>/particles/scale — set scale only ─
        else if (strcmp(subCmd, "particles/scale") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                float sx = msg.isFloat(0) ? msg.getFloat(0) : 1.0f;
                float sy = (msg.size() >= 2 && msg.isFloat(1)) ? msg.getFloat(1) : sx;
                vd->setParticleScale(sx, sy);
                Serial.printf("D%d scale (%.2f, %.2f)\n", displayNum, sx, sy);
            }
        }
        // ── /display/<N>/particles/translate — set translation only ─
        else if (strcmp(subCmd, "particles/translate") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                float tx = msg.isFloat(0) ? msg.getFloat(0) : 0.0f;
                float ty = (msg.size() >= 2 && msg.isFloat(1)) ? msg.getFloat(1) : 0.0f;
                vd->setParticleTranslation(tx, ty);
                Serial.printf("D%d translate (%.1f, %.1f)\n", displayNum, tx, ty);
            }
        }
        // ── /display/<N>/particles/resettransform — reset to identity ─
        else if (strcmp(subCmd, "particles/resettransform") == 0) {
            VirtualDisplay* vd = _display.getDisplay(idx);
            if (vd) {
                vd->resetParticleTransform();
                Serial.printf("D%d transform reset\n", displayNum);
            }
        }
        // ── /display/<N>/animation N — select animation script ─
        else if (strcmp(subCmd, "animation") == 0) {
            if (msg.isInt(0)) {
                int aid = msg.getInt(0);
                if (aid < 0) aid = 0;
                _display.setAnimation(idx, (uint8_t)aid);
                Serial.printf("D%d animation slot selected -> %d (%s)\n", displayNum, aid,
                              _display.animationName(idx));
            }
        }
        // ── /display/<N>/animation/start — start selected animation now ─
        else if (strcmp(subCmd, "animation/start") == 0) {
            _display.startAnimation(idx);
            Serial.printf("D%d animation started\n", displayNum);
        }
        // ── /display/<N>/animation/stop — stop animation runtime ─
        else if (strcmp(subCmd, "animation/stop") == 0) {
            _display.stopAnimation(idx);
            Serial.printf("D%d animation stopped\n", displayNum);
        }
        // ── /display/<N>/state — dump current runtime params as JSON ─
        else if (strcmp(subCmd, "state") == 0) {
            _display.notifyDisplayState(idx);
        }

        if (strcmp(subCmd, "state") != 0 && strcmp(subCmd, "text/list") != 0) {
            _display.schedulePersist();
        }
    }
    // ── /brightness ──────────────────────────────────────────
    else if (strcmp(address, "/brightness") == 0) {
        if (msg.isInt(0)) {
            _display.setBrightness(msg.getInt(0));
            Serial.printf("Brightness → %ld\n", (long)msg.getInt(0));
            _display.schedulePersist();
        }
    }
    // ── /mode — set display mode for all displays ────────────
    else if (strcmp(address, "/mode") == 0) {
        DisplayMode mode;
        if (_parseDisplayModeArg(msg, 0, &mode)) {
            _display.setModeAll(mode);
            Serial.printf("All mode → %d\n", (int)mode);
            _display.schedulePersist();
        }
    }
    // ── /particles/enable — toggle particles overlay (all) ──
    else if (strcmp(address, "/particles/enable") == 0) {
        if (msg.isInt(0)) {
            _display.setParticlesEnabled(msg.getInt(0) != 0);
            Serial.printf("All particles → %s\n", msg.getInt(0) ? "ON" : "OFF");
            _display.schedulePersist();
        }
    }
    // ── /particles/brightness — particle layer brightness (all) ─
    else if (strcmp(address, "/particles/brightness") == 0) {
        if (msg.isInt(0)) {
            _display.setParticleBrightness(msg.getInt(0));
            Serial.printf("All particle brightness → %ld\n", (long)msg.getInt(0));
            _display.schedulePersist();
        }
    }
    // ── /particles/color — particle colour (all, R G B) ─────
    else if (strcmp(address, "/particles/color") == 0) {
        if (msg.size() >= 3 && msg.isInt(0)) {
            _display.setParticleColor(msg.getInt(0), msg.getInt(1), msg.getInt(2));
            Serial.printf("All particle color (%ld,%ld,%ld)\n",
                          (long)msg.getInt(0), (long)msg.getInt(1), (long)msg.getInt(2));
            _display.schedulePersist();
        }
    }
    // ── /text/enable — toggle text layer (all) ──────────────
    else if (strcmp(address, "/text/enable") == 0) {
        if (msg.isInt(0)) {
            _display.setTextEnabled(msg.getInt(0) != 0);
            Serial.printf("All text → %s\n", msg.getInt(0) ? "ON" : "OFF");
            _display.schedulePersist();
        }
    }
    // ── /text/brightness — text layer brightness (all) ──────
    else if (strcmp(address, "/text/brightness") == 0) {
        if (msg.isInt(0)) {
            _display.setTextBrightness(msg.getInt(0));
            Serial.printf("All text brightness → %ld\n", (long)msg.getInt(0));
            _display.schedulePersist();
        }
    }
    // ── /scroll — set scroll mode for all displays ──────────
    else if (strcmp(address, "/scroll") == 0) {
        if (msg.isInt(0)) {
            int mode = msg.getInt(0);
            _display.setScrollModeAll(mode);
            Serial.printf("All scroll → %d\n", mode);
            _display.schedulePersist();
        }
    }
    // ── /scrollspeed — scroll animation speed (ms per pixel) ─
    else if (strcmp(address, "/scrollspeed") == 0) {
        if (msg.isInt(0)) {
            _display.setScrollSpeed(msg.getInt(0));
            Serial.printf("Scroll speed → %ld ms\n", (long)msg.getInt(0));
            _display.schedulePersist();
        }
    }
    // ── /scrollcontinuous — auto-cycle textStack in scroll mode ─
    else if (strcmp(address, "/scrollcontinuous") == 0) {
        if (msg.isInt(0)) {
            _display.setScrollContinuous(msg.getInt(0) != 0);
            Serial.printf("Scroll continuous → %s\n", msg.getInt(0) ? "ON" : "OFF");
            _display.schedulePersist();
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
            _display.schedulePersist();
        }
    }
    else if (strcmp(address, "/text/pop") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->textPop();
        }
        Serial.println("Text pop (all displays)");
        _display.schedulePersist();
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
            _display.schedulePersist();
        }
    }
    else if (strcmp(address, "/text/clear") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->textClear();
        }
        Serial.println("Text stack cleared (all displays)");
        _display.schedulePersist();
    }
    else if (strcmp(address, "/text/stack") == 0) {
        if (msg.isString(0)) {
            char text[512];
            msg.getString(0, text, sizeof(text));
            for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
                VirtualDisplay* vd = _display.getDisplay(i);
                if (vd) vd->replaceTextStack(text);
            }
            Serial.println("Text stack replaced (all displays)");
            _display.schedulePersist();
        }
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
    // ── /text2particles — convert text to frozen particles (all) ─
    else if (strcmp(address, "/text2particles") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->textToParticles();
        }
        Serial.println("All text→particles");
        _display.schedulePersist();
    }
    // ── /screen2particles — capture screen to particles (all) ─
    else if (strcmp(address, "/screen2particles") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->screenToParticles();
        }
        Serial.println("All screen→particles");
        _display.schedulePersist();
    }
    // ── /particles/pause — pause/resume physics (all) ────────
    else if (strcmp(address, "/particles/pause") == 0) {
        if (msg.isInt(0)) {
            for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
                VirtualDisplay* vd = _display.getDisplay(i);
                if (vd) vd->setPhysicsPaused(msg.getInt(0) != 0);
            }
            Serial.printf("All physics %s\n", msg.getInt(0) ? "PAUSED" : "RUNNING");
            _display.schedulePersist();
        }
    }
    // ── /particles/restore — restore scaffold positions (all) ─
    else if (strcmp(address, "/particles/restore") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) { vd->restoreScaffoldPositions(); vd->setPhysicsPaused(true); }
        }
        Serial.println("All scaffold positions restored");
        _display.schedulePersist();
    }
    // ── /particles/restorecolors — restore scaffold colors (all) ─
    else if (strcmp(address, "/particles/restorecolors") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->restoreScaffoldColors();
        }
        Serial.println("All scaffold colours restored");
        _display.schedulePersist();
    }
    // ── /particles/rotate — rotate all (degrees) ─────────────
    else if (strcmp(address, "/particles/rotate") == 0) {
        if (msg.isFloat(0)) {
            for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
                VirtualDisplay* vd = _display.getDisplay(i);
                if (vd) vd->setParticleRotation(msg.getFloat(0));
            }
            Serial.printf("All rotate %.1f°\n", msg.getFloat(0));
            _display.schedulePersist();
        }
    }
    // ── /particles/scale — scale all ─────────────────────────
    else if (strcmp(address, "/particles/scale") == 0) {
        if (msg.isFloat(0)) {
            float sx = msg.getFloat(0);
            float sy = (msg.size() >= 2 && msg.isFloat(1)) ? msg.getFloat(1) : sx;
            for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
                VirtualDisplay* vd = _display.getDisplay(i);
                if (vd) vd->setParticleScale(sx, sy);
            }
            Serial.printf("All scale (%.2f, %.2f)\n", sx, sy);
            _display.schedulePersist();
        }
    }
    // ── /particles/translate — translate all ──────────────────
    else if (strcmp(address, "/particles/translate") == 0) {
        float tx = msg.isFloat(0) ? msg.getFloat(0) : 0.0f;
        float ty = (msg.size() >= 2 && msg.isFloat(1)) ? msg.getFloat(1) : 0.0f;
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->setParticleTranslation(tx, ty);
        }
        Serial.printf("All translate (%.1f, %.1f)\n", tx, ty);
        _display.schedulePersist();
    }
    // ── /particles/resettransform — reset all transforms ─────
    else if (strcmp(address, "/particles/resettransform") == 0) {
        for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
            VirtualDisplay* vd = _display.getDisplay(i);
            if (vd) vd->resetParticleTransform();
        }
        Serial.println("All transforms reset");
        _display.schedulePersist();
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
        _display.schedulePersist();
    }
    // ── /clearqueue — flush all scroll queues ────────────────
    else if (strcmp(address, "/clearqueue") == 0) {
        _display.clearQueueAll();
        Serial.println("All queues cleared");
        _display.schedulePersist();
    }
    // ── /clearall  |  /clear ─────────────────────────────────
    else if (strcmp(address, "/clearall") == 0 ||
             strcmp(address, "/clear")    == 0) {
        _display.clearAll();
        Serial.println("All displays cleared");
        _display.schedulePersist();
    }
    // ── /status — query animation state ──────────────────────
    else if (strcmp(address, "/status") == 0) {
        Serial.printf("ANIMATING %d\n", _display.isAnimating() ? 1 : 0);
    }
    else if (strcmp(address, "/wifi/state") == 0) {
        printWiFiState(Serial);
    }
    // ── /rasterscan — light each LED in sequence ─────────────
    else if (strcmp(address, "/rasterscan") == 0) {
        uint16_t ms = (msg.size() >= 1 && msg.isInt(0)) ? msg.getInt(0) : 30;
        _display.showRasterScan(ms);
    }
    // ── /saveparams [/save] — persist to ESP32 NVS ───────────
    else if (strcmp(address, "/saveparams") == 0 || strcmp(address, "/save") == 0) {
        if (msg.size() >= 1 && msg.isInt(0)) {
            int bank = msg.getInt(0);
            if (bank < 1) bank = 1;
            _display.saveParams((uint8_t)bank);
        } else {
            _display.saveParams();
        }
    }
    // ── /loadparams [/load] — restore from ESP32 NVS ─────────
    else if (strcmp(address, "/loadparams") == 0 || strcmp(address, "/load") == 0) {
        if (msg.size() >= 1 && msg.isInt(0)) {
            int bank = msg.getInt(0);
            if (bank < 1) bank = 1;
            _display.loadParams((uint8_t)bank);
        } else {
            _display.loadParams();
        }
        _display.schedulePersist();
    }
    // ── /startupbank [bank] — get/set startup NVS bank ───────
    else if (strcmp(address, "/startupbank") == 0) {
        if (msg.size() >= 1 && msg.isInt(0)) {
            int bank = msg.getInt(0);
            if (bank < 1) bank = 1;
            _display.setStartupBank((uint8_t)bank);
        }
        Serial.printf("STARTUP_BANK %d\n", _display.startupBank());
    }
    // ── /animation N — select animation for all displays ─────
    else if (strcmp(address, "/animation") == 0) {
        if (msg.isInt(0)) {
            int aid = msg.getInt(0);
            if (aid < 0) aid = 0;
            _display.setAnimationAll((uint8_t)aid);
            Serial.printf("All animation slot selected -> %d\n", aid);
            _display.schedulePersist();
        }
    }
    // ── /animation/stop — stop running animations on all displays ─
    else if (strcmp(address, "/animation/stop") == 0) {
        _display.stopAnimationAll();
        Serial.println("All animations stopped");
        _display.schedulePersist();
    }
    // ── /script/* — runtime script upload + storage ──────────
    else if (strcmp(address, "/script/begin") == 0) {
        _runtimeScripts.beginUpload();
        Serial.println("SCRIPT_UPLOAD BEGIN");
    }
    else if (strcmp(address, "/script/append") == 0) {
        if (msg.isString(0)) {
            char line[160];
            msg.getString(0, line, sizeof(line));
            String err;
            if (_runtimeScripts.appendUploadLine(line, &err)) {
                Serial.printf("SCRIPT_UPLOAD APPEND %u\n", (unsigned)_runtimeScripts.stagedBytes());
            } else {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
            }
        } else {
            Serial.println("SCRIPT_ERROR append expects a string line");
        }
    }
    else if (strcmp(address, "/script/commit") == 0) {
        String err;
        if (_runtimeScripts.commitUpload(&err)) {
            Serial.printf("SCRIPT_LOADED %u %s\n",
                _runtimeScripts.lastInstalledId(),
                _runtimeScripts.lastInstalledName().c_str());
        } else {
            Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
        }
    }
    else if (strcmp(address, "/script/bank/commit") == 0) {
        if (msg.size() >= 1 && msg.isInt(0)) {
            int slot = msg.getInt(0);
            String err;
            if (_runtimeScripts.installBankSlotFromStaged((uint8_t)slot, &err)) {
                Serial.printf("BANK_SLOT_LOADED %d %s\n", slot,
                    _runtimeScripts.lastInstalledName().c_str());
            } else {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
            }
        } else {
            Serial.println("SCRIPT_ERROR bank/commit expects slot number");
        }
    }
    else if (strcmp(address, "/script/cancel") == 0) {
        _runtimeScripts.cancelUpload();
        Serial.println("SCRIPT_UPLOAD CANCEL");
    }
    else if (strcmp(address, "/script/save") == 0) {
        if (msg.isString(0)) {
            char path[96];
            msg.getString(0, path, sizeof(path));
            String err;
            if (_runtimeScripts.saveStagedScript(path, &err)) {
                Serial.printf("SCRIPT_SAVED %s\n", path);
            } else {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
            }
        } else {
            Serial.println("SCRIPT_ERROR save expects a file name");
        }
    }
    else if (strcmp(address, "/script/load") == 0) {
        if (msg.isString(0)) {
            char path[96];
            msg.getString(0, path, sizeof(path));
            String err;
            if (_runtimeScripts.loadScriptFile(path, &err)) {
                Serial.printf("SCRIPT_LOADED %u %s\n",
                    _runtimeScripts.lastInstalledId(),
                    _runtimeScripts.lastInstalledName().c_str());
            } else {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
            }
        } else {
            Serial.println("SCRIPT_ERROR load expects a file name");
        }
    }
    else if (strcmp(address, "/script/bank/load") == 0) {
        if (msg.size() >= 2 && msg.isInt(0) && msg.isString(1)) {
            int slot = msg.getInt(0);
            char path[96];
            msg.getString(1, path, sizeof(path));
            String err;
            if (_runtimeScripts.installBankSlotFromFile((uint8_t)slot, path, &err)) {
                Serial.printf("BANK_SLOT_LOADED %d %s\n", slot,
                    _runtimeScripts.lastInstalledName().c_str());
            } else {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
            }
        } else {
            Serial.println("SCRIPT_ERROR bank/load expects slot and file name");
        }
    }
    else if (strcmp(address, "/script/bank/reseed") == 0) {
        if (msg.size() >= 1 && msg.isInt(0)) {
            int slot = msg.getInt(0);
            String err;
            if (_runtimeScripts.reseedBuiltinBankSlot((uint8_t)slot, &err)) {
                Serial.printf("BANK_SLOT_LOADED %d %s\n", slot,
                    _runtimeScripts.lastInstalledName().c_str());
            } else {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
            }
        } else {
            Serial.println("SCRIPT_ERROR bank/reseed expects slot number");
        }
    }
    else if (strcmp(address, "/script/builtin/load") == 0) {
        if (msg.size() >= 1 && msg.isInt(0)) {
            int builtinId = msg.getInt(0);
            int slot = 0;
            if (msg.size() >= 2) {
                if (!msg.isInt(1)) {
                    Serial.println("SCRIPT_ERROR builtin/load expects builtin id and optional slot number");
                    return;
                }
                slot = msg.getInt(1);
            }

            String err;
            if (_runtimeScripts.installBuiltinBankSlot((uint8_t)builtinId, (uint8_t)slot, &err)) {
                Serial.printf("BANK_SLOT_LOADED %u %s\n",
                    _runtimeScripts.lastInstalledId(),
                    _runtimeScripts.lastInstalledName().c_str());
            } else {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
            }
        } else {
            Serial.println("SCRIPT_ERROR builtin/load expects builtin id and optional slot number");
        }
    }
    else if (strcmp(address, "/script/delete") == 0) {
        if (msg.isString(0)) {
            char path[96];
            msg.getString(0, path, sizeof(path));
            String err;
            if (_runtimeScripts.deleteScriptFile(path, &err)) {
                Serial.printf("SCRIPT_DELETED %s\n", path);
            } else {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
            }
        } else {
            Serial.println("SCRIPT_ERROR delete expects a file name");
        }
    }
    else if (strcmp(address, "/script/startup") == 0) {
        String err;
        if (msg.size() >= 1 && msg.isString(0)) {
            char path[96];
            msg.getString(0, path, sizeof(path));
            String value(path);
            value.trim();
            value.toLowerCase();
            bool ok = false;
            if (value == "" || value == "none" || value == "off" || value == "clear") {
                ok = _runtimeScripts.clearStartupScriptFile(&err);
            } else {
                ok = _runtimeScripts.setStartupScriptFile(path, &err);
            }
            if (!ok) {
                Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
                return;
            }
        }

        String startup = _runtimeScripts.startupScriptPath();
        if (startup.length() == 0) {
            Serial.println("SCRIPT_STARTUP NONE");
        } else {
            Serial.printf("SCRIPT_STARTUP %s\n", startup.c_str());
        }
    }
    else if (strcmp(address, "/script/files") == 0) {
        _runtimeScripts.listScriptFiles(Serial);
    }
    else if (strcmp(address, "/script/bank/list") == 0) {
        _runtimeScripts.listBankSlots(Serial);
    }
    else if (strcmp(address, "/script/builtin/list") == 0) {
        _runtimeScripts.listBuiltinBankSlots(Serial);
    }
    else if (strcmp(address, "/script/list") == 0) {
        _runtimeScripts.listRuntimeScripts(Serial);
    }
    else if (strcmp(address, "/script/unload") == 0) {
        if (msg.isInt(0)) {
            int id = msg.getInt(0);
            if (id < 1) {
                Serial.println("SCRIPT_ERROR unload expects id >= 1");
            } else {
                String err;
                if (_runtimeScripts.unloadRuntimeScript((uint8_t)id, &err)) {
                    Serial.printf("SCRIPT_UNLOADED %d\n", id);
                } else {
                    Serial.printf("SCRIPT_ERROR %s\n", err.c_str());
                }
            }
        } else {
            Serial.println("SCRIPT_ERROR unload expects an integer id");
        }
    }
    else if (strcmp(address, "/script/status") == 0) {
        String startup = _runtimeScripts.startupScriptPath();
        Serial.printf("SCRIPT_STATUS storage=%d staged=%u runtime=%u\n",
            _runtimeScripts.storageReady() ? 1 : 0,
            (unsigned)_runtimeScripts.stagedBytes(),
            runtimeAnimationScriptCount());
        if (startup.length() == 0) {
            Serial.println("SCRIPT_STARTUP NONE");
        } else {
            Serial.printf("SCRIPT_STARTUP %s\n", startup.c_str());
        }
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
#if SCOREBOARD_RS485_ENABLED

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

#endif  // SCOREBOARD_RS485_ENABLED

// ── Shared text-line command parser (used by USB Serial, RS485 & Web) ──

#if SCOREBOARD_HAS_M5UNIFIED
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
            char strArg[160];
            size_t si = 0;
            while (*p && *p != '"' && si < sizeof(strArg) - 1) {
                if (*p == '\\' && p[1] != '\0') {
                    p++;
                }
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
                char tok[160];
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

#if SCOREBOARD_HAS_M5UNIFIED
    _lcdSerialDebug(line);
#endif

    _processMessage(msg);
}
