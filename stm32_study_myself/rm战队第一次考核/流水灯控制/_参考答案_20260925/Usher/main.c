/**
 ******************************************************************************
 * @file    main.c
 * @brief   电控组第一次考核：按键切换三种流水灯效果
 *
 *  ============ 需求对照（验收时照着念就行） ============
 *    外设：GPIO 输出（LED）+ GPIO 输入（按键）+ TIM 定时器 PWM 模式（亮度）
 *    ① 传统流水灯（逐个点亮）    → Effect_Flow_Init / Effect_Flow_Step
 *    ② 呼吸灯（渐亮渐灭）        → Effect_Breathe_Init / Effect_Breathe_Step
 *    ③ 亮度波浪（正弦，左→右）   → Effect_Wave_Init / Effect_Wave_Step
 *    按键（PA0）每按一次 → 切到下一个效果，3 个效果循环
 *
 *  ============ 架构（这就是"用函数实现、按键切换调用"的落地方式） ============
 *    每个效果 = 一对函数：Init()（进入时配置一次）+ Step()（走一小步就返回）
 *    再用一张"效果表"把 3 个效果排好队，主循环按 g_mode 取出当前那一行来调。
 *
 *    ⭐ 这样做的好处：
 *       · 主循环永远不被卡住（Step 只走一小步，20ms 就返回）→ 按键随时能响应
 *       · 想加第 4 个效果，只在表里加一行，主循环一个字都不用改
 *       · 等价于 switch-case，但"表"更像数据，考核讲解时更好讲
 *
 *  ============ 为什么不用"在效果函数里 while(1) 死循环 + 延时"？ ============
 *    你原来那份代码就是这么写的，结果是：进入波浪灯后主循环被吃掉了，
 *    key_run() 再也不执行 → 出不来；退出后 key_mode 仍是 3 → 又一次进死循环。
 *    （这就是"卡死在波浪灯、只能复位"的根因）
 ******************************************************************************
 */

#include "stm32f10x.h"
#include "LED.h"
#include "KEY.h"
#include "Delay.h"

/* ---------- 效果表：一行 = 一个效果 ----------
 *  名字用 ASCII（Watch 里看得清）；中文名放注释里 —— AC5 对"字符串里的中文"
 *  会报 #870-D invalid multibyte character sequence（注释里的中文不受影响）
 */
typedef struct
{
    const char *name;           /* 效果名（Watch 窗口里能看到现在跑到哪个效果） */
    void      (*init)(void);    /* 进入这个效果时要做的配置 */
    void      (*step)(void);    /* 每 EFFECT_STEP_MS 调一次，走一小步 */
} Effect_t;

static const Effect_t g_effects[] =
{
    { "1-Flow",    Effect_Flow_Init,    Effect_Flow_Step    },   /* ① 传统流水灯 */
    { "2-Breathe", Effect_Breathe_Init, Effect_Breathe_Step },   /* ② 呼吸灯 */
    { "3-Wave",    Effect_Wave_Init,    Effect_Wave_Step    },   /* ③ 亮度波浪 */
};

#define EFFECT_COUNT    (sizeof(g_effects) / sizeof(g_effects[0]))

/* ---------- 便于 Keil Watch 窗口观察的全局变量（volatile 防止被优化掉） ---------- */
volatile uint8_t     g_mode       = 0u;             /* 当前效果编号 0~2 */
volatile const char *g_mode_name  = "1-Flow";       /* Watch 里展开可看到名字 */
volatile uint32_t    g_key_count  = 0u;             /* 按键被按下多少次 */
volatile uint32_t    g_loop_count = 0u;             /* 主循环跑了多少圈 */
volatile uint16_t    g_led1_duty  = 0u;             /* LED1 当前占空比（PWM 模式才有意义） */
volatile uint16_t    g_led2_duty  = 0u;
volatile uint16_t    g_led3_duty  = 0u;

/* ============================ 主函数 ============================ */
int main(void)
{
    KEY_Init();                     /* ① GPIO 输入：按键 PA0（上拉输入） */

    g_effects[g_mode].init();       /* ② 上电先进第 1 个效果（流水灯） */

    while (1)
    {
        /* ---- 第 1 件事：看一眼按键 ---- */
        if (KEY_Scan() != 0u)
        {
            g_key_count++;

            g_mode++;                                       /* 切到下一个效果（循环） */
            if (g_mode >= (uint8_t)EFFECT_COUNT)
            {
                g_mode = 0u;
            }

            g_effects[g_mode].init();                       /* 重新配引脚 + 复位该效果的状态 */
            g_mode_name  = g_effects[g_mode].name;
            g_loop_count = 0u;
        }

        /* ---- 第 2 件事：当前效果走一小步（不阻塞） ---- */
        g_effects[g_mode].step();

        /* ---- 第 3 件事：这一步的节奏 20ms ---- */
        Delay_ms(EFFECT_STEP_MS);

        g_loop_count++;
        g_led1_duty = LED_GetBrightness(0u);    /* 便于 Watch 观察三路亮度 */
        g_led2_duty = LED_GetBrightness(1u);
        g_led3_duty = LED_GetBrightness(2u);
    }

    /* return 0;  空循环不会走到这里 */
}
