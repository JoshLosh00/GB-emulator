#include <stdint.h>
#include <stdbool.h>
#include "LCD_1in3.h"
#include "DEV_Config.h"
// #include "01-special.h"
// #include "dmg.h"
//#include <SDL2/SDL.h>

#define Z 0x80
#define N 0x40
#define Hf 0x20
#define Cy 0x10
//common memory addresses
#define IOREG(m, addr) ((m)->IO[addr - 0xFF00])

#define JOYPaddr 0xFF00
#define SCXaddr 0xFF43
#define SCYaddr 0xFF42
#define LYaddr 0xFF44//current scanline
#define LCDCaddr 0xFF40
#define WYaddr 0xFF4A//window y position
#define WXaddr 0xFF4B//window x position + 7
//Everything beneath and to the right the top left corner as specified by WY and WX is the window
#define BGPaddr 0xFF47 //background palette
#define OBP0addr 0xFF48
#define OBP1addr 0xFF49//object palettes
#define STATaddr 0xFF41
#define IEaddr  0xFFFF//interrupt enable register
#define IFaddr  0xFF0F//Interrupt request flags
#define LYCaddr 0xFF45
#define DMAaddr 0xFF46
#define SBaddr 0xFF01
#define SCaddr 0xFF02
#define DIVaddr 0xFF04
#define TIMAaddr 0xFF05
#define TMAaddr 0xFF06
#define TACaddr 0xFF07
#define NR10addr 0xFF10
#define NR11addr 0xFF11
#define NR12addr 0xFF12
#define NR13addr 0xFF13
#define NR14addr 0xFF14
#define NR21addr 0xFF16
#define NR22addr 0xFF17
#define NR23addr 0xFF18
#define NR24addr 0xFF19
#define NR30addr 0xFF1A
#define NR31addr 0xFF1B
#define NR32addr 0xFF1C
#define NR33addr 0xFF1D
#define NR34addr 0xFF1E
#define NR41addr 0xFF20
#define NR42addr 0xFF21
#define NR43addr 0xFF22
#define NR44addr 0xFF23
#define NR50addr 0xFF24
#define NR51addr 0xFF25
#define NR52addr 0xFF26
#define BANKaddr 0xFF50

#define JOYP(m) IOREG((m),JOYPaddr)
#define SB(m)   IOREG((m),SBaddr)
#define SC(m)   IOREG((m),SCaddr)
#define SCX(m)  IOREG((m),SCXaddr)
#define SCY(m)  IOREG((m),SCYaddr)
#define LY(m)   IOREG((m),LYaddr)
#define LCDC(m) IOREG((m),LCDCaddr)
#define WY(m)   IOREG((m),WYaddr)
#define WX(m)   IOREG((m),WXaddr)
#define BGP(m)  IOREG((m),BGPaddr)
#define OBP0(m) IOREG((m),OBP0addr)
#define OBP1(m) IOREG((m),OBP1addr)
#define STAT(m) IOREG((m),STATaddr)
#define IF(m)   IOREG((m),IFaddr)
#define LYC(m)  IOREG((m),LYCaddr)
#define DMA(m)  IOREG((m),DMAaddr)
#define DIV(m)  IOREG((m),DIVaddr)
#define TIMA(m) IOREG((m),TIMAaddr)
#define TMA(m)  IOREG((m),TMAaddr)
#define TAC(m)  IOREG((m),TACaddr)
#define NR10(m) IOREG((m),NR10addr)
#define NR11(m) IOREG((m),NR11addr)
#define NR12(m) IOREG((m),NR12addr)
#define NR13(m) IOREG((m),NR13addr)
#define NR14(m) IOREG((m),NR14addr)
#define NR21(m) IOREG((m),NR21addr)
#define NR22(m) IOREG((m),NR22addr)
#define NR23(m) IOREG((m),NR23addr)
#define NR24(m) IOREG((m),NR24addr)
#define NR30(m) IOREG((m),NR30addr)
#define NR31(m) IOREG((m),NR31addr)
#define NR32(m) IOREG((m),NR32addr)
#define NR33(m) IOREG((m),NR33addr)
#define NR34(m) IOREG((m),NR34addr)
#define NR41(m) IOREG((m),NR41addr)
#define NR42(m) IOREG((m),NR42addr)
#define NR43(m) IOREG((m),NR43addr)
#define NR44(m) IOREG((m),NR44addr)
#define NR50(m) IOREG((m),NR50addr)
#define NR51(m) IOREG((m),NR51addr)
#define NR52(m) IOREG((m),NR52addr)
#define BANK(m) IOREG((m),BANKaddr)
#define IE(m) (m)->Interrupt_Enable

