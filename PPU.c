#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include "emulator.h"



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


void ppu(cpu *CPU, uint8_t *mem, int *dots, ppu_data *data){
    //one dot is a quater of a machine cycle 1/2 in GBC double speed mode
    //154 scanlines (0-153)
    //144-153 are in mode 1 VBLANK
    //first 80 dots of each scanline are in mode 2 the next 376 are other in mode 3 or 0 
    //Currently mode 3 lasts 160 dots - this would be longer on a cycle accurate emulator
    mem[LY] = (*dots)/456;
    uint8_t scanline_dots = (*dots) % 456;
    uint8_t mode;

    if(mem[LY] >= 144){
        //mode 1 Vblank
        mode = 1;
        //nothing happens and all video memory is accessible
    } else{
        if(scanline_dots < 80){//to do set memory
            mode = 2;
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
        } else if((scanline_dots >= 80) && (scanline_dots < 240)){
            //one dot per pixel - always faster then DMG;
            mode = 3;
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
                    obj_y = mem[data->objects[i]];//this is the top row of the object + 16 so a value of 0 corresponds to being offscreen
                    obj_data = mem[data->objects[i]+3];
                    if( (obj_left <= scanx) && (scanx < obj_x)){

                        bool is_low_tile = 
                            (mem[LCDC] & 0x04) && //8x16 objects are enabled
                            ((((mem[LY] + 8)>=obj_y) && ((obj_data&0x40)==0)) ||//either LY is in lines 8 to 15 of the object and there is no y-flip
                            (((mem[LY] + 8)<obj_y) && (obj_data&0x40)));//or LY is in lines 0 to 7 of the object and there is a y-flip

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
            //configure memory access
        }
    } 

    if((*dots)==144*456){
        CPU->vblank_rq = 1;
    }
    if(mem[LY] == mem[LYC]){
        mem[STAT] |= 0x04;//This is the LY == LYC condition
    } else{
        mem[STAT] &= ~0x04;//
    }
    if((mem[LY]>=144) && CPU->vblank_rq){
        mem[0xFF0F] |= 0x01;//requesting inteerrupt
        CPU->vblank_rq = 0;//so I don't constatnly get vblank requests
    }

    mem[STAT] &= 0xFC;//clearing the last 2 bits
    mem[STAT] |= mode;


    if((*dots) >= 70223){//one frame has passed
        *(dots )= 0;
        //SDL draw thing
    } else{
        (*dots)++;
    }
}
