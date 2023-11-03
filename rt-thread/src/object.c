/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2006-03-14     Bernard      the first version
 * 2006-04-21     Bernard      change the scheduler lock to interrupt lock
 * 2006-05-18     Bernard      fix the object init bug
 * 2006-08-03     Bernard      add hook support
 * 2007-01-28     Bernard      rename RT_OBJECT_Class_Static to RT_Object_Class_Static
 * 2010-10-26     yi.qiu       add module support in rt_object_allocate and rt_object_free
 * 2017-12-10     Bernard      Add object_info enum.
 * 2018-01-25     Bernard      Fix the object find issue when enable MODULE.
 *
 *
  * Anotation：所有的其他内核对象都继承自这个对象。这个文件的很多思想都是面向对象的，在一些操作中，可以直接操作变量，但是他却封装成为了一个函数
  * 去对变量的值进行修改。
 */

#include <rtthread.h>
#include <rthw.h>

/*
 * define object_info for the number of rt_object_container items.
 * Anotation：这个枚举变量列举继承自rt_object的所有类型，其枚举量值作为rt_object_container数组的索引（下标），
  * 最后使用 RT_Object_Info_Unknown 来计算所有对象类型的个数。
 */
enum rt_object_info_type
{
    RT_Object_Info_Thread = 0,                         /**< The object is a 线程. */
#ifdef RT_USING_SEMAPHORE
    RT_Object_Info_Semaphore,                          /**< The object is a 信号量. */
#endif
#ifdef RT_USING_MUTEX
    RT_Object_Info_Mutex,                              /**< The object is a 互斥量. */
#endif
#ifdef RT_USING_EVENT
    RT_Object_Info_Event,                              /**< The object is a 事件. */
#endif
#ifdef RT_USING_MAILBOX
    RT_Object_Info_MailBox,                            /**< The object is a 邮箱. */
#endif
#ifdef RT_USING_MESSAGEQUEUE
    RT_Object_Info_MessageQueue,                       /**< The object is a 消息队列. */
#endif
#ifdef RT_USING_MEMHEAP
    RT_Object_Info_MemHeap,                            /**< The object is a 内存堆*/
#endif
#ifdef RT_USING_MEMPOOL
    RT_Object_Info_MemPool,                            /**< The object is a 内存池. */
#endif
#ifdef RT_USING_DEVICE
    RT_Object_Info_Device,                             /**< The object is a 驱动 */
#endif
    RT_Object_Info_Timer,                              /**< The object is a 定时器. */
    RT_Object_Info_Unknown,                            /**< The object is unknown.  使用枚举变量定义的特性对其动态计数*/
};

/*
 * Anotation：为了方便rt_object_container数组 初始化而定义的一个参数宏。这个函数是给rt_list_t object_list成员赋值，
 *   以{}的形式给结构体赋值，是C语言的一种语法，下面很多结构体初始化使用了类似的语法。
 *   由于 object_list 为rt_llist_t类型，其中包含两个rt_list_t指针 所以
 *   {&(_object_container[c].object_list), &(_object_container[c].object_list)}的形式给两个指针都赋值，形成回环的双向链表 。
 *
 * */
#define _OBJ_CONTAINER_LIST_INIT(c)     \
    {&(rt_object_container[c].object_list), &(rt_object_container[c].object_list)}

/*
 * Anotation：对象容器，定义一个全局的静态变量，记录容器中对象的类型、不同类型对象的链表头，不同对象的内存大小。
 * 这个对象容器并没有为系统内核的对象分配实际的对象的内存，而仅仅是记录了各个对象的信息，以及用于连接对象的链表头
 * 最终使用时，将以动态内存分配的方式为对应的对象部分分配相应的内存。
 * */
static struct rt_object_information rt_object_container[RT_Object_Info_Unknown] =
{
    /* initialize object container - thread */
    {RT_Object_Class_Thread, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Thread), sizeof(struct rt_thread)},
#ifdef RT_USING_SEMAPHORE
    /* initialize object container - semaphore */
    {RT_Object_Class_Semaphore, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Semaphore), sizeof(struct rt_semaphore)},
