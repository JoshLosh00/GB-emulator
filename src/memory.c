#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "emulator.h"

void write_IO(cpu *CPU, memory *mem, uint16_t addr, uint8_t value){
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
        default:
        mem->IO[addr - 0xFF00] = value;
    }
}

void write_exRAM(struct cartridge *cart, uint16_t addr, uint8_t value){//need to make sure RAM number is at most #ram banks

    if(!cart->RAM_enable || cart->RAM == NULL){
        return;
    }

    addr &= 0x1FFF;

    switch(cart->type){
        case(MBC5):
            addr |= (cart->RAM_bank_number % cart->RAM_banks) << 13;
            break;
        case(MBC1):
            addr |= cart->Banking_mode_select ? (cart->RAM_bank_number % cart->RAM_banks) << 13 : 0;
            break;
    }
    cart->RAM[addr] = value;
}

void write_std(cpu *CPU, memory *mem, uint16_t addr, uint8_t value){

    if(CPU->transfer && (addr <= 0xFF80)){
        return;
    }
    if((addr >= 0xFF00) && (addr <= 0xFF7F)){
        write_IO(CPU, mem, addr, value);
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

void mem_write(struct cartridge *cart, cpu *CPU, memory *mem, uint16_t addr, uint8_t value){
    if (addr<=0x7FFF){
        switch(cart->type){
            case(MBC1):
            write_MBC1(cart, addr, value);
            break;
            case(MBC5):
            write_MBC5(cart, addr, value);
            break;
            default:
            //message for unsupported MBC type
        }
    } else if(addr >= 0xA000 && addr <= 0xBFFF){
        write_exRAM(cart, addr, value);
    } else {
        write_std(CPU, mem, addr, value);
    }
}

uint8_t mem_read(struct cartridge *cart, cpu *CPU, memory *mem, uint16_t addr){
    if(CPU->transfer && (addr < 0xFF80 || addr == 0xFFFF)){
        return 0xFF;//some rubbish value but 0xFF is the "canonical" returned value
    }

    if(mem->boot_mapped && addr<0x100){
        return mem->boot_ROM[addr];
    }

    if(addr <= 0x7FFF || (addr >= 0xA000 && addr <= 0xBFFF)){//need to read cartridge data
        switch(cart->type){
            case(MBC1):
            return read_MBC1(cart, addr);
            case(MBC5):
            return read_MBC5(cart, addr);
            case(ROM_ONLY):
            return cart->ROM[addr];
            break;
            default:
            //message for unsupported MBC type
        }
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


//Unrestricted read is used for the debugger
uint8_t unrestricted_read(struct cartridge *cart, memory *mem, uint16_t addr){
    if(mem->boot_mapped && addr<0x100){
        return mem->boot_ROM[addr];
    }

    if(addr <= 0x7FFF || (addr >= 0xA000 && addr <= 0xBFFF)){//need to read cartridge data
        switch(cart->type){
            case(MBC1):
            return read_MBC1(cart, addr);
            case(MBC5):
            return read_MBC5(cart, addr);
            case(ROM_ONLY):
            return cart->ROM[addr];
            default:
            //message for unsupported MBC type
        }
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

