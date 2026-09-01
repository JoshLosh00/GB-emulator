#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "emulator.h"



//The duty cycles of the pulse channels
uint8_t waveform[4][8] = {
    {1,1,1,1,1,1,1,0},
    {0,1,1,1,1,1,1,0},
    {0,1,1,1,1,0,0,0},
    {1,0,0,0,0,0,0,1}
};

//This function should be called when the APU is triggered after previously having been off. 
//It should reset things that are reset when the APU is turned off 
void APU_init(apu_data *data, memory *mem){
    data->pulse_pos[0] = 0;
    data->pulse_pos[1] = 0;
}

void lfsr_step(apu_data *data, memory *mem){
    //When CH4 is ticked (at the frequency specified via NR43):
    //The result of LFSR_0 ⊙ LFSR_1 (1 if bit 0 and bit 1 are identical, 0 otherwise) is written to bit 15.
    //If “short mode” was selected in NR43, then bit 15 is copied to bit 7 as well.

    uint16_t bit = ~(data->lfsr ^ (data->lfsr >> 1)) & 1;
    uint16_t mask = (1 << 15) | (NR43(mem) ? (1 << 7) : 0);
    data->lfsr = (data->lfsr & ~mask) | (bit ? mask : 0);

    //Finally, the entire LFSR is shifted right, and bit 0 selects between 0 and the chosen volume
    //The above comments are from pandocs. They imply that it is the new bit 0 that determines the amplitude. Not the old one that was shifted out

    data->lfsr >>= 1;
    //Logical vs arithmetic shifts do not matter here. Bit 15 is rewitten on the next time anyway so what it ends up being here makes no difference  
    uint8_t value = data->lfsr & 1;
    data->ch4_amp = value * data->vols[3];
}

void trigger_pulse(apu_data *data, memory *mem, int channel){
    //Ch1 or 2 is enabled.
    //channel == 0 is channel 1 and channel == 1 is channel 2
    data->dacs[channel] = 1;

    //If length timer expired it is reset.
    //Why do I have the second condition here?
    if((data->timers[channel] >= 64) /*|| (data->timers[channel] == 0)*/){
        data->timers[channel] = NRX1(mem, channel) & 0x3F;
    }

    //The period divider is set to the contents of NR13 and NR14.
    data->pulse_divs[channel] = (NRX4(mem, channel) << 8 | NRX3(mem, channel)) & 0x7FF; 

    //Envelope timer is reset.
    data->env_timers[channel] = 0;

    //Volume is set to contents of NR12 initial volume.
    data->vols[channel] = (NRX2(mem, channel) >> 4) & 0xF; 

    //Sweep does several things.
    //The sweep pace is read on retriggering the channel as well as the completion of a sweep iteration. 
    /*
    During a trigger event, several things occur:

    CH1 period value is copied to the “shadow register”.
    The “sweep timer” is reset.
    The “enabled flag” is set if either the sweep pace or individual step are non-zero, cleared otherwise.
    If the individual step is non-zero, frequency calculation and overflow check are performed immediately.
    */
    if(channel == 0){

        //CH1 period value is copied to the “shadow register”.



        data->sweep_pace = (NR10(mem) & 0x70) >> 4;
        data->sweep_pos = 0;
    }
    //It is worth noting that (re)trigering the channel does not increment the "duty step" counter, which is pulse_pos here.
    //That counter only get externally reset when the APU is turned off.
}

void trigger_wave(apu_data *data, memory *mem){

    //Writing any value to NR34 with bit 7 set triggers the channel, causing the following to occur:

    //Ch3 is enabled.
    data->dacs[2]=1;
    //If the length timer expired it is reset.
    if(data->timers[2] >= 256){
        data->timers[2] = NR31(mem);
    }
    //The period divider is set to the contents of NR33 and NR34.
    data->ch3_div = (NR34(mem) << 8 | NR33(mem)) & 0x7FF;
    //Volume is set to contents of NR32 initial volume.
    data->vols[2] = (NR32(mem) >> 5) & 3;
    //Wave RAM index is reset, but its not refilled.
    data->ch3_pos = 0;
    
    /*
        Channel 3 subtleties -- yet to implement
    
        PLAYBACK DELAY

        Triggering the wave channel does not immediately start playing wave RAM; 
        instead, the last sample ever read (which is reset to 0 when the APU is off) is output
        until the channel next reads a sample.

        ACCESS ORDER

        When CH3 is started, the first sample read is the one at index 1, 
        i.e. the lower nibble of the first byte, NOT the upper nibble. 
        
    */
}