#endif
#ifdef RT_USING_MUTEX
    /* initialize object container - mutex */
    {RT_Object_Class_Mutex, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Mutex), sizeof(struct rt_mutex)},
#endif
#ifdef RT_USING_EVENT
    /* initialize object container - event */
    {RT_Object_Class_Event, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Event), sizeof(struct rt_event)},
#endif
#ifdef RT_USING_MAILBOX
    /* initialize object container - mailbox */
    {RT_Object_Class_MailBox, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_MailBox), sizeof(struct rt_mailbox)},
#endif
#ifdef RT_USING_MESSAGEQUEUE
    /* initialize object container - message queue */
    {RT_Object_Class_MessageQueue, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_MessageQueue), sizeof(struct rt_messagequeue)},
#endif
#ifdef RT_USING_MEMHEAP
    /* initialize object container - memory heap */
    {RT_Object_Class_MemHeap, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_MemHeap), sizeof(struct rt_memheap)},
#endif
#ifdef RT_USING_MEMPOOL
    /* initialize object container - memory pool */
    {RT_Object_Class_MemPool, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_MemPool), sizeof(struct rt_mempool)},
#endif
#ifdef RT_USING_DEVICE
    /* initialize object container - device */
    {RT_Object_Class_Device, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Device), sizeof(struct rt_device)},
#endif
    /* initialize object container - timer */
    {RT_Object_Class_Timer, _OBJ_CONTAINER_LIST_INIT(RT_Object_Info_Timer), sizeof(struct rt_timer)},
};


/*
 * Anotation：这里不是函数的声明，而是定义了几个函数指针。
 * 存储期限制：
 * 带static修饰符的全局函数指针：函数指针声明为static的话，其作用域仅限于定义它的源文件（或编译单元）。这意味着该函数指针只能在定义它的源文件内访问，其他源文件无法访问它。
 * 不带static修饰符的全局函数指针：如果全局函数指针没有使用static修饰符，它的作用域将扩展到整个程序，其他源文件也可以访问它，前提是在其他文件中进行了声明。
 *
 * 可见性和重复定义：
 * 带static修饰符的全局函数指针：由于其作用域仅限于定义它的源文件，不同的源文件可以定义具有相同名称的不同static函数指针，而不会发生冲突。
 * 不带static修饰符的全局函数指针：全局函数指针没有static修饰符的话，不同的源文件中定义具有相同名称的全局函数指针将导致重复定义错误，因为它们在整个程序范围内是可见的。
 * */
#ifdef RT_USING_HOOK
// 带static的全局变量会有在本源文件访问的限制。
static void (*rt_object_attach_hook)(struct rt_object *object);
static void (*rt_object_detach_hook)(struct rt_object *object);
void (*rt_object_trytake_hook)(struct rt_object *object);
void (*rt_object_take_hook)(struct rt_object *object);
void (*rt_object_put_hook)(struct rt_object *object);

/**
 * @addtogroup Hook
 */

/**@{*/

/**
 * This function will set a hook function, which will be invoked when object
 * attaches to kernel object system.
 *
 * @param hook the hook function
 * Anotation：给rt_object_attach_hook静态全局函数指针赋值
 */
void rt_object_attach_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_attach_hook = hook;
}

/**
 * This function will set a hook function, which will be invoked when object
 * detaches from kernel object system.
 *
 * @param hook the hook function
 * Anotation：给rt_object_detach_sethook静态全局函数指针赋值
 */
void rt_object_detach_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_detach_hook = hook;
}

/**
 * This function will set a hook function, which will be invoked when object
 * is taken from kernel object system.
 *
 * The object is taken means:
 * semaphore - semaphore is taken by thread
 * mutex - mutex is taken by thread
 * event - event is received by thread
 * mailbox - mail is received by thread
 * message queue - message is received by thread
 *
 * @param hook the hook function
 * Anotation：给rt_object_trytake_hook全局函数指针赋值
 */
void rt_object_trytake_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_trytake_hook = hook;
}

