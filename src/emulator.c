#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "tetris.h"
#include "dmg.h"
#include <stdio.h>
#include "pico/stdlib.h"


uint64_t frames;

volatile enum inspection_state inspect_state;

uint32_t statistics[9];

volatile bool timer_fired = false;
volatile bool init_alarm = false;

bool repeating_timer_callback(__unused struct repeating_timer *t) {
    switch(inspect_state){
        case PPU:           statistics[0]++;    break;
        case MEM_READ:      statistics[1]++;    break;
        case MEM_WRITE:     statistics[2]++;    break;
        case EXECUTE:       statistics[3]++;    break;
        case GENERAL:       statistics[4]++;    break;
        case TIMERS:        statistics[5]++;    break;
        case DRAW:          statistics[6]++;    break;
        case DMA:           statistics[7]++;    //break;
        case DOT_LOOP:      statistics[8]++;    //break;
        
    }
    return true;
}

int64_t alarm_callback_0(alarm_id_t id, __unused void *user_data) {
    init_alarm = true;
    // Can return a value here in us to fire in the future
    return 0;
}

int64_t alarm_callback(alarm_id_t id, __unused void *user_data) {
    timer_fired = true;
    // Can return a value here in us to fire in the future
    return 0;
}

static void dma_handler(void)//from RP2040-LCD-LVGL/examples/src/LVGL_example.c
{
    if (dma_channel_get_irq0_status(dma_tx)) {
        dma_channel_acknowledge_irq0(dma_tx);
        DEV_Digital_Write(LCD_CS_PIN, 1);
        dma_finish = true;
    }
}

