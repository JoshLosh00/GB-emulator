#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "emulator.h"
#define LOG_SIZE 5 



void delay(uint64_t start, uint64_t target, uint64_t tpms){

    while(1){
        uint64_t now = SDL_GetPerformanceCounter();
        uint64_t elapsed = now - start;

        if(elapsed >= target){
            break;
        }

        uint64_t remaining = target - elapsed;

        if (remaining >= tpms*1.1){//ticks per milisecond with a 10% buffer
            SDL_Delay(1);
        }
    }
}

int main(int argc, char *argv[]){

    struct state log[LOG_SIZE];

    struct MBC1 mbcinstance = {0};
    struct MBC1 *MBC = &mbcinstance;

    uint8_t *mem = calloc(0x10000, 1);
    uint32_t cycles =1;
    uint32_t div_timer = 0;
    uint32_t TIMA_timer = 0;
    uint32_t period;
    static int counter = 0;
    int log_counter = 0;
    char countdown = 0;
    uint64_t start = 0;
    uint8_t joypad = 0xFF; //Bits 0-3 are D-pad, 4-7 are the others

    FILE *fp;

    SDL_Init(SDL_INIT_VIDEO);//should probably make this into an init function that checks whether it succeeds

    SDL_Window *window = SDL_CreateWindow(
        "gb",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        160, 144,
        0
    );

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);//maybe don't use that flag

    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,160,144);

    uint64_t freq = SDL_GetPerformanceFrequency();
    uint64_t target = (uint64_t)((double)freq / 59.7275);
    uint64_t tpms = freq /1000;

    fp = fopen(argv[1], "rb");

    if (fp == NULL){
        printf("There is no file of that name to open.\n");
        return 1;
        }
    fread(mem, 1, 0x8000, fp);//data banks should be considered here
    rewind(fp);
    MBC->banks = 2<<mem[0x0148]; 
    uint8_t bank[MBC->banks][0x4000];//ONLY WORKS FOR ROM BANKS
    for (int i =0; i<MBC->banks; i++){
        fread(bank[i], 1, 0x4000, fp);
    }

    printf("There are %d banks\n", MBC->banks);

    fclose(fp);

    MBC->Banking_mode_select=0;
    MBC->current_RAM = 0;
    MBC->current_bank_a = 0;
    MBC->current_bank_b=1;
    MBC->RAM_bank_number=0;
    MBC->RAM_enable=0;
    MBC->ROM_bank_number=1;

    cpu cpuinstance = {0};
    ppu_data datainstance = {0};
    ppu_data *data = &datainstance;
    for(int i = 0; i<144*160; i++){
        data->framebuffer[i]=0xFFFFFFFF;
    }
    data->nobjects = 0;
    for(int i = 0; i<10; i++){
        data->objects[i]=0;
    }
    data->transfer = 0;
    data->transfer_timer = 0;//Might not technically need this here but good prictice
    cpu *CPU = &cpuinstance;
    //Do an actual boot sequence 
    CPU->A = 0x01;
    CPU->F = 0xB0;
    CPU->B = 0x00;
    CPU->C = 0x13;
    CPU->D = 0x00;
    CPU->E = 0xD8;
    CPU->H = 0x01;
    CPU->L = 0x4D;

    CPU->PC = 0x0100;
    CPU->SP = 0xFFFE;

    CPU->IME=1;//CHANGE
    CPU->interrupt_pending = 0;
    CPU->vblank_rq = 0;
    CPU->halted = 0;
    CPU->transfer_pending = 0;
    CPU->transfer = 0;
    CPU->VRAM_access = 1;
    CPU->OAM_access = 1;
    CPU->frame_timer = 0;
    CPU->draw = 0;
    CPU->instance = 0;

    //skips the boot sequence
    mem[P1] = 0xCF;
    mem[SB] = 0x00;
    mem[SC] = 0x7E;
    mem[DIV] = 0xAB;//different value on DMG0 
    mem[TIMA] = 0x00; 
    mem[TMA] = 0x00; 
    mem[TAC] = 0xF8;
    mem[IF] = 0xE1;
    mem[NR10] = 0x80;
    mem[NR11] = 0xBF;
    mem[NR12] = 0xF3;
    mem[NR13] = 0xFF;
    mem[NR14] = 0xBF;
    mem[NR21] = 0x3F;
    mem[NR22] = 0x00;
    mem[NR23] = 0xFF;
    mem[NR24] = 0xBF;
    mem[NR30] = 0x7F;
    mem[NR31] = 0xFF;
    mem[NR32] = 0x9F;
    mem[NR33] = 0xFF;
    mem[NR34] = 0xBF;
    mem[NR41] = 0xFF;
    mem[NR42] = 0x00;
    mem[NR43] = 0x00;
    mem[NR44] = 0xBF;
    mem[NR50] = 0x77;
    mem[NR51] = 0xF3;
    mem[NR52] = 0xF1;
    mem[LCDC] = 0x91;
    mem[STAT] = 0x81;
    mem[SCX] = 0x00;
    mem[SCY] = 0x00;
    mem[LY] = 0x00;
    mem[LYC] = 0x00;
    mem[DMA] = 0xFF;
    mem[BGP] = 0xFC;
    mem[IE] = 0x00;

    printf("cart type: %02X\n", mem[0x0147]);

    while(1){

        div_timer += cycles;
        if (div_timer > 64){//writing here resets the value to 0
            mem[0xFF04]++;//incrementing the DIV register
            div_timer -= 64;
        }
        if(mem[0xFF07] & 0x04){//TIMA
            switch(mem[0xFF07] & 0x03){
                case 0x00:
                    period = 256;
                    break;
                case 0x01:
                    period = 4;
                    break;
                case 0x02:
                    period = 16;
                    break;
                case 0x03:
                    period = 64;
                    break;
            }
            TIMA_timer += cycles;
            uint8_t old = mem[0xFF05];
            while(TIMA_timer >= period){
                mem[0xFF05]++;
                TIMA_timer -= period;
            }
            if(old>mem[0xFF05]){
                mem[0xFF05] = mem[0xFF06];
                mem[0xFF0F] |= 0x04;
            }
        }

        switch(CPU->interrupt_pending){
            case(1):
                CPU->interrupt_pending++;
                break;
            case(2):
                CPU->IME = 1;
                CPU->interrupt_pending = 0;
        }
        uint8_t pending = mem[IE] & mem[IF];

        if(pending){
            CPU->halted = 0;
        }

        if(CPU->IME && pending){
            //printf("interrupt called\n");
            for(int i = 0; i<5; i++){
                if(pending & (1<<i)){
                    cycles = interrupt_service(MBC,CPU,mem,i);//always 5
                    /*if(i == 0){
                        printf("VBlank interrupt called\n opcode %02X, next bytes: %02X %02X\n", mem[CPU->PC], mem[CPU->PC+1], mem[CPU->PC +2]);
                        return 10;
                    }*/
                    break;
                }
            }
        } else{
            cycles = execute(MBC, CPU, mem, counter);
        }

        /*if((MBC->current_bank_a != (MBC->RAM_bank_number<<5) ) && MBC->Banking_mode_select){
            memcpy(mem, bank[MBC->RAM_bank_number<<5], 0x4000);
            MBC->current_bank_a = MBC->RAM_bank_number<<5;
        } else if((MBC->current_bank_a != 0 ) && MBC->Banking_mode_select){
            memcpy(mem, bank[0], 0x4000);
            MBC->current_bank_a = 0;
        }
        if(MBC->current_bank_b != (MBC->ROM_bank_number | (MBC->RAM_bank_number<<5))){
            memcpy(mem + 0x4000, bank[MBC->ROM_bank_number | (MBC->RAM_bank_number<<5)], 0x4000);
            MBC->current_bank_b = MBC->ROM_bank_number | (MBC->RAM_bank_number<<5);
        }*/
        //RAM swap goes here
        for(int i=0; i<4*cycles; i++){
            ppu(CPU, mem, data);
        }
        /*if (CPU->PC == 0xD601) {
                printf("D601 stub: %02X %02X %02X\n",
    mem[0xD601], mem[0xD602], mem[0xD603]);
        }*/

        if(CPU->instance == 2){
            countdown++;
        }
        if(countdown){
            counter++;
        }

        CPU->F &= 0xF0;
        if(CPU->draw){//once a frame at vblank
            //printf("drawn\n");
            SDL_UpdateTexture(
                texture,
                NULL,
                data->framebuffer,
                160 * sizeof(uint32_t)
            );

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            CPU->draw = 0;
            //delay(start, target, tpms);
            start = SDL_GetPerformanceCounter();

        }
        //For test rom validation
        /*if (mem[0xFF02] & 0x80) {
            putchar(mem[0xFF01]);
            fflush(stdout);
            mem[0xFF02] = 0x00; // clear transfer

        }*/
       SDL_Event e;
            while(SDL_PollEvent(&e)){
                if(e.type == SDL_QUIT){
                    SDL_DestroyTexture(texture);
                    SDL_DestroyRenderer(renderer);
                    SDL_DestroyWindow(window);
                    SDL_Quit();

                    free(mem);
                    return 0;
                }
            }

            const Uint8 *keys = SDL_GetKeyboardState(NULL);

            joypad = ~(
                keys[SDL_SCANCODE_RIGHT] |
                (keys[SDL_SCANCODE_LEFT]<<1) |
                (keys[SDL_SCANCODE_UP]<<2) |
                (keys[SDL_SCANCODE_DOWN]<<3) |
                (keys[SDL_SCANCODE_S]<<4) |
                (keys[SDL_SCANCODE_D]<<5) |
                (keys[SDL_SCANCODE_E]<<6) |
                (keys[SDL_SCANCODE_W]<<7) 
            );

            mem[JOYP] |= 0x0F;
            if(!(mem[JOYP] & 0x10)){//if bit 4 is 0, the D-pad gets read
                mem[JOYP] &= joypad | 0xF0;
            }
            if(!(mem[JOYP] & 0x20)){//if bit 5 is 0, the other buttons also get read
                mem[JOYP] &= (joypad >> 4) | 0xF0;
            }

        log[log_counter].PC = CPU->PC;
        log[log_counter].A = CPU->A;
        log[log_counter].F = CPU->F;
        log[log_counter].B = CPU->B;
        log[log_counter].C = CPU->C;
        log[log_counter].D = CPU->D;
        log[log_counter].E = CPU->E;
        log[log_counter].H = CPU->H;
        log[log_counter].L = CPU->L;
        log[log_counter].opcode = mem[CPU->PC];
        log[log_counter].b1 = mem[CPU->PC +1];
        log[log_counter].b2 = mem[CPU->PC +2];
        log[log_counter].counter = counter;
        log[log_counter].ly = mem[LY];
        log[log_counter].stat = mem[STAT];
        log[log_counter].lcdc = mem[LCDC];
        log[log_counter].SP = CPU->SP;

        if(log_counter == LOG_SIZE -1){//log size
            log_counter = 0;
        } else{
            log_counter++;
        }

        /*if(CPU->LCD_off && CPU->LCD_on){
            printf("PPU switched off outside of Vblank");
            break;
        }*/

    

        if(counter == 2){
            for(int j = 0; j < LOG_SIZE; j++){
                int i = (log_counter +j)%LOG_SIZE; 
                printf(
                    "PC: %04X opcode: %02X next two bits: %02X %02X A: %02X F: %02X BC: %02X%02X DE: %02X%02X HL %02X%02X SP: %04X LY: %02X STAT: %02X LCDC: %02X\n", 
                    log[i].PC, 
                    log[i].opcode, 
                    log[i].b1, log[i].b2, 
                    log[i].A, 
                    log[i].F, 
                    log[i].B, 
                    log[i].C, 
                    log[i].D, 
                    log[i].E, 
                    log[i].H, 
                    log[i].L, 
                    log[i].SP, 
                    log[i].ly, 
                    log[i].stat, 
                    log[i].lcdc);
            }
            break;
        }
    }
    //will make this into a proper error function.
    /* here is not to print the log for debugging purposes
    for(int i = 0; i < LOG_SIZE; i++){
        printf("PC: %04X opcode: %02X next two bits: %02X %02X A: %02X F: %02X\n", log[i].PC, log[i].opcode, log[i].b1, log[i].b2, log[i].A, log[i].F);
    }*/
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    free(mem);
}