void trigger_noise(apu_data *data, memory *mem){
    //Writing any value to NR44 with bit 7 set triggers the channel, causing the following to occur:

    //Ch4 is enabled.
    data->dacs[3] = 1;
    //If the length timer expired it is reset.
    if(data->timers[3] >= 64){
        data->timers[3] = NR41(mem);
    }
    // Envelope timer is reset.
    data->env_timers[2] = 0;
    // Volume is set to contents of NR42 initial volume.
    data->vols[3] = (NR42(mem) >> 4) & 0x0F;
    // LFSR bits are reset.
    data->lfsr = 0;
}

//I dislike having the two period parameters. Look at how to change this
void clock_pulse(apu_data *data, memory *mem/*, uint16_t period_1, uint16_t period_2 */){
    for(int i = 0; i<2; i++){
        uint16_t period =( ((NRX4(mem, i)) << 8) | NRX3(mem, i)) & 0x7FF;
        uint8_t test = (NRX1(mem, i) >> 6) & 0x03;
        // if(i == 0){
        //     period = NR;
        // } else{
        //     period = period_2;
        // }
        if(data->pulse_divs[i] == 0x7FF){
            data->pulse_divs[i] = period; 
            bool is_high = waveform[test][data->pulse_pos[i]];
            if(is_high){
                data->pulse_amps[i] = data->vols[i];
            } else{
                data->pulse_amps[i] = 0;
            }
            data->pulse_pos[i]++;
            data->pulse_pos[i] %= 8;
        } else{
            data->pulse_divs[i]++;
        }
    }
}

void clock_wave(apu_data *data, memory *mem){
    if(data->ch3_div == 0x7FF){
        //Wave RAM is read left to right sample-wise so the 2nth sample is the upper 4 bits of the nth byte of WRAM
        char is_high_bits = 1 - (data->ch3_pos & 1);
        uint8_t reg = data->ch3_pos/2;
        uint8_t sample = (IOREG(mem, 0xFF30 + reg) >> 4 * is_high_bits) & 0x0F;
        uint8_t volume = (NR32(mem) > 5) & 3; 
        if(volume == 0){
            data->ch3_amp = 0;
        } else {
            // printf("hi\n");
            // fflush(stdout);
            data->ch3_amp = sample >> (volume-1);
        }
        data->ch3_pos++;
        data->ch3_pos &= 0x1F;
        data->ch3_div = (((uint16_t) NR34(mem)) << 8 | NR33(mem)) & 0x7FF;
    } else{
        data->ch3_div++;
    }
}


