import serial
import time
import json
import sys

def send_and_get_state(ser, cmd):
    print(f"Sending command: {cmd}")
    ser.write((cmd + "\n").encode())
    time.sleep(1) # Wait for processing
    
    # Empty the buffer of any previous output/logs
    ser.readline() 
    
    ser.write(b"/display/1/state\n")
    time.sleep(1)
    
    state_json = ""
    start_time = time.time()
    while time.time() - start_time < 5:
        line = ser.readline().decode(errors='ignore').strip()
        if line:
            print(f"Serial Output: {line}")
            if line.startswith('{"id":"DISPLAY_STATE"'):
                state_json = line
                break
    
    if state_json:
        try:
            return json.loads(state_json)
        except Exception as e:
            print(f"Failed to parse JSON: {e}")
    return None

try:
    ser = serial.Serial('/dev/cu.usbmodem2101', 115200, timeout=1)
    time.sleep(3) # Wait for boot
    ser.flushInput()
    
    print("--- Test 1: Stack Text ---")
    state = send_and_get_state(ser, '/display/1/text/stack "{hello player, the game, will start, GO}"')
    if state:
        print(f"textCount: {state.get('textCount')}, textItems length: {len(state.get('textItems', []))}")
    
    print("\n--- Test 2: Add Particles ---")
    state = send_and_get_state(ser, '/display/1/particles/add')
    if state:
        particles = state.get('particles', {})
        print(f"particles.count: {particles.get('count')}, particlesEnabled: {particles.get('enabled')}")

    print("\n--- Test 3: Clear Particles ---")
    state = send_and_get_state(ser, '/display/1/particles/clear')
    if state:
        particles = state.get('particles', {})
        print(f"particles.count: {particles.get('count')}, particlesEnabled: {particles.get('enabled')}")

    print("\n--- Test 4: Clear Display ---")
    state = send_and_get_state(ser, '/display/1/clear')
    if state:
        print(f"animationSlot: {state.get('animationSlot')}, textEnabled: {state.get('textEnabled')}, particlesEnabled: {state.get('particles', {}).get('enabled')}")

    ser.close()
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
