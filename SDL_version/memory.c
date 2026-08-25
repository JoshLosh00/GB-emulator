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
        case(NR14addr):{
            //"Writing any value to NR14 with bit 7 set triggers the channel"
            //so it seems as though it's correct to check this before changing the value. 
            if(NR14(mem) & 0x80){
                CPU->audio_triggers[0] = 1;
            }
            mem->IO[addr - 0xFF00] = value;
            break;
        }
        case(NR24addr):{
            if(NR14(mem) & 0x80){
                CPU->audio_triggers[1] = 1;
            }
            mem->IO[addr - 0xFF00] = value;
            break;
        }
        case(NR34addr):{
            if(NR34(mem) & 0x80){
                CPU->audio_triggers[2] = 1;
            }
            mem->IO[addr - 0xFF00] = value;
            break;
        }
        case(NR44addr):{
            if(NR44(mem) & 0x80){
                CPU->audio_triggers[3] = 1;
            }
            mem->IO[addr - 0xFF00] = value;
            break;
        }
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
        default:
        //unsupported MBC type, should probably print an error message
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

//commented out function graveyard
/*
uint8_t read_mem(cpu *CPU, uint16_t addr, uint8_t *mem){
//    bool ignore = //this may need to be filled out with more conditions
//        (CPU->transfer && (addr<0xFF80)) ||
//        (!CPU->VRAM_access && (addr>= 0x8000) && (addr<=0x9FFF)) || 
  //      (!CPU->OAM_access && (addr>= 0xFE00) && (addr<=0xFE9F));
    //if(ignore){
      //  return 0xFF;//rubbish value
    //} else{

            return mem[addr];
    //}

}

static uint16_t read_mem2(cpu *CPU, uint16_t addr, uint8_t *mem){//This instruction is slated for deletion.
//    bool ignore = //this may need to be filled out with more conditions
  //      (CPU->transfer && (addr<0xFF80)) ||
    //    (!CPU->VRAM_access && (addr>= 0x8000) && (addr<=0x9FFF)) || 
      //  (!CPU->OAM_access && (addr>= 0xFE00) && (addr<=0xFE9F));
//    if(ignore){
  //      return 0xFF;//rubbish value
    //} else{
        return addr;
    //}

}

static inline uint8_t readPC(struct cartridge *cart, cpu *CPU, memory *mem){//memory cannot be accessed at certain times such as during a DMA transfer
    uint8_t value = read(cart, CPU, mem, CPU->PC);
    (CPU->PC)++;
    return value;
}

void write8(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, uint16_t addr, uint8_t value){//memory cannot be accessed at certain times such as during a DMA transfer
    if(addr <= 0x1FFF){
        if((value&0x0F)==0x0A){
            MBC->RAM_enable = 1;
        } else{
            MBC->RAM_enable = 0;
        }
    } else if((addr >= 0x2000) && (addr <= 0x3FFF)){
        value &= 0x1F;
        if(value == 0){
            value = 0x01;
        }

        MBC->ROM_bank_number = value % (MBC->banks);

    } else if((addr >= 0x4000) && (addr <= 0x5FFF)){
        MBC->RAM_bank_number = value & 0x03;
    } else if((addr >= 0x6000) && (addr <= 0x7FFF)){
        MBC->Banking_mode_select = value&0x01;
    // if(addr < 0x8000){
        //nothing happens used for degugging tetris
    } else if(addr == DIV){
        mem[DIV]=0;//DIV reset
    } else if(addr == DMA){
        mem[DMA] = value;
        CPU->transfer_pending = 1;
    } else if(addr == LCDC){
        //printf("LCDC write old=%02X new=%02X PC=%04X\n", mem[LCDC], value, CPU->PC);
        if((mem[LCDC]&0x80)&&((value&0x80) == 0)){//on to off 
            mem[LY] = 0;
            mem[STAT] &= 0xFC;//setting mode to 0
            CPU->VRAM_access = 1;
            CPU->OAM_access = 1;
        } else if(((mem[LCDC]&0x80)==0)&&(value&0x80)){//off to on
            CPU->frame_timer = 0;
            mem[LY] = 0;
        }
        mem[LCDC] = value;
    } else if(addr == STAT){
        //printf("STAT write old=%02X new=%02X PC=%04X\n", mem[STAT], value, CPU->PC);
        mem[STAT] = (mem[STAT] & 0x07)  | (value & 0x78);
    } else if(addr == JOYP){
        //printf("JOYP write old=%02X new=%02X PC=%04X\n", mem[0xFF00], value, CPU->PC);
        uint8_t state = mem[JOYP] & 0x0F;
        mem[addr] = (value & 0x30)|state;
    }
    else if(addr == LY){
        //printf("Attempted write value %02X to LY at PC=%04X\n",value, CPU->PC);
        //CPU->instance++;
    } else if(addr == BANK){
        mem[addr] = value;
        CPU->boot_finished = 1;
    }
    else if((addr <= 0xFF7F)&&(addr >= 0xFF00)){
        //printf("Write to %04X old=%02X new=%02X PC=%04X\n", addr, mem[addr], value, CPU->PC);
        mem[addr] = value;
    }
    else{
//        if(CPU->transfer){
  //          if(addr>=0xFF80){
    //            mem[addr]=value;
      //      }
        //} else{
//            bool ignore = 
  //              (!CPU->VRAM_access && (addr>= 0x8000) && (addr<=0x9FFF)) || 
    //            (!CPU->OAM_access && (addr>= 0xFE00) && (addr<=0xFE9F));
      //      if(!ignore){
                mem[addr]=value;
        //    }
        //}
    }

    /*Echo RAM. can probably do this a better way but isn't a priority now
    else if (addr >= 0xE000 && addr <= 0xFDFF) {
        mem[addr] = value;
        mem[addr - 0x2000] = value;
    } else if (addr >= 0xC000 && addr <= 0xDDFF) {
        mem[addr] = value;
        mem[addr + 0x2000] = value;
    }*/
    //skips the boot sequence
    /*mem[P1] = 0xCF;
    mem[SB] = 0x00;
    mem[SC] = 0x7E;
    mem[DIV] = 0xAB;//different value on DMG0 
    mem[TIMA] = 0x00; 
    mem[TMA] = 0x00; 
    mem[TAC] = 0xF8;
    mem[IF] = 0xE1;
    mem[NR10] = 0x80;
    mem[NR11] = 0xBF;
    mem[NR12] = 0xF3;
    mem[NR13] = 0xFF;
    mem[NR14] = 0xBF;
    mem[NR21] = 0x3F;
    mem[NR22] = 0x00;
    mem[NR23] = 0xFF;
    mem[NR24] = 0xBF;
    mem[NR30] = 0x7F;
    mem[NR31] = 0xFF;
    mem[NR32] = 0x9F;
    mem[NR33] = 0xFF;
    mem[NR34] = 0xBF;
    mem[NR41] = 0xFF;
    mem[NR42] = 0x00;
    mem[NR43] = 0x00;
    mem[NR44] = 0xBF;
    mem[NR50] = 0x77;
    mem[NR51] = 0xF3;
    mem[NR52] = 0xF1;
    mem[LCDC] = 0x91;
    mem[STAT] = 0x81;
    mem[SCX] = 0x00;
    mem[SCY] = 0x00;
    mem[LY] = 0x00;
    mem[LYC] = 0x00;
    mem[DMA] = 0xFF;
    mem[BGP] = 0xFC;
    mem[IE] = 0x00;
    */
           /*if((MBC->current_bank_a != (MBC->RAM_bank_number<<5) ) && MBC->Banking_mode_select){
            memcpy(mem, bank[MBC->RAM_bank_number<<5], 0x4000);
            MBC->current_bank_a = MBC->RAM_bank_number<<5;
        } else if((MBC->current_bank_a != 0 ) && MBC->Banking_mode_select){
            memcpy(mem, bank[0], 0x4000);
            MBC->current_bank_a = 0;
        }
        if(MBC->current_bank_b != (MBC->ROM_bank_number | (MBC->RAM_bank_number<<5))){
            memcpy(mem + 0x4000, bank[MBC->ROM_bank_number | (MBC->RAM_bank_number<<5)], 0x4000);
            MBC->current_bank_b = MBC->ROM_bank_number | (MBC->RAM_bank_number<<5);
        }*/
        //RAM swap goes here
                /*log[log_counter].PC = CPU->PC;
        log[log_counter].A = CPU->A;
        log[log_counter].F = CPU->F;
        log[log_counter].B = CPU->B;
        log[log_counter].C = CPU->C;
        log[log_counter].D = CPU->D;
        log[log_counter].E = CPU->E;
        log[log_counter].H = CPU->H;
        log[log_counter].L = CPU->L;
        log[log_counter].opcode = mem[CPU->PC];
        log[log_counter].b1 = mem[CPU->PC +1];
        log[log_counter].b2 = mem[CPU->PC +2];
        log[log_counter].counter = counter;
        log[log_counter].ly = mem[LY];
        log[log_counter].stat = mem[STAT];
        log[log_counter].lcdc = mem[LCDC];
        log[log_counter].SP = CPU->SP;

        if(log_counter == LOG_SIZE -1){//log size
            log_counter = 0;
        } else{
            log_counter++;
        }*/

        /*if(CPU->LCD_off && CPU->LCD_on){
            printf("PPU switched off outside of Vblank");
            break;
        }*/

    

        /*if(counter == 2){
            for(int j = 0; j < LOG_SIZE; j++){
                int i = (log_counter +j)%LOG_SIZE; 
                printf(
                    "PC: %04X opcode: %02X next two bits: %02X %02X A: %02X F: %02X BC: %02X%02X DE: %02X%02X HL %02X%02X SP: %04X LY: %02X STAT: %02X LCDC: %02X\n", 
                    log[i].PC, 
                    log[i].opcode, 
                    log[i].b1, log[i].b2, 
                    log[i].A, 
                    log[i].F, 
                    log[i].B, 
                    log[i].C, 
                    log[i].D, 
                    log[i].E, 
                    log[i].H, 
                    log[i].L, 
                    log[i].SP, 
                    log[i].ly, 
                    log[i].stat, 
                    log[i].lcdc);
            }
            break;
        }*/