import serial
import time
import sys

def run_test():
    port = '/dev/cu.usbmodem2101'
    baudrate = 115200
    try:
        ser = serial.Serial(port, baudrate, timeout=0.1)
    except Exception as e:
        print(f"Error opening serial port: {e}")
        return

    print("Connected. Waiting for boot...")
    time.sleep(3)
    
    # Read everything to clear
    while ser.in_waiting:
        ser.read(ser.in_waiting)
        time.sleep(0.1)

    print("Sending commands...")
    ser.write(b'/script/bank/list\n')
    time.sleep(1)
    ser.write(b'/display/1/animation 1\n')
    time.sleep(1)
    ser.write(b'/display/1/animation/start\n')
    
    print("Listening...")
    start_time = time.time()
    while time.time() - start_time < 10:
        line = ser.readline().decode('utf-8', errors='replace').strip()
        if line:
            print(f"RECV: {line}")
        time.sleep(0.01)

    ser.close()

if __name__ == "__main__":
    run_test()
