#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"

void OAM_DMA_Transfer(uint8_t *mem){//might also need the MBC to go in here too
    memcpy(mem + 0xFE00, mem + 0x0100*mem[DMA], 0xA0);
}

uint32_t draw_pixel(char object_exists, char BG_enable, uint8_t obj_data, uint8_t bg_colour_id, uint8_t obj_colour_id, uint8_t *mem){

    uint8_t bg_colour;
    uint8_t obj_colour;
    uint32_t dmg_colours[4] = {
    0xFFFFFFFF, // white
    0xFFAAAAAA, // light gray
    0xFF555555, // dark gray
    0xFF000000  // black
    };
    if(BG_enable){
        bg_colour = (mem[BGP]>>(2*bg_colour_id)) & 0x03;
    }
    else{
        bg_colour = 0;
    }
    bool obj_priority = 
        object_exists &&
        (mem[LCDC] & 0x02) &&
        (obj_colour_id != 0) &&
        ((bg_colour_id == 0) || !(obj_data&0x80));

    if (obj_priority){
        uint8_t palette = (obj_data&0x10) ? mem[OBP1] : mem[OBP0];
        obj_colour = (palette>>(2*obj_colour_id))&0x03;
        return dmg_colours[obj_colour];
    } else {
        return dmg_colours[bg_colour];
    }
}