static void LCD_1IN3_KEY_Init()
{
    DEV_KEY_Config(LCD_KEY_A);
    DEV_KEY_Config(LCD_KEY_B);
    DEV_KEY_Config(LCD_KEY_X);
    DEV_KEY_Config(LCD_KEY_Y);
    DEV_KEY_Config(LCD_KEY_UP);
    DEV_KEY_Config(LCD_KEY_DOWN);
    DEV_KEY_Config(LCD_KEY_LEFT);
    DEV_KEY_Config(LCD_KEY_RIGHT);
    DEV_KEY_Config(LCD_KEY_CTRL);
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

int emulate(){

    // set up alarms in 15 seconds
    add_alarm_in_ms(15000, alarm_callback_0, NULL, false);

    struct repeating_timer timer;
    //add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer);

    dma_finish = true;

    // uint16_t buffer[160 * 144];
    // memset(buffer, 0x1257, sizeof(buffer));
    memset(statistics, 0, sizeof(statistics));

    struct cartridge cart = {0};

    // apu_data audio_data = {0};

    // audio_data.DIV_APU = 0;
    // audio_data.prev_DIV = 0;
    // audio_data.tick = 0;

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
    int ppu_countdown = 0;
    int transfer_timer = 0;
    uint64_t start = 0;
    uint8_t joypad = 0xFF; //Bits 0-3 are D-pad, 4-7 are the others

    bool leave = 0;
    bool quick_boot = 0;
    bool OAM_transfer = false;

    if(DEV_Module_Init()!=0){
        return -1;
    }
    
    /*KEY Init*/
    LCD_1IN3_KEY_Init();

    /*LCD Init*/
    LCD_1IN3_Init(HORIZONTAL);
    LCD_1IN3_Clear(WHITE);

    //dma_init
    dma_channel_set_irq0_enabled(dma_tx, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    cart.ROM = Tetris__World__gb;

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

    memcpy(mem->boot_ROM, dmg_bin, dmg_bin_len);
    mem->boot_mapped = 1;

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
    //LCD_1IN3_DisplayWindows(0, 0, 160, 144, data->framebuffer);

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

    cart_init(&cart);
    inspect_state = GENERAL;

    
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
    
    bool cancelled;

    data->ly_count = 0;
    data->mode = MODE2;

    while(1){

        //assign_joyp(mem, joypad);//The way the joypad works is that it must be continuously updated probably not every call though

        if(init_alarm){
            add_alarm_in_ms(20000, alarm_callback, NULL, false);
            add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer);
            init_alarm = false;
        }

        if(timer_fired){
            cancelled = cancel_repeating_timer(&timer); 
            while(!dma_finish){}
            LCD_1IN3_Clear(BLUE); 
            printf(
            "PPU         %lu\n"
            "MEM_READ    %lu\n"
            "MEM_WRITE   %lu\n"
            "EXECUTE     %lu\n"
            "GENERAL     %lu\n"
            "TIMERS      %lu\n"
            "DRAW        %lu\n"
            "DMA         %lu\n"
            "DOT_LOOP    %lu\n",
            statistics[0],statistics[1],statistics[2],statistics[3],statistics[4],statistics[5],statistics[6],statistics[7],statistics[8]);
            printf("Frame counter   %llu", frames);
            DEV_Delay_ms(4000); 
            timer_fired = 0;
        }

        uint8_t prev_DIV = DIV(mem);

        div_timer += cycles;
        if (div_timer > 64){//writing here resets the value to 0
            DIV(mem)++;//incrementing the DIV register
            div_timer -= 64;
        }

        // if(((prev_DIV & 0x10) - (DIV(mem) & 0x10)) == 0x10){//falling edge
        //     audio_data.DIV_APU++;
        // }

        inspect_state = TIMERS;
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
        inspect_state = GENERAL;

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
                    cycles = interrupt_service(&cart,CPU,mem,i);//always 5
                    break;
                }
            }
        } else{
            inspect_state = EXECUTE;
            cycles = execute(&cart, CPU, mem);
            inspect_state = GENERAL;
            if(CPU->transfer){//tranfers take 640 dots in normal speed or 320 in double speed
                data->transfer_timer += cycles;
                if(data->transfer_timer >= 640){//this means we get 640 iterations of the loop when this flag is active (values 0 to 639)
                    CPU->transfer = 0;
                }
            }
        }

        // if(CPU->transfer){//tranfers take 640 dots in normal speed or 320 in double speed
        //     data->transfer_timer++;
        //     if(data->transfer_timer == 640){//this means we get 640 iterations of the loop when this flag is active (values 0 to 639)
        //         data->transfer = 0;
        //         CPU->transfer = 0;
        //     }
        // }

        // if(data->transfer){//tranfers take 640 dots in normal speed or 320 in double speed
        //         data->transfer_timer += 4*cycles;
        //         if(data->transfer_timer == 640){//this means we get 640 iterations of the loop when this flag is active (values 0 to 639)
        //             data->transfer = 0;
        //             CPU->transfer = 0;
        //         }
        //     }

        if(CPU->transfer_pending){
            //printf("transfer\n");
            inspect_state = DMA;
            OAM_DMA_Transfer(&cart, mem);
            OAM_transfer = true;
            CPU->transfer_pending = false;
        }

        if(CPU->transfer){
            transfer_timer += 4*cycles;
            if(transfer_timer >= 640){
                transfer_timer = 0;
                CPU->transfer = false;
            }
            inspect_state = DOT_LOOP;
        }


        //actions which take place every dot
        for(int i=0; i<cycles; i++){
            inspect_state = DOT_LOOP;

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
                    inspect_state = PPU;
                    ppu_countdown = ppu(&cart, CPU, mem, data);
                    inspect_state = DOT_LOOP;
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

            //apu


            // if(CPU->frame_timer >= 70223){//one frame has passed
            // CPU->frame_timer= 0;
            // } else{
            //     CPU->frame_timer++;
            // }
        }
        inspect_state = GENERAL;

        //CPU->F &= 0xF0;
        if(CPU->draw){
            inspect_state = DRAW;
            if(dma_finish){
                LCD_1IN3_DisplayWindows(0, 0, 160, 144, data->framebuffer);
                dma_finish = 0;
            }
            CPU->draw = 0;
            joypad = ~(
                DEV_Digital_Read(KEY_RIGHT) |
                DEV_Digital_Read(KEY_LEFT) |
                DEV_Digital_Read(KEY_UP) |
                DEV_Digital_Read(KEY_DOWN) |
                DEV_Digital_Read(KEY_A) |
                DEV_Digital_Read(KEY_B) |
                DEV_Digital_Read(KEY_X) |
                DEV_Digital_Read(KEY_Y)
            );

            assign_joyp(mem, joypad);
            frames++;
            inspect_state = GENERAL;
        }

        //For test rom validation
        // if (SC(mem) & 0x80) {
        //     //puts("serial output");
        //     putchar(SB(mem));
        //     fflush(stdout);
        //     SC(mem) = 0x00; // clear transfer

        // }


       //if(leave) break;

    }

    free_cart(&cart);
}