void apu_div_actions(apu_data *data, cpu *CPU, memory *mem){
    if(!(data->DIV_APU & 1)){
        /*If the functionality is enabled, a channel’s length timer ticks up at 256 Hz (tied to DIV-APU) from the value it’s initially set at.
        When the length timer reaches 64 (CH1, CH2, and CH4) or 256 (CH3), the channel is turned off.
        
        Internally, the length timer is inverted when written, and that ticks down until it reaches 0. But the effect is as if the counter ticked up.*/
        for (int i = 0; i<4; i++){
            uint16_t max = (i == 2) ? 256 : 64;
            if((CPU->length_enable[i]) && (data->timers[i] < max)){
                (data->timers[i])++;
                if (data->timers[i] == max){
                    data->dacs[i] = 0;
                    CPU->length_enable[i] = false;
                }
            }
        }
    }

    //Ch1 sweep occurs in units of 128Hz
    // Channel 1 sweep functionality
    if((data->DIV_APU % 4) == 0){

    /*
    This code seems unnecessary. Its purpose is to initialise the sweep functionality but 
    this already happens upon triggering the channel

    if(data->sweep_check){//
        data->sweep_pace = 0x07 & (NR10(mem) >> 4);
        data->sweep_check = 0;
        data->sweep_pos = 0;
    } else{*/
        uint16_t ch1_period =( (NR14(mem) << 8) | NR13(mem)) & 0x7FF;
        if(data->sweep_pace != 0){
            data->sweep_pos++;
            if(data->sweep_pos >= data->sweep_pace){
                uint16_t change = ch1_period/(1<< (NR10(mem) & 0x07));
                ch1_period = NR10(mem) & 0x08 ? ch1_period + change : ch1_period - change;
                if (ch1_period > 0x7FF)
                    data->dacs[0] = 0;
                NR13(mem) = ch1_period;
                NR14(mem) &= 0xF8;
                NR14(mem) |= (ch1_period >> 8) & 0x07;
            }
        } else{
            //while a pace of 0 disables the sweep functionality, it still checks for an overflow
            if(((2 * ch1_period) > 0x7FF) && (NR10(mem) & 0x08)){
                data->dacs[0] = 0;
            }
        }

    }

    if((data->DIV_APU % 8) == 0){//64Hz
        // Envelope sweep volume function
        // Ch3 does not have this functionality 
        for (int i = 0; i < 3; i++){
            //getting the correct index for the channels' envelope function
            int j = (i == 2) ? 3 : i;
            
            //If NRX2 has bits 0-2, the sweep pace, all set to 0, the envelope functionality is disabled
            if(NRX2(mem, j) & 0x07){
                //The volume is adjusted every "sweep pace" calls to this if statement
                data->env_timers[i]++;
                if(data->env_timers[i] == (NRX2(mem, j) & 0x7)){
                    data->env_timers[i] = 0;
                    if((NRX2(mem,j) & 0x8) && (data->vols[j] < 4)){
                        data->vols[j]++;
                    }
                    else if(!(NRX2(mem,j) & 0x8) && (data->vols[j] > 0)){
                        data->vols[j]--;
                    }
                    
                }
            }

        }
    }
}




/*

Information on DACs

Channel x’s DAC is enabled if and only if [NRx2] & $F8 != 0; the exception is CH3, 
whose DAC is directly controlled by bit 7 of NR30 instead. 
Note that the envelope functionality changes the volume, 
but not the value stored in NRx2, and thus doesn’t disable the DACs.

If a DAC is enabled, the digital range $0 to $F is linearly translated 
to the analog range -1 to 1, in arbitrary units. 
Importantly, the slope is negative: “digital 0” maps to “analog 1”, not “analog -1”.

If a DAC is disabled, it fades to an analog value of 0, which corresponds to “digital 7.5”. 
The nature of this fade is not entirely deterministic and varies between models.

NR52’s low 4 bits report whether the channels are turned on, not their DACs.

A DAC may be enabled even if its corresponding channel is not.  A disabled channel outputs 0, which an enabled DAC will dutifully convert into “analog 1”. 
The opposite is not true. Disabling a DAC also disables the corresponding channel

*/



/*TO DO

*Reexamine how the channel one sweep works using the explanation given in the Audio Details section.  

*Work out the differences between the DAC and the channel itself

*Refactor much of the code, start after the length timers

*Trigger flags, move away from CPU

*Sort out how the volume is meant to work

*Send the data to SDL using some kind of accumulator and the  SDL_queue_audio() function

*Initial value for the amplitude

*The 5x registers, master control

*/
// void apu(memory *mem, apu_data *data, cpu *CPU/*I only need the frame timer, should probably take this out of the CPU struct*/){

//     // if(NR52(mem) & (1<<7)){
//         uint16_t ch1_period = 0x07FF & ((NR14(mem) << 8) | NR13(mem));
//         uint16_t ch2_period = 0x07FF & ((NR24(mem) << 8) | NR23(mem));
//         // uint16_t ch3_period = 0x07FF & ((NR34(mem) << 8) | NR33(mem));
//         // uint16_t ch4_period = 0x07FF & ((NR44(mem) << 8) | NR43(mem));

        

