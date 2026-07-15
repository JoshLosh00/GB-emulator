#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "emulator.h"
#include <string.h>
#include <SDL2/SDL.h>
#include "font8x8_basic.h"


//need to implement the debug features

//kind of have x/8 as my cursor 

void newline(struct debug_state *debug){
    for (int i = 0; i < (DEBUG_HEIGHT - 8); i+= 8){
        memcpy(
            debug->framebuffer + i*DEBUG_WIDTH, 
            debug->framebuffer + i*DEBUG_WIDTH + 8*DEBUG_WIDTH, 
            8*DEBUG_WIDTH*sizeof(uint32_t));//apparently theres a function called memmove
    }
    for (int i = DEBUG_WIDTH*(DEBUG_HEIGHT-8); i<(DEBUG_HEIGHT*DEBUG_WIDTH); i++){
        debug->framebuffer[i] = 0xFF000000;//black
    }
    debug->x = 0;
}

void scroll_down(struct debug_state *debug){
    for (int i = DEBUG_HEIGHT-8; i >= 7; i-= 8){
        memcpy(
            debug->framebuffer + i*DEBUG_WIDTH, 
            debug->framebuffer + i*DEBUG_WIDTH - 8*DEBUG_WIDTH, 
            8*DEBUG_WIDTH*sizeof(uint32_t));//apparently theres a function called memmove
    }
    for (int i = 0; i<8*DEBUG_WIDTH; i++){
        debug->framebuffer[i] = 0xFF000000;//black
    }
}

void draw_glyph(struct debug_state *debug, char glyph){
    if(debug->x > (DEBUG_WIDTH-8)){
        newline(debug);
    }
    unsigned char row = debug->y %8;//needed so I get the right indices
    unsigned char column = debug->x %8;
    for (int i = 0; i<8; i++){
        for (int j = 0 ; j<8; j++){
            if (font8x8_basic[(unsigned char)glyph][row+i]&(1<<(column+j))){
                debug->framebuffer[(debug->y+i) * DEBUG_WIDTH + debug->x+j] = 0xFF00FF00;//green
            } else{
                debug->framebuffer[(debug->y+i) * DEBUG_WIDTH + debug->x+j] = 0xFF000000;//black
            }
        }
    }
    debug->x += 8;
}

static void backspace(struct debug_state *debug){
    debug->command[strlen(debug->command) - 1] = '\0';
    debug->command_length--;
    if(debug->x == 0){
        debug->x = DEBUG_WIDTH -8;
        scroll_down(debug);
    } else{
        debug->x -=8;
    }
    draw_glyph(debug, ' ');
    if(debug->x == 0){
        scroll_down(debug);
        debug->x = DEBUG_WIDTH -8;
    } else{
        debug->x -=8;
    }
}

void draw_line(struct debug_state *debug, char *word){
    size_t length = strlen(word);
    for(int i = 0; i<length; i++){
        draw_glyph(debug, word[i]);
    }
    newline(debug);
}

uint16_t get_number(char symbol){
    if (symbol >='0' && symbol <= '9'){
        return symbol - '0';
    } else if (symbol >= 'A' && symbol <= 'F'){
        return 10 + symbol - 'A';
    } else if (symbol >= 'a' && symbol <= 'f'){
        return 10 + symbol - 'a';
    } else{
        return 0xFFFF;
    }
}

int32_t get_address(char *cmd, cpu *CPU){
    while(*cmd == ' '){
        cmd++;
    }
    size_t len = strlen(cmd);
    int32_t addr = 0;
    if (len==0){
        return -1;
    } else if(strcmp(cmd,"PC")==0){
        return CPU->PC;
    }else if (len>4 || (strspn(cmd,"0123456789abcdefABCDEF")!=len)){
        return -2;
        //draw_line(debug, "Invalid address");
    }
    else{
        for (int i = 0; i<len; i++){
            addr<<=4; 
            addr |= get_number(cmd[i]);
        }
        return addr;
    }
}

void breakpoint(struct debug_state *debug, cpu *CPU){
    int32_t tmp_addr = get_address(debug->command +6,CPU);// command starts with  "break " so this makes sense.
    if (tmp_addr == -1){
        draw_line(debug, "Give breakpoint");
    } else if (tmp_addr == -2){
        draw_line(debug, "Invalid address");
    } else{
        uint16_t addr = tmp_addr;
        char response[64];
        snprintf(response, sizeof(response), "Breakpoint set at %04X", addr);
        draw_line(debug,response);
        debug->breakpoints[debug->nbreaks] = addr;
        debug->nbreaks++;
    }
}

