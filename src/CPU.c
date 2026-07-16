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

static inline uint8_t readPC(struct cartridge *cart, cpu *CPU, memory *mem){//memory cannot be accessed at certain times such as during a DMA transfer
    uint8_t value = mem_read(cart, CPU, mem, CPU->PC);
    (CPU->PC)++;
    return value;
}

static inline uint16_t read16(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t lo = readPC(cart,CPU,mem);
    uint8_t hi = readPC(cart,CPU,mem);
    uint16_t value = (((uint16_t) hi) << 8) | lo;
    return value;
}
static inline uint16_t pop16(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t lo = mem_read(cart, CPU, mem, CPU->SP);
    CPU->SP++;
    uint8_t hi = mem_read(cart, CPU, mem, CPU->SP);
    CPU->SP++;
    return (((uint8_t) hi) << 8) | lo;

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
static inline void push16(struct cartridge *cart, cpu *CPU, memory *mem, uint16_t value){
    uint8_t hi = value >> 8;
    uint8_t lo = value;
    CPU->SP--;
    mem_write(cart, CPU, mem, CPU->SP, hi);
    CPU->SP--;
    mem_write(cart, CPU, mem, CPU->SP, lo);
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

uint8_t* get345reg(cpu *CPU, uint8_t reg, memory *mem){
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
        //case(0x30): 
        //We check if the register is (HL) and do something else if so
        case(0x38):
            return &CPU->A;
        default:
            return NULL;
    }
}

uint8_t* get012reg(cpu *CPU, uint8_t reg, memory *mem){
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
        // case(0x06):
        //We check if the register is (HL) and do something else if so
        case(0x07):
            return &CPU->A;
        default:
            return NULL;
    }
}

void ld_imm16(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t offset){
    uint8_t lo = readPC(cart,CPU,mem);
    uint8_t hi = readPC(cart,CPU,mem);

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
            CPU->SP = (((uint16_t) hi)<<8) | lo;
            break;
    }
}

void ld_imm16_sp(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t addr = read16(cart,CPU,mem);
    uint8_t lo = (uint8_t) CPU->SP;
    uint8_t hi = (uint8_t) (CPU->SP >> 8);
    mem_write(cart, CPU, mem, addr, lo);
    mem_write(cart, CPU, mem, addr + 1, hi);
}

void ld_a_r16mem(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t offset){
    uint16_t HL = getHL(CPU);
    switch(offset){
        case 0x00:
            CPU->A = mem_read(cart, CPU, mem, getBC(CPU));
            break;
        case 0x10:
            CPU->A = mem_read(cart, CPU, mem, getDE(CPU));
            break;
        case 0x20:
            CPU->A = mem_read(cart, CPU, mem, HL);
            HL++;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break;
        case 0x30:
            CPU->A =  mem_read(cart, CPU, mem, HL);
            HL--;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break;
    }
}

void ld_r16mem_a(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t offset){
    uint16_t HL = getHL(CPU);
    switch(offset){
        case 0x00:
            mem_write(cart, CPU, mem, getBC(CPU), CPU->A);
            break;
        case 0x10:
            mem_write(cart, CPU, mem, getDE(CPU), CPU->A);
            break;
        case 0x20:
            mem_write(cart, CPU, mem, HL, CPU->A);
            HL++;
            CPU->L = (uint8_t) HL;//The casts are not strictly necessary but they clarify what's going on
            CPU->H = (uint8_t) (HL >> 8);
            break; 
        case 0x30:
            mem_write(cart, CPU, mem, HL, CPU->A);
            HL--;
            CPU->L = (uint8_t) HL;
            CPU->H = (uint8_t) (HL >> 8);
            break; 
    }
}

