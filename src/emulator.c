#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "dmg.h"
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

int SDL_main(int argc, char *argv[]){

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);//should probably make this into an init function that checks whether it succeeds

    struct cartridge cart = {0};

    apu_data audio_data = {0};

    audio_data.DIV_APU = 0;
    audio_data.prev_DIV = 0;
    audio_data.tick = 0;
    uint32_t audio_timer = 0;


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

    int16_t samples[2048];
    for (int i = 0; i < 1024; i++) {
        samples[i] = 0;
    }
    
    //memset(samples, 0, sizeof(samples));
    int sample_size = 0;

    SDL_AudioSpec desired = {0};
    SDL_AudioSpec obtained;

    desired.freq = SAMPLE_RATE;
    desired.format = AUDIO_S16MSB;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = NULL;

    SDL_AudioDeviceID audio_device =
        SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);

    if (audio_device == 0) {
        printf("Audio error: %s\n", SDL_GetError());
        return -1;
    }

    SDL_PauseAudioDevice(audio_device, 0);

    long int audio_phase = 0;

    bool leave = 0;
    bool quick_boot = 0;

    FILE *fp;

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

    memcpy(mem->boot_ROM, dmg_bin, dmg_bin_len);
    mem->boot_mapped = 1;


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
        data->framebuffer[i]=0x9990;
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
        NR32(mem) = 0x9F;
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
    
    cart_init(&cart);

    uint64_t generator = 0;
    int transfer_timer = 0;

    //CPU->begin_count = true;
    CPU->counter = 0;
    CPU->frames = 0;

    int ppu_countdown = 0;
    data->ly_count = 0;
    data->mode = MODE2;
    data->length = 0;

    int ch4_counter = 0;
    while(1){

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


        assign_joyp(mem, joypad);//The way the joypad works is that it must be continuously updated probably not every call though

        uint8_t prev_DIV = DIV(mem);

        div_timer += cycles;//M cycles
        if (div_timer > 64){//writing here resets the value to 0
            DIV(mem)++;//incrementing the DIV register
            div_timer -= 64;
        }

        if(((prev_DIV & 0x10) - (DIV(mem) & 0x10)) == 0x10){//falling edge
            audio_data.prev_DIV = audio_data.DIV_APU;
            (audio_data.DIV_APU)++;
            apu_div_actions(&audio_data, CPU, mem);
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
            for(int i = 0; i<5; i++){
                if(pending & (1<<i)){
                    cycles = interrupt_service(&audio_data, &cart,CPU,mem,i);//always 5
                    break;
                }
            }
        } else{
            cycles = execute(&audio_data, &cart, CPU, mem);
            if(CPU->transfer){//tranfers take 640 dots in normal speed or 320 in double speed
                data->transfer_timer += cycles;
                if(data->transfer_timer >= 640){//this means we get 640 iterations of the loop when this flag is active (values 0 to 639)
                    CPU->transfer = 0;
                }
            }
        }

        if(CPU->transfer_pending){
            //printf("transfer\n");
            OAM_DMA_Transfer(&cart, mem);
            // OAM_transfer = true;
            CPU->transfer_pending = false;
        }

        if(CPU->transfer){
            transfer_timer += 4*cycles;
            if(transfer_timer >= 640){
                transfer_timer = 0;
                CPU->transfer = false;
            }
        }

        for(int i=0; i<cycles; i++){
   
            if((LCDC(mem)&0x80) == 0){//LCD off
                //mode = 0;
                //if ... {
                    LY(mem) = 0;
                    CPU->OAM_access = 1;
                    CPU->VRAM_access = 1;
                    ppu_countdown = 0;
                    data->finish = 0;
                    data->length = 0;
                    data->ly_count = 0;
                    data->mode = MODE2;
                //}
            } else{
                if(ppu_countdown <= 0){
                    ppu_countdown = ppu(&cart, CPU, mem, data);
                }
                data->ly_count +=4;
                if(data->ly_count >= 456){
                    LY(mem)++;
                    if(LY(mem) == 154)  LY(mem) = 0;
                    if(LY(mem) == LYC(mem)){
                        STAT(mem) |= 0x04;//This is the LY == LYC condition
                    } else{
                        STAT(mem) &= ~0x04;//
                    }
                    data->ly_count = 0;
                }
                ppu_countdown -= 4;
            }

            //APU
            if(audio_data.on){
                clock_pulse(&audio_data, mem);
                clock_wave(&audio_data, mem);
                clock_wave(&audio_data, mem);
                if(audio_data.ch4_clock){
                    if(++ch4_counter >= audio_data.ch4_target){
                        ch4_counter = 0;
                        lfsr_step(&audio_data, mem);
                    }
                }
            }

            audio_phase += 4*SAMPLE_RATE;
                if(audio_phase >= MASTER_CLOCK){
                audio_phase -= MASTER_CLOCK;
                samples[sample_size++] = get_sample_left(&audio_data, mem);
                samples[sample_size++] = get_sample_right(&audio_data, mem);
                if(sample_size == 2048){
                    SDL_QueueAudio(audio_device, samples, sizeof(samples));
                    sample_size = 0;
                }
            }

        }

        CPU->F &= 0xF0;
        if(CPU->draw){

            //once a frame at vblank
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

            // if(countdown == 100000){
            //    debug->on =0;
            // }

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
        //if (SC(mem) & 0x80) {
            //puts("serial output");
            // putchar(SB(mem));
            // fflush(stdout);
            //LCD_1IN3_Clear(RED);
            //SC(mem) = 0x00; // clear transfer

        //}


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

