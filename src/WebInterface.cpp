#include "WebInterface.h"
#include "OSCHandler.h"

void WebInterface::_handleCmd() {
    if (!_server.hasArg("q")) {
        _server.send(400, "text/plain", "Missing ?q= parameter");
        return;
    }
    String q = _server.arg("q");
    // Sanitize: limit length, ensure it starts with '/'
    if (q.length() == 0 || q.length() > 200 || q[0] != '/') {
        _server.send(400, "text/plain", "Invalid command");
        return;
    }
    _osc.executeCommand(q.c_str());
    _server.send(200, "text/plain", "OK");
}

void WebInterface::_handleStatus() {
    // Minimal JSON status — can be expanded later
    String json = "{\"ap\":true,\"clients\":";
    json += WiFi.softAPgetStationNum();
    json += "}";
    _server.send(200, "application/json", json);
}