void inc_r16(cpu *CPU, memory *mem, uint8_t offset){
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

void dec_r16(cpu *CPU, memory *mem, uint8_t offset){
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

void add_HL_r16(cpu *CPU, memory *mem, uint8_t offset){
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

void inc_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t offset){//come back (HL)
    if(offset == 0x30){
        uint8_t value = mem_read(cart, CPU, mem, getHL(CPU));
        mem_write(cart, CPU, mem, getHL(CPU), value+1);
        SetZ8(value+1,CPU);
        SetHAdd8(value,1,CPU);
    } else{
        uint8_t *value = get345reg(CPU, offset, mem);
        SetHAdd8(*value, 1, CPU);
        (*value)++;

        //flags
        SetZ8(*value, CPU);
    }
    CPU->F &= ~N;
}

void dec_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t offset){//come back
    if(offset == 0x30){
        uint8_t value = mem_read(cart, CPU, mem, getHL(CPU));
        mem_write(cart, CPU, mem, getHL(CPU), value-1);
        SetZ8(value-1,CPU);
        SetHSub8(value,1,CPU);
    } else{
        uint8_t *value = get345reg(CPU, offset, mem);
        SetHSub8(*value, 1, CPU);
        (*value)--;

        //flags
        SetZ8(*value, CPU);
    }

    CPU->F |= N;
}

void ld_rd_imm8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t offset){
    uint8_t value = readPC(cart,CPU,mem);
    if(offset == 0x30){
        mem_write(cart, CPU, mem, getHL(CPU), value);
    }else{
        uint8_t *reg = get345reg(CPU, offset, mem);
        *reg = value;
    }
}

void rlca(cpu *CPU){
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

void rrca(cpu *CPU){
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

void rla(cpu *CPU){
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
}

void rra(cpu *CPU){
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
    
}

void daa(cpu *CPU){
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

void cpl(cpu *CPU){
    CPU->A = ~CPU->A;
    CPU->F |= N;
    CPU->F |= Hf;
}

void scf(cpu *CPU){
    CPU->F |= Cy;
    CPU->F &= ~(N | Hf);
}

void ccf(cpu *CPU){
    CPU->F = (CPU->F & Cy) ? CPU->F & ~Cy : CPU->F | Cy;
    CPU->F &= ~(N | Hf);
}

void jr_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    int8_t value = readPC(cart,CPU,mem);
    CPU->PC += value;
}

void jr_nz_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    int8_t value = readPC(cart,CPU,mem);
    if((CPU->F & Z) == 0){
    CPU->PC += value;
    }
}

void jr_nc_imm8(struct cartridge *cart,cpu *CPU, memory *mem){
    int8_t value = readPC(cart,CPU,mem);
    if((CPU->F & Cy) == 0){
        CPU->PC += value;
    }
}

void jr_z_imm8(struct cartridge *cart,cpu *CPU, memory *mem){
    int8_t value = readPC(cart,CPU,mem);
    if(CPU->F & Z){
        CPU->PC += value;
    }
}

void jr_c_imm8(struct cartridge *cart,cpu *CPU, memory *mem){
    int8_t value = readPC(cart,CPU,mem);
    if(CPU->F & Cy){
        CPU->PC += value;
    }
}

void stop(cpu *CPU, memory *mem){
    //Do later
}

void nop(){
}

void halt(cpu *CPU){
    CPU->halted = 1;
}

void ld_r8_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t source, uint8_t dest){
    uint8_t *d = get345reg(CPU, dest, mem);
    uint8_t tmp;
    uint8_t *s;
    if (source == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        s = &tmp;
    } else{
        s = get012reg(CPU, source, mem);
    }
    if (dest == 0x30){
        mem_write(cart, CPU, mem, getHL(CPU), *s);
    }else {
        *d = *s;//destination = source.
    }
}

void add_a_r8(cpu *CPU, uint8_t value){
    uint8_t old = CPU->A;
    CPU->A += value;
    SetZ8(CPU->A, CPU);
    SetHAdd8(old, value, CPU);
    SetCAdd8(old, value, CPU);
    CPU->F &= ~N;
}

void add_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t old = CPU->A;
    uint8_t s = readPC(cart, CPU, mem);
    CPU->A += s;
    SetZ8(CPU->A, CPU);
    SetHAdd8(old, s, CPU);
    SetCAdd8(old, s, CPU);

    CPU->F &= ~N;
}

void adc_a_r8(cpu *CPU, uint8_t value){
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


void adc_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t old = CPU->A;
    uint8_t s = readPC(cart,CPU,mem);
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

void sub_a_r8(cpu *CPU, uint8_t value){
    uint8_t old = CPU->A;
    CPU->A -= value;
    SetZ8(CPU->A,CPU);
    SetHSub8(old, value, CPU);
    SetCSub8(old, value, CPU);
    CPU->F |= N;
}

void sub_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t old = CPU->A;
    uint8_t s = readPC(cart,CPU,mem);
    CPU->A -= s;
    SetZ8(CPU->A,CPU);
    SetHSub8(old, s, CPU);
    SetCSub8(old, s, CPU);
    CPU->F |= N;
}