void clear(struct debug_state *debug){
    for (int i = 0; i<debug->nbreaks; i++){
        debug->breakpoints[i]=0;
    }
    debug->nbreaks = 0;
    draw_line(debug, "Breakpoints cleared");
}

void read_memory(struct cartridge *cart, struct debug_state *debug, memory *mem, cpu *CPU){
    char *cmd = debug->command + 5;
    uint32_t tmp_addr = get_address(cmd,CPU);
    switch(tmp_addr){
        case(-1):
        draw_line(debug, "Give address");
        break;
        case(-2):
        draw_line(debug, "Invalid address");
        break;
        default:{
            uint16_t addr = tmp_addr;
            char response[32];
            snprintf(response, sizeof(response), "Address %04X holds %02X", addr, mem_read(cart,CPU,mem,addr));
            draw_line(debug,response);
        }
    }
}

void instr(struct cartridge *cart, struct debug_state *debug, cpu *CPU, memory *mem, uint16_t *position){
    uint16_t PC = *position;
    uint8_t opcode = mem_read(cart, CPU, mem, PC);
    (*position)++;
    op_info data;
    if(opcode == 0xCB){
        data = operations[mem_read(cart,CPU,mem,(*position)++) + 0x100];
    }  else{
        data = operations[opcode];
    }
    char response[32];
    switch(data.format){//The bytes shown by the responces are subject to blocked reads 
        case(IMM8):
        snprintf(response,32, "PC:%04X %s %02X", PC, data.name, mem_read(cart,CPU,mem,(*position)++));
        break;
        case(IMM8_2):
        snprintf(response, 32, data.name, PC, (0xFF00+mem_read(cart,CPU,mem,(*position)++)));
        break;
        case(IMM8_3):
        snprintf(response,32, "PC:%04X %s %02d", PC, data.name, (int8_t)mem_read(cart,CPU,mem,(*position)++));
        break;
        case(IMM16):{
            uint8_t lo = mem_read(cart,CPU,mem,(*position)++);
            uint8_t hi = mem_read(cart,CPU,mem,(*position)++);
            uint16_t addr = (hi<<8)|lo;
            snprintf(response,32, "PC:%04X %s $%04X",PC, data.name, addr);
            break;
        }
        case(IMM16_2):{
            uint8_t lo = mem_read(cart,CPU,mem,(*position)++);
            uint8_t hi = mem_read(cart,CPU,mem,(*position)++);
            uint16_t addr = (hi<<8)|lo;
            snprintf(response,32, data.name, PC, addr);
            break;
        }
        case(JR):{
            int8_t operand = mem_read(cart,CPU,mem,(*position)++);
            uint16_t dest = *position + operand;
            snprintf(response,32, "PC:%04X %s $%04X", PC, data.name, dest);
            break;
        }
        default:
            snprintf(response,32, "PC:%04X %s", PC, data.name);
    }
    draw_line(debug, response);
}

void fps(struct debug_state *debug){
    debug->y = 0;
    unsigned int save_x = debug->x;
    char response[12];
    *response = '\0';
    snprintf(response, sizeof(response), "FPS: %5.2f", debug->emu_fps);
    size_t len = strlen(response);
    debug->x = DEBUG_WIDTH - 8*len;
    for (int i = 0; i<len; i++){
        draw_glyph(debug, response[i]);
    }
    debug->y = DEBUG_HEIGHT -8;
    debug->x = save_x;
}

void show(struct debug_state *debug, cpu*CPU, memory *mem, struct cartridge *cart){
    uint16_t position = CPU->PC;
    for (int i = 0; i<4; i++){
        instr(cart, debug, CPU, mem, &position);
    }
}

void lcd(struct debug_state *debug, memory *mem){
    char response[32];
    snprintf(response,32, "LCDC:%02X  LY:%02X",LCDC(mem),LY(mem));
    draw_line(debug, response);
}

void decode_command(struct debug_state *debug, struct cartridge *cart, cpu*CPU, memory *mem){
    char *cmd = debug->command;
    if(strcmp(cmd,"help")==0){
        draw_line(debug, "Available commands: break XXXX, clear, show, show PC, read XXXX, lcd, continue");
    } else if (strncmp(cmd,"break ",6)==0){
        breakpoint(debug, CPU);
    } else if (strcmp(cmd, "clear")==0){
        clear(debug);
    } else if (strcmp(cmd, "show")==0){
        show(debug, CPU, mem,cart);
    } else if (strncmp(cmd, "read ", 5)==0){
        read_memory(cart,debug, mem, CPU);
    } else if(strcmp(cmd, "continue")==0){
        draw_line(debug, "Game continues.");
        debug->paused = 0;
    } else if (strcmp(cmd, "lcd")==0){
        lcd(debug, mem);
    }
    else{
        draw_line(debug, "Type 'help' for a list of available commands");
    }
}