//         // for(int i = 0; i<4; i++){
//         //     if(CPU->audio_triggers[i]){
//         //         CPU->audio_triggers[i] = 0;
//         //         if(data->dacs[i]){
//         //             switch (i){
//         //                 case 2:
//         //                     trigger_wave(data,mem);
//         //                     break;
//         //                 case 3:
//         //                     trigger_noise(data,mem);
//         //                     break;
//         //                 default:
//         //                     trigger_pulse(data, mem, i);
//         //             }
//         //             CPU->audio_triggers[i] = 0;
//         //         }
//         //     }
//         // }

//         //DAC control
//         //PanDocs: Channel x’s DAC is enabled if and only if [NRx2] & $F8 != 0; 
//         //the exception is CH3, whose DAC is directly controlled by bit 7 of NR30 instead. 
//         // for(int i = 0; i < 3; i++){
//         //     int j = (i == 2) ? 3 : i;
//         //     if((NRX2(mem, j) & 0xF8) == 0){
//         //         data->dacs[j] = 0;
//         //         data->channel_status[j] = 0;
//         //     } else{
//         //         data->dacs[j] = 1;
//         //     }
//         // }

//         // if((NR30(mem) & 0x70) == 0){
//         //     data->dacs[2] = 0;
//         //     data->channel_status[2] = 0;
//         // } else {
//         //     data->dacs[2] = 1;
//         // }

//         // for(int i = 0; i<4; i++){
//         //     uint8_t mask = (1<<i);
//         //     NR52(mem) &= ~mask;
//         //     if(data->channel_status[i]){
//         //         NR52(mem) |= mask;
//         //     }
//         // }

//         //The DIV_APU is incremented at a rate of 512HZ
//         //At the moment, this is not the case since the system is only stalled to align with each frame ~60Hz. This may cause issues but should be fine

//         //The rate at which the following actions occur is dependant on DIV_APU
//         if(data->DIV_APU != data->prev_DIV){//DIV increased. It can only increase by 1
//             data->prev_DIV = data->DIV_APU;
//             if(!(data->DIV_APU & 1)){
//                 /*If the functionality is enabled, a channel’s length timer ticks up at 256 Hz (tied to DIV-APU) from the value it’s initially set at.
//                 When the length timer reaches 64 (CH1, CH2, and CH4) or 256 (CH3), the channel is turned off.
                
//                 Internally, the length timer is inverted when written, and that ticks down until it reaches 0. But the effect is as if the counter ticked up.*/
//                 for (int i = 0; i<4; i++){
//                     uint16_t max = (i == 2) ? 256 : 64;
//                     if((CPU->length_enable[i]) && (data->timers[i] < max)){
//                         (data->timers[i])++;
//                         if (data->timers[i] == max){
//                             data->dacs[i] = 0;
//                             CPU->length_enable[i] = false;
//                         }
//                     }
//                 }
//             }

//             //Ch1 sweep occurs in units of 128Hz
//             // Channel 1 sweep functionality
//             if((data->DIV_APU % 4) == 0){

//             /*
//             This code seems unnecessary. Its purpose is to initialise the sweep functionality but 
//             this already happens upon triggering the channel

//             if(data->sweep_check){//
//                 data->sweep_pace = 0x07 & (NR10(mem) >> 4);
//                 data->sweep_check = 0;
//                 data->sweep_pos = 0;
//             } else{*/
//                 if(data->sweep_pace != 0){
//                     data->sweep_pos++;
//                     if(data->sweep_pos >= data->sweep_pace){
//                         uint16_t change = ch1_period/(1<< (NR10(mem) & 0x07));
//                         ch1_period = NR10(mem) & 0x08 ? ch1_period + change : ch1_period - change;
//                         if (ch1_period > 0x7FF)
//                             data->dacs[0] = 0;
//                         NR13(mem) = ch1_period;
//                         NR14(mem) &= 0xF8;
//                         NR14(mem) |= (ch1_period >> 8) & 0x07;
//                     }
//                 } else{
//                     //while a pace of 0 disables the sweep functionality, it still checks for an overflow
//                     if(((2 * ch1_period) > 0x7FF) && (NR10(mem) & 0x08)){
//                         data->dacs[0] = 0;
//                     }
//                 }