void sbc_a_r8(cpu *CPU, uint8_t value){
    uint8_t old = CPU->A;
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

void sbc_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t old = CPU->A;
    uint8_t s = readPC(cart,CPU,mem);
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

void and_a_r8(cpu *CPU, uint8_t value){
    CPU->A &= value;
    SetZ8(CPU->A,CPU);
    CPU->F &= ~N;
    CPU->F |= Hf;
    CPU->F &= ~Cy;

}

void and_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t s = readPC(cart,CPU,mem);
    CPU->A &= s;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F |= Hf;
    CPU->F &= ~Cy;
}

void xor_a_r8(cpu *CPU, uint8_t value){
    CPU->A ^= value;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    CPU->F &= ~Cy;

}

void xor_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t s = readPC(cart,CPU,mem);
    CPU->A ^= s;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    CPU->F &= ~Cy;
}

void or_a_r8(cpu *CPU, uint8_t value){
    CPU->A |= value;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    CPU->F &= ~Cy;

}

void or_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t s = readPC(cart,CPU,mem);
    CPU->A |= s;
    SetZ8(CPU->A,CPU);
 
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    CPU->F &= ~Cy;
}

void cp_a_r8(cpu *CPU, uint8_t value){
    SetZ8(CPU->A - value,CPU);
    SetHSub8(CPU->A, value, CPU);
    SetCSub8(CPU->A, value, CPU);
    CPU->F |= N;
}

void cp_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t s = readPC(cart,CPU,mem);
    SetZ8(CPU->A - s,CPU);
    SetHSub8(CPU->A, s, CPU);
    SetCSub8(CPU->A, s, CPU);

    CPU->F |= N;
}

void ret(struct cartridge *cart,cpu *CPU, memory *mem){
    CPU->PC = pop16(cart, CPU, mem);
}

void ret_i(struct cartridge *cart, cpu *CPU, memory *mem){
    CPU->PC = pop16(cart, CPU, mem);
    CPU->IME = 1;
}

void ret_z(struct cartridge *cart, cpu *CPU, memory *mem){
    if(CPU->F & Z){
        CPU->PC =  pop16(cart, CPU, mem);
    }
}

void ret_c(struct cartridge *cart, cpu *CPU, memory *mem){
    if(CPU->F & Cy){
        CPU->PC =  pop16(cart, CPU, mem);
    }
}

void ret_nz(struct cartridge *cart, cpu *CPU, memory *mem){
    if((CPU->F & Z) == 0){
        CPU->PC =  pop16(cart, CPU, mem);
    }
}

void ret_nc(struct cartridge *cart, cpu *CPU, memory *mem){
    if((CPU->F & Cy) == 0){
        CPU->PC =  pop16(cart, CPU, mem);
    }
}

void jp_imm16(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem); 
    CPU->PC = value; 
}

void jp_nz_imm16(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem);
    if((CPU->F & Z) == 0){
        CPU->PC = value;
    }
}

void jp_nc_imm16(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem);
    if((CPU->F & Cy) == 0){
        CPU->PC = value;
    }
}

void jp_z_imm16(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem);
    if(CPU->F & Z){
        CPU->PC = value;
    }
}

void jp_c_imm16(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem);
    if(CPU->F & Cy){
        CPU->PC = value;
    }
}

void jp_hl(cpu *CPU){
    CPU->PC = getHL(CPU);
}

void call(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t oldpc = CPU->PC;
    uint16_t value = read16(cart,CPU,mem);
    push16(cart, CPU, mem, CPU->PC);
    CPU->PC = value;
}

void call_nz(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem);
    if((CPU->F & Z) == 0){
        push16(cart, CPU, mem, CPU->PC);
        CPU->PC = value;
    }
}

void call_nc(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem);
    if((CPU->F & Cy) == 0){
        push16(cart, CPU, mem, CPU->PC);
        CPU->PC = value;
    }
}

void call_z(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem);
    if(CPU->F & Z){
        push16(cart, CPU, mem, CPU->PC);
        CPU->PC = value; 
    }
}