/**
 * This function will set a hook function, which will be invoked when object
 * have been taken from kernel object system.
 *
 * The object have been taken means:
 * semaphore - semaphore have been taken by thread
 * mutex - mutex have been taken by thread
 * event - event have been received by thread
 * mailbox - mail have been received by thread
 * message queue - message have been received by thread
 * timer - timer is started
 *
 * @param hook the hook function
 * Anotation：给rt_object_take_hook全局函数指针赋值
 */
void rt_object_take_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_take_hook = hook;
}

/**
 * This function will set a hook function, which will be invoked when object
 * is put to kernel object system.
 *
 * @param hook the hook function
 * Anotation：给rt_object_put_hook全局函数指针赋值
 */
void rt_object_put_sethook(void (*hook)(struct rt_object *object))
{
    rt_object_put_hook = hook;
}

/**@}*/
#endif

/**
 * @ingroup SystemInit
 *
 * This function will initialize system object management.
 *
 * @deprecated since 0.3.0, this function does not need to be invoked
 * in the system initialization.
 *
 * Anotation：用于系统对象管理的初始化，但是已经被弃用。
 */
void rt_system_object_init(void)
{
}

/**
 * @addtogroup KernelObject
 */

/**@{*/

/**
 * This function will return the specified type of object information.
 *
 * @param type the type of object, which can be
 *             RT_Object_Class_Thread/Semaphore/Mutex... etc
 *
 * @return the object type information or RT_NULL
 *
 * Anotation：由RT_Object_Info_Unknown记录了内核中对象类型的个数，通过比对rt_object_container中的rt_object_container[index].type来获取
 * rt_object_information结构体的信息。
 * rt_object_container 本身就是一个rt_object_information结构体数组构成的容器。
 */
struct rt_object_information *
rt_object_get_information(enum rt_object_class_type type)
{
    int index;

    for (index = 0; index < RT_Object_Info_Unknown; index ++)
        if (rt_object_container[index].type == type)
            return &rt_object_container[index];

    return RT_NULL;
}

/**
 * This function will return the length of object list in object container.
 *
 * @param type the type of object, which can be
 *             RT_Object_Class_Thread/Semaphore/Mutex... etc
 * @return the length of object list
 *
 * Anotation：获取某种类型对象的长度，主要就是获取rt_object_container数组中存储的相应类型的链表头，然后对这个链表进行计数。
 */
int rt_object_get_length(enum rt_object_class_type type)
{
    int count = 0;
    rt_ubase_t level;
    struct rt_list_node *node = RT_NULL; //链表计数
    struct rt_object_information *information = RT_NULL;//rt_object_container中对应类型的rt_object_information结构体。

    // 通过对象类型的枚举值获取描述对象信息的结构体
    information = rt_object_get_information((enum rt_object_class_type)type);
    if (information == RT_NULL) return 0;

    /* 失能硬件中断，建立临界保护区，防止在遍历对象链表的过程中，在中断中对此链表进行了增删改查，从而导致系统错误*/
    level = rt_hw_interrupt_disable();
    /* get the count of objects */
    rt_list_for_each(node, &(information->object_list))
    {
        count ++;
    }
    rt_hw_interrupt_enable(level);

    return count;
}

/**
 * This function will copy the object pointer of the specified type,
 * with the maximum size specified by maxlen.
 *
 * @param type the type of object, which can be
 *             RT_Object_Class_Thread/Semaphore/Mutex... etc
 * @param pointers the pointers will be saved to    用于保存链表的指针。
 * @param maxlen the maximum number of pointers can be saved  能被拷贝的最大长度。
 *
 * @return the copied number of object pointers 已经拷贝的最大长度。
 *
 * Anotation：用于对某一个类型对象进行链表拷贝的函数。
 */
