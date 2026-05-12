#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "emulator.h"
#define LOG_SIZE 100 


struct state {
    uint16_t PC;
    uint8_t opcode;
    uint8_t b1;
    uint8_t b2;
    uint8_t A;
    uint8_t F;
    int counter;
};


int main(int argc, char *argv[]){
    /*uint8_t mem_init[0x10010];
    for(int i = 0; i<0x10010; i++){
        mem_init[i]=0;
    }*/

    struct state log[LOG_SIZE];

    struct MBC1 mbcinstance = {0};
    struct MBC1 *MBC = &mbcinstance;

    uint8_t *mem = calloc(0x10000, 1);
    uint32_t cycles =1;
    uint32_t div_timer = 0;
    uint32_t TIMA_timer = 0;
    uint32_t period;
    static int counter = 0;
    int ppu_timer = 0;
    int log_counter = 0;


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
    ppu_data datainstance = {0};//Maybe this won't work
    ppu_data *data = &datainstance;
    for(int i = 0; i<144*160; i++){
        data->framebuffer[i]=0xFFFFFFFF;
    }
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

    CPU->IME=0;//CHANGE
    CPU->interrupt_pending = 0;
    CPU->vblank_rq = 0;
    CPU->halted = 0;

    mem[0xFF05] = 0x00; // TIMA
    mem[0xFF06] = 0x00; // TMA
    mem[0xFF07] = 0x00; // TAC
    mem[0xFF10] = 0x80;
    mem[0xFF11] = 0xBF;
    mem[0xFF12] = 0xF3;
    mem[0xFF14] = 0xBF;
    mem[0xFF16] = 0x3F;
    mem[0xFF17] = 0x00;
    mem[0xFF19] = 0xBF;
    mem[0xFF1A] = 0x7F;
    mem[0xFF1B] = 0xFF;
    mem[0xFF1C] = 0x9F;
    mem[0xFF1E] = 0xBF;
    mem[0xFF20] = 0xFF;
    mem[0xFF21] = 0x00;
    mem[0xFF22] = 0x00;
    mem[0xFF23] = 0xBF;
    mem[0xFF24] = 0x77;
    mem[0xFF25] = 0xF3;
    mem[0xFF26] = 0xF1;
    mem[LCDC] = 0x91; // LCDC
    mem[0xFF42] = 0x00;
    mem[0xFF43] = 0x00;
    mem[0xFF45] = 0x00;
    mem[0xFF47] = 0xFC;
    mem[0xFF48] = 0xFF;
    mem[0xFF49] = 0xFF;
    mem[0xFF4A] = 0x00;
    mem[0xFF4B] = 0x00;
    mem[IE] = 0x00;

    printf("cart type: %02X\n", mem[0x0147]);

    while(1){
            /*if((counter % 10000) == 0){//(23000<counter < 24000){
                printf("Current PC %04x, current opcode %02x, next two bytes %02x %02x A: %02x F: %02x\n", 
                    CPU->PC, mem[CPU->PC], mem[CPU->PC+1],mem[CPU->PC+2], CPU->A, CPU->F);
                }
            if ((cycles > 10) || (div_timer > 10000)){
                printf("failed, cycles is %d\n",cycles);
                return 1;
            }*/
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
                        cycles = interrupt_service(MBC,CPU,mem,i);
                        break;
                    }
                }
            } else{
                cycles = execute(MBC, CPU, mem, counter);
            }

            if((MBC->current_bank_a != (MBC->RAM_bank_number<<5) ) && MBC->Banking_mode_select){
                memcpy(mem, bank[MBC->RAM_bank_number<<5], 0x4000);
                MBC->current_bank_a = MBC->RAM_bank_number<<5;
            } else if((MBC->current_bank_a != 0 ) && MBC->Banking_mode_select){
                memcpy(mem, bank[0], 0x4000);
                MBC->current_bank_a = 0;
            }
            if(MBC->current_bank_b != (MBC->ROM_bank_number | (MBC->RAM_bank_number<<5))){
                memcpy(mem + 0x4000, bank[MBC->ROM_bank_number | (MBC->RAM_bank_number<<5)], 0x4000);
                MBC->current_bank_b = MBC->ROM_bank_number | (MBC->RAM_bank_number<<5);
            }
            //RAM swap goes here
            for(int i=0; i<4*cycles; i++){
                ppu(CPU, mem, &ppu_timer, data);
            }
            /*if (CPU->PC == 0xD601) {
                    printf("D601 stub: %02X %02X %02X\n",
        mem[0xD601], mem[0xD602], mem[0xD603]);
            }*/
            counter++;

            if(ppu_timer == 0){
                SDL_UpdateTexture(
                    texture,
                    NULL,
                    data->framebuffer,
                    160 * sizeof(uint32_t)
                );

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
            }

            CPU->F &= 0xF0;
            if (mem[0xFF02] & 0x80) {
                putchar(mem[0xFF01]);
                fflush(stdout);
                /*if(mem[0xFF01] =='4'){
                    for(int i = 0; i < LOG_SIZE; i++){
                    printf("Instruction number: %7d PC: %04X opcode: %02X next two bits: %02X %02X A: %02X F: %02X\n", log[i].counter, log[i].PC, log[i].opcode, log[i].b1, log[i].b2, log[i].A, log[i].F);
                    }
                    break;
                }*/
                mem[0xFF02] = 0x00; // clear transfer

            }

            log[log_counter].PC = CPU->PC;
            log[log_counter].A = CPU->A;
            log[log_counter].F = CPU->F;
            log[log_counter].opcode = mem[CPU->PC];
            log[log_counter].b1 = mem[CPU->PC +1];
            log[log_counter].b2 = mem[CPU->PC +2];
            log[log_counter].counter = counter;

            if(log_counter == LOG_SIZE -1){//log size
                log_counter = 0;
            } else{
                log_counter++;
            }

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

            /*if(counter == 16000000){
                for(int i = 0; i < LOG_SIZE; i++){
                    printf("PC: %04X opcode: %02X next two bits: %02X %02X A: %02X F: %02X\n", log[i].PC, log[i].opcode, log[i].b1, log[i].b2, log[i].A, log[i].F);
                }
                break;
            }*/

            //if (mem[0xFF02] != 0) {
            //    printf("Serial control: %02X\n", mem[0xFF02]);
            //}
            /*if (CPU->PC == 0x0100) {
                printf("Starting ROM\n");
            }*/
            //printf("%x ",CPU->PC);
            //if (CPU->PC == 0x04FD || CPU->PC == 0x04FF || CPU->PC == 0x0501 || CPU->PC == 0x0502) {
            //printf("opcode = %02X NEXT = %02X PC=%04X A=%02X F=%02X\n", opcode, next, programmecounter, CPU->A, CPU->F);
            //}
        }
        //will make this into a proper error function.
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

        free(mem);
    }