#include "stm32f10x.h"

void pwm_init()
{
//配置的第一步，先打开时钟
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);

//设置引脚pwm
	GPIO_InitTypeDef GPIO_InitStruct;
	GPIO_InitStruct.GPIO_Mode =GPIO_Mode_AF_PP;//复用输出模式，pwm输出
	GPIO_InitStruct.GPIO_Pin=GPIO_Pin_0 ;//0c1通道输出
	GPIO_InitStruct.GPIO_Speed =GPIO_Speed_50MHz;//决定我的电平变化是否陡峭
	GPIO_Init(GPIOA,&GPIO_InitStruct);
//设置时基单元
	TIM_InternalClockConfig(TIM2);
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStruct;
//提前设定结构体，目的是为了自动补全高级定时我没用到的但是代码函数需要用到的参数
	TIM_TimeBaseStructInit(&TIM_TimeBaseInitStruct);
	TIM_TimeBaseInitStruct.TIM_CounterMode=TIM_CounterMode_Up;
	TIM_TimeBaseInitStruct.TIM_Period =65536-1;//arr重装寄存器
	TIM_TimeBaseInitStruct.TIM_Prescaler = 72-1;//设置标准的时钟fc是1mhz，psc
	TIM_TimeBaseInitStruct.TIM_ClockDivision =0;//不分频
	TIM_TimeBaseInit(TIM2,&TIM_TimeBaseInitStruct);    
//消除软件的egr的ug带来的sr的uif标志位是1的情况
    TIM_ClearFlag(TIM2,TIM_FLAG_Update);
//设置oc单元
	TIM_OCInitTypeDef TIM_OCInitStruct;
//提前设置结构体，目的是为了自动补全高级定时我没用到的但是代码函数需要用到的参数
	TIM_OCStructInit(&TIM_OCInitStruct);
	TIM_OCInitStruct.TIM_OCMode=TIM_OCMode_PWM1;
	TIM_OCInitStruct.TIM_OCNPolarity = TIM_OCPolarity_High ;
	TIM_OCInitStruct.TIM_OutputState = TIM_OutputState_Enable;
	//TIM_OCInitStruct.TIM_OCIdleState = ;设置死区，这里我用不到
	TIM_OCInitStruct.TIM_Pulse =0; //ccr
	TIM_OC2Init(TIM2,&TIM_OCInitStruct);
//开启计数器
    TIM_Cmd(TIM2, ENABLE);	
}
void PWM_SetCompare1(uint16_t Compare)
{
	TIM_SetCompare1(TIM2, Compare);		//设置CCR1的值
}
void PWM_SetPrescaler(uint16_t Prescaler)
{
	TIM_PrescalerConfig(TIM2, Prescaler, TIM_PSCReloadMode_Immediate);		//设置PSC的值
}