int rt_object_get_pointers(enum rt_object_class_type type, rt_object_t *pointers, int maxlen)
{
    int index = 0;
    rt_ubase_t level;

    struct rt_object *object;
    struct rt_list_node *node = RT_NULL;
    struct rt_object_information *information = RT_NULL;

    if (maxlen <= 0) return 0;

    information = rt_object_get_information((enum rt_object_class_type)type);
    if (information == RT_NULL) return 0;

    level = rt_hw_interrupt_disable();
    /* retrieve pointer of object */
    rt_list_for_each(node, &(information->object_list)) // 遍历环形链表
    {
        /*
         * node：对应type类型的内核对象的链表头，rt_list_entry：
                  * 通过一个结构体成员指针返回指向整个结构体开始的指针。
         * Function：获取一个对象的指针
         * */
        object = rt_list_entry(node, struct rt_object, list);

        pointers[index] = object;
        index ++;

        if (index >= maxlen) break;
    }
    rt_hw_interrupt_enable(level);

    return index;
}

/**
 * This function will initialize an object and add it to object system
 * management.
 *
 * @param object the specified object to be initialized.
 * @param type the object type.
 * @param name the object name. In system, the object's name must be unique.
 *
 *
 * Anotation：初始化内核对象object，这是所有继承自 rt_object 的对象都需要调用的构造函数，用C++的角度来讲是这样。
 */
void rt_object_init(struct rt_object         *object,
                    enum rt_object_class_type type,
                    const char               *name)
{
    register rt_base_t temp;
    struct rt_list_node *node = RT_NULL;
    struct rt_object_information *information;

    /* get object information */
    information = rt_object_get_information(type);
    RT_ASSERT(information != RT_NULL);

    /* check object type to avoid re-initialization */

    /* enter critical */
    /* 禁止调度器调度 */
    rt_enter_critical();
    /* try to find object */
    for (node  = information->object_list.next;
            node != &(information->object_list);
            node  = node->next)
    {
        /* 这里是一个面向对象的思想 多态！！！！ 通过一个父类的指针去实现子类的初始化 */
        struct rt_object *obj;

        obj = rt_list_entry(node, struct rt_object, list);
        if (obj) /* skip warning when disable debug */
        {
            RT_ASSERT(obj != object);
        }
    }
    /* leave critical */
    rt_exit_critical();

    /* initialize object's parameters */
    /* set object type to static */
    object->type = type | RT_Object_Class_Static;
    /* copy name */
    rt_strncpy(object->name, name, RT_NAME_MAX);

    RT_OBJECT_HOOK_CALL(rt_object_attach_hook, (object));

    /* lock interrupt */
    temp = rt_hw_interrupt_disable();

    /* insert object into information object list */
    // 将对象中的节点挂载到内核容器 rt_object_container 中
    rt_list_insert_after(&(information->object_list), &(object->list));

    /* unlock interrupt */
    rt_hw_interrupt_enable(temp);
}

/**
 * This function will detach a static object from object system,
 * and the memory of static object is not freed.
 *
 * @param object the specified object to be detached.
 */
void rt_object_detach(rt_object_t object)
{
    register rt_base_t temp;

    /* object check */
    RT_ASSERT(object != RT_NULL);

    RT_OBJECT_HOOK_CALL(rt_object_detach_hook, (object));

    /* reset object type */
    object->type = 0;

    /* lock interrupt */
    /* 所有的内核容器操作都要进行临界区的保护 */
    temp = rt_hw_interrupt_disable();

    /* remove from old list */
    rt_list_remove(&(object->list));

    /* unlock interrupt */
    rt_hw_interrupt_enable(temp);
}

#ifdef RT_USING_HEAP
/**
 * This function will allocate an object from object system
 *
 * @param type the type of object
 * @param name the object name. In system, the object's name must be unique.
 *
 * @return object
 * Anotation：给相应type类型，名为name的对象分配一块内存位于系统rt_object_container容器对应数组位置中链表的末端。
 */
