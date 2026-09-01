#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"

static const uint16_t DMGcolours[4] = {
    0xFFFF,
    ((170/8) << 11) | ((170/4) << 5) | (170/8),
    ((88/8) << 11) | ((88/4) << 5) | (88/8),
    0

};


void OAM_DMA_Transfer(struct cartridge *cart, memory *mem){//might also need the MBC to go in here too
    uint16_t addr = (uint16_t)DMA(mem) << 8;
    for(uint16_t i = 0; i<0xA0; i++){
        mem->OAM[i] = unrestricted_read(cart, mem, addr + i);
    }
}

/*

Mode 3 design

*5 dots to inerpret a block of BG tiles
**get all the BG data using get_tile and get_tile_data
**interpret the colour values 
**see if there were any objects overlapping this tile
**see which ones
**get the tile data for the objects
**mix 
**send

*/


//These are to be used in conjunction with an if/else to decide whether to fetch BG tiles at all
void get_tile (memory *mem, ppu_data *data){
    //Tilemaps are at 0x9800 or 0x9C00, here it's 0x1XXX because VRAM starts at 0x8000
    if(LCDC(mem) & 1){
    uint16_t map = 0x1800;
    int window_start = ((LCDC(mem) & 0x20) && (LY(mem) >= WY(mem))) ? WX(mem) - 7 : 160;
    uint8_t tile_x;
    uint8_t tile_y;
    uint8_t pixel_y;

    bool inside_window = (data->scanx >= window_start);
    if(inside_window){
        if(LCDC(mem) & 0x40){
            map = 0x1C00;
        }
        tile_x = (data->scanx - WX(mem) + 7) / 8;//no underflow because of the inside_window condition
        tile_y = (LY(mem) - WY(mem)) / 8;
        pixel_y = (LY(mem) - WY(mem)) & 7;
    } else{
        if (LCDC(mem) & 0x08){
            map = 0x1C00;
        }
        tile_x = ((SCX(mem) / 8) + data->fetch_x) & 0x1F;
        tile_y = (((SCY(mem) + LY(mem))) / 8) & 0x1F;
        pixel_y = ((SCY(mem) + LY(mem))) & 0x07;
    }
    uint8_t tile_id = mem->VRAM[map + tile_y*32 + tile_x];
    data->fetch_x++;
//}

//void get_tile_data(memory *mem, ppu_data *data){
    int start;
    int end;
    //check whether you're in the window already
    //if not check where the start is
    //if(data->tilex == 0){
    if(inside_window){
        //if we're inside the window we set things up so scanx is always at the start of the window tiles meaning we can output the full tile unless it's at the end of the screen
        start = 0;
        end   = data->scanx + 8 > 160 ? 160 - data->scanx : 8;
    } else{
        //If we're not in the window we output starting from the lower bits of scx + scanx until we hit the end of the screen or the window
        start =  (SCX(mem) + data->scanx) % 8;
        //we calculate when the background stops by checking if the boundary is within the row of pixels that we would otherwise output
        end   =  data->scanx + 8 - start > window_start ? window_start - data->scanx : 8;
    }

    uint16_t addr;
    if (LCDC(mem) & 0x10){// address mode select
        addr = 16*tile_id;
    } else{
        addr = 0x1000 + 16*((int8_t) tile_id);
    }
    uint16_t line = addr + 2*pixel_y;
    uint8_t lo = mem->VRAM[line];
    uint8_t hi = mem->VRAM[line + 1];
    for(int i = start; i<end; i++){
        int j = i - start;
        data->BGcolours[j] = ((hi >> (7-i) & 1 ) << 1) | 
                             (lo >> (7-i) & 1); 
    }

    //take note of how much we should advance scanx by
    data->increment = end - start;
    }
    else {
        memset(data->BGcolours, 0, 8);
        data->increment = data->scanx + 8 >160 ? 160 - data->scanx : 8;
        data->fetch_x++;
    }
}