void debugger(struct debug_state *debug, struct cartridge *cart, cpu *CPU, memory *mem){
    if(debug->on_req){
        SDL_Init(SDL_INIT_VIDEO);
        debug->Window = SDL_CreateWindow(
            "debug",
            100,//SDL_WINDOWPOS_UNDEFINED,//should be something else
            100,//SDL_WINDOWPOS_UNDEFINED,
            DEBUG_WIDTH, DEBUG_HEIGHT,
            0
        );
        debug->Renderer = SDL_CreateRenderer(debug->Window, -1, SDL_RENDERER_ACCELERATED);//maybe don't use that flag
        debug->Texture = SDL_CreateTexture(debug->Renderer, SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,DEBUG_WIDTH,DEBUG_HEIGHT);
        debug->on_req = 0;
        debug->on = 1;//debug->on should never be set to 1 outside of this line
        debug->command = malloc(256);
        debug->command[0] = '\0';
        debug->command_length=0;
        debug->send = 0;
        //debug->buffer = "";
        debug->x = 0; 
        debug->y = DEBUG_HEIGHT-8;
        debug->paused = 1;
        debug->broken = 0;
        debug->then = SDL_GetPerformanceCounter();
    }
    if (debug->on){
        
        if(debug->broken){
            show(debug, CPU, mem, cart);
            debug->broken = 0;
        }

        while(strlen(debug->command)>debug->command_length){
            draw_glyph(debug, debug->command[debug->command_length]);
            debug->command_length++;
        }

        if (debug->send){
            newline(debug);
            decode_command(debug, cart, CPU, mem);
            debug->send = 0;
            debug->command_length = 0;
            debug->command[0] = '\0';
        }

        fps(debug);

        uint64_t freq = SDL_GetPerformanceFrequency();
        uint64_t target = (uint64_t)((double)freq / 59.7275);
        uint64_t now = SDL_GetPerformanceCounter();
        debug->timer  += now-debug->then;
        debug->then = now;


        //Uncomment the following line and corresponding } to only get SDL polling every frame instead of always.  Can lead to unresponsiveness
        //if (debug->timer>target){ 
            SDL_UpdateTexture(
                debug->Texture,
                NULL,
                debug->framebuffer,
                DEBUG_WIDTH * sizeof(uint32_t)
            );

            SDL_RenderClear(debug->Renderer);
            SDL_RenderCopy(debug->Renderer, debug->Texture, NULL, NULL);
            SDL_RenderPresent(debug->Renderer);

            SDL_Event e;
            while(SDL_PollEvent(&e)){
                if(e.type == SDL_QUIT){
                    debug->on=0;
                    SDL_DestroyTexture(debug->Texture);
                    SDL_DestroyRenderer(debug->Renderer);
                    SDL_DestroyWindow(debug->Window);
                }else if (e.type == SDL_TEXTINPUT){
                    strcat(debug->command, e.text.text);
                    break;
                }else if (e.type == SDL_KEYDOWN) {
                    switch (e.key.keysym.sym) {
                        case SDLK_RETURN:
                            debug->send = 1;
                            break;

                        case SDLK_BACKSPACE:
                            if(debug->command_length>0){
                                backspace(debug);
                            }
                            break;
                        case SDLK_ESCAPE:
                            debug->on = 0;
                            debug->paused = 0;
                            SDL_DestroyTexture(debug->Texture);
                            SDL_DestroyRenderer(debug->Renderer);
                            SDL_DestroyWindow(debug->Window);
                            clear(debug);
                            break;
                    }
                }
            }
            debug->timer -= target;
        //} 
    }

}

/* This is to test the debugger by itself 
int main(int argc, char *argv[]){
    cpu CPU = {0};
    struct debug_state dbinstance = {0};
    struct debug_state *debug = &dbinstance;
    debug->on_req=1;// for testing
    int counter = 0;
    uint8_t t = 0;
    memory *mem = &t;
    while(1){//set up this way so it's like howit's called in the main programme
        counter++;
        if (counter >= 144*240){
            counter = 0;
            debugger(debug, &CPU, mem);
            SDL_Delay(10);
        }
        if(!(debug->on_req) && !(debug->on)){
            break;
        }
    }
    return 0;
}
*/
