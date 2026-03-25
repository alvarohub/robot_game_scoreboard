#pragma once
// ═══════════════════════════════════════════════════════════════
//  OSCHandler — receives OSC-over-UDP and drives DisplayManager
// ═══════════════════════════════════════════════════════════════

#include "config.h"
#include "DisplayManager.h"
#include <OSCMessage.h>

#ifdef USE_WIFI
  #include <WiFi.h>
  #include <WiFiUdp.h>
#elif defined(USE_ETHERNET_W5500)
  #include <SPI.h>
  #include <Ethernet.h>
  #include <EthernetUdp.h>
#endif

class OSCHandler {
public:
    explicit OSCHandler(DisplayManager& display);

    /// Connects to the network and starts listening for OSC.
    /// Returns false on failure.
    bool begin();

    /// Call every loop() — processes all pending UDP packets.
    void update();

    /// The IP address assigned to this device.
    IPAddress localIP();

private:
    DisplayManager& _display;

#ifdef USE_WIFI
    WiFiUDP _udp;
#elif defined(USE_ETHERNET_W5500)
    EthernetUDP _udp;
#endif

    void _processMessage(OSCMessage& msg);
};
