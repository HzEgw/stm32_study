#include"stm32f10x.h"
 
void IC_Init()
{
//开启时钟
	 RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
	 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
//设置引脚
	 GPIO_InitTypeDef GPIO_InitStruct;
	 GPIO_InitStruct.GPIO_Mode =GPIO_Mode_IPU;//输入模式
	 GPIO_InitStruct.GPIO_Pin=GPIO_Pin_1 ;//ic2通道输入
	 GPIO_InitStruct.GPIO_Speed =GPIO_Speed_50MHz;//决定我的电平变化是否陡峭，但是输入模式没用
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
//设置ic单元
	TIM_ICInitTypeDef TIM_ICStruct;
	TIM_ICStruct.TIM_Channel=TIM_Channel_2;//选择ic捕获，因为我的通道1是输出pwm波形
	TIM_ICStruct.TIM_ICFilter=0xF;//滤波器选择，这里提一个方面，就是这个和我的pwm输出引脚定义的输出speed有关系，speed越小，滤波器数值越大，误差会增加
	TIM_ICStruct.TIM_ICPolarity= TIM_ICPolarity_Rising; //捕获选择
	TIM_ICStruct.TIM_ICPrescaler= TIM_ICPSC_DIV1;
	TIM_ICStruct.TIM_ICSelection=TIM_ICSelection_DirectTI ; //选择信号来源，只有ch1和ch2可以交叉捕获
	TIM_ICInit(TIM2, &TIM_ICStruct);

/*选择触发源及从模式*/
	TIM_SelectInputTrigger(TIM2, TIM_TS_TI2FP2);					//触发源选择TI1FP1
	TIM_SelectSlaveMode(TIM2, TIM_SlaveMode_Reset);					//从模式选择复位
																	//即TI1产生上升沿时，会触发CNT归零
																	/*TIM使能*/
	TIM_Cmd(TIM2, ENABLE);			//使能TIM3，定时器开始运行
}
uint32_t IC_GetFreq(void)
{
	return 1000000 / (TIM_GetCapture2(TIM2) + 1);		//测周法得到频率fx = fc / N，这里不执行+1的操作也可
}
//测周法的意思就是，设定一个已经知道的频率fc，当待测频率达到捕获条件之后，此时记录ccr的数值，就可以知道我的待测频率完成了一个周期的时候，我的fc已经完成多少个周期了，就可以推断出两者的关系

