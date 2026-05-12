#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "emulator.h"

static inline uint16_t getHL(cpu *CPU){
    return ((uint16_t) CPU->H << 8) |(uint16_t) CPU->L;
}
static inline uint16_t getBC(cpu *CPU){
    return ((uint16_t) CPU->B << 8) | (uint16_t) CPU->C;
}
static inline uint16_t getDE(cpu *CPU){
    return ((uint16_t) CPU->D << 8) | (uint16_t) CPU->E;
}
static inline uint8_t read8(cpu *CPU, uint8_t *mem){
    uint8_t value = mem[CPU->PC];
    (CPU->PC)++;
    return value;
}

void write8(struct MBC1 *MBC, uint8_t *mem, uint16_t addr, uint8_t value){
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
    } else if(addr == 0xFF04){
        mem[0xFF04]=0;//DIV reset
    }/*if (addr >= 0xE000 && addr <= 0xFDFF) {
        mem[addr] = value;
        mem[addr - 0x2000] = value;
    } else if (addr >= 0xC000 && addr <= 0xDDFF) {
        mem[addr] = value;
        mem[addr + 0x2000] = value;
    } */else{
        mem[addr] = value;
    }
}
static inline uint16_t read16(cpu *CPU, uint8_t *mem){
    uint8_t lo = read8(CPU,mem);
    uint8_t hi = read8(CPU,mem);
    uint16_t value = (((uint16_t) hi) << 8) | lo;
    return value;
}
static inline uint16_t pop16(cpu *CPU, uint8_t *mem){
    uint8_t lo = mem[CPU->SP];
    CPU->SP++;
    uint8_t hi = mem[CPU->SP];
    CPU->SP++;
    return (((uint8_t) hi) << 8) | lo;

    printf("POP  %04X from SP=%04X\n", (((uint8_t) hi) << 8) | lo, CPU->SP);
}
static inline void SetCAdd16(uint16_t old, uint16_t value, cpu *CPU){
    if (((uint32_t) old + (uint32_t) value) > 0xFFFF){
        CPU->F |= Cy;
    } else {
        CPU->F &= ~Cy;
    }
}
static inline void SetHAdd16(uint16_t old, uint16_t value, cpu *CPU){
    if (((old & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF){
        CPU->F |= Hf;
    } else {
        CPU->F &= ~Hf;
    }
}
static inline void SetZ8(uint8_t value, cpu *CPU){
     if(value == 0){
        CPU->F |= Z;
    } else {
        CPU->F &= ~Z;
    } 
}

static inline void SetHAdd8(uint8_t old, uint8_t value, cpu *CPU){
    if(((old & 0x0F) + (value & 0x0F))> 0x0F){
        CPU->F |= Hf;
    } else {
        CPU->F &= ~Hf;
    }
}

static inline void SetCAdd8(uint8_t old, uint8_t value, cpu *CPU){
    if(((uint16_t) old + (uint16_t) value) > 0xFF){
        CPU->F |= Cy;
    } else {
        CPU->F &= ~Cy;
    }
}

static inline void SetHSub8(uint8_t old, uint8_t value, cpu *CPU){
    if((old & 0x0F) < (value & 0x0F)){
        CPU->F |= Hf;
    } else {
        CPU->F &= ~Hf;
    }
}
static inline void SetCSub8(uint8_t old, uint8_t value, cpu *CPU){
    if(old < value){
        CPU->F |= Cy;
    } else {
        CPU->F &= ~Cy;
    }
}
static inline void push16(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, uint16_t value){
    uint8_t hi = value >> 8;
    uint8_t lo = value;
    CPU->SP--;
    write8(MBC, mem, CPU->SP, hi);
    CPU->SP--;
    write8(MBC, mem, CPU->SP, lo);
    //printf("PUSH %04X at SP=%04X\n", value, CPU->SP);
}
static inline void SetCShiftL(cpu *CPU, uint8_t value){
    if((value & 0x80) == 0x80){
        CPU->F |= Cy;
    } else {
        CPU->F &= ~Cy;
    }
}

static inline void SetCShiftR(cpu *CPU, uint8_t value){
    if((value & 0x01) == 0x01){
        CPU->F |= Cy;
    } else {
        CPU->F &= ~Cy;
    }
}

uint8_t* get345reg(cpu *CPU, uint8_t reg, uint8_t *mem){
        switch(reg){
        case(0x00):
            return &CPU->B;
        case(0x08):
            return &CPU->C;
        case(0x10):
            return &CPU->D;
        case(0x18):
            return &CPU->E;
        case(0x20):
            return &CPU->H;
        case(0x28):
            return &CPU->L;
        case(0x30):
            {uint16_t HL = getHL(CPU);
            return &(mem[HL]);}
        case(0x38):
            return &CPU->A;
        default:
            return NULL;
    }
}

uint8_t* get012reg(cpu *CPU, uint8_t reg, uint8_t *mem){
        switch(reg){
        case(0x00):
            return &CPU->B;
        case(0x01):
            return &CPU->C;
        case(0x02):
            return &CPU->D;
        case(0x03):
            return &CPU->E;
        case(0x04):
            return &CPU->H;
        case(0x05):
            return &CPU->L;
        case(0x06):
            {uint16_t HL = getHL(CPU);
            return &mem[HL];}
        case(0x07):
            return &CPU->A;
        default:
            return NULL;
    }
}

void ld_imm16(cpu *CPU, uint8_t *mem, uint8_t offset){
    uint8_t lo = read8(CPU, mem);
    uint8_t hi = read8(CPU, mem);

    switch(offset){
        case 0x00:
            CPU->B = hi;
            CPU->C = lo;
            break;
        case 0x10:
            CPU->D = hi;
            CPU->E = lo;
            break;
        case 0x20:
            CPU->H = hi;
            CPU->L = lo;
            break; 
        case 0x30:
            //printf("SP changed from %04x ", CPU->SP);
            CPU->SP = (((uint16_t) hi)<<8) | lo;
            //printf("to %04x via ld_imm16 with opcode at PC = %04x\n", CPU->SP, CPU->PC-3);
            break;
    }
}

void ld_imm16_sp(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    uint16_t addr = read16(CPU, mem);
    uint8_t lo = (uint8_t) CPU->SP;
    uint8_t hi = (uint8_t) (CPU->SP >> 8);
    write8(MBC, mem, addr, lo);
    write8(MBC, mem, addr + 1, hi);
}

void ld_a_r16mem(cpu *CPU, uint8_t *mem, uint8_t offset){
    uint16_t HL = getHL(CPU);
    switch(offset){
        case 0x00:
            CPU->A = mem[getBC(CPU)];
            break;
        case 0x10:
            CPU->A = mem[getDE(CPU)];
            break;
        case 0x20:
            CPU->A =  mem[HL];
            HL++;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break;
        case 0x30:
            CPU->A =  mem[HL];
            HL--;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break;
    }
}

void ld_r16mem_a(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, uint8_t offset){
    uint16_t HL = getHL(CPU);
    switch(offset){
        case 0x00:
            write8(MBC, mem, getBC(CPU), CPU->A);
            break;
        case 0x10:
            write8(MBC, mem, getDE(CPU), CPU->A);
            break;
        case 0x20:
            write8(MBC, mem, HL, CPU->A);
            HL++;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break; 
        case 0x30:
            write8(MBC, mem, HL, CPU->A);
            HL--;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break; 
    }
}

void inc_r16(cpu *CPU, uint8_t *mem, uint8_t offset){
    switch(offset){
        case 0x00:
            {uint16_t BC = getBC(CPU);
            BC++;
            CPU->C = (uint8_t) BC;
            CPU->B = (uint8_t) (BC >> 8);
            break;}
        case 0x10:
            {uint16_t DE = getDE(CPU);
            DE++;
            CPU->E = (uint8_t) DE;
            CPU->D = (uint8_t) (DE >> 8);
            break;}
        case 0x20:
            {uint16_t HL = getHL(CPU);
            HL++;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break;}
        case 0x30:
            (CPU->SP)++;
            break;
    }
}

void dec_r16(cpu *CPU, uint8_t *mem, uint8_t offset){
    switch(offset){
        case 0x00:
            {uint16_t BC = getBC(CPU);
            BC--;
            CPU->C = (uint8_t) BC;
            CPU->B = (uint8_t) (BC >> 8);
            break;}
        case 0x10:
            {uint16_t DE = getDE(CPU);
            DE--;
            CPU->E = (uint8_t) DE;
            CPU->D = (uint8_t) (DE >> 8);
            break;}
        case 0x20:
            {uint16_t HL = getHL(CPU);
            HL--;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break;}
        case 0x30:
            CPU->SP--;
            break;
    }
}

void add_HL_r16(cpu *CPU, uint8_t *mem, uint8_t offset){
    uint16_t HL = getHL(CPU);
    uint16_t value;
    uint16_t old = HL;
    switch(offset){
        case 0x00:
            value = getBC(CPU);
            break;
        case 0x10:
            value = getDE(CPU);
            break;
        case 0x20:
            value = HL;
            break;
        case 0x30:
            value = CPU->SP;
            break;
    }
    HL += value;
    CPU->L = (uint8_t) HL;
    CPU->H = (uint8_t) (HL >> 8);
    
    CPU->F &= (~N);
    SetCAdd16(old, value, CPU);
    SetHAdd16(old, value, CPU);
}

void inc_r8(cpu *CPU, uint8_t *mem, uint8_t offset){
    uint8_t *value = get345reg(CPU, offset, mem);
    SetHAdd8(*value, 1, CPU);
    (*value)++;

    //flags
    SetZ8(*value, CPU);
    CPU->F &= ~N;
}

void dec_r8(cpu *CPU, uint8_t *mem, uint8_t offset){
    uint8_t *value = get345reg(CPU, offset, mem);
    SetHSub8(*value, 1, CPU);
    (*value)--;

    //flags
    SetZ8(*value, CPU);

    CPU->F |= N;
}

void ld_rd_imm8(cpu *CPU, uint8_t *mem, uint8_t offset){
    uint8_t value = read8(CPU,mem);
    uint8_t *reg = get345reg(CPU, offset, mem);
    *reg = value;
}

void rlca(cpu *CPU, uint8_t *mem){
    uint8_t *Aval;
    Aval = &CPU->A;
    uint8_t bit7 = (*Aval & 0x80) >> 7;
    if((*Aval) & 0x80){
        CPU->F = Cy;
    } else {
        CPU->F = 0;//all flags are 0
    }
    (*Aval) <<= 1;
    *Aval |= bit7;
    
    
}

void rrca(cpu *CPU, uint8_t *mem){
    uint8_t *Aval;
    Aval = &CPU->A;
    uint8_t bit1 = (*Aval & 0x01) << 7;
    if((*Aval) & 0x01){
        CPU->F = Cy;
    } else {
        CPU->F = 0;//all flags are 0
    }
    (*Aval) >>= 1;
    *Aval |= bit1;
}

void rla(cpu *CPU, uint8_t *mem){
    uint8_t *Aval;
    uint8_t tempCy;
    Aval = &CPU->A;
    if((*Aval) & 0x80){
        tempCy = 1;
    } else {
        tempCy = 0;
    }
    (*Aval) <<= 1;
    if(CPU->F & Cy){
        (*Aval) |= 0x01;
    }
    if(tempCy){
        CPU->F = Cy;
    } else{
        CPU->F = 0;
    }
    ;
    
}

void rra(cpu *CPU, uint8_t *mem){
    uint8_t *Aval;
    uint8_t tempCy;
    Aval = &CPU->A;
    if((*Aval) & 0x01){
        tempCy = 1;
    } else {
        tempCy = 0;
    }
    (*Aval) >>= 1;
    if(CPU->F & Cy){
        (*Aval) |= 0x80;
    }
    if(tempCy){
        CPU->F = Cy;
    } else{
        CPU->F = 0;
    }
    ;
    
}

void daa(cpu *CPU, uint8_t *mem){
    uint8_t adj = 0;
    if (CPU->F & N){
        if(CPU->F & Hf){
            adj += 0x06;
        }
        if (CPU->F & Cy){
            adj += 0x60;
        }
        CPU->A -= adj;
    } else{
        if((CPU->F & Hf) || ((CPU->A & 0x0F )>0x09)){
            adj += 0x06;
        }
        if((CPU->F & Cy) || (CPU->A > 0x99)){
            adj += 0x60;
            CPU->F |= Cy;
        }
        CPU->A += adj;
    }
    SetZ8(CPU->A, CPU);
    CPU->F &= ~Hf;
}

void cpl(cpu *CPU, uint8_t *mem){
    CPU->A = ~CPU->A;
    CPU->F |= N;
    CPU->F |= Hf;
}

void scf(cpu *CPU, uint8_t *mem){
    CPU->F |= Cy;
    CPU->F &= ~(N | Hf);
}

void ccf(cpu *CPU, uint8_t *mem){
    CPU->F = (CPU->F & Cy) ? CPU->F & ~Cy : CPU->F | Cy;
    CPU->F &= ~(N | Hf);
}

void jr_imm8(cpu *CPU, uint8_t *mem, int counter){
    int8_t value = read8(CPU,mem);
    CPU->PC += value;
    //printf("tempPC %04d \tPC %04d\tvalue %02d\n",tempPC, CPU->PC, value);
    //printf("tempPC %04x\tPC %04x\n",tempPC,CPU->PC);
}

void jr_nz_imm8(cpu *CPU, uint8_t *mem){
    int8_t value = read8(CPU,mem);
    if((CPU->F & Z) == 0){
    //    printf("PC before: %04x ",CPU->PC);
    CPU->PC += value;
    //printf("PC after: %04x, value: %02d, %02x\n",CPU->PC, value, value);
    //printf("F: %02x jump taken\n", CPU->F);
    } else{
    //    printf("F: %02x jump not taken\n", CPU->F);
    }
}

void jr_nc_imm8(cpu *CPU, uint8_t *mem){
    int8_t value = read8(CPU,mem);
    if((CPU->F & Cy) == 0){
        CPU->PC += value;
        //printf("F: %02x jump taken\n", CPU->F);
    } else{
    //    printf("F: %02x jump not taken\n", CPU->F);
    }
}

void jr_z_imm8(cpu *CPU, uint8_t *mem){
    int8_t value = read8(CPU,mem);
    if(CPU->F & Z){
        CPU->PC += value;
    } else{
    //    printf("F: %02x jump not taken\n", CPU->F);
    }
}

void jr_c_imm8(cpu *CPU, uint8_t *mem){
    int8_t value = read8(CPU,mem);
    if(CPU->F & Cy){
        CPU->PC += value;
    //printf("F: %02x jump taken\n", CPU->F);
    } else{
    //    printf("F: %02x jump not taken\n", CPU->F);
    }
}

void stop(cpu *CPU, uint8_t *mem){
    //Do later
}

void nop(cpu *CPU){
}

void halt(cpu *CPU, uint8_t *mem){
    CPU->halted = 1;
}

void ld_r8_r8(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, uint8_t source, uint8_t dest){
    uint8_t *d = get345reg(CPU, dest, mem);
    uint8_t *s = get012reg(CPU, source, mem);
    if (dest == 0x30){
        write8(MBC, mem, getHL(CPU), *s);
        uint8_t HL = getHL(CPU);
    }else {
        *d = *s;//destination = source.
    }
}

void add_a_r8(cpu *CPU, uint8_t *mem, uint8_t *s){
    uint8_t value = *s;//needed to avoid corruption when *s =A
    uint8_t old = CPU->A;
    CPU->A += value;
    SetZ8(CPU->A, CPU);
    SetHAdd8(old, value, CPU);
    SetCAdd8(old, value, CPU);
    CPU->F &= ~N;
}

void add_a_imm8(cpu *CPU, uint8_t *mem){
    uint8_t old = CPU->A;
    uint8_t s = read8(CPU, mem);
    CPU->A += s;
    SetZ8(CPU->A, CPU);
    SetHAdd8(old, s, CPU);
    SetCAdd8(old, s, CPU);

    CPU->F &= ~N;
}

void adc_a_r8(cpu *CPU, uint8_t *mem, uint8_t *s){
    uint8_t value = *s;
    uint8_t old = CPU->A;
    uint8_t c = (CPU->F & Cy) ? 0x01 : 0x00;
    uint16_t result = old + value + c;
    CPU->A = (uint8_t) result;
    SetZ8(CPU->A,CPU);
    if(((old&0xF) + (value&0xF) + c)>0xF){
        CPU->F |= Hf;
    } else{
        CPU->F &= ~Hf;    
    }
    if(result > 0x00FF){
        CPU->F |= Cy;
    } else{
        CPU->F &= ~Cy;    
    }
    CPU->F &= ~N;
}

void adc_a_imm8(cpu *CPU, uint8_t *mem){
    uint8_t old = CPU->A;
    uint8_t s = read8(CPU, mem);
    uint8_t c = (CPU->F & Cy) ? 0x01 : 0x00;
    uint16_t result = old + s +c;
    CPU->A = (uint8_t) result;
    SetZ8(CPU->A,CPU);
    if(((old&0xF) + (s&0xF) + c)>0xF){
        CPU->F |= Hf;
    } else{
        CPU->F &= ~Hf;    
    }
    if(result > 0x00FF){
        CPU->F |= Cy;
    } else{
        CPU->F &= ~Cy;    
    }
    CPU->F &= ~N;
}

void sub_a_r8(cpu *CPU, uint8_t *mem, uint8_t *s){
    uint8_t old = CPU->A;
    CPU->A -= *s;
    SetZ8(CPU->A,CPU);
    SetHSub8(old, *s, CPU);
    SetCSub8(old, *s, CPU);
    CPU->F |= N;
}

void sub_a_imm8(cpu *CPU, uint8_t *mem){
    uint8_t old = CPU->A;
    uint8_t s = read8(CPU, mem);
    CPU->A -= s;
    SetZ8(CPU->A,CPU);
    SetHSub8(old, s, CPU);
    SetCSub8(old, s, CPU);
    CPU->F |= N;
}

void sbc_a_r8(cpu *CPU, uint8_t *mem, uint8_t *s){
    uint8_t old = CPU->A;
    uint8_t value = *s;
    uint8_t c = (CPU->F & Cy) ? 0x01 : 0x00;
    CPU->A -= (value + c);
    SetZ8(CPU->A,CPU);
    if((old&0xF)<((value&0xF)+c)){
        CPU->F |= Hf;
    } else{
        CPU->F &= ~Hf;
    }
    if((value==0xFF)&& c){
        CPU->F |= Cy;
    } else{
        SetCSub8(old, value + c, CPU);    
    }
    CPU->F |= N;
}

void sbc_a_imm8(cpu *CPU, uint8_t *mem){
    uint8_t old = CPU->A;
    uint8_t s = read8(CPU, mem);
    uint8_t c = (CPU->F & Cy) ? 0x01 : 0x00;
    CPU->A -= (s + c);
    SetZ8(CPU->A,CPU);
    if((old&0xF)<((s&0xF)+c)){
        CPU->F |= Hf;
    } else{
        CPU->F &= ~Hf;
    }
    if((s==0xFF)&& c){
        CPU->F |= Cy;
    } else{
        SetCSub8(old, s + c, CPU);    
    }
    CPU->F |= N;
}

void and_a_r8(cpu *CPU, uint8_t *mem, uint8_t *s){
    CPU->A &= *s;
    SetZ8(CPU->A,CPU);
    CPU->F &= ~N;
    CPU->F |= Hf;
    CPU->F &= ~Cy;

}

void and_a_imm8(cpu *CPU, uint8_t *mem){
    uint8_t s = read8(CPU, mem);
    CPU->A &= s;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F |= Hf;
    CPU->F &= ~Cy;
}

void xor_a_r8(cpu *CPU, uint8_t *mem, uint8_t *s){
    CPU->A ^= *s;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    CPU->F &= ~Cy;

}

void xor_a_imm8(cpu *CPU, uint8_t *mem){
    uint8_t s = read8(CPU, mem);
    CPU->A ^= s;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    CPU->F &= ~Cy;
}

void or_a_r8(cpu *CPU, uint8_t *mem, uint8_t *s){
    CPU->A |= *s;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    CPU->F &= ~Cy;

}

void or_a_imm8(cpu *CPU, uint8_t *mem){
    uint8_t s = read8(CPU, mem);
    CPU->A |= s;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    CPU->F &= ~Cy;
}

void cp_a_r8(cpu *CPU, uint8_t *mem, uint8_t *s){
    SetZ8(CPU->A - *s,CPU);
    SetHSub8(CPU->A, *s, CPU);
    SetCSub8(CPU->A, *s, CPU);
    CPU->F |= N;
}

void cp_a_imm8(cpu *CPU, uint8_t *mem){
    uint8_t s = read8(CPU, mem);
    SetZ8(CPU->A - s,CPU);
    SetHSub8(CPU->A, s, CPU);
    SetCSub8(CPU->A, s, CPU);

    CPU->F |= N;
}

void ret(cpu *CPU, uint8_t *mem){
    CPU->PC = pop16(CPU, mem);
}

void ret_i(cpu *CPU, uint8_t *mem){
    CPU->PC = pop16(CPU, mem);
    CPU->IME = 1;
}

void ret_z(cpu *CPU, uint8_t *mem){
    if(CPU->F & Z){
        CPU->PC =  pop16(CPU, mem);
    }
}

void ret_c(cpu *CPU, uint8_t *mem){
    if(CPU->F & Cy){
        CPU->PC =  pop16(CPU, mem);
    }
}

void ret_nz(cpu *CPU, uint8_t *mem){
    if((CPU->F & Z) == 0){
        CPU->PC =  pop16(CPU, mem);
    }
}

void ret_nc(cpu *CPU, uint8_t *mem){
    if((CPU->F & Cy) == 0){
        CPU->PC =  pop16(CPU, mem);
    }
}

void jp_imm16(cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU, mem); 
    CPU->PC = value; 
}

void jp_nz_imm16(cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU, mem);
    if((CPU->F & Z) == 0){
        CPU->PC = value;
    }
}