//             }

//             if((data->DIV_APU % 8) == 0){//64Hz
//                 // Envelope sweep volume function
//                 // Ch3 does not have this functionality 
//                 for (int i = 0; i < 3; i++){
//                     //getting the correct index for the channels' envelope function
//                     int j = (i == 2) ? 3 : i;
                    
//                     //If NRX2 has bits 0-2, the sweep pace, all set to 0, the envelope functionality is disabled
//                     if(NRX2(mem, j) & 0x07){
//                         //The volume is adjusted every "sweep pace" calls to this if statement
//                         data->env_timers[i]++;
//                         if(data->env_timers[i] == (NRX2(mem, j) & 0x7)){
//                             data->env_timers[i] = 0;
//                             if((NRX2(mem,j) & 0x8) && (data->vols[j] < 4)){
//                                 data->vols[j]++;
//                             }
//                             else if(!(NRX2(mem,j) & 0x8) && (data->vols[j] > 0)){
//                                 data->vols[j]--;
//                             }
                            
//                         }
//                     }

//                 }
//             }

//         }

//         //Pandocs: The pulse channels’ period dividers are clocked at 1048576 Hz, once per four dots
//         //CPU->frame_timer is incrememtned every dot, doesn't really conceptually belong to CPU. Should change 
//         if ((CPU->frame_timer % 4) == 0){ 
//          //   clock_pulse(data, mem, ch1_period, ch2_period);
//         }


//         if((CPU->frame_timer % 2) == 0){
//             clock_wave(data, mem);
//         }


//         //This way of clocking the noise channel seems hazardous. What if NR43 changes at an inopportune time?
        // uint8_t shift = (NR43(mem) >> 4) & 0x0F;
        // bool ch4_clock = (~NR43(mem)) & 0xE0;
        // //shift being equal to 14 or 15 stops the channel from being clocked entirely.
        // if(shift < 14){
        //     uint8_t divider = NR43(mem) & 0x07;
        //     //This calculation gives the number of dots for each lfsr clock. It is derived from info about the freq of the lfsr clock on PanDocs
        //     uint32_t lfsr_freq = divider ?  (16 * divider * (1<<shift)) : (8 * (1<<shift)); 
        //     if((CPU->frame_timer % lfsr_freq) == 0){
        //         data->ch4_amp = lfsr_step(data, mem) * data->vols[3];
        //     }
        // }
//     // } else {
//     //     for(int i = 0; i<4; i++){
//     //         data->dacs[i] = 0;
//     //         data->timers[i] = 0;
//     //     }
//     // }


// }


// will be made better
int16_t get_sample_left(apu_data *data, memory *mem){

    if(NR50(mem) & (1<<7))  return 0;

    int16_t ch1 = data->dacs[0] && (NR51(mem) & (1<<4)) ? data->pulse_amps[0] : 0;
    ch1 = 15 - 2*ch1;  

    int16_t ch2 = data->dacs[0] && (NR51(mem) & (1<<5)) ? data->pulse_amps[1] : 0;
    ch2 = 15 - 2*ch2;

    int16_t ch3 = data->dacs[0] && (NR51(mem) & (1<<6)) ? data->pulse_amps[2] : 0;
    ch3 = 15 - 2*ch3;

    int16_t ch4 = data->dacs[0] && (NR51(mem) & (1<<7)) ? data->pulse_amps[3] : 0;
    ch4 = 15 - 2*ch4;

    uint8_t volume = ((NR50(mem) & 0x70) >> 4) + 1;

    int16_t sample = (
                      ch1
                      +
                      ch2
                      +
                      ch3
                      +
                      ch4
                      ) * volume
                    ;
    return sample;
}

