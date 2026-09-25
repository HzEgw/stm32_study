#include "stm32f10x.h"
#include "delay.h"
extern  int16_t key_mode;
void KEY_INIT(void)
{

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);	
}
void key_run(void)
{
if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)==0)
{
  Delay_ms(30);
  if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)==0) //再次确认按键按下
        {
            while(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)==0); //阻塞等待松手
            Delay_ms(30);    //松开消抖
            key_mode++;
            
        }
}
}