void jp_nc_imm16(cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU, mem);
    if((CPU->F & Cy) == 0){
        CPU->PC = value;
    }
}

void jp_z_imm16(cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU, mem);
    if(CPU->F & Z){
        CPU->PC = value;
    }
}

void jp_c_imm16(cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU, mem);
    if(CPU->F & Cy){
        CPU->PC = value;
    }
}

void jp_hl(cpu *CPU, uint8_t *mem){
    CPU->PC = getHL(CPU);
}

void call(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    uint16_t oldpc = CPU->PC;
    uint16_t value = read16(CPU,mem);
    push16(MBC, CPU, mem, CPU->PC);
    CPU->PC = value;
}

void call_nz(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU,mem);
    if((CPU->F & Z) == 0){
        push16(MBC, CPU, mem, CPU->PC);
        CPU->PC = value;
    }
}

void call_nc(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU,mem);
    if((CPU->F & Cy) == 0){
        push16(MBC, CPU, mem, CPU->PC);
        CPU->PC = value;
    }
}

void call_z(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU,mem);
    if(CPU->F & Z){
        push16(MBC, CPU, mem, CPU->PC);
        CPU->PC = value; 
    }
}

void call_c(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    uint16_t value = read16(CPU,mem);
    if(CPU->F & Cy){
        push16(MBC, CPU, mem, CPU->PC);
        CPU->PC = value;
    }
}
void rst(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, uint8_t vec){
    push16(MBC, CPU, mem, CPU->PC);
    CPU->PC = vec;
}

