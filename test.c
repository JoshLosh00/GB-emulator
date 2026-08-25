#include "emulator.h"
#include <string.h>

static void dma_handler(void)//from RP2040-LCD-LVGL/examples/src/LVGL_example.c
{
    if (dma_channel_get_irq0_status(dma_tx)) {
        dma_channel_acknowledge_irq0(dma_tx);
        DEV_Digital_Write(LCD_CS_PIN, 1);
        dma_finish = true;
    }
}

static void LCD_1IN3_KEY_Init()
{
    DEV_KEY_Config(LCD_KEY_A);
    DEV_KEY_Config(LCD_KEY_B);
    DEV_KEY_Config(LCD_KEY_X);
    DEV_KEY_Config(LCD_KEY_Y);
    DEV_KEY_Config(LCD_KEY_UP);
    DEV_KEY_Config(LCD_KEY_DOWN);
    DEV_KEY_Config(LCD_KEY_LEFT);
    DEV_KEY_Config(LCD_KEY_RIGHT);
    DEV_KEY_Config(LCD_KEY_CTRL);
}

#include <stdio.h>
#include "pico/stdlib.h"
 
volatile bool timer_fired = false;
volatile int i; 

int64_t alarm_callback(alarm_id_t id, __unused void *user_data) {
    printf("Timer %d fired!\n", (int) id);
    timer_fired = true;
    // Can return a value here in us to fire in the future
    return 0;
}
 
bool repeating_timer_callback(__unused struct repeating_timer *t) {
    printf("Repeat at %lld\n", time_us_64());
    i++;
    i &= 3;
    return true;
}
 
// int main() {
//     stdio_init_all();
//     printf("Hello Timer!\n");
 
//     // Call alarm_callback in 2 seconds
//     add_alarm_in_ms(2000, alarm_callback, NULL, false);
 
//     // Wait for alarm callback to set timer_fired
//     while (!timer_fired) {
//         tight_loop_contents();
//     }
 
//     // Create a repeating timer that calls repeating_timer_callback.
//     // If the delay is > 0 then this is the delay between the previous callback ending and the next starting.
//     // If the delay is negative (see below) then the next call to the callback will be exactly 500ms after the
//     // start of the call to the last callback
//     struct repeating_timer timer;
//     add_repeating_timer_ms(500, repeating_timer_callback, NULL, &timer);
//     sleep_ms(3000);
//     bool cancelled = cancel_repeating_timer(&timer);
//     printf("cancelled... %d\n", cancelled);
//     sleep_ms(2000);
 
//     // Negative delay so means we will call repeating_timer_callback, and call it again
//     // 500ms later regardless of how long the callback took to execute
//     add_repeating_timer_ms(-500, repeating_timer_callback, NULL, &timer);
//     sleep_ms(3000);
//     cancelled = cancel_repeating_timer(&timer);
//     printf("cancelled... %d\n", cancelled);
//     sleep_ms(2000);
//     printf("Done\n");
//     return 0;
// }

int main(){

    struct repeating_timer timer;
    add_repeating_timer_ms(500, repeating_timer_callback, NULL, &timer);

    if(DEV_Module_Init()!=0){
        return -1;
    }
    
    /*KEY Init*/
    LCD_1IN3_KEY_Init();

    /*LCD Init*/
    LCD_1IN3_Init(HORIZONTAL);
    LCD_1IN3_Clear(WHITE);

    //dma_init
    dma_channel_set_irq0_enabled(dma_tx, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    static const uint16_t DMGcolours[4] = {
    0x1111,  // lightest
    0x4444,  // light
    0x4567,  // dark
    0x9876   // darkest
    };

    uint16_t buffer[144*160];

    while(1){
        memset(buffer , DMGcolours[i], 160*144*2);
        while(!dma_finish){}
        LCD_1IN3_DisplayWindows(0, 0, 160, 144, buffer);
        DEV_Delay_ms(500);

    }
}