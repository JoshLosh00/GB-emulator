import serial
import sys

def main():
    if len(sys.argv) != 3: 
        print("Usage: gb-load <port> <rom> ")
        sys.exit(1)
         
    port = sys.argv[1]
         
    name = sys.argv[2]
    
    with open(name, 'rb') as f:
        game = bytearray(f.read())
    
    ser = serial.Serial('COM4', 115200, timeout = None)
    
    ser.write(1)
    
    ack = ser.read(1)
    
    ser.write(bytes([game[0x148]]))
    
    ack = ser.read(1)
        
    rom_size = 0x8000 * (1 << game[0x148])
        
    start = 0
    while(start < rom_size):
        ser.write(game[start : start + 0x100])
        start += 0x100
        ack = ser.read(1)
        
    
    
    
    
