#include <stdio.h>
#include <stdint.h>
#include "emulator.h"

op_info operations[512];

void init_table(void){

    const char *alu_names[8] = 
        {
            "ADD","ADC","SUB","SBC","AND","XOR","OR","CP"
        };

    const char *registers[8] = 
        {
            "B", "C", "D","E","H","L","(HL)","A"
        };

    const char *CB_ops[8] = 
    {
        "RLC", "RRC", "RL","RR","SLA","SRA","SWAP","SRL"
    };

    const char *CB_test[3]=
    {
        "BIT","RES","SET"
    };


    const char *flags[4]=
    {
        "NZ","Z", "NC", "C" 
    };

    const char *JR_names[5]=
    {
        "JR", "JR NZ","JR Z", "JR NC", "JR C" 
    };

    const char *long_regs[4]=
    {
        "BC","DE","HL","SP"
    };

    const char *long_regs2[4]=
    {
        "(BC)","(DE)","(HL+)","(HL-)"
    };

    for (int i =0; i<0x200; i++){
        operations[i].name[0] = '\0'; 
        operations[i].format = NONE;
        operations[i].length = 1;
    }

    for (int i = 0x40; i<0x80; i++){
        uint8_t op = i; 
        snprintf(operations[i].name, 32, "LD %s,%s", registers[(op&0x38)>>3], registers[op&0x07]);
        operations[i].length = 1;
        operations[i].format = NONE;
    }

    for (int i = 0x80; i<0xC0; i++){
        uint8_t op = i; 
        snprintf(operations[i].name, 32, "%s A,%s", alu_names[(op&0x38)>>3], registers[op&0x07]);
        operations[i].length =1;
        operations[i].format = NONE;
    }

    for(int i = 0x100; i<0x200; i++){
        uint8_t x = (i&0xC0)>>6;
        uint8_t y = (i&0x38)>>3;
        uint8_t z = i&0x07;
        if(x==0){
            snprintf(operations[i].name, 32, "%s,%s", CB_ops[y], registers[z]);
        }
        else{
            snprintf(operations[i].name, 32, "%s %d,%s", CB_test[x-1], y, registers[z]);
        }
        operations[i].length = 2;
        operations[i].format = NONE;
    }

    operations[0] = (op_info){"NOP", 1, NONE};
    for (int i = 1; i<0x32; i+=16){
        snprintf(operations[i].name, 32, "LD %s", long_regs[(i-1)/16]);
        operations[i].length = 3;
        operations[i].format = IMM16;
    }
    for (int i = 2; i<0x33; i+=16){
        snprintf(operations[i].name, 32, "LD %s,A", long_regs2[(i-2)/16]);
        operations[i].length = 1;
        operations[i].format = NONE;
    }
    operations[2] = (op_info){"LD (BC),A", 1, NONE};
    operations[3] = (op_info){"INC BC", 1, NONE};
    for (int i = 4; i<0x3D; i+=8){
        snprintf(operations[i].name, 32, "INC %s", registers[(i-4)/8]);
        operations[i].format = NONE;
        operations[i].length = 1;
    }
    for (int i = 5; i<0x3E; i+=8){
        snprintf(operations[i].name, 32, "DEC %s", registers[(i-5)/8]);
        operations[i].format = NONE;
        operations[i].length = 1;
    }
    for (int i = 6; i<0x3F; i+=8){
        snprintf(operations[i].name, 32, "LD %s", registers[(i-6)/8]);//need to add another token to the string when I use it later
        operations[i].format = IMM8;
        operations[i].length = 2;
    }
    operations[7] = (op_info){"RLCA", 1, NONE};
    operations[8] = (op_info){"LD $%04X,SP", 3, IMM16_2};
    for (int i = 9; i<0x3A; i+=16){
        snprintf(operations[i].name, 32, "ADD HL,%s", long_regs[(i-9)/16]);
        operations[i].length = 1;
        operations[i].format = NONE;
    }
    for (int i = 10; i<0x3B; i+=16){
        snprintf(operations[i].name, 32, "LD A,%s", long_regs2[(i-10)/16]);
        operations[i].length = 1;
        operations[i].format = NONE;
    }
    operations[15] = (op_info){"RRCA", 1, NONE};
    operations[16] = (op_info){"STOP", 2, NONE};//There is some nuance about how many instructions STOP takes
    operations[0x17] = (op_info){"RLA", 1, NONE};
    for(int i = 0x18; i<0x39; i+=8){
        snprintf(operations[i].name, 32, "%s", JR_names[(i-0x18)/8]);
        operations[i].format = JR;
        operations[i].length = 2;
    }
    operations[0x27] = (op_info){"DAA", 1, NONE};
    operations[0x37] = (op_info){"SCF", 1, NONE};
    operations[0x2F] = (op_info){"CPL", 1, NONE};
    operations[0x3F] = (op_info){"CCF", 1, NONE};
    operations[0x1F] = (op_info){"RRA", 1, NONE};
    operations[0x76] = (op_info){"HALT", 1, NONE};
    for (int i = 0xC0; i<0xD9; i+=8){
        snprintf(operations[i].name, 32, "RET %s", flags[(i-0xC0)/8]);
        operations[i].format = NONE;
        operations[i].length = 1;
    }
    for (int i = 0xC2; i<0xDB; i+=8){
        snprintf(operations[i].name, 32, "JP %s", flags[(i-0xC2)/8]);
        operations[i].format = IMM16;
        operations[i].length = 3;
    }
    for (int i = 0xC4; i<0xDD; i+=8){
        snprintf(operations[i].name, 32, "CALL %s", flags[(i-0xC4)/8]);
        operations[i].format = IMM16;
        operations[i].length = 3;
    }
    for (int i = 0xC1; i<0xF2; i+=16){
        const char *reg = (i==0xF1)? "AF" : long_regs[(i-0xC1)/16];
        snprintf(operations[i].name, 32, "POP %s", reg);
        operations[i].format = NONE;
        operations[i].length = 1;
    }

    for (int i = 0xC5; i<0xF6; i+=16){
        const char *reg = (i==0xF5)? "AF" : long_regs[(i-0xC5)/16];
        snprintf(operations[i].name, 32, "PUSH %s", reg);
        operations[i].format = NONE;
        operations[i].length = 1;
    }
    for (int i = 0xC6; i<0xFF; i+=8){
        snprintf(operations[i].name, 32, "%s,", alu_names[(i-0xC6)/8]);
        operations[i].format = IMM8;
        operations[i].length = 2;
    }
    for (int i = 0xC7; i<0x100; i+=8){
        snprintf(operations[i].name, 32, "RST %04X", i-0xC7);
        operations[i].format = NONE;
        operations[i].length = 1;
    }
    operations[0xC3] = (op_info){"JP",3, IMM16};
    operations[0xCD] = (op_info){"CALL",3, IMM16};
    operations[0xC9] = (op_info){"RET",1, NONE};
    operations[0xD9] = (op_info){"RETI",1, NONE};
    operations[0xF3] = (op_info){"DI",1, NONE};
    operations[0xFB] = (op_info){"EI",1, NONE};
    operations[0xE0] = (op_info){"PC:%04X LD ($%04X),A",2,IMM8_2};
    operations[0xF0] = (op_info){"PC:%04X LD A,($%04X)",2,IMM8_2};
    operations[0xF2] = (op_info){"LD A,($FF00)",1,NONE};
    operations[0xE2] = (op_info){"LD ($FF00+C),A",1,NONE};
    operations[0xEA] = (op_info){"PC:%04X LD ($%04X),A",3,IMM16_2};
    operations[0xFA] = (op_info){"PC:%04X LD A,($%04X)",3,IMM16_2};
    operations[0xE8] = (op_info){"ADD SP,",2,IMM8_3};
    operations[0xF8] = (op_info){"LD HL,SP+,",2,IMM8_3};
    operations[0xF9] = (op_info){"LD SP,HL",1,NONE};
    operations[0xE9] = (op_info){"JP HL", 1, NONE};
    operations[0xCB] = (op_info){"void", 0, CB};
}