//Debug macros
#define DEBUG_HEIGHT 144*2//must be multiples of 8
#define DEBUG_WIDTH 160*3


//APU macros
#define NRX1(mem, i) IOREG((mem), NR11addr + 5*i)
#define NRX2(mem, i) IOREG((mem), NR12addr + 5*i)
#define NRX3(mem, i) IOREG((mem), NR13addr + 5*i)
#define NRX4(mem, i) IOREG((mem), NR14addr + 5*i)


#pragma once

/*struct MBC1{
    uint8_t RAM_enable;
    uint8_t ROM_bank_number;
    uint8_t RAM_bank_number;
    uint8_t Banking_mode_select;
    int current_RAM;
    int current_bank_a;
    int current_bank_b;
    int banks;
};*/

struct cartridge{
    enum {
        ROM_ONLY,
        MBC1,
        MBC3,
        MBC5
    } type;//more to be added later


    uint8_t *ROM;
    size_t ROM_size;
    int ROM_banks;
    
    uint8_t *RAM;
    size_t RAM_size;
    int RAM_banks;

    uint8_t RAM_enable;
    uint8_t ROM_bank_number;
    uint8_t ROM_bank_number_9;
    uint8_t RAM_bank_number;
    uint8_t Banking_mode_select;

    void (*MBC_control) (struct cartridge *cart, uint16_t addr, uint8_t value);
    uint8_t (*cart_read) (struct cartridge *cart, uint16_t addr);
    void (*write_exRAM) (struct cartridge *cart, uint16_t addr, uint8_t value);
};

//DO NOT reallocate memory without considering how it affects the APU 
typedef struct{
    uint8_t boot_ROM[0x100];
    bool boot_mapped;

    //uint8_t *ROM;//0-7FFF
    uint8_t VRAM[0x2000];//2 banks in gbc mode 8000-9FFF
    //uint8_t *external_RAM;//A000-BFFF
    uint8_t WRAM[0x2000];//C000-DFFFF, in gbc mode D000-DFFF is switchable
    //Echo ram E000-FDFF
    uint8_t OAM[0xA0];//FE000-FE9F
    //FEA0 - FEFF is unused
    uint8_t IO[0x80];//FF00-FF7F I/O registers
    uint8_t HRAM[0x7F];//FF80-FFFE
    uint8_t Interrupt_Enable;//FFFF

} memory;

typedef struct {
    uint8_t A, F;//registers and flags
    uint8_t B, C; 
    uint8_t D, E;
    uint8_t H, L;

    uint16_t SP;//stack pointer
    uint16_t PC;//programme counter

    uint8_t IME;//interrupt master enable flag
    uint8_t ie_pending;
    uint8_t vblank_rq;
    int frame_timer;
    int transfer_timer;
    bool halted;
    bool transfer_pending;
    bool transfer;
    bool OAM_access;
    bool VRAM_access;
    bool draw;
    int instance;
    //flags to (re)trigger audio channels
    //These do not really conceptually belong here. I'll put them here for now but will change
    bool audio_triggers[4];
} cpu;

/*
typedef struct{
    //references to the memory addresses stored in the memory structure
    //DO NOT reallocate memory without considering how it affects the APU 
    uint8_t *NRX1[4];
    uint8_t *NRX2[4];
    uint8_t *NRX3[4];
    uint8_t *NRX4[4];


    uint16_t DIV_APU;
    uint16_t prev_DIV;

    //The length timers are dependent on NRX1 which describes its initial position 
    uint16_t timers[4];

    uint16_t pulse_divs[2];
    uint16_t ch3_div;
    uint16_t ch4_div;

    uint16_t lfsr;
    
    //data relating to ch1's sweep functionality
    uint16_t shadow;
    uint8_t sweep_pace;
    uint8_t sweep_pos;
    bool sweep_enabled;
    
    //The waveforms of ch1 and 2 are blockwaves, the amplitude is either max or 0
    //To calculate whether the amp is high or low, the position of the current sample (0 - 7) is consulted and checked against the waveform 
    //specified by the duty cycle.
    //ch1_pos gives the number 1-8 of which sample we're on.

    uint8_t pulse_pos[2];
    uint8_t ch3_pos;
    //The length timers are dependent on NRX1 which describes its initial position 
    //The amp entries give the current amplitude of the channel's waveform
    uint8_t pulse_amps[2];
    uint8_t ch3_amp;
    uint8_t ch4_amp;
    //The vol entries give the volume of the channel
    uint8_t vols[4];
    //Envelope timers for channels 1, 2 and 4
    uint8_t env_timers[3];

    bool tick;
    //dac status
    bool dacs[4];
    //channel status. This is also reported by NR52 but
    //"writing to those does not enable or disable the channels, despite many emulators behaving as if it does." 
    bool channel_status[4];
} apu_data;
*/

