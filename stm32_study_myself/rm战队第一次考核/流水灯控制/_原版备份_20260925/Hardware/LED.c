#include "stm32f10x.h"
#include "delay.h"
#include "PWM.h"
extern  int16_t key_mode;
void LED_INIT(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode=GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Pin=GPIO_Pin_1|GPIO_Pin_2|GPIO_Pin_3;
	GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_InitStructure);	
}

void LED_RUN_Current(int16_t key_mdoe)
{
if(key_mdoe==0)
{
LED_INIT();
GPIO_WriteBit(GPIOA,GPIO_Pin_3,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_2,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_1,0);;
}
else if(key_mdoe==1)
{

GPIO_WriteBit(GPIOA,GPIO_Pin_3,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_2,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_1,1);//PIN_1
Delay_ms(200);
GPIO_WriteBit(GPIOA,GPIO_Pin_3,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_2,1);
GPIO_WriteBit(GPIOA,GPIO_Pin_1,0);//PIN_2
Delay_ms(200);
GPIO_WriteBit(GPIOA,GPIO_Pin_3,1);
GPIO_WriteBit(GPIOA,GPIO_Pin_2,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_1,0);//PIN_3
Delay_ms(200);	
}
else if(key_mdoe==2)
{

GPIO_WriteBit(GPIOA,GPIO_Pin_3,1);
GPIO_WriteBit(GPIOA,GPIO_Pin_2,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_1,0);//PIN_3
Delay_ms(100);
GPIO_WriteBit(GPIOA,GPIO_Pin_3,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_2,1);
GPIO_WriteBit(GPIOA,GPIO_Pin_1,0);//PIN_2
Delay_ms(100);
GPIO_WriteBit(GPIOA,GPIO_Pin_3,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_2,0);
GPIO_WriteBit(GPIOA,GPIO_Pin_1,1);//PIN_1
Delay_ms(200);	
}
else if (key_mode==3)
{
	PWM_INIT();
	float t = 0;
float	temp = 50 + 50*sin(t);
if(temp < 0) temp = 0;
if(temp > 100) temp = 100;
uint16_t ccr2 = temp;
    uint8_t exit_flag = 0;
  while(1)
    {
        // 三路相位错开
        uint16_t ccr2 = 50 + 50*sin(t);
        uint16_t ccr3 = 50 + 50*sin(t + 2*3.14159/3);
        uint16_t ccr4 = 50 + 50*sin(t + 4*3.14159/3);

        PWM_SETCPMPARA_OC2(ccr2);
        PWM_SETCPMPARA_OC3(ccr3);
        PWM_SETCPMPARA_OC4(ccr4);

        t += 0.05f;  //这个值控制波浪流动速度，越大越快

        Delay_ms(20);

        //直接读按键引脚，按下退出波浪
        if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)==0)
        {
            Delay_ms(30);
            if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_0)==0)
            {
                exit_flag = 1;
                break;
            }
        }
    }
}
else if (key_mode==4)
{
	key_mode=0;
}
}
