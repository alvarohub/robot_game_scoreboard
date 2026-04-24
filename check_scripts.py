import serial
import time
import json
import sys

def check_bank_slots():
    try:
        ser = serial.Serial('/dev/cu.usbmodem2101', 115200, timeout=1)
        print("Waiting for boot...")
        time.sleep(5)
        ser.flushInput()
        
        print("Sending /script/bank/list")
        ser.write(b"/script/bank/list\n")
        
        bank_slots_line = ""
        start_time = time.time()
        while time.time() - start_time < 10:
            line = ser.readline().decode(errors='ignore').strip()
            if line:
                print(f"Serial Output: {line}")
                if '"id":"BANK_SLOTS"' in line:
                    bank_slots_line = line
                    break
        
        if not bank_slots_line:
            print("Error: Could not find BANK_SLOTS line")
            ser.close()
            return

        print("\nCaptured BANK_SLOTS:")
        print(bank_slots_line)
        
        data = json.loads(bank_slots_line)
        slots = data.get("slots", [])
        
        # Check slots 1 to 5 (assuming 0-indexed or 1-indexed based on output)
        # The instruction says "every slot 1..5"
        
        results = []
        any_empty = False
        all_named = True
        
        # Mapping slots by their "index" field if it exists, else use list position
        slot_map = {s.get("index"): s.get("name") for s in slots if "index" in s}
        
        for i in range(1, 6):
            name = slot_map.get(i)
            if name is None:
              # maybe they are 0-indexed in the JSON but user wants 1-1-5?
              # Let's just look at the raw slots list if index is not present
              if i-1 < len(slots):
                name = slots[i-1].get("name")
              else:
                name = "MISSING"
            
            if name == "EMPTY" or name == "MISSING":
                any_empty = True
                all_named = False
            
            results.append(f"Slot {i}: {name}")
            
        print("\nSummary:")
        for res in results:
            print(res)
            
        if all_named:
            print("Every slot 1..5 has a name.")
        else:
            print("Not all slots 1..5 have a name.")
            
        if any_empty:
            print("At least one slot says EMPTY (or is missing).")
        else:
            print("No slots say EMPTY.")
            
        ser.close()
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    check_bank_slots()
