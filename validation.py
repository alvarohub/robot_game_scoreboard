import serial
import time
import json
import sys

def get_state(ser):
    ser.write(b"/display/1/state\n")
    start_time = time.time()
    while time.time() - start_time < 3:
        line = ser.readline().decode(errors='ignore').strip()
        if "DISPLAY_STATE" in line:
            # Clean up line to start with {
            idx = line.find('{')
            if idx != -1:
                try:
                    return json.loads(line[idx:])
                except:
                    pass
    return None

try:
    ser = serial.Serial('/dev/cu.usbmodem2101', 115200, timeout=1)
    time.sleep(3) # Wait for boot
    ser.reset_input_buffer()
    
    print("--- Test 1 ---")
    ser.write(b"/display/1 \"HI\"\n")
    time.sleep(1)
    ser.write(b"/display/1/screen2particles\n")
    time.sleep(1)
    state1 = get_state(ser)
    if state1:
        p = state1.get('particles', {})
        print(f"RESULT: particlesEnabled={p.get('enabled')}")
        print(f"RESULT: particles.count={p.get('count')}")
        print(f"RESULT: particles.physicsPaused={p.get('physicsPaused')}")
    else:
        print("FAILED to get state 1")
    
    print("\n--- Test 2 ---")
    # Sending long particles config command
    ser.write(b"/display/1/particles 8 25 18 0.90 0.78 0.45 4 1.2 0 1.5 3 1 17 0.9998 0 0 0 5 1 0 10 0 0 10 0 1 0\n")
    time.sleep(1)
    state2 = get_state(ser)
    if state2:
        p = state2.get('particles', {})
        print(f"RESULT: renderMs={p.get('renderMs')}")
        print(f"RESULT: substepMs={p.get('substepMs')}")
        print(f"RESULT: attractEnabled={p.get('attractEnabled')}")
        print(f"RESULT: springEnabled={p.get('springEnabled')}")
        print(f"RESULT: collisionEnabled={p.get('collisionEnabled')}")
    else:
        print("FAILED to get state 2")

    ser.close()
except Exception as e:
    print(f"Error: {e}")
    sys.exit(1)
