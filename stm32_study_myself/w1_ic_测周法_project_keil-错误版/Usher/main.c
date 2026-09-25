 #include "stm32f10x.h" 
 #include "IC.h"
 #include "pwm.h"
 
 int main()
 {
   pwm_init();	 
   IC_Init();
	 
	PWM_SetPrescaler(720 - 1);					//PWM频率Freq = 72M / (PSC + 1) / 100
	PWM_SetCompare1(50);
	 
 }
 
 