int16_t get_sample_right(apu_data *data, memory *mem){
    if(NR50(mem) & (1<<3))  return 0;

    int16_t ch1 = data->dacs[0] && (NR51(mem) & (1)) ? data->pulse_amps[0] : 0;
    ch1 = 15 - 2*ch1;  

    int16_t ch2 = data->dacs[0] && (NR51(mem) & (1<<1)) ? data->pulse_amps[1] : 0;
    ch2 = 15 - 2*ch2;

    int16_t ch3 = data->dacs[0] && (NR51(mem) & (1<<2)) ? data->pulse_amps[2] : 0;
    ch3 = 15 - 2*ch3;

    int16_t ch4 = data->dacs[0] && (NR51(mem) & (1<<3)) ? data->pulse_amps[3] : 0;
    ch4 = 15 - 2*ch4;
    
    uint8_t volume = ((NR50(mem) & 0x07) >> 4) + 1;

    int16_t sample = (
                      ch1
                      +
                      ch2
                      +
                      ch3
                      +
                      ch4
                      ) 
                      * volume
                      ;
    return sample;
}

//The duty cycle setting affects the phase of the wave in the following way. 
//The period is split into eigths
//00 has the last eighth low
//01 has have first and last low
//10 has the first and last 3 low
//11 has 2-7 low. 
//Need to find out what part of the wave I'm at to be able to apply this. 

//from PanDocs:
//Period changes (written to NR13 or NR14) only take effect after the current “sample” ends; see description above.


//M = sample rate of SDL 
//Make samples in batches of 512 or some other power of 2.
//Each sample gives the data of what the output was at that partucular 1/M'th of a second
//"What is the amplitude at this given point in time?" 

//Strategy is to send a packet of samples to SDL's audio system every so often

//The period divider of pulse and wave channels is an up counter. Each time it is clocked, its value increases by 1; 
//when it overflows (being clocked when it’s already 2047, or $7FF), its value is set from the contents of NR13 and NR14

//The obscure behaviour section has a high-pass filter explanation 

//Code graveyrad

/*

    *The first is the waveform of the pulse channels before the introduction of the 2D array.

    switch(test){//make this into a  2D array
        case 0:{
            if(data->pulse_pos[i] == 7){
                is_high = 0;
            }
            break;
        }
        case 1:{
            if((data->pulse_pos[i] == 7) || (data->pulse_pos[i] == 0) ){
                is_high = 0;
            }
            break;
        }
        case 2:{
            if((data->pulse_pos[i] == 7) || (data->pulse_pos[i] == 0) || (data->pulse_pos[i] == 5) || (data->pulse_pos[i] == 6) ){
                is_high =0;
            }
            break;
        }
        case 3:{
            if(!((data->pulse_pos[i] == 7) || (data->pulse_pos[i] == 0)) ){
                is_high = 0;
            }
            break;
        }
    }


    *the functions related to the length timer

    if(NR14(mem) & (1 << 6)){
        data->data->ch1_timer++;
        if (data->ch1_timer == 64){
            data->ch1_dac = 0;
        }
    }
    if(NR24(mem) & (1 << 6)){
        data->ch2_timer++;
        if (data->ch2_timer == 64){
            data->ch2_dac = 0;
        }
    }
    if(NR34(mem) & (1 << 6)){
        data->ch3_timer++;
        if (data->ch3_timer == 256/*overflow of 8 bit number){
            data->ch3_dac = 0;
        }
    }
    if(NR41(mem) & (1 << 6)){
        data->ch4_timer++;
        if (data->ch4_timer == 64){
            data->ch4_dac = 0;
        }
    }

    *volume envelope
    
    if(NR12(mem) & 0x07){
        //The volume is adjusted every "sweep pace" calls to this if statement
        data->ch1_env++;
        if(data->ch1_env == (NR12(mem) & 0x7)){
            data->ch1_env = 0;
            if((NR12(mem) & 0x8) && (data->ch1_vol < 4)){
                data->ch1_vol++;
            }
            if(!(NR12(mem) & 0x8) && (data->ch1_vol > 0)){
                data->ch1_vol--;
            }
            
        }
    }
*/