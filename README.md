# GB-emulator
A project to write a Game Boy emulator in C using SDL2.
Current features:
Fully functional CPU instruction decoder, basic PPU tile renderer, joypad input, timers and interrupts, and ROM bank switching via MBC1.
Current limitations:
As yet unimplemented functionality such as, memory bank controllers beyond MBC1 and MBC5, save data support, link cable support and audio.
Limited testing on commercial games. 
The PPU is not yet cycle accurate. 
No GameBoy Color support.

ROMs not included.
This emulator passes all tests in the cpu_instrs.gb test ROM hosted at https://github.com/L-P/blargg-test-roms/. These tests are disigned to examine the implementation of an emulators CPU operations
such as stack maipulation, control flow, flag manipulation and interrupts.

The emulator is a command line tool that is used with the command ./emulator <name of GB ROM> <optional parameters>. At present the optional parameters are "quick_boot" to skip the boot screen and "debug" to open the debugger at startup. 

Tested games: Teris (World)

The emulator is built using Cmake and SDL2. 

Guide to install SDL2:
Windows: inside a MSYS2 shell run the following command: pacman -S mingw-w64-x86_64-SDL2
This requires MSYS2
Linux:sudo apt install libsdl2-dev
MacOS: brew install sdl2
This requires homebrew

To build the emulator first run the following command (this should be a MSYS2 shell if on Windows): git clone https://github.com/JoshLosh00/GB-emulator
Once inside the emulator directory run the command: cmake -B build && cmake --build build
The emulator will then be inside the build directory.


Technical Notes
This programme aims to decode the data on GameBoy cartirdges and play GameBoy games. 
The file CPU.c decodes one byte instructions and implements the corresponding CPU operation.
The file PPU.c emulates the pixel processing unit of the GameBoy. Graphics are processed by the GameBoy's PPU using a pipeline containing 4 modes during which the PPU may scan
for objects appearing on a particular scanline, push pixels to the screen, or request a 'VBLANK' interrupt. VRAM access is restricted during some of these modes.
In order to circumvent the technical limitation of the address space of a CPU whose programme counter is a 16-bit value, many GB games contain a Memory Bank Controller (MBC).
The MBC determines which of several banks of ROM or RAM are accessed when the CPU reads/writes to memory. 