//for when you already know the object is there
//calculate overlap from previous object
void get_object(memory *mem, ppu_data *data, int i/*other parameter to specify which object*/){

    //obj_y is the position of the object's top edge - 16
    uint8_t obj_y = mem->OAM[data->objects[i]];
    //obj_x is the position of the object's left edge - 8 so obj_x = 0 means left edge at -8, right edge at -1
    uint8_t obj_x = mem->OAM[data->objects[i]+1];
    uint8_t obj_id = mem->OAM[data->objects[i]+2];
    uint8_t obj_data = mem->OAM[data->objects[i]+3];

    bool is_low_tile = 
        (LCDC(mem) & 0x04) && 
        ((((LY(mem) + 8) >= obj_y) && ((obj_data & 0x40) == 0)) || //no y-flip and we're below halfway
        (((LY(mem) + 8) < obj_y) && (obj_data & 0x40)));

    //This is how the hardware deals with 2x1 objects
    if(LCDC(mem)&0x04)      obj_id &= 0xFE;
    obj_id += is_low_tile ? 1 : 0;

    uint8_t obj_lo;
    uint8_t obj_hi;

    uint8_t obj_pixel_y =(LY(mem) - (obj_y /* - 16*/)) & 0x07;//the 16 commented out as it is mathematically redundant but it's instructive for what the variables mean
    if(obj_data&0x40){//yflip
        obj_lo = mem->VRAM[16*obj_id + 2*(7-obj_pixel_y)];
        obj_hi = mem->VRAM[16*obj_id + 2*(7-obj_pixel_y) +1];//start at 8000, tiles are blocks of 16 bytes, the id is which tile. scanline give the two bytes 
    } else{
        obj_lo = mem->VRAM[16*obj_id + 2*obj_pixel_y];
        obj_hi = mem->VRAM[16*obj_id + 2*obj_pixel_y+1];
    }

    int start = obj_x - 8;
    int end = obj_x;
    if(start < 0)       start = 0;
    if(end > 160)       end = 160;

    if(obj_data&0x20){//xflip
        //Highest bit give the info for the leftmost pixel
        //2 to the colour if the high data bit is set and 1 if the low data bit is set
        //input colour indices
        int k = start - (obj_x- 8);
        for(int j = start; j<end; j++){
            if(data->obj_scanline[j] == 0){
                //we only set metadata flags if the colour is non-zero
                //use of = rather than == in the if condition is deliberate
                if(data->obj_scanline[j] = (((obj_hi >> k) & 1) << 1) | ((obj_lo >> k) & 1)){//right to left
                    if(obj_data & 0x80)     data->obj_scanline[j] |= (1<<7); //BG priority flag
                    if(obj_data & 0x10)     data->obj_scanline[j] |= (1<<6); //DMG pallette
                }
            }
            k++;
        }
    } else{
        int k = start - (obj_x - 8) + 7;
        for(int j = start; j<end; j++){
            if(data->obj_scanline[j] == 0){
                int8_t colour = data->obj_scanline[j] = (((obj_hi >> k) & 1) << 1) | ((obj_lo >> k) & 1);
                if(colour != 0){//left to right
                    if(obj_data & 0x80)     data->obj_scanline[j] |= (1<<7); //BG priority flag
                    if(obj_data & 0x10)     data->obj_scanline[j] |= (1<<6); //DMG pallette

                }
            }
            k--;
        }
    }
}

void populate_objects(memory *mem, ppu_data *data){
    for(int i = data->object_start; i<data->nobjects; i++){
        //printf("object is %d    test is %d\n", mem->OAM[data->objects[i]+1], data->scanx);
        if (mem->OAM[data->objects[i]+1]  <= (data->scanx+ data->increment)){//The right edge of the object
            //puts("test passed");
            if(LCDC(mem) & (1<<1))      get_object(mem, data, i);
            //we keep track of which objects we've already got
            data->object_start++;
        }
        else    break;
    }
}

