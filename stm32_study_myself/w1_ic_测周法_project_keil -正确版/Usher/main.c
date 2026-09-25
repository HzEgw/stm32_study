#include "stm32f10x.h"
#include "IC.h"
#include "PWM.h"
void LED_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}
int main(void)
{
PWM_Init();
IC_INIT();
LED_Init();
while(1)
{
  uint32_t freq = IC_GetFreq();
	if(freq > 900 && freq < 1100) // 1000Hz，放宽一点容错
		{
			GPIOA->BSRR = (1U << 11);   //PA11置高
		}
		else
		{
			GPIOA->BSRR = (1U << (11 + 16)); //PA11置低
		}
}


}
