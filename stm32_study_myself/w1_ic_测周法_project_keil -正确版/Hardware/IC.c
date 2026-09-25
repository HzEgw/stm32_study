#include"stm32f10x.h"
//本次目标是为了开启一个频率是1000hz的时钟，时用tim2定时器输出pwm信号，用tim3定时器来完成输入捕获动作
//本节为IC代码,标准频率是1mhz
 
 void IC_INIT()
 {
 //开启时钟
 RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3,ENABLE);
 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
 //配置引脚,ch1,PA6,TIM3
 GPIO_InitTypeDef GPIO_InitStructure;
 GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPU;
 GPIO_InitStructure.GPIO_Pin=GPIO_Pin_6;
 GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
 GPIO_Init(GPIOA,&GPIO_InitStructure);
 //配置时基单元
 TIM_InternalClockConfig(TIM3);
 TIM_TimeBaseInitTypeDef TIM_TimBaseInitStructure;
 TIM_TimBaseInitStructure.TIM_CounterMode=TIM_CounterMode_Up;	
 TIM_TimBaseInitStructure.TIM_Period=65536-1;
 TIM_TimBaseInitStructure.TIM_Prescaler=72-1;
 TIM_TimBaseInitStructure.TIM_ClockDivision=TIM_CKD_DIV1;
 TIM_TimBaseInitStructure.TIM_RepetitionCounter =0 ;	
 TIM_TimeBaseInit(TIM3,&TIM_TimBaseInitStructure);
 //配置ic单元
 TIM_ICInitTypeDef TIM_ICInitStructure;
 TIM_ICInitStructure.TIM_Channel=TIM_Channel_1;
 TIM_ICInitStructure.TIM_ICFilter=0XF;//输入滤波器参数，可以过滤信号抖动
 TIM_ICInitStructure.TIM_ICPolarity=TIM_ICPolarity_Rising;//选择什么沿触发
 TIM_ICInitStructure.TIM_ICPrescaler=TIM_ICPSC_DIV1;
 TIM_ICInitStructure.TIM_ICSelection=TIM_ICSelection_DirectTI;//选择是什么链接，只有ch1和ch2可以交叉链接
 TIM_ICInit(TIM3,&TIM_ICInitStructure);
 
 //选择从模式，自己给自己信号，信号选择来源是TI1FP1
  TIM_SelectInputTrigger(TIM3, TIM_TS_TI1FP1);					//触发源选择TI1FP1
  TIM_SelectSlaveMode(TIM3, TIM_SlaveMode_Reset);	
 
 TIM_Cmd(TIM3, ENABLE); 
 }
 uint32_t IC_GetFreq(void)
{
	
        uint16_t cap = TIM_GetCapture1(TIM3);
        
        return 1000000U / cap;
   
	 
}
