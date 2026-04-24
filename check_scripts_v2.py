import serial
import time
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
                if 'BANK_SLOTS' in line:
                    bank_slots_line = line
                    break
        
        ser.close()
        
        if not bank_slots_line:
            print("Error: Could not find BANK_SLOTS line")
            return

        # Format: BANK_SLOTS 1:demo_infinite_goto_breathe;2:text_hold_burst_reassemble;3:demo_finite_goto_pulse;4:no script;5:no script
        # Note: sometimes "no script" might mean empty?
        
        slots_data = bank_slots_line.replace("BANK_SLOTS ", "").split(";")
        
        slots = {}
        for item in slots_data:
            if ":" in item:
                idx, name = item.split(":", 1)
                slots[int(idx)] = name.strip()
            
        print("\nSummary of slots 1..5:")
        all_named = True
        any_empty = False
        
        for i in range(1, 6):
            name = slots.get(i, "MISSING")
            print(f"Slot {i}: {name}")
            
            if name == "no script" or name == "EMPTY" or name == "MISSING":
                any_empty = True
                # "no script" seems to be the literal name for an empty slot here
            else:
                # Slot has a name!
                pass
                
        # User definition of "has a name" vs "says EMPTY"
        # Based on output: 4 and 5 are "no script". 1, 2, 3 are named.
        
        if all(slots.get(i) and slots.get(i) != "no script" for i in range(1, 6)):
            print("Status: Every slot 1..5 has a name.")
        else:
            print("Status: NOT every slot 1..5 has a name.")

        if any(slots.get(i) == "no script" or slots.get(i) == "EMPTY" for i in range(1, 6)):
             print("Status: Some slots are empty (displayed as 'no script' or 'EMPTY').")
        else:
             print("Status: No slots are empty.")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    check_bank_slots()
