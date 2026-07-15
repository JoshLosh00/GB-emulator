#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"

void OAM_DMA_Transfer(struct cartridge *cart, memory *mem){//might also need the MBC to go in here too
    uint16_t addr = (uint16_t)DMA(mem) << 8;
    for(uint16_t i = 0; i<0xA0; i++){
        mem->OAM[i] = unrestricted_read(cart, mem, addr + i);
    }
}

uint32_t draw_pixel(char object_exists, char BG_enable, uint8_t obj_data, uint8_t bg_colour_id, uint8_t obj_colour_id, memory *mem){

    uint8_t bg_colour;
    uint8_t obj_colour;
    uint32_t dmg_colours[4] = {
    0xFFFFFFFF, // white
    0xFFAAAAAA, // light gray
    0xFF555555, // dark gray
    0xFF000000  // black
    };
    if(BG_enable){
        bg_colour = (BGP(mem)>>(2*bg_colour_id)) & 0x03;
    }
    else{
        bg_colour = 0;
    }
    bool obj_priority = 
        object_exists &&
        (LCDC(mem) & 0x02) &&
        (obj_colour_id != 0) &&
        ((bg_colour_id == 0) || !(obj_data&0x80));

    if (obj_priority){
        uint8_t palette = (obj_data&0x10) ? OBP1(mem) : OBP0(mem);
        obj_colour = (palette>>(2*obj_colour_id))&0x03;
        return dmg_colours[obj_colour];
    } else {
        return dmg_colours[bg_colour];
    }
}


void ppu(struct cartridge *cart ,cpu *CPU, memory *mem, ppu_data *data){
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
        OAM_DMA_Transfer(cart, mem);
        CPU->transfer = 1;
        CPU->transfer_pending = 0;
        data->transfer = 1;
        data->transfer_timer = 0;
    }

    //LY(mem) = CPU->frame_timer/456;
    //uint8_t scanline_dots = CPU->frame_timer % 456;
    //uint8_t mode;

    if((LCDC(mem)&0x80) == 0){
        //mode = 0;
        //LY(mem) = 0;
        CPU->OAM_access = 1;
        CPU->VRAM_access = 1;
    } else{
        LY(mem) = CPU->frame_timer/456;
        uint8_t scanline_dots = CPU->frame_timer % 456;
        uint8_t mode;
        if(LY(mem) >= 144){
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
                    uint8_t height = (LCDC(mem) & 0x04) ? 16 : 8;
                    if(data->nobjects<10){
                        if(((LY(mem)+16) >= mem->OAM[4*i]) &&  ((LY(mem)+16) < (mem->OAM[4*i] + height))){//The height of objects is 16 is bit 2 of LCDC is set and 8 is not
                            data->objects[data->nobjects] = 4*i;//the location of byte 0 of that object
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
                bool inside_window = (LCDC(mem) & 0x20) && ((scanx + 7) >= WX(mem)) && (LY(mem) >= WY(mem));
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
                uint16_t map = 0x1800;


                if(LCDC(mem)&0x02){
                    for(i = 0; i<data->nobjects;i++){
                        obj_x = mem->OAM[data->objects[i]+1];//this is the right edge of the object
                        int obj_left = obj_x -8;
                        obj_y = mem->OAM[data->objects[i]];
                        obj_data = mem->OAM[data->objects[i]+3];
                        if( (obj_left <= scanx) && (scanx < obj_x)){

                            bool is_low_tile = 
                                (LCDC(mem) & 0x04) && 
                                ((((LY(mem) + 8)>=obj_y) && ((obj_data&0x40)==0)) ||
                                (((LY(mem) + 8)<obj_y) && (obj_data&0x40)));

                            uint8_t top_tile = mem->OAM[data->objects[i]+2];

                            if(LCDC(mem)&0x04){
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
                        uint8_t obj_pixel_y =(LY(mem) + 16 - obj_y)%8;//the 16  is mathematically redundant but instructive for what the variables mean
                        if(obj_data&0x40){//yflip
                            obj_lo = mem->VRAM[16*obj_id + 2*(7-obj_pixel_y)];
                            obj_hi = mem->VRAM[16*obj_id + 2*(7-obj_pixel_y) +1];//start at 8000, tiles are blocks of 16 bytes, the id is which tile. scanline give the two bytes 
                        } else{
                            obj_lo = mem->VRAM[16*obj_id + 2*obj_pixel_y];
                            obj_hi = mem->VRAM[16*obj_id + 2*obj_pixel_y+1];
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

                if(LCDC(mem)&0x01){//BG/window is enabled
                    if(inside_window){
                        if(LCDC(mem) & 0x40){
                            map = 0x1C00;
                        }
                        pos_x = scanx - WX(mem) + 7;//no underflow because of the inside_window condition
                        pos_y = LY(mem) - WY(mem);
                    } else{
                        if (LCDC(mem) & 0x08){
                            map = 0x1C00;
                        }
                        pos_x = (SCX(mem) + scanx) % 256;
                        pos_y = (SCY(mem) + LY(mem)) % 256;
                    }
                    uint8_t tile_x = pos_x/8;
                    uint8_t pixel_x = pos_x % 8;
                    uint8_t tile_y = pos_y/8;
                    uint8_t pixel_y = pos_y % 8;
                    uint8_t tile_id = mem->VRAM[map + tile_y*32 + tile_x];
                    uint16_t addr;
                    if (LCDC(mem) & 0x10){
                        addr = 16*tile_id;
                    } else{
                        addr = 0x1000 + 16*((int8_t) tile_id);
                    }
                    uint16_t line = addr + 2*pixel_y;
                    uint8_t lo = mem->VRAM[line];
                    uint8_t hi = mem->VRAM[line + 1];
                    bg_colour_id = 2*(((1<<(7-pixel_x))&hi) != 0) + (((1<<(7-pixel_x))&lo) != 0);//x travels from left to right, from the "high bits" to the low
                } else{
                    bg_colour_id =0;
                }

                data->framebuffer[160*LY(mem) + scanx] = draw_pixel(object_exists, LCDC(mem)&0x01, obj_data, bg_colour_id, obj_colour_id, mem);
            } else{
                //Hblank
                mode = 0;
                CPU->OAM_access = 1;
                CPU->VRAM_access = 1;
                //configure memory access
            }
        }
        
        if(LY(mem) == LYC(mem)){
            STAT(mem) |= 0x04;//This is the LY == LYC condition
        } else{
            STAT(mem) &= ~0x04;//
        }
        if((LY(mem)==144) && (scanline_dots == 0)){
            IF(mem) |= 0x01;//requesting inteerrupt
            CPU->draw = 1;
            //CPU->vblank_rq = 0;//so I don't constatnly get vblank requests
        }

        STAT(mem) &= 0xFC;//clearing the last 2 bits
        STAT(mem) |= mode;

    
        if(CPU->frame_timer >= 70223){//one frame has passed
            CPU->frame_timer= 0;
            //SDL draw thing
        } else{
            CPU->frame_timer++;
        }
    
    }
}
