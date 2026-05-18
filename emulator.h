#include <stdint.h>
#include <stdbool.h>

#define Z 0x80
#define N 0x40
#define Hf 0x20
#define Cy 0x10
//common memory addresses
#define JOYP 0xFF00
#define SCX 0xFF43
#define SCY 0xFF42
#define LY 0xFF44//current scanline
#define LCDC 0xFF40
#define WY 0xFF4A//window y position
#define WX 0xFF4B//window x position + 7
//Everything beneath and to the right the top left corner as specified by WY and WX is the window
#define BGP 0xFF47 //background palette
#define OBP0 0xFF48
#define OBP1 0xFF49//object palettes
#define STAT 0xFF41
#define IE  0xFFFF//interrupt enable register
#define IF  0xFF0F//Interrupt request flags
#define LYC 0xFF45
#define DMA 0xFF46
#define P1 0xFF00
#define SB 0xFF01
#define SC 0xFF02
#define DIV 0xFF04
#define TIMA 0xFF05
#define TMA 0xFF06
#define TAC 0xFF07
#define NR10 0xFF10
#define NR11 0xFF11
#define NR12 0xFF12
#define NR13 0xFF13
#define NR14 0xFF14
#define NR21 0xFF16
#define NR22 0xFF17
#define NR23 0xFF18
#define NR24 0xFF19
#define NR30 0xFF1A
#define NR31 0xFF1B
#define NR32 0xFF1C
#define NR33 0xFF1D
#define NR34 0xFF1E
#define NR41 0xFF20
#define NR42 0xFF21
#define NR43 0xFF22
#define NR44 0xFF23
#define NR50 0xFF24
#define NR51 0xFF25
#define NR52 0xFF26
#pragma once

typedef struct {
    char left;
    char right;
    char up;
    char dowm;
    char start;
    char select;
    char a;
    char b;
} buttons;

struct MBC1{
    uint8_t RAM_enable;
    uint8_t ROM_bank_number;
    uint8_t RAM_bank_number;
    uint8_t Banking_mode_select;
    int current_RAM;
    int current_bank_a;
    int current_bank_b;
    int banks;
};

typedef struct {
    uint8_t A, F;//registers and flags
    uint8_t B, C; 
    uint8_t D, E;
    uint8_t H, L;

    uint16_t SP;//stack pointer
    uint16_t PC;//programme counter

    uint8_t IME;//interrupt master enable flag
    uint8_t interrupt_pending;
    uint8_t vblank_rq;
    int frame_timer;
    bool halted;
    bool transfer_pending;
    bool transfer;
    bool OAM_access;
    bool VRAM_access;
    bool draw;
    int instance;
    //struct MBC1 mbc;
} cpu;

typedef struct {//As of now this contains redundant fields.They're needed for the FIFO
    int nobjects;
    uint16_t objects[10];
    /*
    The following entries will be needed for making the emulator cycle accurate
    int8_t BGFIFO[16];//The FIFOs hold information for 16 pixels
    uint8_t OBJFIFO[16];
    uint16_t tilemap;
    uint8_t tilex;
    uint8_t tiley;
    uint8_t fetchx;
    uint8_t scanx;*/
    uint32_t framebuffer[144 * 160];
    bool transfer;
    int transfer_timer;
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

void ppu(cpu *CPU, uint8_t *mem, ppu_data *data);

uint32_t execute(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, int counter);

uint32_t interrupt_service(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, int bit);