void ppu(cpu *CPU, uint8_t *mem, ppu_data *data){
    //one dot is a quater of a machine cycle 1/2 in GBC double speed mode
    //Probably needs another argument
    //154 scanlines (0-153)
    //144-153 are in mode 1
    //first 80 dots of each scanline are in mode 2 the next 376 are other in mode 3 or 0

    if(data->transfer){//tranfers take 640 dots in normal speed or 320 in double speed
        data->transfer_timer++;
        if(data->transfer_timer == 640){//this means we get 640 iterations of the loop when this flag is active (values 0 to 639)
            data->transfer = 0;
            CPU->transfer = 0;
        }
    }

    if(CPU->transfer_pending){
        OAM_DMA_Transfer(mem);
        CPU->transfer = 1;
        CPU->transfer_pending = 0;
        data->transfer = 1;
        data->transfer_timer = 0;
    }

    //mem[LY] = CPU->frame_timer/456;
    //uint8_t scanline_dots = CPU->frame_timer % 456;
    //uint8_t mode;

    if((mem[LCDC]&0x80) == 0){
        //mode = 0;
        //mem[LY] = 0;
        CPU->OAM_access = 1;
        CPU->VRAM_access = 1;
    } else{
        mem[LY] = CPU->frame_timer/456;
        uint8_t scanline_dots = CPU->frame_timer % 456;
        uint8_t mode;
        if(mem[LY] >= 144){
            //mode 1 Vblank
            CPU->OAM_access = 1;
            CPU->VRAM_access = 1;
            mode = 1;
            //nothing happens and all video memory is accessible
        } else{
            if(scanline_dots < 80){//to do set memory
                mode = 2;
                CPU->OAM_access = 0;
                CPU->VRAM_access = 1;
                //mode 2 - OAM scan
                //VRAM and GBC palettes are accessible
                if((scanline_dots % 2) == 0){
                    if(scanline_dots == 0){
                        data->nobjects = 0;
                    } 
                    //looks for objects on this scan-line
                    int i = scanline_dots/2;
                    uint8_t height = (mem[LCDC] & 0x04) ? 16 : 8;
                    if(data->nobjects<10){
                        if(((mem[LY]+16) >= mem[0xFE00 + 4*i]) &&  ((mem[LY]+16) < (mem[0xFE00 + 4*i] + height))){//The height of objects is 16 is bit 2 of LCDC is set and 8 is not
                            data->objects[data->nobjects] = 0xFE00 + 4*i;//the location of byte 0 of that object
                            data->nobjects++;//important: the objects array stores objects in the order in which they occur in OAM   
                        }
                    }
                }
            } else if((scanline_dots >= 80) && (scanline_dots < 240)){//mode 3 that renders things naively
                //one dot per pixel - always faster then DMG;
                mode = 3;
                CPU->OAM_access = 0;
                CPU->VRAM_access = 0;
                uint8_t scanx = scanline_dots - 80;
                bool inside_window = (mem[LCDC] & 0x20) && ((scanx + 7) >= mem[WX]) && (mem[LY] >= mem[WY]);
                bool object_exists = 0;
                uint8_t pos_x;
                uint8_t pos_y;
                uint8_t i;
                uint8_t obj_x;
                uint8_t obj_y;
                uint8_t obj_id;
                uint8_t obj_hi;
                uint8_t obj_lo;
                uint8_t obj_data = 0;
                uint8_t obj_colour_id=0;
                uint8_t bg_colour_id=0;
                uint16_t map = 0x9800;


                if(mem[LCDC]&0x02){
                    for(i = 0; i<data->nobjects;i++){
                        obj_x = mem[data->objects[i]+1];//this is the right edge of the object
                        int obj_left = obj_x -8;
                        obj_y = mem[data->objects[i]];
                        obj_data = mem[data->objects[i]+3];
                        if( (obj_left <= scanx) && (scanx < obj_x)){

                            bool is_low_tile = 
                                (mem[LCDC] & 0x04) && 
                                ((((mem[LY] + 8)>=obj_y) && ((obj_data&0x40)==0)) ||
                                (((mem[LY] + 8)<obj_y) && (obj_data&0x40)));

                            uint8_t top_tile = mem[data->objects[i]+2];

                            if(mem[LCDC]&0x04){
                                top_tile &=0xFE;
                            }

                            obj_id = is_low_tile ? top_tile + 1: top_tile;

                                //PanDocs comment
                                /*This is enforced by the hardware: the least significant bit of the tile index is ignored; 
                                that is, the top 8×8 tile is “NN & $FE”, and the bottom 8×8 tile is “NN | $01”.*/
                            object_exists = 1;
                            break;
                            //This only works in DMG mode. I can do this bc the objects in the array occur in the order in which they are placed in OAM so the priorities are right
                        }
                    }

                    if(object_exists){//if an object was found
                        uint8_t obj_pixel_x = scanx - obj_x + 8;
                        uint8_t obj_pixel_y =(mem[LY] + 16 - obj_y)%8;//the 16  is mathematically redundant but instructive for what the variables mean
                        if(obj_data&0x40){//yflip
                            obj_lo = mem[0x8000 + 16*obj_id + 2*(7-obj_pixel_y)];
                            obj_hi = mem[0x8000 + 16*obj_id + 2*(7-obj_pixel_y) +1];//start at 8000, tiles are blocks of 16 bytes, the id is which tile. scanline give the two bytes 
                        } else{
                            obj_lo = mem[0x8000 + 16*obj_id + 2*obj_pixel_y];
                            obj_hi = mem[0x8000 + 16*obj_id + 2*obj_pixel_y+1];
                        }
                        if(obj_data&0x20){//xflip
                            //Highest bit give the info for the leftmost pixel
                            //2 to the colour if the high data bit is set and 1 if the low data bit is set
                            obj_colour_id = 2*(((1<<obj_pixel_x)&obj_hi) != 0) + (((1<<obj_pixel_x)&obj_lo) != 0);//right to left
                        } else{
                            obj_colour_id = 2*(((1<<(7-obj_pixel_x))&obj_hi) != 0) + (((1<<(7-obj_pixel_x))&obj_lo) != 0);//left to right
                        }
                    }
                } else{//objects are disabled
                    obj_colour_id = 0;//makes sense as objects with this ID are transparent
                    
                }

                //obj_colour_id = 0;//for debugging
                //object_exists = 0;

                if(mem[LCDC]&0x01){//BG/window is enabled
                    if(inside_window){
                        if(mem[LCDC] & 0x40){
                            map = 0x9C00;
                        }
                        pos_x = scanx - mem[WX] + 7;//no underflow because of the inside_window condition
                        pos_y = mem[LY] - mem[WY];
                    } else{
                        if (mem[LCDC] & 0x08){
                            map = 0x9C00;
                        }
                        pos_x = (mem[SCX] + scanx) % 256;
                        pos_y = (mem[SCY] + mem[LY]) % 256;
                    }
                    uint8_t tile_x = pos_x/8;
                    uint8_t pixel_x = pos_x % 8;
                    uint8_t tile_y = pos_y/8;
                    uint8_t pixel_y = pos_y % 8;
                    uint8_t tile_id = mem[map + tile_y*32 + tile_x];
                    uint16_t addr;
                    if (mem[LCDC] & 0x10){
                        addr = 0x8000 + 16*tile_id;
                    } else{
                        addr = 0x9000 + 16*((int8_t) tile_id);
                    }
                    uint16_t line = addr + 2*pixel_y;
                    uint8_t lo = mem[line];
                    uint8_t hi = mem[line + 1];
                    bg_colour_id = 2*(((1<<(7-pixel_x))&hi) != 0) + (((1<<(7-pixel_x))&lo) != 0);//x travels from left to right, from the "high bits" to the low
                } else{
                    bg_colour_id =0;
                }

                data->framebuffer[160*mem[LY] + scanx] = draw_pixel(object_exists, mem[LCDC]&0x01, obj_data, bg_colour_id, obj_colour_id, mem);
            } else{
                //Hblank
                mode = 0;
                CPU->OAM_access = 1;
                CPU->VRAM_access = 1;
                //configure memory access
            }
        }
        

        /*if(mem[LY]<144){
            if(!(mem[LCDC]&0x80)){
                if(CPU->LCD_on){
                    CPU->LCD_off = 1;
                }
            } else{
                CPU->LCD_on = 1;
            }
        } else{
            CPU->LCD_off = 0;
            CPU->LCD_on = 0;
        }*/

        //if(CPU->frame_timer==144*456){
        //    CPU->vblank_rq = 1;
        //}
        if(mem[LY] == mem[LYC]){
            mem[STAT] |= 0x04;//This is the LY == LYC condition
        } else{
            mem[STAT] &= ~0x04;//
        }
        if((mem[LY]==144) && (scanline_dots == 0)){
            mem[IF] |= 0x01;//requesting inteerrupt
            CPU->draw = 1;
            //CPU->vblank_rq = 0;//so I don't constatnly get vblank requests
        }

        mem[STAT] &= 0xFC;//clearing the last 2 bits
        mem[STAT] |= mode;

    
        if(CPU->frame_timer >= 70223){//one frame has passed
            CPU->frame_timer= 0;
            //SDL draw thing
        } else{
            CPU->frame_timer++;
        }
    
    }
}
/*
Upon entering PPU-off mode LY is set to 0 and the mode is Hblank
Tetris: For SameBoy the PC does not go to $02b2 after $0407 but it does for me. 
Likely has to do with behaviour relating to loading LY or another inaccurate hardware register into a CPU reg
Sameboy also starts drawing the copyright screen 1-2 frames after it reaches $0407
only like 3-4 VBlank interrupts until it displays anything
*/