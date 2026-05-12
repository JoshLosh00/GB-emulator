# GB-emulator
A project to write a Game Boy emulator in C using SDL2.
Current features:
CPU instruction decoder, basic PPU tile renderer, timers and interrupts, and ROM bank switching via MBC1.
Current limitations:
As yet unimplemented functionality such as the object attribute memory transfer function, memory bank controllers beyond MBC1, joypad input and audio.
Limited commercial game compatibility. 
The PPU is not yet cycle accurate. 

ROMs not included.
This emulator passes all tests in the cpu_instrs.gb test ROM hosted at https://github.com/L-P/blargg-test-roms/.

To build run the command gcc emulator.c PPU.c CPU.c -o GBemulator $(pkg-config --cflags --libs sdl2) on a Unix shell from inside the downloaded folder. 