void pop_r16stk(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t hi = mem[CPU->SP + 1];
    uint8_t lo = mem[CPU->SP];
    switch(reg){
        case(0x00)://BC
            CPU->B = hi;
            CPU->C = lo;
            break;
        case(0x10)://DE
            CPU->D = hi;
            CPU->E = lo;
            break;
        case(0x20)://HL
            CPU->H = hi;
            CPU->L = lo;
            break;
        case(0x30)://AF
            CPU->A = hi;
            CPU->F = lo & 0xF0;
            break;
    }
    CPU->SP += 2;
}

void push_r16stk(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, uint8_t reg){
    switch(reg){
        case(0x00)://BC
            push16(MBC, CPU, mem, getBC(CPU));
            break;
        case(0x10)://DE
            push16(MBC, CPU, mem, getDE(CPU));
            break;
        case(0x20)://HL
            push16(MBC, CPU, mem, getHL(CPU));
            break;
        case(0x30)://AF
            push16(MBC, CPU, mem, ((CPU->A<<8)|CPU->F));
            break;
    }
        
}

void ldh_c_a(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    write8(MBC, mem, 0xFF00 + CPU->C, CPU->A);
}
void ldh_imm8_a(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    write8(MBC, mem, 0xFF00 + read8(CPU,mem), CPU->A);
}
void ld_imm16_a(struct MBC1 *MBC, cpu *CPU, uint8_t *mem){
    write8(MBC, mem, read16(CPU,mem), CPU->A);
}
void ldh_a_c(cpu *CPU, uint8_t *mem){
    CPU->A = mem[0xFF00 + CPU->C];
}
void ldh_a_imm8(cpu *CPU, uint8_t *mem){
    CPU->A = mem[0xFF00 + read8(CPU,mem)];
}
void ld_a_imm16(cpu *CPU, uint8_t *mem){
    CPU->A = mem[read16(CPU, mem)];
}