typedef struct {//As of now this contains redundant fields.They're needed for the FIFO
    int nobjects;
    uint16_t objects[10];
    int8_t obj_scanline[160];

    uint8_t BGcolours[8];
    uint8_t OBJFIFO[16];
    
    uint8_t object_start;
    uint8_t increment;
    uint16_t tilemap;
    uint8_t fetch_x;//fetcherX
    uint8_t fetchy;
    uint8_t scanx;
    uint16_t framebuffer[144 * 160];
    bool transfer;
    bool finish;
    int transfer_timer;
    unsigned int countdown;
    int length;
    int ly_count;
} ppu_data;

struct state {
    uint8_t opcode;
    uint8_t A, F;//registers and flags
    uint8_t B, C; 
    uint8_t D, E;
    uint8_t H, L;

    uint16_t SP;//stack pointer
    uint16_t PC;//programme counter
    uint8_t b1;
    uint8_t b2;
    uint8_t ly;
    uint8_t lcdc;
    uint8_t stat;
    int counter;
};

// struct debug_state {
//     bool on;
//     bool on_req;
//     bool broken;
//     uint32_t framebuffer[DEBUG_HEIGHT * DEBUG_WIDTH];
//     unsigned int x;
//     unsigned int y;
//     SDL_Window *Window;
//     SDL_Texture *Texture;
//     SDL_Renderer *Renderer;
//     char *command;
//     size_t command_length;
//     //char *buffer;
//     bool send;
//     bool paused;
//     uint64_t timer;
//     uint64_t then;
//     double emu_fps;
//     uint16_t breakpoints[10];
//     size_t nbreaks;
// };

typedef enum {
    JR,
    NONE,
    IMM16,
    IMM8,
    IMM16_2,
    IMM8_2,
    IMM8_3,
    CB
} op_format;

typedef struct {
    char name[32];
    size_t length;
    op_format format;
} op_info;

//the returned value is how long the performed actions takes
int ppu(struct cartridge *cart, cpu *CPU, memory *mem, ppu_data *data);

uint32_t execute(struct cartridge *cart, cpu *CPU, memory *mem);

uint32_t interrupt_service(struct cartridge *cart, cpu *CPU, memory *mem, int bit);

//void debugger(struct debug_state *debug, struct cartridge *cart, cpu *CPU, memory *mem);

//void init_table(void);

void mem_write(struct cartridge *cart, cpu *CPU, memory *mem, uint16_t addr, uint8_t value);

uint8_t mem_read(struct cartridge *cart, cpu *CPU, memory *mem, uint16_t addr);

uint8_t unrestricted_read(struct cartridge *cart, memory *mem, uint16_t addr);

void free_cart(struct cartridge *cart);

void cart_init(struct cartridge *cart);

int emulate();

void OAM_DMA_Transfer(struct cartridge *cart, memory *mem);

extern op_info operations[512];

//extern volatile uint32_t statistics[5];

enum inspection_state {
    PPU,
    MEM_READ,
    MEM_WRITE,
    EXECUTE,
    GENERAL,
    TIMERS,
    DRAW,
    DMA,
    DOT_LOOP
}; 

extern volatile enum inspection_state inspect_state;

// uint8_t waveform[4][8] = {
//     {1,1,1,1,1,1,1,0},
//     {0,1,1,1,1,1,1,0},
//     {0,1,1,1,1,0,0,0},
//     {1,0,0,0,0,0,0,1}
// };