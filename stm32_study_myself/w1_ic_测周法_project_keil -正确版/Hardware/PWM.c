 #include "stm32f10x.h"
 
 //本次目标是为了开启一个频率是1000hz的时钟，时用tim2定时器输出pwm信号，用tim3定时器来完成输入捕获动作
 //本节为pwm代码
 void PWM_Init(void)
 {
//开启时钟
	 RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
	 RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
//设置GPIOA PA1 CH2通道为我的pwm输出引脚
	 GPIO_InitTypeDef GPIO_InitStructure;
	 GPIO_InitStructure.GPIO_Pin=GPIO_Pin_1;
	 
	 GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;
	 GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
//GPIO_CRL预设定配置完毕
	 GPIO_Init(GPIOA,&GPIO_InitStructure); //个人真的觉得指针（此处传为地址）很有意思，传入一个地址，就可以操作一个变量，其实我并不是很能够理解，因为地址和变量内存只是标签关系，我始终遗漏了这个问题
//设置tim时基单元
	 
//选择输入信号，可以来自内部，etr，其他定时器。
TIM_InternalClockConfig(TIM2);
	 
//设定基础设置
TIM_TimeBaseInitTypeDef TIM_TimBaseInitStructure;
TIM_TimBaseInitStructure.TIM_CounterMode=TIM_CounterMode_Up;	
TIM_TimBaseInitStructure.TIM_Period = 1000-1;
TIM_TimBaseInitStructure.TIM_Prescaler =72-1;
TIM_TimBaseInitStructure.TIM_ClockDivision=TIM_CKD_DIV1;  //时钟分频，选择不分频，此参数用于配置滤波器时钟，不影响时基单元功能
TIM_TimBaseInitStructure.TIM_RepetitionCounter =0 ;	//只对高级定时器有用
TIM_TimeBaseInit(TIM2,&TIM_TimBaseInitStructure);
//设定oc区域
TIM_OCInitTypeDef TIM_OCInitStructure;
TIM_OCStructInit(&TIM_OCInitStructure);
TIM_OCInitStructure.TIM_OCMode=TIM_OCMode_PWM1 ; //选择输出ref的模式
TIM_OCInitStructure.TIM_OCPolarity=TIM_OCPolarity_High;//极性通过控制器
TIM_OCInitStructure.TIM_OutputState=ENABLE;//确认是否开始
TIM_OCInitStructure.TIM_Pulse=200;//ccr
TIM_OC2Init(TIM2,&TIM_OCInitStructure);
//开启计数器
TIM_Cmd(TIM2,ENABLE);
}

void PWM_SetCompare2(uint16_t Compare)
{
	TIM_SetCompare2(TIM2, Compare);		//设置CCR2的值
}

void PWM_SetPrescaler(uint16_t Prescaler)
{
	TIM_PrescalerConfig(TIM2, Prescaler, TIM_PSCReloadMode_Immediate);		//设置PSC的值
}
