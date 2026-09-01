#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "emulator.h"

void write_IO(apu_data *audio, cpu *CPU, memory *mem, uint16_t addr, uint8_t value){
    switch(addr){
        case(DIVaddr):
        DIV(mem)=0;
        break;
        case(DMAaddr):
        DMA(mem) = value;
        CPU->transfer_pending = 1;
        break;
        case(LCDCaddr):{
            if((LCDC(mem)&0x80)&&((value & 0x80) == 0)){//on to off 
                LY(mem) = 0;
                STAT(mem) &= 0xFC;//setting mode to 0
                CPU->VRAM_access = 1;
                CPU->OAM_access = 1;
            } else if(((LCDC(mem)&0x80)==0)&&(value&0x80)){//off to on
                CPU->frame_timer = 0;
                LY(mem) = 0;
            }
        LCDC(mem) = value;
        break;
        }
        case(STATaddr):
        STAT(mem) = (STAT(mem) & 0x07)  | (value & 0x78);
        break;
        case(JOYPaddr):{
            uint8_t state = JOYP(mem) & 0x0F;
            JOYP(mem) = (value & 0x30)|state;
            break;
        }
        case(LYaddr):
        break;
        case(BANKaddr):
        BANK(mem) = value;
        mem->boot_mapped = 0;
        break;
        case(NR12addr):{
            if(!(value & 0xF8)){
                audio->dacs[0] = 0;
                audio->channel_status[0] = 0;
                NR52(mem) &= ~(1);
            }
            IOREG(mem,addr) = value;
            break;
        }
        case(NR14addr):{
            //"Writing any value to NR14 with bit 7 set triggers the channel"
            //so it seems as though it's correct to check this before changing the value. 
            CPU->length_enable[0] = value & (1<<6);

            if((NR14(mem) & 0x80) || (value & (1<<7))){
                trigger_pulse(audio, mem, 0);
                // CPU->audio_triggers[0] = 1;
            } ;
            mem->IO[addr - 0xFF00] = value;
            // if(NR14(mem) & 0x80){
            //     CPU->audio_triggers[0] = 1;
            // }

            break;
        }
        case(NR22addr):{
            if(!(value & 0xF8)){
                audio->dacs[1] = 0;
                audio->channel_status[1] = 0;
                NR52(mem) &= ~(1<<1);
            }
            IOREG(mem,addr) = value;
            break;
        }
        case(NR24addr):{
            CPU->length_enable[1] = value & (1<<6);
            
            if((NR24(mem) & 0x80) || (value & (1<<7))){
                trigger_pulse(audio, mem, 1);
                // CPU->audio_triggers[1] = 1;
            }  
            mem->IO[addr - 0xFF00] = value;

            break;
        }
        case(NR30addr):{
            if(value & (1<<7)){
                audio->dacs[2] = 1;
            } else {
                audio->dacs[2] = 0;
                audio->channel_status[2] = 0;
                NR52(mem) &= ~(1<<2);
            }
            IOREG(mem, addr) = value;
        }
        case(NR34addr):{
            CPU->length_enable[2] = value & (1<<6);
            
            if((NR34(mem) & 0x80) || (value & (1<<7))){
                trigger_wave(audio, mem);
                // CPU->audio_triggers[2] = 1;
            }  
            mem->IO[addr - 0xFF00] = value;
            break;
        }
        case(NR42addr):{
            if(!(value & 0xF8)){
                audio->dacs[3] = 0;
                audio->channel_status[3] = 0;
                NR52(mem) &= ~(1<<3);
            }
            IOREG(mem,addr) = value;
            break;
        }

        case(NR43addr):{
            // audio->ch4_clock = (~value) & 0xE0;
            uint8_t shift = (value & 0xF0) >> 4;
            audio->ch4_clock = true;
            uint8_t divider = value & 7;
            //we use machine cycles here. We would have 16 and 8 if we were using dot cycles.
            audio->ch4_target = divider ?  (4 * divider * (1<<shift)) : (2 * (1<<shift)); 

            IOREG(mem, addr) = value;
        }

        case(NR44addr):{
            CPU->length_enable[3] = value & (1<<6);
            
            if((NR44(mem) & 0x80) || (value & (1<<7))){
                trigger_noise(audio, mem);
                // CPU->audio_triggers[3] = 1;
            }  
            mem->IO[addr - 0xFF00] = value;
            break;
        }
        default:
        mem->IO[addr - 0xFF00] = value;
    }
}

void write_exRAM_MBC1(struct cartridge *cart, uint16_t addr, uint8_t value){//need to make sure RAM number is at most #ram banks
    addr &= 0x1FFF;
    addr |= cart->Banking_mode_select ? (cart->RAM_bank_number % cart->RAM_banks) << 13 : 0;
    cart->RAM[addr] = value;
}

