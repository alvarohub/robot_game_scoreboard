import serial
import serial.tools.list_ports
import time
import sys

def find_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if "usbmodem" in port.device.lower():
            return port.device
    return None

def main():
    port = find_port()
    if not port:
        print("Error: Could not find usbmodem port.")
        sys.exit(1)
    
    print(f"Connecting to {port}...")
    try:
        ser = serial.Serial(port, 115200, timeout=2)
    except Exception as e:
        print(f"Error opening serial port: {e}")
        sys.exit(1)

    time.sleep(2) # Wait for firmware readiness

    # Clear buffer
    ser.read_all()

    def send_cmd(cmd):
        print(f"Sending: {cmd}")
        ser.write((cmd + "\n").encode())
        time.sleep(0.5)
        response = ser.read_all().decode(errors='ignore')
        if response:
            print(f"Response:\n{response}")
        return response

    send_cmd("/script/begin")
    
    with open("animations/demo1.anim", "r") as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                send_cmd(f'/script/append "{line}"')

    send_cmd("/script/bank/commit 1")
    send_cmd("/script/bank/list")
    send_cmd("/display/1/state")

    ser.close()

if __name__ == "__main__":
    main()
