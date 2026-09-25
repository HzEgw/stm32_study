#include "stm32f10x.h"
#include "LED.h"
#include "PWM.h"
#include "KEY.h"
int16_t key_mode=0;
int main(void)
{
KEY_INIT();

while(1)
{      
	LED_RUN_Current(key_mode);
	key_run();


}	
	
}

