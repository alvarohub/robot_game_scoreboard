import serial
import time
import json
import sys

def run_test():
    try:
        ser = serial.Serial('/dev/cu.usbmodem2101', 115200, timeout=1)
        print("Waiting for boot...")
        time.sleep(3)
        ser.reset_input_buffer()
        
        print("Sending commands to set animation...")
        ser.write(b"/display/1/animation 1\n")
        time.sleep(0.2)
        ser.write(b"/display/1/animation/start\n")
        
        print("Waiting 2 seconds for autosave...")
        time.sleep(2.0)
        
        print("Resetting board...")
        ser.setDTR(False)
        ser.setRTS(True)
        time.sleep(0.1)
        ser.setRTS(False)
        time.sleep(0.5)
        
        print("Waiting for reboot...")
        boot_logs = []
        start_time = time.time()
        found_json = False
        
        # Capture boot logs and request state
        ser.write(b"/display/1/state\n")
        
        while time.time() - start_time < 10:
            line = ser.readline().decode(errors='ignore').strip()
            if line:
                if not found_json:
                    print(f"Log: {line}")
                    boot_logs.append(line)
                
                if '{"id":"DISPLAY_STATE"' in line:
                    found_json = True
                    print("\nFound DISPLAY_STATE JSON:")
                    print(line)
                    try:
                        data = json.loads(line)
                        print(f"Animation Slot: {data.get('animationSlot')}")
                        print(f"Animation Running: {data.get('animationRunning')}")
                    except:
                        print("Failed to parse JSON")
                    break
            
            # Periodically retry requesting state if not received
            if int(time.time() - start_time) % 3 == 0 and not found_json:
                 ser.write(b"/display/1/state\n")

        ser.close()
    except Exception as e:
        print(f"Error: {e}")

run_test()
