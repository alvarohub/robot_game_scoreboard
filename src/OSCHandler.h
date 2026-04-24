#pragma once
// ═══════════════════════════════════════════════════════════════
//  OSCHandler — receives OSC-over-UDP and drives DisplayManager
//               also accepts text commands over USB-Serial
// ═══════════════════════════════════════════════════════════════

#include "config.h"
#include "DisplayManager.h"
#include "RuntimeScriptManager.h"
#include <OSCMessage.h>

#if SCOREBOARD_HAS_WIFI
  #include <WiFi.h>
  #include <WiFiUdp.h>
#elif SCOREBOARD_HAS_ETHERNET
  #include <SPI.h>
  #include <Ethernet.h>
  #include <EthernetUdp.h>
#endif

class OSCHandler {
public:
    explicit OSCHandler(DisplayManager& display); // explicit means that the constructor cannot be used for implicit conversions or copy-initialization

    /// Connects to the network and starts listening for OSC.
    /// Returns false on failure.
    bool begin();

#if SCOREBOARD_HAS_WIFI
    /// Force WiFi into AP mode and start listening for OSC.
    bool startAccessPoint();

    /// Stop WiFi networking and the OSC UDP listener.
    void stopWiFi();
#endif

    /// Call every loop() — processes all pending UDP packets.
    void update();

    /// Execute a text command (same syntax as serial: "/address arg1 arg2").
    /// Used by the web interface to reuse the serial parser.
    void executeCommand(const char* line);

    /// Returns true when an animation is currently running on any display.
    bool isAnimating() const { return _display.isAnimating(); }

    /// Returns whether runtime script storage is available.
    bool runtimeStorageReady() const { return _runtimeScripts.storageReady(); }

    /// Returns the configured runtime script name for one bank slot.
    String bankSlotName(uint8_t slot) const;

    /// Returns the configured WiFi startup mode as a short string.
    const char* configuredWiFiModeName() const;

    /// Returns true when the WiFi AP interface is currently active.
    bool accessPointActive() const;

    /// Number of connected WiFi clients when AP mode is active.
    uint8_t connectedClientCount() const;

    /// Prints a one-line WiFi state summary for serial / OSC inspection.
    void printWiFiState(Print& out) const;

    /// Prints one display's current JSON state snapshot.
    void printDisplayState(uint8_t displayIndex, Print& out) const;

    /// Initialize runtime script facilities such as on-device storage.
    void beginRuntimeScripts();

#if SERIAL_CMD_ENABLED
    /// Call every loop() — reads serial lines and executes them
    /// as if they were OSC messages.
    void processSerial();
#endif

#if SCOREBOARD_RS485_ENABLED
    /// Call every loop() — reads RS485 (Serial2) lines and executes them
    /// as if they were OSC messages.
    void processRS485();
#endif

    /// The IP address assigned to this device.
    IPAddress localIP() const;

private:
    DisplayManager& _display;

#if SCOREBOARD_HAS_WIFI
    WiFiUDP _udp;
#elif SCOREBOARD_HAS_ETHERNET
    EthernetUDP _udp;
#endif

    void _processMessage(OSCMessage& msg);
    RuntimeScriptManager _runtimeScripts;

#if SERIAL_CMD_ENABLED
    static constexpr size_t SERIAL_BUF_SIZE = 512;
    char    _serialBuf[SERIAL_BUF_SIZE];
    uint8_t _serialPos = 0;
#endif

#if SCOREBOARD_RS485_ENABLED
    static constexpr size_t RS485_BUF_SIZE = 512;
    char    _rs485Buf[RS485_BUF_SIZE];
    uint8_t _rs485Pos = 0;
#endif

    bool _startUdpListener();
    void _handleSerialLine(const char* line);
};
