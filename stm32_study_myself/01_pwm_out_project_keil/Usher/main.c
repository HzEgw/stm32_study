#include"stm32f10x.h"
#include"pwm.h"
#include "Delay.h"
int main()
{
	pwm_init();
	while(1){
		
for (uint16_t i = 0; i < 100; i++)
{
   PWM_SetCompare2(i);
	Delay_ms(20);
}
for (uint16_t i = 100; i >0; i--)
{
   PWM_SetCompare2(i);
	Delay_ms(20);
}

	}

}