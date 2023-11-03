/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-03-12     Bernard      first version
 * 2006-05-27     Bernard      add support for same priority thread schedule
 * 2006-08-10     Bernard      remove the last rt_schedule in rt_tick_increase
 * 2010-03-08     Bernard      remove rt_passed_second
 * 2010-05-20     Bernard      fix the tick exceeds the maximum limits
 * 2010-07-13     Bernard      fix rt_tick_from_millisecond issue found by kuronca
 * 2011-06-26     Bernard      add rt_tick_set function.
 * 2018-11-22     Jesven       add per cpu tick
 *
 *
 * Anotation：对系统时钟操作的一系列函数和变量。
 */

#include <rthw.h>
#include <rtthread.h>

//作用于本文件的全局静态变量
static rt_tick_t rt_tick = 0;

/**
 * This function will initialize system tick and set it to zero.
 * @ingroup SystemInit
 *
 * @deprecated since 1.1.0, this function does not need to be invoked
 * in the system initialization.
 */
void rt_system_tick_init(void)
{
}

/**
 * @addtogroup Clock
 */

/**@{*/

/**
 * This function will return current tick from operating system startup
 *
 * @return current tick
 *
 * Anotation：获取当前时间片；
 */
rt_tick_t rt_tick_get(void)
{
    /* return the global tick */
    return rt_tick;
}

/**
 * This function will set current tick
 *
 * Anotation：设置当前时间片；
 */
void rt_tick_set(rt_tick_t tick)
{
    rt_base_t level;

    level = rt_hw_interrupt_disable();
    rt_tick = tick;
    rt_hw_interrupt_enable(level);
}

/**
 * This function will notify kernel there is one tick passed. Normally,
 * this function is invoked by clock ISR.
 *
 * Anotation：系统时间改变
 */
void rt_tick_increase(void)
{
    struct rt_thread *thread;

    /* increase the global tick */
    ++ rt_tick;

    /* check time slice
     *
     * Anotation：获取当前允许线程的指针
     *  */
    thread = rt_thread_self();

    // 对当前允许内存进行 时间片的调整。
    -- thread->remaining_tick;
    if (thread->remaining_tick == 0)
    {
        /* change to initialized tick
         *  Anotation：重新初始化线程的时间片，用以下一次响应
         * */
        thread->remaining_tick = thread->init_tick;

        /* yield
         * Anotation：挂起时间片运行完的线程。
         * */
        rt_thread_yield();
    }

    /* check timer */
    rt_timer_check();
}

/**
 * This function will calculate the tick from millisecond.
 *
 * @param ms the specified millisecond
 *           - Negative Number wait forever
 *           - Zero not wait
 *           - Max 0x7fffffff
 *
 * @return the calculated tick
 *
 * Anotation：校验定时器时钟时间
 */
rt_tick_t rt_tick_from_millisecond(rt_int32_t ms)
{
    rt_tick_t tick;

    if (ms < 0)
    {
        tick = (rt_tick_t)RT_WAITING_FOREVER;
    }
    else
    {
        tick = RT_TICK_PER_SECOND * (ms / 1000);
        tick += (RT_TICK_PER_SECOND * (ms % 1000) + 999) / 1000;
    }

    /* return the calculated tick */
    return tick;
}

/**@}*/

