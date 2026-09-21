#include"stm32f10x.h"
#include"pwm.h"

int main()
{
pwm_init();
for (uint16_t i = 0; i < 100; i++)
{
   PWM_SetCompare1(i);
}
for (uint16_t i = 100; i >0; i--)
{
   PWM_SetCompare1(i);
}

}