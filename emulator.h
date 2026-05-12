#include <stdint.h>

#define Z 0x80
#define N 0x40
#define Hf 0x20
#define Cy 0x10
//common memory addresses
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
#pragma once

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
    char halted;
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
} ppu_data;

struct state {
    uint16_t PC;
    uint8_t opcode;
    uint8_t b1;
    uint8_t b2;
    uint8_t A;
    uint8_t F;
    int counter;
};

void ppu(cpu *CPU, uint8_t *mem, int *dots, ppu_data *data);

uint32_t execute(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, int counter);

uint32_t interrupt_service(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, int bit);
