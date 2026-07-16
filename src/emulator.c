#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
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

void assign_joyp(memory *mem, uint8_t joypad){
    JOYP(mem) |= 0x0F;
    if(!(JOYP(mem) & 0x10)){//if bit 4 is 0, the D-pad gets read
        JOYP(mem) &= joypad | 0xF0;
    }
    if(!(JOYP(mem) & 0x20)){//if bit 5 is 0, the other buttons also get read
        JOYP(mem) &= (joypad >> 4) | 0xF0;
    }
}

int main(int argc, char *argv[]){

    struct state log[LOG_SIZE];

    struct cartridge cart = {0};

    memory meminstance = {0};
    memory *mem = &meminstance; 
    mem->boot_mapped = 1;
    uint32_t cycles =1;
    uint32_t div_timer = 0;
    uint32_t TIMA_timer = 0;
    uint32_t period;
    static int counter = 0;
    int log_counter = 0;
    int countdown = 0;
    uint64_t start = 0;
    uint8_t joypad = 0xFF; //Bits 0-3 are D-pad, 4-7 are the others
    struct debug_state debug = {0};
    debug.on_req=0;
    debug.paused=0;
    debug.timer = 0;
    debug.emu_fps = 60.0;
    debug.nbreaks = 0;

    bool leave = 0;
    bool quick_boot = 0;



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
    fseek(fp, 0x148, SEEK_SET);
    uint8_t size_code = fgetc(fp);
    cart.ROM_banks = 2<<size_code;//always at least 2 ROM banks
    cart.ROM_size = 0x4000 * cart.ROM_banks;
    rewind(fp);

    cart.ROM = malloc(cart.ROM_size);
    fread(cart.ROM,1,cart.ROM_size,fp);

    uint8_t cart_type = cart.ROM[0x147];

    switch(cart_type){
        case(0): cart.type = ROM_ONLY;
        break;
        case(1):
        case(2):
        case(3): cart.type = MBC1;
        break;
        case(0x0F):
        case(0x10):
        case(0x11):
        case(0x12):  
        case(0x13): cart.type = MBC3;//I may need to adjust this for whether it has an external battery or not
        break;
        case(0x19):
        case(0x1A):
        case(0x1B):
        case(0x1C):
        case(0x1D):
        case(0x1E): cart.type = MBC5;
        break;
        default:
            printf("Unsupported MBC");

    }

    uint8_t RAM_code = cart.ROM[0x149];
    cart.RAM_banks = RAM_code ? 1 << 2*(RAM_code-2) : 0;
    if(RAM_code == 5){
        cart.RAM_banks = 8;
    }
    cart.RAM_size = 0x2000 * cart.RAM_banks;

    //Reading save files goes here. If there is no .sav (just a binary with a different extension) to read, if there is no .sav then just load 0's into the exram 
    if(cart.RAM_size > 0){//Only temporary. needs support for saves
        cart.RAM = calloc(cart.RAM_size,1);
    } else{
        cart.RAM = NULL;
    }


    printf("There are %d banks\n", cart.ROM_banks);

    fclose(fp);

    fp = fopen("dmg.bin", "rb");

    fread(mem->boot_ROM, 1, 0x100, fp);
    mem->boot_mapped = 1;
    
    fclose(fp);

    init_table();

    cart.Banking_mode_select=0;
    cart.RAM_enable=0;
    cart.ROM_bank_number = 0;
    cart.ROM_bank_number_9 = 0;
    cart.RAM_bank_number = 0;

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
    data->transfer_timer = 0;
    cpu *CPU = &cpuinstance;
    CPU->PC = 0;
    CPU->A = 0;
    CPU->F = 0;
    CPU->B = 0;
    CPU->C = 0;
    CPU->D = 0;
    CPU->E = 0;
    CPU->H = 0;
    CPU->L = 0;
    CPU->SP = 0;

    CPU->IME=1;
    CPU->ie_pending = 0;
    CPU->vblank_rq = 0;
    CPU->halted = 0;
    CPU->transfer_pending = 0;
    CPU->transfer = 0;
    CPU->VRAM_access = 1;
    CPU->OAM_access = 1;
    CPU->frame_timer = 0;
    CPU->draw = 0;
    CPU->instance = 0;

    memset(mem->VRAM,0,0x2000);
    memset(mem->OAM, 0, 0xA0);
    memset(mem->HRAM, 0, 0x7F);
    memset(mem->WRAM,0,0x2000);
    memset(mem->IO, 0, 0x80);
    IE(mem) = 0;

    for (int i = 2; i < argc; i++){
        if(strcmp(argv[i],"quick_boot")==0) quick_boot = 1;
        if(strcmp(argv[i],"debug")==0) debug.on_req = 1;
        //room for more command line prompts
    }
    
    if(quick_boot){
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
        
        JOYP(mem) = 0xCF;
        SB(mem) = 0x00;
        SC(mem) = 0x7E;
        DIV(mem) = 0xAB;//different value on DMG0 
        TIMA(mem) = 0x00; 
        TMA(mem) = 0x00; 
        TAC(mem) = 0xF8;
        IF(mem) = 0xE1;
        NR10(mem) = 0x80;
        NR11(mem) = 0xBF;
        NR12(mem) = 0xF3;
        NR13(mem) = 0xFF;
        NR14(mem) = 0xBF;
        NR21(mem) = 0x3F;
        NR22(mem) = 0x00;
        NR23(mem) = 0xFF;
        NR24(mem) = 0xBF;
        NR30(mem) = 0x7F;
        NR31(mem) = 0xFF;
        NR32(mem) =0x9F;
        NR33(mem) = 0xFF;
        NR34(mem) = 0xBF;
        NR41(mem) = 0xFF;
        NR42(mem) = 0x00;
        NR43(mem) = 0x00;
        NR44(mem) = 0xBF;
        NR50(mem) = 0x77;
        NR51(mem) = 0xF3;
        NR52(mem) = 0xF1;
        LCDC(mem) = 0x91;
        STAT(mem) = 0x81;
        SCX(mem) = 0x00;
        SCY(mem) = 0x00;
        LY(mem) = 0x00;
        LYC(mem) = 0x00;
        DMA(mem) = 0xFF;
        BGP(mem) = 0xFC;
        IE(mem) = 0x00;

        mem->boot_mapped = 0;
    }
    


    while(1){

        SDL_Event e;
        while(SDL_PollEvent(&e)){
            if(e.type == SDL_QUIT){
                SDL_DestroyTexture(texture);
                SDL_DestroyRenderer(renderer);
                SDL_DestroyWindow(window);
                SDL_Quit();

                free_cart(&cart);
                return 0;
            }
            else if (e.type == SDL_KEYDOWN) {
                if(e.key.keysym.sym == SDLK_ESCAPE) {
                    leave = 1;
                    break;
                }
            }
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);

        if(keys[SDL_SCANCODE_P]){
            if(!debug.on){
                debug.on_req = 1;
            }
            debug.paused = 1;
        }



        if(debug.on_req || (debug.on && debug.paused)){
            debugger(&debug, &cart, CPU, mem);
            continue;
        }

        if(debug.on && debug.paused){
            debugger(&debug, &cart, CPU, mem);
            continue;
        }

        //return -1;

        assign_joyp(mem, joypad);//The way the joypad works is that it must be continuously updated

        div_timer += cycles;
        if (div_timer > 64){//writing here resets the value to 0
            DIV(mem)++;//incrementing the DIV register
            div_timer -= 64;
        }
        if(TAC(mem) & 0x04){//TIMA
            switch(TAC(mem) & 0x03){
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
            uint8_t old = TIMA(mem);
            while(TIMA_timer >= period){
                TIMA(mem)++;
                TIMA_timer -= period;
            }
            if(old>TIMA(mem)){
                TIMA(mem) = TMA(mem);
                IF(mem) |= 0x04;
            }
        }

        switch(CPU->ie_pending){
            case(1):
                CPU->ie_pending++;
                break;
            case(2):
                CPU->IME = 1;
                CPU->ie_pending = 0;
        }
        uint8_t pending = IE(mem) & IF(mem);

        if(pending){
            CPU->halted = 0;
        }

        if(CPU->IME && pending){
            //printf("interrupt called\n");
            for(int i = 0; i<5; i++){
                if(pending & (1<<i)){
                    cycles = interrupt_service(&cart,CPU,mem,i);//always 5
                    break;
                }
            }
        } else{
            cycles = execute(&cart, CPU, mem);
        }

        for(int i=0; i<4*cycles; i++){
            ppu(&cart, CPU, mem, data);
        }

        CPU->F &= 0xF0;
        if(CPU->draw){//once a frame at vblank
            //countdown++;
            SDL_UpdateTexture(
                texture,
                NULL,
                data->framebuffer,
                160 * sizeof(uint32_t)
            );

            if(debug.on){
                debugger(&debug, &cart, CPU, mem);
            }

            //if(countdown == 100000){
            //    debug->on =0;
            //}

            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, NULL, NULL);
            SDL_RenderPresent(renderer);
            CPU->draw = 0;
            delay(start, target, tpms);

            uint64_t now = SDL_GetPerformanceCounter();
            uint64_t ticks = now-start;
            start = now;

            debug.emu_fps = (double) freq/(double) ticks;

            SDL_Event e;
            while(SDL_PollEvent(&e)){
                if(e.type == SDL_QUIT){
                    SDL_DestroyTexture(texture);
                    SDL_DestroyRenderer(renderer);
                    SDL_DestroyWindow(window);
                    SDL_Quit();

                    free_cart(&cart);
                    return 0;
                }
                else if (e.type == SDL_KEYDOWN) {
                    if(e.key.keysym.sym == SDLK_ESCAPE) {
                        leave = 1;
                        break;
                    }
                }
            }

            const Uint8 *keys = SDL_GetKeyboardState(NULL);

            if(keys[SDL_SCANCODE_P]){
                if(!debug.on){
                    debug.on_req = 1;
                }
                debug.paused = 1;
            }

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

        }
        //For test rom validation
        /*if (mem[0xFF02] & 0x80) {
            putchar(mem[0xFF01]);
            fflush(stdout);
            mem[0xFF02] = 0x00; // clear transfer

        }*/


       if(leave) break;

       for(int i=0;i<debug.nbreaks;i++){
            if(CPU->PC == debug.breakpoints[i]){
                debug.on = 1;//might lead to errors. Maybe say on_req is 1 too?
                debug.paused = 1;
                debug.broken = 1;
            }
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    free_cart(&cart);
}