void write_exRAM_MBC5(struct cartridge *cart, uint16_t addr, uint8_t value){//need to make sure RAM number is at most #ram banks
    addr &= 0x1FFF;
    addr |= (cart->RAM_bank_number % cart->RAM_banks) << 13;
    cart->RAM[addr] = value;
}

void write_exRAM_none(struct cartridge *cart, uint16_t addr, uint8_t value){

}

void write_exRAM(struct cartridge *cart, uint16_t addr, uint8_t value){//need to make sure RAM number is at most #ram banks

    if(!cart->RAM_enable || cart->RAM == NULL){
        return;
    }

    addr &= 0x1FFF;

    switch(cart->type){
        case MBC5 :
            addr |= (cart->RAM_bank_number % cart->RAM_banks) << 13;
            break;
        case MBC1:
            addr |= cart->Banking_mode_select ? (cart->RAM_bank_number % cart->RAM_banks) << 13 : 0;
            break;
        default:
        //unsupported MBC type, should probably print an error message
    }
    cart->RAM[addr] = value;
}

void write_std(apu_data *audio, cpu *CPU, memory *mem, uint16_t addr, uint8_t value){

    if(CPU->transfer && (addr <= 0xFF80)){
        return;
    }
    if((addr >= 0xFF00) && (addr <= 0xFF7F)){
        write_IO(audio, CPU, mem, addr, value);
    }
    else if(((addr >= 0xC000)&&(addr <= 0xDFFF))||((addr >= 0xE000)&&(addr <= 0xFDFF))){
        if(addr >= 0xE000){
            addr -= 0x2000;
        }
        mem->WRAM[addr-0xC000] = value;
    } else if((addr>=0xFE00)&&(addr<=0xFE9F)&&CPU->OAM_access){
        mem->OAM[addr-0xFE00] = value;
    } else if((addr>=0x8000)&&(addr<=0x9FFF)&&CPU->VRAM_access){
        mem->VRAM[addr-0x8000] = value;
    } else if(addr >= 0xFEA0 && addr <= 0xFEFF){
        //unusable memory
    } else if((addr>=0xFF80)&&(addr<=0xFFFE)){
        mem->HRAM[addr-0xFF80] = value;
    } else if (addr == 0xFFFF){
        mem->Interrupt_Enable = value;
    }
}


void write_MBC1(struct cartridge *cart, uint16_t addr, uint8_t value){

    if (addr <= 0x1FFF){
        value &= 0x0F;
        cart->RAM_enable = (value == 0xA) ? 1 : 0;
    } else if (addr >= 0x2000 && addr <= 0x3FFF){
        value &= 0x1F;
        if(value == 0){
            value = 0x01;
        }
        cart->ROM_bank_number = value;
    } else if((addr >= 0x4000) && (addr <= 0x5FFF)){
        cart->RAM_bank_number = value & 0x03;
    } else if((addr >= 0x6000) && (addr <= 0x7FFF)){
        cart->Banking_mode_select = value&0x01;
    }
}

void write_none(struct cartridge *cart, uint16_t addr, uint8_t value){

}

uint8_t read_MBC1(struct cartridge *cart, uint16_t addr){//needs work for multi game compilation carts
    uint32_t address = addr&0x3FFF;
    if (addr<=0x3FFF){
        uint32_t offset = cart->Banking_mode_select ? addr | cart->RAM_bank_number << 5 : 0;
        offset %= cart->ROM_banks;
        address = cart->Banking_mode_select ? addr | cart->RAM_bank_number << 19 : addr;
        return cart->ROM[address];
    } else if (addr <= 0x7FFF){
        addr &= 0x3FFF;
        uint32_t offset = cart->RAM_bank_number << 5 | cart->ROM_bank_number;
        offset %= cart->ROM_banks;
        return cart->ROM[address | (offset << 14)];
    } else if (addr >= 0xA000 && addr <= 0xBFFF){
        addr &= 0x1FFF;
        if(cart->RAM_banks ==0){
            return 0xFF;
        } else{
            address = cart->Banking_mode_select ? addr | (cart->RAM_bank_number %cart->RAM_banks) << 13: addr;
            return cart->RAM[address];
        }
    }
}

uint8_t read_ROM(struct cartridge *cart, uint16_t addr){//needs work for multi game compilation carts
    return cart->ROM[addr];
}

void write_MBC5(struct cartridge *cart, uint16_t addr, uint8_t value){

    if (addr <= 0x1FFF){
        value &= 0x0F;
        cart->RAM_enable = (value == 0xA) ? 1 : 0;
    } else if (addr >= 0x2000 && addr <= 0x2FFF){
        cart->ROM_bank_number = value;
    } else if( addr <= 0x3FFF){
        value &= 1;
        cart->ROM_bank_number_9 = value;
    }
    else if((addr >= 0x4000) && (addr <= 0x5FFF)){
        cart->RAM_bank_number = value & 0x0F;
    }
}

