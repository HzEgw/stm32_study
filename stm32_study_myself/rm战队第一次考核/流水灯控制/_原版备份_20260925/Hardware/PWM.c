#include "stm32f10x.h"
void PWM_INIT(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
TIM_TimeBaseInitTypeDef TIM_TimBaseStructure;
	TIM_TimeBaseStructInit(&TIM_TimBaseStructure);
	TIM_TimBaseStructure.TIM_CounterMode= TIM_CounterMode_Up;
	TIM_TimBaseStructure.TIM_Period=100-1;
	TIM_TimBaseStructure.TIM_Prescaler=720;
	TIM_TimBaseStructure.TIM_ClockDivision=0;
	TIM_TimeBaseInit(TIM2,&TIM_TimBaseStructure);
	TIM_ClearFlag(TIM2,TIM_FLAG_Update);
TIM_OCInitTypeDef TIM_OCInitStructure;
	TIM_OCStructInit(&TIM_OCInitStructure);
	TIM_OCInitStructure.TIM_OCMode=TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OCPolarity=TIM_OCPolarity_High;
	TIM_OCInitStructure.TIM_OutputState=ENABLE;
	TIM_OCInitStructure.TIM_Pulse=0;
	TIM_OC2Init(TIM2,&TIM_OCInitStructure);
	TIM_OC3Init(TIM2,&TIM_OCInitStructure);
	TIM_OC4Init(TIM2,&TIM_OCInitStructure);
TIM_Cmd(TIM2,ENABLE);
}
void PWM_SETCPMPARA_OC2(uint16_t ccr)
{
TIM_SetCompare2(TIM2, ccr);
}
void PWM_SETCPMPARA_OC3(uint16_t ccr)
{
TIM_SetCompare3(TIM2, ccr);
}
void PWM_SETCPMPARA_OC4(uint16_t ccr)
{
TIM_SetCompare4(TIM2, ccr);
}