void add_sp_imm8(cpu *CPU, uint8_t *mem){
    int16_t old = CPU->SP;
    int8_t s = read8(CPU,mem);
    CPU->SP += s;
    CPU->F &= ~Z;
    CPU->F &= ~N;
    SetHAdd8((uint8_t) old, (uint8_t)s, CPU);//The way these flags are set is not intuitive
    SetCAdd8((uint8_t) old, (uint8_t)s, CPU);
}

void ld_hl_sp_imm8(cpu *CPU, uint8_t *mem){//flags
    int8_t s = read8(CPU,mem); 
    uint16_t HL = CPU->SP + s;
    CPU->L = (uint8_t) HL;
    CPU->H = (uint8_t) (HL >> 8); 
    CPU->F &= ~Z;
    CPU->F &= ~N;
    SetHAdd8((uint8_t) CPU->SP, (uint8_t) s, CPU);
    SetCAdd8((uint8_t) CPU->SP, (uint8_t) s, CPU);
}

void ld_sp_hl(cpu *CPU, uint8_t *mem){
    CPU->SP = getHL(CPU);
}

void ei(cpu *CPU, uint8_t *mem){
    CPU->interrupt_pending = 1;
    
}
void di(cpu *CPU, uint8_t *mem){
    CPU->IME = 0;
}