uint8_t read_MBC5(struct cartridge *cart, uint16_t addr){//needs work for multi game compilation carts
    uint32_t address = addr;
    if (addr<=0x3FFF){
        return cart->ROM[addr];
    } else if (addr <= 0x7FFF){
        address &= 0x3FFF;
        uint32_t offset = ((cart->ROM_bank_number_9 <<8) | cart->ROM_bank_number) % cart->ROM_banks;
        address |= offset << 14;
        return cart->ROM[address];
    } else if (addr >= 0xA000 && addr <= 0xBFFF){
        address &= 0x1FFF;
        uint32_t offset = cart->RAM_bank_number % cart->RAM_banks;
        address |= offset << 13;
        return cart->RAM[address];
    }
}

void cart_init(struct cartridge *cart){
    switch(cart->type){
        case MBC1:{
            cart->MBC_control = write_MBC1;
            cart->cart_read = read_MBC1;
            cart->write_exRAM = write_exRAM_MBC1;
            break;
        }
        caseMBC5:{
            cart->MBC_control = write_MBC5;
            cart->cart_read = read_MBC5;
            cart->write_exRAM = write_exRAM_MBC5;
            break;
        }
        case ROM_ONLY:{
            cart->MBC_control = write_none;//should be something else
            cart->cart_read = read_ROM;
            cart->write_exRAM = write_exRAM_none;
            break;
        }
        default:{
            cart->MBC_control = write_none;
            cart->cart_read = read_ROM;
            cart->write_exRAM = write_exRAM_none;
        }
        //message for unsupported MBC type
    }

    if(!cart->RAM_enable || cart->RAM == NULL){
        cart->write_exRAM = write_exRAM_none;
    }    
}

void mem_write(apu_data *audio, struct cartridge *cart, cpu *CPU, memory *mem, uint16_t addr, uint8_t value){
    // inspect_state = MEM_WRITE;
    if (addr<=0x7FFF){
        cart->MBC_control(cart, addr, value);
    } else if(addr >= 0xA000 && addr <= 0xBFFF){
        cart->write_exRAM(cart, addr, value);
    } else {
        write_std(audio, CPU, mem, addr, value);
    }
}

uint8_t mem_read(struct cartridge *cart, cpu *CPU, memory *mem, uint16_t addr){
    // inspect_state = MEM_READ;
    if(CPU->transfer && (addr < 0xFF80 || addr == 0xFFFF)){
        return 0xFF;//some rubbish value but 0xFF is the "canonical" returned value
    }

    if(mem->boot_mapped && addr<0x100){
        return mem->boot_ROM[addr];
    }

    if(addr <= 0x7FFF || (addr >= 0xA000 && addr <= 0xBFFF)){//need to read cartridge data
        return cart->cart_read(cart, addr);
    } else if (addr >= 0x8000 && addr <= 0x9FFF){
        return CPU->VRAM_access ? mem->VRAM[addr - 0x8000] : 0xFF;
    } else if (addr >= 0xC000 && addr <= 0xDFFF){
        return mem->WRAM[addr - 0xC000];
    } else if (addr >= 0xE000 && addr <= 0xFDFF){
        return mem->WRAM[addr - 0xE000];
    } else if (addr >= 0xFE00 && addr <= 0xFE9F){
        return CPU->OAM_access ? mem->OAM[addr - 0xFE00] : 0xFF;
    } else if (addr >= 0xFF00 && addr <= 0xFF7F){
        return IOREG(mem, addr);
    } else if (addr >= 0xFF80 && addr <=0xFFFE){
        return mem->HRAM[addr - 0xFF80];
    } else if (addr == 0xFFFF){
        return mem->Interrupt_Enable;
    }
}

uint8_t unrestricted_read(struct cartridge *cart, memory *mem, uint16_t addr){
    if(mem->boot_mapped && addr<0x100){
        return mem->boot_ROM[addr];
    }

    if(addr <= 0x7FFF || (addr >= 0xA000 && addr <= 0xBFFF)){//need to read cartridge data
        cart->cart_read(cart,addr);
    } else if (addr >= 0x8000 && addr <= 0x9FFF){
        return mem->VRAM[addr - 0x8000];
    } else if (addr >= 0xC000 && addr <= 0xDFFF){
        return mem->WRAM[addr - 0xC000];
    } else if (addr >= 0xE000 && addr <= 0xFDFF){
        return mem->WRAM[addr - 0xE000];
    } else if (addr >= 0xFE00 && addr <= 0xFE9F){
        return mem->OAM[addr - 0xFE00];
    } else if (addr >= 0xFF00 && addr <= 0xFF7F){
        return IOREG(mem, addr);
    } else if (addr >= 0xFF80 && addr <=0xFFEF){
        return mem->HRAM[addr - 0xFF80];
    } else if (addr == 0xFFFF){
        return mem->Interrupt_Enable;
    }
}

void free_cart(struct cartridge *cart){
    free(cart->ROM);
    free(cart->RAM);
}