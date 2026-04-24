#include "WebInterface.h"
#include "OSCHandler.h"
#include "Animations.h"

namespace {

class StringPrint : public Print {
public:
    String value;

    size_t write(uint8_t c) override {
        value += (char)c;
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        for (size_t i = 0; i < size; ++i) {
            value += (char)buffer[i];
        }
        return size;
    }
};

void appendJsonString(String& out, const String& value) {
    out += '"';
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    out += '"';
}

}

void WebInterface::_handleCmd() {
    if (!_server.hasArg("q")) {
        _server.send(400, "text/plain", "Missing ?q= parameter");
        return;
    }
    String q = _server.arg("q");
    // Sanitize: limit length, ensure it starts with '/'
    if (q.length() == 0 || q.length() > 512 || q[0] != '/') {
        _server.send(400, "text/plain", "Invalid command");
        return;
    }
    _osc.executeCommand(q.c_str());
    _server.send(200, "text/plain", "OK");
}

void WebInterface::_handleStatus() {
    String json;
    json.reserve(640);
    json += "{\"wifi\":{\"enabled\":";
    json += SCOREBOARD_WIFI_ENABLED ? "true" : "false";
    json += ",\"mode\":";
    appendJsonString(json, _osc.configuredWiFiModeName());
    json += ",\"apActive\":";
    json += _osc.accessPointActive() ? "true" : "false";
    json += ",\"ssid\":";
    appendJsonString(json, WIFI_AP_SSID);
    json += ",\"ip\":";
    appendJsonString(json, _osc.localIP().toString());
    json += ",\"clients\":";
    json += _osc.connectedClientCount();
    json += "},\"animating\":";
    json += _osc.isAnimating() ? "true" : "false";
    json += ",\"storageReady\":";
    json += _osc.runtimeStorageReady() ? "true" : "false";
    json += ",\"bankSlots\":[";
    for (uint8_t slot = 1; slot <= ANIMATION_BANK_SLOT_COUNT; ++slot) {
        if (slot > 1) {
            json += ',';
        }
        json += "{\"slot\":";
        json += slot;
        json += ",\"name\":";
        appendJsonString(json, _osc.bankSlotName(slot));
        json += '}';
    }
    json += ']';
    int displayNumber = 0;
    if (_server.hasArg("display")) {
        displayNumber = _server.arg("display").toInt();
    }
    if (displayNumber >= 1 && displayNumber <= NUM_DISPLAYS) {
        StringPrint state;
        _osc.printDisplayState((uint8_t)(displayNumber - 1), state);
        json += ",\"displayState\":";
        json += state.value;
    }
    json += '}';
    _server.send(200, "application/json", json);
}