rt_object_t rt_object_allocate(enum rt_object_class_type type, const char *name)
{
    struct rt_object *object;
    register rt_base_t temp;
    struct rt_object_information *information;

    RT_DEBUG_NOT_IN_INTERRUPT;

    /* get object information */
    information = rt_object_get_information(type);
    RT_ASSERT(information != RT_NULL);

    object = (struct rt_object *)RT_KERNEL_MALLOC(information->object_size);
    if (object == RT_NULL)
    {
        /* no memory can be allocated */
        return RT_NULL;
    }

    /* clean memory data of object */
    rt_memset(object, 0x0, information->object_size);

    /* initialize object's parameters */

    /* set object type */
    object->type = type;

    /* set object flag */
    object->flag = 0;

    /* copy name */
    rt_strncpy(object->name, name, RT_NAME_MAX);

    RT_OBJECT_HOOK_CALL(rt_object_attach_hook, (object));

    /* lock interrupt */
    temp = rt_hw_interrupt_disable();

    /* insert object into information object list */
    rt_list_insert_after(&(information->object_list), &(object->list));

    /* unlock interrupt */
    rt_hw_interrupt_enable(temp);

    /* return object */
    return object;
}

/**
 * This function will delete an object and release object memory.
 *
 * @param object the specified object to be deleted.
 * Anotation：删除一个对象，
 */
void rt_object_delete(rt_object_t object)
{
    register rt_base_t temp;

    /* object check */
    RT_ASSERT(object != RT_NULL);
    RT_ASSERT(!(object->type & RT_Object_Class_Static));

    RT_OBJECT_HOOK_CALL(rt_object_detach_hook, (object));

    /* reset object type */
    object->type = RT_Object_Class_Null;

    /* lock interrupt */
    temp = rt_hw_interrupt_disable();

    /* remove from old list */
    rt_list_remove(&(object->list));

    /* unlock interrupt */
    rt_hw_interrupt_enable(temp);

    /* free the memory of object */
    RT_KERNEL_FREE(object);
}
#endif

/**
 * This function will judge the object is system object or not.
 * Normally, the system object is a static object and the type
 * of object set to RT_Object_Class_Static.
 *
 * @param object the specified object to be judged.
 *
 * @return RT_TRUE if a system object, RT_FALSE for others.
 *
 * Anotation：所有内核对象都有一个 RT_Object_Class_Static的标志位。
 */
rt_bool_t rt_object_is_systemobject(rt_object_t object)
{
    /* object check */
    RT_ASSERT(object != RT_NULL);

    if (object->type & RT_Object_Class_Static)
        return RT_TRUE;

    return RT_FALSE;
}

/**
 * This function will return the type of object without
 * RT_Object_Class_Static flag.
 *
 * @param object the specified object to be get type.
 *
 * @return the type of object.
 */
rt_uint8_t rt_object_get_type(rt_object_t object)
{
    /* object check */
    RT_ASSERT(object != RT_NULL);

    return object->type & ~RT_Object_Class_Static;
}

/**
 * This function will find specified name object from object
 * container.
 *
 * @param name the specified name of object.
 * @param type the type of object
 *
 * @return the found object or RT_NULL if there is no this object
 * in object container.
 *
 * @note this function shall not be invoked in interrupt status.
 *
 * Anotation：通过name和type 去查询一个对象
 */
rt_object_t rt_object_find(const char *name, rt_uint8_t type)
{
    struct rt_object *object = RT_NULL;
    struct rt_list_node *node = RT_NULL;
    struct rt_object_information *information = RT_NULL;

    information = rt_object_get_information((enum rt_object_class_type)type);

    /* parameter check */
    if ((name == RT_NULL) || (information == RT_NULL)) return RT_NULL;

    /* which is invoke in interrupt status */
    RT_DEBUG_NOT_IN_INTERRUPT;

    /* enter critical */
    rt_enter_critical();

    /* try to find object */
    // RT_NAME_MAX 为定死的一个长度，方便系统的构建
    rt_list_for_each(node, &(information->object_list))
    {
        object = rt_list_entry(node, struct rt_object, list);
        if (rt_strncmp(object->name, name, RT_NAME_MAX) == 0)
        {
            /* leave critical */
            rt_exit_critical();

            return object;
        }
    }

    /* leave critical */
    rt_exit_critical();

    return RT_NULL;
}

/**@}*/
