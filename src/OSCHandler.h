#pragma once
// ═══════════════════════════════════════════════════════════════
//  OSCHandler — receives OSC-over-UDP and drives DisplayManager
//               also accepts text commands over USB-Serial
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

    /// Execute a text command (same syntax as serial: "/address arg1 arg2").
    /// Used by the web interface to reuse the serial parser.
    void executeCommand(const char* line);

#if SERIAL_CMD_ENABLED
    /// Call every loop() — reads serial lines and executes them
    /// as if they were OSC messages.
    void processSerial();
#endif

#ifdef USE_RS485
    /// Call every loop() — reads RS485 (Serial2) lines and executes them
    /// as if they were OSC messages.
    void processRS485();
#endif

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

#if SERIAL_CMD_ENABLED
    static constexpr size_t SERIAL_BUF_SIZE = 128;
    char    _serialBuf[SERIAL_BUF_SIZE];
    uint8_t _serialPos = 0;
#endif

#ifdef USE_RS485
    static constexpr size_t RS485_BUF_SIZE = 128;
    char    _rs485Buf[RS485_BUF_SIZE];
    uint8_t _rs485Pos = 0;
#endif

    void _handleSerialLine(const char* line);
};