void mix(memory *mem, ppu_data *data){

    int end = data->scanx + data->increment;

    for(int i = data->scanx; i<end; i++){
        int j = i - data->scanx;
        uint8_t colour;
        if(i == 17) printf("position 17 is currently %d\n", data->obj_scanline[17]);
        if((data->obj_scanline[i] > 0) || ((data->obj_scanline[i] < 0) && (data->BGcolours[j] == 0))){
            uint8_t palette = (data->obj_scanline[i] & (1<<6)) ? OBP1(mem) : OBP0(mem);
            uint8_t id = data->obj_scanline[i] & 0x03;
            colour = (palette >> (2*id)) & 0x03;
        } else{
            uint8_t id = data->BGcolours[j] & 0x03;
            colour = (BGP(mem) >> (2*id)) & 0x03;
        }
        data->framebuffer[160*LY(mem) + i] = DMGcolours[colour];
    }
    
    data->scanx += data->increment;
    //printf("scanx is %d \nincrement is %d \n", data->scanx, data->increment);
}

//the returned value is how long the performed actions takes
int ppu(struct cartridge *cart ,cpu *CPU, memory *mem, ppu_data *data){
  
    uint8_t mode;

    switch(data->mode){
        case MODE1:{
            //I only do this once, ppu never gets accessed if ly > 144.
            //nothing happens and all video memory is accessible
            //data->countdown = 4560; 
            IF(mem) |= 0x01;//requesting inteerrupt
            CPU->draw = 1;
            CPU->OAM_access = 1;
            CPU->VRAM_access = 1;
            mode = 1;
            data->mode = MODE2;
            return 4560; 
            //nothing happens and all video memory is accessible
        }

        case MODE2:{
            //this process takes 2 dots
            //mode 2 - OAM scan
            //VRAM and GBC palettes are accessible
            if(data->finish)    data->finish = 0;
            data->nobjects = 0;
            STAT(mem) &= 0xFC;//clearing the last 2 bits
            STAT(mem) |= mode;
            mode = 2;
    
            uint8_t height = (LCDC(mem) & 0x04) ? 16 : 8;
    
            for(int i = 0; i < 160; i += 4){
                if(data->nobjects<10){
                    //if objects glitch think about returning this to previous version
                    if(((LY(mem)+16) >= mem->OAM[i]) &&  ((LY(mem)+16) < (mem->OAM[i] + height))){//The height of objects is 16 is bit 2 of LCDC is set and 8 is not
                        data->objects[data->nobjects] = i;//the location of byte 0 of that object
                        data->nobjects++;//important: the objects array stores objects in the order in which they occur in OAM   
                    }
                } 
                else      break;
            }
            
            data->mode = MODE3;
            data->init_mode3 = true;

            return 80;
        } 

        case MODE3:{
            //each block in my mode 3 takes 8 dots
            //data->countdown = 8;
            if(data->init_mode3){
                data->init_mode3 = false;
                data->length = 0;
                // puts("mode 3");
                // fflush(stdout);
                memset(data->BGcolours,0,8);
                memset(data->obj_scanline,0,160);
                data->fetch_x = 0;
                data->scanx = 0;
                mode = 3;
                STAT(mem) &= 0xFC;//clearing the last 2 bits
                STAT(mem) |= mode;
                //CPU->OAM_access = 0;
                //CPU->VRAM_access = 0;
                data->object_start = 0;
                data->finish = 0;
            }

            get_tile(mem,data);
            populate_objects(mem,data);
            mix(mem,data);
            data->length += 8;
            if(data->scanx >= 160){
                data->finish = 1;
                data->mode = MODE0;
            }
            return 8;

        }

        case MODE0:{
            //Hblank
            // data->countdown = 456 - 80 - data->length;
            mode = 0;
            CPU->OAM_access = 1;
            CPU->VRAM_access = 1;
            STAT(mem) &= 0xFC;//clearing the last 2 bits
            STAT(mem) |= mode;

            if(LY(mem) == 143)      data->mode = MODE1;
            else                    data->mode = MODE2;
            //configure memory access

            return 456 - 80 - data->length;
        }
    }
}