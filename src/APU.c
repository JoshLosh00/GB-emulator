#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "emulator.h"

//A possible bug is in the channel triggers. At present, setting bit 7 of NRX4 to 1 when it was previously 0 does not trigger the channel.
//This is how it's supposed to work according to PanDocs "Writing any value to NRX4 with bit 7 set triggers the channel"
//But it is possible that writing 1 to bit 7 also causes a channel trigger


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



// will be made better
uint16_t get_sample_left(apu_data *data, memory *mem){

    if(NR50(mem) & (1<<7))  return 0;

    int16_t ch1 = data->dacs[0] && (NR51(mem) & (1<<4)) ? data->pulse_amps[0] : 0;
    ch1 = 15 - 2*ch1; 
    int16_t ch2 = data->dacs[1] && (NR51(mem) & (1<<5)) ? data->pulse_amps[1] : 0;
    ch2 = 15 - 2*ch2; 
    int16_t ch3 = data->dacs[2] && (NR51(mem) & (1<<6)) ? data->pulse_amps[2] : 0;
    ch3 = 15 - 2*ch3; 
    int16_t ch4 = data->dacs[3] && (NR51(mem) & (1<<7)) ? data->pulse_amps[3] : 0;
    ch4 = 15 - 2*ch4;  

    uint8_t volume = ((NR50(mem) & 0x70) >> 4) + 1;

    uint16_t sample = (
                      ch1
                      +
                      ch2
                      +
                      ch3
                      +
                      ch4
                      ) 
                    //   * volume
                    ;
    return sample;
}

uint16_t get_sample_right(apu_data *data, memory *mem){
    if(NR50(mem) & (1<<3))  return 0;

    // uint16_t ch1 = (NR51(mem) & (1<<0)) ? data->pulse_amps[0] : 0; 
    int16_t ch1 = data->dacs[0] && (NR51(mem) & (1)) ? data->pulse_amps[0] : 0;
    ch1 = 15 - 2*ch1; 
    int16_t ch2 = data->dacs[1] && (NR51(mem) & (1<<1)) ? data->pulse_amps[1] : 0;
    ch2 = 15 - 2*ch2; 
    int16_t ch3 = data->dacs[2] && (NR51(mem) & (1<<2)) ? data->pulse_amps[2] : 0;
    ch3 = 15 - 2*ch3; 
    int16_t ch4 = data->dacs[3] && (NR51(mem) & (1<<3)) ? data->pulse_amps[3] : 0;
    ch4 = 15 - 2*ch4; 

    // uint16_t ch2 = (NR51(mem) & (1<<1)) ? data->pulse_amps[1] : 0; 
    // uint16_t ch3 = (NR51(mem) & (1<<2)) ? data->ch3_amp : 0; 
    // uint16_t ch4 = (NR51(mem) & (1<<3)) ? data->ch4_amp : 0; 

    uint8_t volume = ((NR50(mem) & 0x07) >> 4) + 1;

    uint16_t sample = (
                      ch1 
                      +
                      ch2 
                      +
                      ch3 
                      +
                      ch4 
                      ) 
                    //   * volume
                      ;
    return sample;
}

void audiooff(apu_data *data, memory *mem){
    data->on = 0;
    for(int i = 0; i<4; i++){
        NRX1(mem, i) = 0;
        NRX2(mem, i) = 0;
        NRX3(mem, i) = 0;
        NRX4(mem, i) = 0;
        data->dacs[i] = 0;
        data->timers[i] = 0;
        data->vols[i] = 0;
    }
    data->pulse_amps[0] = 0;
    data->pulse_amps[1] = 0;
    data->ch3_amp = 0;
    data->ch4_amp = 0;

    //make the other registers read only
}