#include "pwm.h"
#include"stm32f10x.h"

void pwm_init()
{
//写程序之前我应该先打开外设时钟，我先写一个oc口输出的
RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);//看了引脚定义，发现PA1,2,3分别是ch2，3，4的复用功能引脚，但是PA1对应的是ch的etr？etr和cch为什么会扯上关系所以三个输出我优先选择123引脚

//我现在应该定义一下引脚
GPIO_InitTypeDef GPIO_Struct_INIT;
GPIO_Struct_INIT.GPIO_Mode=GPIO_Mode_AF_PP;//复用推挽输出模式
GPIO_Struct_INIT.GPIO_Pin=GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3;
GPIO_Struct_INIT.GPIO_Speed=GPIO_Speed_50MHz;//这里我的输出已经被我的oc接管了，这里填入的数值应该是一个无效数值？
GPIO_Init(GPIOA,&GPIO_Struct_INIT);//这个函数是通过bsrr写如到的odr，那么复用模式下，cpu还能直接写入吗，毕竟引脚都被oc接管了，那跟新odr是不是硬件跟新，不再需要软件了？
//   if (GPIO_InitStruct->GPIO_Mode == GPIO_Mode_IPD)
//         {
//           GPIOx->BRR = (((uint32_t)0x01) << (pinpos + 0x08));
//         }
//         /* Set the corresponding ODR bit */
//         if (GPIO_InitStruct->GPIO_Mode == GPIO_Mode_IPU)
//         {
//           GPIOx->BSRR = (((uint32_t)0x01) << (pinpos + 0x08));
//         }注意到源码的代码有一个关键判断条件，我可以判断出bsrr不会被cpu启动，所以odr的数值应该是由pwm硬件直接接管



//引脚设置好了，我下一步应该是设置tim计时器了
TIM_TimeBaseInitTypeDef  TIM_Struct_Init;
TIM_Struct_Init.TIM_ClockDivision= 0x0000;//这里应该说的是死区，但是我用的是普通定时器，死区暂时不考虑
TIM_Struct_Init.TIM_CounterMode=TIM_CounterMode_Up;
TIM_Struct_Init.TIM_Period=100;
TIM_Struct_Init.TIM_Prescaler=720;//因为我的输入已经是确定了是rcc内部时钟，所以我这里生成的是1khz的pwm输出时钟

//写到这里，我意识到我好像没有选择输入源我忘记api是什么了，让我看看
TIM_InternalClockConfig(TIM2);//选择内部的72mhz时钟，虽然写的顺序有点错误，但是足够醒目

TIM_TimeBaseInit(TIM2,&TIM_Struct_Init);//时基单元部分完成了
//TIMx->EGR = TIM_PSCReloadMode_Immediate;  这里的egr被调用了，那么就会因为软件产生一次跟新事件，为了不产生中断事件，我应该将此处的标志位去除
TIM_ClearFlag(TIM2, TIM_FLAG_Update);
//现在来设置pwm输出
TIM_OCInitTypeDef TIM_OC_INIT;
TIM_OCStructInit(&TIM_OC_INIT);
TIM_OC_INIT.TIM_OCMode=TIM_OCMode_PWM1;//设置为pwm1的模式
TIM_OC_INIT.TIM_OutputState=TIM_OutputState_Enable ;//允许输出
TIM_OC_INIT.TIM_OCPolarity=TIM_OCPolarity_High ;//这个应该是极性选择，我这里就不选择翻转了
TIM_OC_INIT.TIM_Pulse=30;//占空比设置成30
//注意到我还有很多没有设置，那是高级定时器带有的，我应该把没有设置的让软件帮我自动补全,顺序要在上面
TIM_OC2Init(TIM2,&TIM_OC_INIT);
TIM_OC3Init(TIM2,&TIM_OC_INIT);
TIM_OC4Init(TIM2,&TIM_OC_INIT);
//发现我的计时器还没有打开
TIM_Cmd(TIM2, ENABLE);

}
void PWM_SetCompare2(uint16_t Compare)
{
	TIM_SetCompare2(TIM2, Compare);
}
void PWM_SetCompare3(uint16_t Compare)
{
	TIM_SetCompare3(TIM2, Compare);
}
void PWM_SetCompare4(uint16_t Compare)
{
	TIM_SetCompare4(TIM2, Compare);
}
void delay_ms(uint32_t ms)
{
	uint32_t i,j;
	for(i=0;i<ms;i++)
		for(j=0;j<7200;j++);
}