void call_c(struct cartridge *cart, cpu *CPU, memory *mem){
    uint16_t value = read16(cart,CPU,mem);
    if(CPU->F & Cy){
        push16(cart, CPU, mem, CPU->PC);
        CPU->PC = value;
    }
}
void rst(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t vec){
    push16(cart, CPU, mem, CPU->PC);
    CPU->PC = vec;
}

void pop_r16stk(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    uint8_t hi = mem_read(cart, CPU, mem, CPU->SP + 1);
    uint8_t lo = mem_read(cart, CPU, mem, CPU->SP);
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

void push_r16stk(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    switch(reg){
        case(0x00)://BC
            push16(cart, CPU, mem, getBC(CPU));
            break;
        case(0x10)://DE
            push16(cart, CPU, mem, getDE(CPU));
            break;
        case(0x20)://HL
            push16(cart, CPU, mem, getHL(CPU));
            break;
        case(0x30)://AF
            push16(cart, CPU, mem, ((CPU->A<<8)|CPU->F));
            break;
    }
        
}

void ldh_c_a(struct cartridge *cart, cpu *CPU, memory *mem){
    mem_write(cart, CPU, mem, 0xFF00 + CPU->C, CPU->A);
}
void ldh_imm8_a(struct cartridge *cart, cpu *CPU, memory *mem){
    mem_write(cart, CPU, mem, 0xFF00 + readPC(cart,CPU,mem), CPU->A);
}
void ld_imm16_a(struct cartridge *cart, cpu *CPU, memory *mem){
    mem_write(cart, CPU, mem, read16(cart,CPU,mem), CPU->A);
}
void ldh_a_c(struct cartridge *cart, cpu *CPU, memory *mem){
    CPU->A = mem_read(cart, CPU, mem, 0xFF00 + CPU->C);
}
void ldh_a_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    CPU->A = mem_read(cart, CPU, mem, 0xFF00 + readPC(cart,CPU,mem));
}
void ld_a_imm16(struct cartridge *cart, cpu *CPU, memory *mem){
    CPU->A = mem_read(cart, CPU, mem, read16(cart,CPU,mem));
}

void add_sp_imm8(struct cartridge *cart, cpu *CPU, memory *mem){
    int16_t old = CPU->SP;
    int8_t s = readPC(cart, CPU,mem);
    CPU->SP += s;
    CPU->F &= ~Z;
    CPU->F &= ~N;
    SetHAdd8((uint8_t) old, (uint8_t)s, CPU);//The way these flags are set is not intuitive
    SetCAdd8((uint8_t) old, (uint8_t)s, CPU);
}

void ld_hl_sp_imm8(struct cartridge *cart, cpu *CPU, memory *mem){//flags
    int8_t s = readPC(cart,CPU,mem); 
    uint16_t HL = CPU->SP + s;
    CPU->L = (uint8_t) HL;
    CPU->H = (uint8_t) (HL >> 8); 
    CPU->F &= ~Z;
    CPU->F &= ~N;
    SetHAdd8((uint8_t) CPU->SP, (uint8_t) s, CPU);
    SetCAdd8((uint8_t) CPU->SP, (uint8_t) s, CPU);
}

void ld_sp_hl(cpu *CPU){
    CPU->SP = getHL(CPU);
}

void ei(cpu *CPU){
    CPU->ie_pending = 1;
    
}
void di(cpu *CPU){
    CPU->IME = 0;
}

void rlc_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t bit7 = 0x01 & (((*value) & 0x80) >> 7);//the 0x01& is for arithmetic vs logical shifts. Prob not necessary.
    SetCShiftL(CPU, *value);
    *value <<= 1;
    *value |= bit7;
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
    if(reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
}

void rrc_r8(struct cartridge *cart,cpu *CPU, memory *mem, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t bit1 = 0x80 & (((*value) & 0x01) << 7);
    SetCShiftR(CPU, *value);
    *value >>= 1;
    *value |= bit1;
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}
void rl_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t tempC = CPU->F & Cy;
    SetCShiftL(CPU, *value);
    *value <<= 1;
    *value |= (tempC ? 0x01 : 0x00);
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void rr_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t tempC = CPU->F & Cy;
    SetCShiftR(CPU, *value);
    *value >>= 1;
    *value |= (tempC ? 0x80 : 0x00);
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void sla_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    SetCShiftL(CPU, *value);
    *value <<= 1;
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}
void sra_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t bit7 = (*value) & 0x80;
    SetCShiftR(CPU, *value);
    *value >>= 1;
    *value |= bit7;
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void srl_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    SetCShiftR(CPU, *value);
    *value >>= 1;
    *value &= 0x7F;//probably don't need this
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
    SetZ8(*value,CPU);
    CPU->F &= ~N;
    CPU->F &= ~Hf;
}

