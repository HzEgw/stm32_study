#ifndef __PWM_H
#define __PWM_H

#include"stm32f10x.h"
void pwm_init(void);
void PWM_SetCompare2(uint16_t Compare);
void PWM_SetCompare3(uint16_t Compare);
void PWM_SetCompare4(uint16_t Compare);
void pwm_init(void);

#endif
