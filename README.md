# GB-emulator
A project to write a Game Boy emulator in C using SDL2.
Current features:
CPU instruction decoder, basic PPU tile renderer, timers and interrupts, and ROM bank switching via MBC1.
Current limitations:
As yet unimplemented functionality such as the object attribute memory transfer function, memory bank controllers beyond MBC1, joypad input, save data support and audio.
Limited commercial game compatibility. 
The PPU is not yet cycle accurate. 
No GameBoy Color support.

ROMs not included.
This emulator passes all tests in the cpu_instrs.gb test ROM hosted at https://github.com/L-P/blargg-test-roms/.

To build run the command gcc emulator.c PPU.c CPU.c -o GBemulator $(pkg-config --cflags --libs sdl2) on a Unix shell from inside the downloaded folder. 

Technical Notes
This programme aims to decode the data on GameBoy cartirdges and play GameBoy games. 
The file CPU.c decodes one byte instructions and implements the corresponding CPU operation.
The file PPU.c emulates the pixel processing unit of the GameBoy. Graphics are processed by the GameBoy's PPU using a pipeline containing 4 modes during which the PPU may scan
for objects appearing on a particular scanline, push pixels to the screen, or request a 'VBLANK' interrupt. VRAM access is restricted during some of these modes.
In order to circumvent the technical limitation of the address space of a CPU whose programme counter is a 16-bit value, many GB games contain a Memory Bank Controller (MBC).
The MBC determines which of several banks of ROM or RAM are accessed when the CPU reads/writes to memory. 