void swap_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t hi = (*value) & 0xF0;
    uint8_t lo = (*value) & 0x0F;
    *value = (lo<<4) | (hi>>4);
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
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
    return 0;//should never get here
}

void bit_b3_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t test, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t bit = bitswitch(test);
    SetZ8(bit & *value, CPU);
    CPU->F &= ~N;
    CPU->F |= Hf;
}

void res_b3_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t bit, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t test = bitswitch(bit);
    *value &= ~test;
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
}
void set_b3_r8(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t bit, uint8_t reg){
    uint8_t *value;
    uint8_t tmp;
    if (reg == 0x06){
        tmp = mem_read(cart, CPU, mem, getHL(CPU));
        value = &tmp;
    } else{
        value = get012reg(CPU, reg, mem);
    }
    uint8_t test = bitswitch(bit);
    *value |= test;
    if (reg == 0x06){
        mem_write(cart, CPU, mem, getHL(CPU), *value);
    }
}

int CBprefix(struct cartridge *cart, cpu *CPU, memory *mem){
    uint8_t opcode = readPC(cart,CPU,mem);
    uint8_t reg = opcode & 0x07;
    uint8_t bit = opcode & 0x38;
    switch(opcode & 0xC0){
        case(0x00):
            switch(bit){
                case(0x00):
                    rlc_r8(cart, CPU, mem, reg);
                    break;
                case(0x08):
                    rrc_r8(cart, CPU, mem, reg);
                    break;
                case(0x10):
                    rl_r8(cart, CPU, mem, reg);
                    break;
                case(0x18):
                    rr_r8(cart, CPU, mem, reg);
                    break;
                case(0x20):
                    sla_r8(cart, CPU, mem, reg);
                    break;
                case(0x28):
                    sra_r8(cart, CPU, mem, reg);
                    break;
                case(0x30):
                    swap_r8(cart, CPU, mem, reg);
                    break;
                case(0x38):    
                    srl_r8(cart, CPU, mem, reg);
                    break;
            }
            break;
        case(0x40):
            bit_b3_r8(cart, CPU, mem, bit, reg);
            break;
        case(0x80):
            res_b3_r8(cart, CPU, mem, bit, reg);
            break;
        case(0xC0):
            set_b3_r8(cart, CPU, mem, bit, reg);
            break;
    }
    if(reg == 0x06){
        return 4;
    } else {
        return 2;
    }
}

void arith_8bit(struct cartridge *cart, cpu *CPU, memory *mem, uint8_t operation, uint8_t operand){
    uint8_t value;
    if(operand == 0x06){
        value = mem_read(cart, CPU, mem, getHL(CPU));
    } else{
        value = *get012reg(CPU, operand, mem);
    }
    switch(operation){
        case(0x00):
            add_a_r8(CPU, value);
            break;
        case(0x08):
            adc_a_r8(CPU, value);
            break;
        case(0x10):
            sub_a_r8(CPU, value);
            break;
        case(0x18):
            sbc_a_r8(CPU, value);
            break;
        case(0x20):
            and_a_r8(CPU, value);
            break;
        case(0x28):
            xor_a_r8(CPU, value);
            break;
        case(0x30):
            or_a_r8(CPU, value);
            break;
        case(0x38):
            cp_a_r8(CPU, value);
            break;
    }
}

