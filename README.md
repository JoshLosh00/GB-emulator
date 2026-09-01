Bare-metal emulator for the Nintendo Game Boy targeted at the RP2350 using the 1.3 inch LCD screen and the Raspberry pi Pico audio system from Waveshare.

Requirements for loading ROMs. Python 3 pyserial

To use the ROM loader clone the repository and run the following command from the root directory to install the gb-load command python -m pip install -e .

Once installed, enter the command gb-load port path\to\ROM\rom.gb once the emulator has been installed into flash on the RP2350 in "port".

A timer-interrupt based profiler with serial output documenting the relative time spent in each function and the total frame counter in a 20 second interval may be found in the comments of emulator.c.

Known issues: Audio system exists but has been commented out. More work is needed to properly configure the audio. Performance issues when playing ROMs using a Memory Bank Controller. Lack of save data.

Planned Features: Support for games using a wider range of Memory Bank Controllers. Save data support.