void rlc_r8(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t bit7 = 0x01 & (((*value) & 0x80) >> 7);//the 0x01& is for arithmetic vs logical shifts. Prob not necessary.
    SetCShiftL(CPU, *value);
    *value <<= 1;
    *value |= bit7;
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void rrc_r8(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t bit1 = 0x80 & (((*value) & 0x01) << 7);
    SetCShiftR(CPU, *value);
    *value >>= 1;
    *value |= bit1;
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}
void rl_r8(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t tempC = CPU->F & Cy;
    SetCShiftL(CPU, *value);
    *value <<= 1;
    *value |= (tempC ? 0x01 : 0x00);
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void rr_r8(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t tempC = CPU->F & Cy;
    SetCShiftR(CPU, *value);
    *value >>= 1;
    *value |= (tempC ? 0x80 : 0x00);;
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void sla_r8(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    SetCShiftL(CPU, *value);
    *value <<= 1;
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}
void sra_r8(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t bit7 = (*value) & 0x80;
    SetCShiftR(CPU, *value);
    *value >>= 1;
    *value |= bit7;
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void srl_r8(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    SetCShiftR(CPU, *value);
    *value >>= 1;
    *value &= 0x7F;//probably don't need this
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void swap_r8(cpu *CPU, uint8_t *mem, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t hi = (*value) & 0xF0;
    uint8_t lo = (*value) & 0x0F;
    *value = (lo<<4) | (hi>>4);
    if(*value){//setting all flags at once
        CPU->F = 0;
    } else{
        CPU->F = Z;
    }
}

uint8_t bitswitch(uint8_t test){
    switch(test){
        case(0x00):
            return 0x01;
        case(0x08):
            return 0x02;
        case(0x10):
            return 0x04;
        case(0x18):
            return 0x08;
        case(0x20):
            return 0x10;
        case(0x28):
            return 0x20;
        case(0x30):
            return 0x40;
        case(0x38):
            return 0x80;
    }   
    return 0;
}

void bit_b3_r8(cpu *CPU, uint8_t *mem, uint8_t test, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t bit = bitswitch(test);
    SetZ8(bit & *value, CPU);
    CPU->F &= ~N;
    CPU->F |= Hf;
}

void res_b3_r8(cpu *CPU, uint8_t *mem, uint8_t bit, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t test = bitswitch(bit);
    *value &= ~test;
}
void set_b3_r8(cpu *CPU, uint8_t *mem, uint8_t bit, uint8_t reg){
    uint8_t *value = get012reg(CPU, reg, mem);
    uint8_t test = bitswitch(bit);
    *value |= test;
}

int CBprefix(cpu *CPU, uint8_t *mem){
    uint8_t opcode = read8(CPU, mem);
    uint8_t reg = opcode & 0x07;
    uint8_t bit = opcode & 0x38;
    switch(opcode & 0xC0){
        case(0x00):
            switch(bit){
                case(0x00):
                    rlc_r8(CPU, mem, reg);
                    break;
                case(0x08):
                    rrc_r8(CPU, mem, reg);
                    break;
                case(0x10):
                    rl_r8(CPU, mem, reg);
                    break;
                case(0x18):
                    rr_r8(CPU, mem, reg);
                    break;
                case(0x20):
                    sla_r8(CPU, mem, reg);
                    break;
                case(0x28):
                    sra_r8(CPU, mem, reg);
                    break;
                case(0x30):
                    swap_r8(CPU, mem, reg);
                    break;
                case(0x38):    
                    srl_r8(CPU, mem, reg);
                    break;
            }
            break;
        case(0x40):
            bit_b3_r8(CPU, mem, bit, reg);
            break;
        case(0x80):
            res_b3_r8(CPU, mem, bit, reg);
            break;
        case(0xC0):
            set_b3_r8(CPU, mem, bit, reg);
            break;
    }
    if(reg == 0x06){
        return 4;
    } else {
        return 2;
    }
}

void arith_8bit(cpu *CPU, uint8_t *mem, uint8_t operation, uint8_t operand){
    uint8_t *s = get012reg(CPU, operand, mem);
    switch(operation){
        case(0x00):
            add_a_r8(CPU, mem, s);
            break;
        case(0x08):
            adc_a_r8(CPU, mem, s);
            break;
        case(0x10):
            sub_a_r8(CPU, mem, s);
            break;
        case(0x18):
            sbc_a_r8(CPU, mem, s);
            break;
        case(0x20):
            and_a_r8(CPU, mem, s);
            break;
        case(0x28):
            xor_a_r8(CPU, mem, s);
            break;
        case(0x30):
            or_a_r8(CPU, mem, s);
            break;
        case(0x38):
            cp_a_r8(CPU, mem, s);
            break;
    }
}

uint32_t execute(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, int counter){

    if(CPU->halted){
        return 1;//This means the CPU can only start again on t-cycles that are multiples of 4
    } else{
        uint8_t opcode = read8(CPU, mem);
        uint8_t u2block_mask = 0xC0;
        uint8_t l4block_mask = 0x0F;
        uint8_t m45block_mask = 0x30;
        uint8_t m543mask = 0x38;

        uint8_t bits345 = opcode & m543mask;
        uint8_t offset = opcode & 0x30;

        switch(opcode & u2block_mask){
            case 0x00://block 0
                switch(opcode & l4block_mask){
                    case 0x00:
                        switch(opcode & m45block_mask){
                            case 0x00://nop
                                nop(CPU);
                                return 1;
                            case 0x10:
                                stop(CPU, mem);
                                return 1;//STOP instruction. Don't know what to do here. COME BACK
                            case 0x20:
                                jr_nz_imm8(CPU, mem);
                                if(((CPU->F) & Z )== 0){
                                    return 3;
                                } else {
                                    return 2;
                                }
                            case 0x30:
                                jr_nc_imm8(CPU, mem);
                                if(((CPU->F) & Cy )== 0){
                                    return 3;
                                } else {
                                    return 2;
                                }
                        }
                        break;
                    case 0x01:
                        ld_imm16(CPU, mem, offset);
                        return 3;
                    case 0x02:
                        ld_r16mem_a(MBC, CPU, mem, offset);
                        return 2;
                    case 0x0A:
                        ld_a_r16mem(CPU, mem, offset);
                        return 2;
                    case 0x08:
                        switch(opcode & m45block_mask){
                            case 0x00:
                                ld_imm16_sp(MBC, CPU, mem);
                                return 5;
                            case 0x10:
                                jr_imm8(CPU, mem, counter);
                                return 3;
                            case 0x20:
                                jr_z_imm8(CPU, mem);
                                if((CPU->F) & Z){
                                    return 3;
                                } else {
                                    return 2;
                                }
                            case 0x30:
                                jr_c_imm8(CPU, mem);
                                if((CPU->F) & Cy ){
                                    return 3;
                                } else {
                                    return 2;
                                }
                        }
                        break;
                    case 0x03:
                        inc_r16(CPU, mem, offset);
                        return 2;
                    case 0x0B:
                        dec_r16(CPU, mem, offset);
                        return 2;
                    case 0x09:
                        add_HL_r16(CPU, mem, offset);
                        return 2;
                    case 0x04:
                    case 0x0C:
                        inc_r8(CPU, mem, bits345);
                        if(opcode == 0x34){
                            return 3;
                        } else{
                            return 1;
                        }
                    case 0x05:
                    case 0x0D:
                        dec_r8(CPU, mem, bits345);
                        if(opcode == 0x35){
                            return 3;
                        } else{
                            return 1;
                        }
                    case 0x06:
                    case 0x0E:
                        ld_rd_imm8(CPU, mem, bits345);
                        if(opcode == 0x36){
                            return 3;
                        } else{
                            return 2;
                        }
                    case 0x07:
                        switch(opcode & m45block_mask){
                            case(0x00): 
                                rlca(CPU, mem);
                                return 1;
                            case(0x10):
                                rla(CPU, mem);
                                return 1;
                            case(0x20):
                                daa(CPU, mem);
                                return 1;
                            case(0x30):
                                scf(CPU, mem);
                                return 1;
                        }
                        break;
                    case 0x0F:
                        switch(opcode & m45block_mask){
                            case(0x00): 
                                rrca(CPU, mem);
                                return 1;
                            case(0x10):
                                rra(CPU, mem);
                                return 1;
                            case(0x20):
                                cpl(CPU, mem);
                                return 1;
                            case(0x30):
                                ccf(CPU, mem);
                                return 1;
                        }
                        break;
                }
                break;
            case 0x40://block 1
                uint8_t dest = opcode & m543mask;
                uint8_t source = opcode & 0x07;
                if(dest == 0x30 && source == 0x06){
                    halt(CPU, mem);
                    return 1;
                } else {
                    ld_r8_r8(MBC, CPU, mem, source, dest);
                    if((opcode & 0x07) == 0x06){
                        return 2;
                    } else if(bits345 == 0x30){
                        return 2;
                    } else {
                        return 1;
                    }
                }
                break;
            case 0x80://block 2
                {uint8_t operand  = opcode & 0x07;
                uint8_t operation = opcode & m543mask;
                arith_8bit(CPU, mem, operation, operand);
                if(operand == 0x06){
                    return 2;
                } else {
                    return 1;
                }
                break;
            }
            case 0xC0://block 3
                uint8_t reg = opcode & 0x30;
                switch(opcode & 0x0F){
                    case(0x01):
                        pop_r16stk(CPU, mem, reg);
                        return 3;
                    case(0x05):
                        push_r16stk(MBC, CPU, mem, reg);
                        return 4;
                    default:
                        if((opcode & 0x07) == 0x07){
                            uint8_t tgt = opcode & m543mask;
                            rst(MBC, CPU, mem, tgt);
                            return 4;
                        } else{
                            switch(opcode & 0x3F){
                                case(0x06):
                                    add_a_imm8(CPU, mem);
                                    return 2;
                                case(0x0E):
                                    adc_a_imm8(CPU, mem);
                                    return 2;
                                case(0x16):
                                    sub_a_imm8(CPU, mem);
                                    return 2;
                                case(0x1E):
                                    sbc_a_imm8(CPU, mem);
                                    return 2;
                                case(0x26):
                                    and_a_imm8(CPU, mem);
                                    return 2;
                                case(0x2E):
                                    xor_a_imm8(CPU, mem);
                                    return 2;
                                case(0x36):
                                    or_a_imm8(CPU, mem);
                                    return 2;
                                case(0x3E):
                                    cp_a_imm8(CPU, mem);
                                    return 2;
                                case(0x00):
                                    ret_nz(CPU, mem);
                                    if(((CPU->F) & Z) == 0){
                                        return 5;
                                    } else {
                                        return 2;
                                    }
                                case(0x10):
                                    ret_nc(CPU, mem);
                                    if(((CPU->F) & Cy) == 0){
                                        return 5;
                                    } else {
                                        return 2;
                                    }
                                case(0x08):
                                    ret_z(CPU, mem);
                                    if((CPU->F) & Z){
                                        return 5;
                                    } else {
                                        return 2;
                                    }
                                case(0x18):
                                    ret_c(CPU, mem);
                                    if((CPU->F) & Cy){
                                        return 5;
                                    } else {
                                        return 2;
                                    }
                                case(0x09):
                                    ret(CPU, mem);
                                    return 4;
                                case(0x19):
                                    ret_i(CPU, mem);
                                    return 4;
                                case(0x02):
                                    jp_nz_imm16(CPU, mem);
                                    if(((CPU->F) & Z) == 0){
                                        return 4;
                                    } else {
                                        return 3;
                                    }
                                case(0x12):
                                    jp_nc_imm16(CPU, mem);
                                    if(((CPU->F) & Cy) == 0){
                                        return 4;
                                    } else {
                                        return 3;
                                    }
                                case(0x0A):
                                    jp_z_imm16(CPU, mem);
                                    if((CPU->F) & Z){
                                        return 4;
                                    } else {
                                        return 3;
                                    }
                                case(0x1A):
                                    jp_c_imm16(CPU, mem);
                                    if((CPU->F) & Cy){
                                        return 4;
                                    } else {
                                        return 3;
                                    }
                                case(0x03):
                                    jp_imm16(CPU, mem);
                                    return 4;
                                case(0x29):
                                    jp_hl(CPU, mem);
                                    return 1;
                                case(0x04):
                                    call_nz(MBC, CPU, mem);
                                    if(((CPU->F) & Z) == 0){
                                        return 6;
                                    } else {
                                        return 3;
                                    }
                                case(0x14):
                                    call_nc(MBC, CPU, mem);
                                    if(((CPU->F) & Cy) == 0){
                                        return 6;
                                    } else {
                                        return 3;
                                    }
                                case(0x0C):
                                    call_z(MBC, CPU, mem);
                                    if((CPU->F) & Z){
                                        return 6;
                                    } else {
                                        return 3;
                                    }
                                case(0x1C):
                                    call_c(MBC, CPU, mem);
                                    if((CPU->F) & Cy){
                                        return 6;
                                    } else {
                                        return 3;
                                    }
                                case(0x0D):
                                    call(MBC, CPU, mem);
                                    return 6;
                                case(0x0B):
                                    return CBprefix(CPU, mem);
                                case(0x22):
                                    ldh_c_a(MBC, CPU, mem);
                                    return 2;
                                case(0x20):
                                    ldh_imm8_a(MBC, CPU, mem);
                                    return 3;
                                case(0x2A):
                                    ld_imm16_a(MBC, CPU, mem);
                                    return 4;
                                case(0x32):
                                    ldh_a_c(CPU, mem);
                                    return 2;
                                case(0x30):
                                    ldh_a_imm8(CPU, mem);
                                    return 3;
                                case(0x3A):
                                    ld_a_imm16(CPU, mem);
                                    return 4;
                                case(0x28):
                                    add_sp_imm8(CPU, mem);
                                    return 4;
                                case(0x38):
                                    ld_hl_sp_imm8(CPU, mem);
                                    return 3;
                                case(0x39):
                                    ld_sp_hl(CPU, mem);
                                    return 2;
                                case(0x33):
                                    di(CPU, mem);
                                    return 1;
                                case(0x3B):
                                    ei(CPU, mem);
                                    return 1;
                            }
                        }
                }
        }
        printf("UNHANDLED OPCODE: %02X at PC=%04X\n", opcode, CPU->PC - 1);
        exit(1);
    }
}

uint32_t interrupt_service(struct MBC1 *MBC, cpu *CPU, uint8_t *mem, int bit){
    CPU->IME = 0;
    mem[0xFF0F] &= ~(1<<bit); 
    push16(MBC, CPU, mem, CPU->PC);
    switch(bit){
        case 0:
            CPU->PC = 0x40;//Vblank 
            break;
        case 1:
            CPU->PC = 0x48;//LCD
            break;
        case 2:
            CPU->PC = 0x50;//Timer
            break;
        case 3:
            CPU->PC = 0x58;//Serial
            break;
        case 4:
            CPU->PC = 0x60;//Joypad
    }
    return 5;
}