uint32_t execute(struct cartridge *cart, cpu *CPU, memory *mem){

    if(CPU->halted){
        return 1;//This means the CPU can only start again on t-cycles that are multiples of 4
    } else{
        if(CPU->PC == 0x100){
            mem->boot_mapped = 0;
        }
        uint8_t opcode = readPC(cart,CPU,mem);
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
                                nop();
                                return 1;
                            case 0x10:
                                stop(CPU, mem);
                                return 1;//STOP instruction. Actual implementation is obscure, including the number of cycles. COME BACK
                            case 0x20:
                                jr_nz_imm8(cart,CPU, mem);
                                if(((CPU->F) & Z )== 0){
                                    return 3;
                                } else {
                                    return 2;
                                }
                            case 0x30:
                                jr_nc_imm8(cart,CPU, mem);
                                if(((CPU->F) & Cy )== 0){
                                    return 3;
                                } else {
                                    return 2;
                                }
                        }
                        break;
                    case 0x01:
                        ld_imm16(cart, CPU, mem, offset);
                        return 3;
                    case 0x02:
                        ld_r16mem_a(cart, CPU, mem, offset);
                        return 2;
                    case 0x0A:
                        ld_a_r16mem(cart, CPU, mem, offset);
                        return 2;
                    case 0x08:
                        switch(opcode & m45block_mask){
                            case 0x00:
                                ld_imm16_sp(cart, CPU, mem);
                                return 5;
                            case 0x10:
                                jr_imm8(cart, CPU, mem);
                                return 3;
                            case 0x20:
                                jr_z_imm8(cart, CPU, mem);
                                if((CPU->F) & Z){
                                    return 3;
                                } else {
                                    return 2;
                                }
                            case 0x30:
                                jr_c_imm8(cart, CPU, mem);
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
                        inc_r8(cart, CPU, mem, bits345);
                        if(opcode == 0x34){
                            return 3;
                        } else{
                            return 1;
                        }
                    case 0x05:
                    case 0x0D:
                        dec_r8(cart, CPU, mem, bits345);
                        if(opcode == 0x35){
                            return 3;
                        } else{
                            return 1;
                        }
                    case 0x06:
                    case 0x0E:
                        ld_rd_imm8(cart, CPU, mem, bits345);
                        if(opcode == 0x36){
                            return 3;
                        } else{
                            return 2;
                        }
                    case 0x07:
                        switch(opcode & m45block_mask){
                            case(0x00): 
                                rlca(CPU);
                                return 1;
                            case(0x10):
                                rla(CPU);
                                return 1;
                            case(0x20):
                                daa(CPU);
                                return 1;
                            case(0x30):
                                scf(CPU);
                                return 1;
                        }
                        break;
                    case 0x0F:
                        switch(opcode & m45block_mask){
                            case(0x00): 
                                rrca(CPU);
                                return 1;
                            case(0x10):
                                rra(CPU);
                                return 1;
                            case(0x20):
                                cpl(CPU);
                                return 1;
                            case(0x30):
                                ccf(CPU);
                                return 1;
                        }
                        break;
                }
                break;
            case (0x40)://block 1
                {uint8_t dest = opcode & m543mask;
                uint8_t source = opcode & 0x07;
                if(dest == 0x30 && source == 0x06){
                    halt(CPU);
                    return 1;
                } else {
                    ld_r8_r8(cart, CPU, mem, source, dest);
                    if(source == 0x06 || dest == 0x30){
                        return 2;
                    } else {
                        return 1;
                    }
                }
                }
                break;
            case (0x80)://block 2
                {uint8_t operand  = opcode & 0x07;
                uint8_t operation = opcode & m543mask;
                arith_8bit(cart, CPU, mem, operation, operand);
                if(operand == 0x06){
                    return 2;
                } else {
                    return 1;
                }
                break;}
            
            case(0xC0)://block 3
                uint8_t reg = opcode & 0x30;
                switch(opcode & 0x0F){
                    case(0x01):
                        pop_r16stk(cart, CPU, mem, reg);
                        return 3;
                    case(0x05):
                        push_r16stk(cart, CPU, mem, reg);
                        return 4;
                    default:
                        if((opcode & 0x07) == 0x07){
                            uint8_t tgt = opcode & m543mask;
                            rst(cart, CPU, mem, tgt);
                            return 4;
                        } else{
                            switch(opcode & 0x3F){
                                case(0x06):
                                    add_a_imm8(cart,CPU, mem);
                                    return 2;
                                case(0x0E):
                                    adc_a_imm8(cart,CPU, mem);
                                    return 2;
                                case(0x16):
                                    sub_a_imm8(cart,CPU, mem);
                                    return 2;
                                case(0x1E):
                                    sbc_a_imm8(cart,CPU, mem);
                                    return 2;
                                case(0x26):
                                    and_a_imm8(cart,CPU, mem);
                                    return 2;
                                case(0x2E):
                                    xor_a_imm8(cart,CPU, mem);
                                    return 2;
                                case(0x36):
                                    or_a_imm8(cart,CPU, mem);
                                    return 2;
                                case(0x3E):
                                    cp_a_imm8(cart,CPU, mem);
                                    return 2;
                                case(0x00):
                                    ret_nz(cart, CPU, mem);
                                    if(((CPU->F) & Z) == 0){
                                        return 5;
                                    } else {
                                        return 2;
                                    }
                                case(0x10):
                                    ret_nc(cart, CPU, mem);
                                    if(((CPU->F) & Cy) == 0){
                                        return 5;
                                    } else {
                                        return 2;
                                    }
                                case(0x08):
                                    ret_z(cart, CPU, mem);
                                    if((CPU->F) & Z){
                                        return 5;
                                    } else {
                                        return 2;
                                    }
                                case(0x18):
                                    ret_c(cart, CPU, mem);
                                    if((CPU->F) & Cy){
                                        return 5;
                                    } else {
                                        return 2;
                                    }
                                case(0x09):
                                    ret(cart,CPU, mem);
                                    return 4;
                                case(0x19):
                                    ret_i(cart, CPU, mem);
                                    return 4;
                                case(0x02):
                                    jp_nz_imm16(cart, CPU, mem);
                                    if(((CPU->F) & Z) == 0){
                                        return 4;
                                    } else {
                                        return 3;
                                    }
                                case(0x12):
                                    jp_nc_imm16(cart, CPU, mem);
                                    if(((CPU->F) & Cy) == 0){
                                        return 4;
                                    } else {
                                        return 3;
                                    }
                                case(0x0A):
                                    jp_z_imm16(cart, CPU, mem);
                                    if((CPU->F) & Z){
                                        return 4;
                                    } else {
                                        return 3;
                                    }
                                case(0x1A):
                                    jp_c_imm16(cart, CPU, mem);
                                    if((CPU->F) & Cy){
                                        return 4;
                                    } else {
                                        return 3;
                                    }
                                case(0x03):
                                    jp_imm16(cart, CPU, mem);
                                    return 4;
                                case(0x29):
                                    jp_hl(CPU);
                                    return 1;
                                case(0x04):
                                    call_nz(cart, CPU, mem);
                                    if(((CPU->F) & Z) == 0){
                                        return 6;
                                    } else {
                                        return 3;
                                    }
                                case(0x14):
                                    call_nc(cart, CPU, mem);
                                    if(((CPU->F) & Cy) == 0){
                                        return 6;
                                    } else {
                                        return 3;
                                    }
                                case(0x0C):
                                    call_z(cart, CPU, mem);
                                    if((CPU->F) & Z){
                                        return 6;
                                    } else {
                                        return 3;
                                    }
                                case(0x1C):
                                    call_c(cart, CPU, mem);
                                    if((CPU->F) & Cy){
                                        return 6;
                                    } else {
                                        return 3;
                                    }
                                case(0x0D):
                                    call(cart, CPU, mem);
                                    return 6;
                                case(0x0B):
                                    return CBprefix(cart, CPU, mem);
                                case(0x22):
                                    ldh_c_a(cart, CPU, mem);
                                    return 2;
                                case(0x20):
                                    ldh_imm8_a(cart, CPU, mem);
                                    return 3;
                                case(0x2A):
                                    ld_imm16_a(cart, CPU, mem);
                                    return 4;
                                case(0x32):
                                    ldh_a_c(cart, CPU, mem);
                                    return 2;
                                case(0x30):
                                    ldh_a_imm8(cart, CPU, mem);
                                    return 3;
                                case(0x3A):
                                    ld_a_imm16(cart, CPU, mem);
                                    return 4;
                                case(0x28):
                                    add_sp_imm8(cart, CPU, mem);
                                    return 4;
                                case(0x38):
                                    ld_hl_sp_imm8(cart, CPU, mem);
                                    return 3;
                                case(0x39):
                                    ld_sp_hl(CPU);
                                    return 2;
                                case(0x33):
                                    di(CPU);
                                    return 1;
                                case(0x3B):
                                    ei(CPU);
                                    return 1;
                                }
                            }
                        
                }
        }
    }
}

uint32_t interrupt_service(struct cartridge *cart, cpu *CPU, memory *mem, int bit){
    CPU->IME = 0;
    IF(mem) &= ~(1<<bit); 
    push16(cart, CPU, mem, CPU->PC);
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
