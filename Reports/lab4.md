# lab4 进程管理

小组成员：王梓丞、王昱、孟启轩

## 一、实验目的

- 了解内核线程创建/执行的管理过程
- 了解内核线程的切换和基本调度过程

## 二、练习

#### 练习0：填写已有实验

已填写

#### 练习1：分配并初始化一个进程控制块

alloc_proc函数（位于kern/process/proc.c中）负责分配并返回一个新的struct proc_struct结构，用于存储新建立的内核线程的管理信息。ucore需要对这个结构进行最基本的初始化，你需要完成这个初始化过程。

【提示】在alloc_proc函数的实现中，需要初始化的proc_struct结构中的成员变量至少包括：state/pid/runs/kstack/need_resched/parent/mm/context/tf/cr3/flags/name。

请在实验报告中简要说明你的设计实现过程。请回答如下问题：

- 请说明proc_struct中struct context context和struct trapframe *tf成员变量含义和在本实验中的作用是啥？（提示通过看代码和编程调试可以判断出来）

`alloc_proc` 函数负责初始化并分配一个新的进程控制块（PCB），用于管理新创建的内核线程。在这个过程中，需要为 `proc_struct` 结构体的各个成员变量赋值。`proc_init` 调用 `alloc_proc` 后，会验证这些值是否正确，因此初始化时可以根据判断条件设置相应的值。以下是各成员的含义和初始化原因：

- **state**：初始时未分配资源，因此状态为初始状态。  
- **pid**：进程 ID，尚未分配时设为 -1，表示无法运行。  
- **runs**：表示运行次数，初始化为 0。  
- **kstack**：内核栈未分配，初始化为 0。`alloc_proc` 后，`idleproc` 使用启动时设置的内核栈，其他进程需要在 `do_fork` 中分配栈。  
- **need_resched**：表示是否需要重新调度，初始为 0。  
- **parent**：表示父进程，初始化为 NULL。根据后续代码，子进程的父进程会设置为当前进程。  
- **mm**：表示内存管理，初始化为 NULL，稍后通过 `do_fork` 中的 `copy_mm` 分配。  
- **context**：进程上下文，初始化为 0，稍后通过 `copy_thread` 设置。  
- **tf**：中断帧，初始化为 NULL，稍后通过 `copy_thread` 设置。  
- **cr3**：内核线程共享内核空间，因此页表设置为启动时的 `boot_cr3`。  
- **flags**：暂时没有使用，初始化为 0。  
- **name**：进程名，暂时未设置，初始化为空。

```c
static struct proc_struct *alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->runs = 0;
        proc->kstack = 0;
        proc->need_resched = 0;
        proc->parent = NULL;
        proc->mm = NULL;
        memset(&(proc->context), 0, sizeof(struct context));
        proc->tf = NULL;
        proc->cr3 = boot_cr3;
        proc->flags = 0;
        memset(&(proc->name), 0, PROC_NAME_LEN);
    }
    return proc;
}
```

**`context` 和 `tf` 的含义及作用**：

- **context**：保存进程的上下文信息，包括寄存器状态、程序计数器等，确保进程切换时可以恢复其状态。上下文切换是进程调度和管理的核心操作。  
- **tf**：保存中断或异常时的进程状态，包含寄存器状态、指令指针等信息，在中断处理后能够恢复进程执行。  

在本实验中，`context` 和 `tf` 配合使用，以实现进程切换。`proc_run` 中调用 `switch_to` 函数，保存当前进程的上下文并恢复新的进程上下文。初始化时，`context` 的 `ra` 设置为 `forkret` 函数入口，切换到 `forkret` 后，执行 `forkrets` 函数。该函数使用当前进程的 `tf` 参数，通过 `__trapret` 恢复寄存器值。`tf` 中的 `epc` 设置为 `kernel_thread_entry`，该函数通过寄存器 `s0` 和 `s1` 调用新进程的函数，实现 `initproc` 进程的输出“Hello World!”功能。


#### 练习2：为新创建的内核线程分配资源
创建一个内核线程需要分配和设置好很多资源。kernel_thread函数通过调用do_fork函数完成具体内核线程的创建工作。do_kernel函数会调用alloc_proc函数来分配并初始化一个进程控制块，但alloc_proc只是找到了一小块内存用以记录进程的必要信息，并没有实际分配这些资源。ucore一般通过do_fork实际创建新的内核线程。do_fork的作用是，创建当前内核线程的一个副本，它们的执行上下文、代码、数据都一样，但是存储位置不同。因此，我们实际需要"fork"的东西就是stack和trapframe。在这个过程中，需要给新内核线程分配资源，并且复制原进程的状态。你需要完成在kern/process/proc.c中的do_fork函数中的处理过程。它的大致执行步骤包括：

- 调用alloc_proc，首先获得一块用户信息块。
- 为进程分配一个内核栈。
- 复制原进程的内存管理信息到新进程（但内核线程不必做此事）
- 复制原进程上下文到新进程
- 将新进程添加到进程列表
- 唤醒新进程
- 返回新进程号
请在实验报告中简要说明你的设计实现过程。请回答如下问题：

- 请说明ucore是否做到给每个新fork的线程一个唯一的id？请说明你的分析和理由。

```C
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;

    // 1. call alloc_proc to allocate a proc_struct
    if((proc= alloc_proc())==NULL)
    {//如果分配进程失败了，就直接退出
        goto fork_out;
    };
    //proc->parent=current;//fork出来的子进程的父进程是current
    // 2. call setup_kstack to allocate a kernel stack for child process
    if((ret=setup_kstack(proc))==-E_NO_MEM)
    {//如果没有分配下内核堆，就相当于失败了，就要清除掉之前的proc再return
        goto bad_fork_cleanup_proc;
    };
    //每一个内核线程都会专门申请一块内存区域作为自己的堆栈，而不是共用其他内核的堆栈。除了idleproc使用的是内核堆栈
    // 3. call copy_mm to dup OR share mm according clone_flag
    // 复制原来的进程的内存管理信息到新的进程proc当中
    copy_mm(clone_flags,proc);
    // 4. call copy_thread to setup tf & context in proc_struct
    copy_thread(proc,stack,tf);
    // 5. insert proc_struct into hash_list && proc_list
    //需要关闭中断来执行以下：
    proc->pid=get_pid();
    list_add(hash_list+pid_hashfn(proc->pid),&(proc->hash_link));
    list_add(&proc_list,&proc->list_link);
    nr_process++;
    // 6. call wakeup_proc to make the new child process RUNNABLE
    wakeup_proc(proc);
    // 7. set ret vaule using child proc's pid
    ret=proc->pid;

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}
```

##### ucore通过`get_pid()`函数为每个新fork的进程分配唯一的PID

`get_pid()`函数负责为ucore操作系统中的每个新创建的进程分配一个唯一的进程标识符（PID）。以下是该函数的工作机制和逻辑流程：

1. **初始化与首次调用**：
   - 在`get_pid()`首次被调用时，静态变量`last_pid`会被递增并重置为1，因此返回的第一个PID是1。

2. **简单的线性增长**：
   - 对于前几次调用（直到`last_pid < MAX_PID`），函数直接返回递增后的`last_pid`值。例如，第二次调用会返回2，依此类推。

3. **处理PID空间循环**：
   - 当`last_pid`达到或超过`MAX_PID`时，它会被重置为1，并进入更复杂的逻辑来查找未使用的PID。
   - 进入`inside`标签后，遍历整个进程链表以检查是否存在与当前尝试分配的`last_pid`相同的PID。
   - 如果找到匹配项，则`last_pid`递增，并重新开始搜索；否则，更新`next_safe`为遇到的最大PID。

4. **确保唯一性**：
   - 如果在遍历过程中发现了一个等于`last_pid`的PID，`last_pid`会被递增，避免冲突。
   - 如果`last_pid >= next_safe`，则重新开始搜索（`last_pid`重置为1）。
   - 如果当前遍历到的PID比`last_pid`大但仍在`next_safe`范围内，`next_safe`会被更新为当前的PID。
   - 通过这种方式，如果有与`last_pid`相同的PID，`last_pid`将递增；如果没有找到匹配项，`next_safe`会被设置为`MAX_PID`，并且继续遍历直到找到可用的PID。

5. **接近满载时的行为**：
   - 当调用接近`next_safe-1`时，`next_safe`会被重置为`MAX_PID`，并继续遍历链表以确保没有重复的PID。
   - 如果有与`last_pid`相同的PID，`last_pid`递增；如果没有，则继续遍历，确保最终能找到一个唯一的PID。

##### 结论

通过上述机制，`get_pid()`函数能够在大多数情况下为每个新创建的进程分配一个唯一的PID。然而，需要注意的是，这种实现方式在高并发环境下可能需要额外的同步措施以防止竞争条件，并且在PID空间接近满载时效率较低。总体来说，`get_pid()`的设计旨在确保每次调用都能找到一个未被使用的PID，从而保障了进程ID的唯一性。


这段代码通过维护一个静态变量`last_pid`来实现为每个新fork的线程分配一个唯一的id。让我们逐步分析：

1. `last_pid`是一个静态变量，它会记录上一个分配的pid。
2. 当`get_pid`函数被调用时，首先检查是否`last_pid`超过了最大的pid值（`MAX_PID`）。如果超过了，将`last_pid`重新设置为1，从头开始分配。
3. 如果`last_pid`没有超过最大值，就进入内部的循环结构。在循环中，它遍历进程列表，检查是否有其他进程已经使用了当前的`last_pid`。如果发现有其他进程使用了相同的pid，就将`last_pid`递增，并继续检查。
4. 如果没有找到其他进程使用当前的`last_pid`，则说明`last_pid`是唯一的，函数返回该值。

这样，通过这个机制，每次调用`get_pid`都会尽力确保分配一个未被使用的唯一pid给新fork的线程。

#### 练习3：编写 proc_run 函数（需要编码）

*proc_run 用于将指定的进程切换到 CPU 上运行。请回答如下问题：*

* 在本实验的执行过程中，创建且运行了几个内核线程？*

#### 问题1

根据文档的提示说明，我们编写的proc_run()函数如下：

```c++
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        // LAB4:EXERCISE3 2212046
        /*
        * Some Useful MACROs, Functions and DEFINEs, you can use them in below implementation.
        * MACROs or Functions:
        *   local_intr_save():        Disable interrupts
        *   local_intr_restore():     Enable Interrupts
        *   lcr3():                   Modify the value of CR3 register
        *   switch_to():              Context switching between two processes
        */
        uint32_t intr_flag;
        struct proc_struct *prev = current, *next = proc;
        local_intr_save(intr_flag);
        {   
            current = proc;
            lcr3(proc->cr3);
            switch_to(&(prev->context), &(next->context));
        }
        local_intr_restore(intr_flag);
    }
}
```

此函数**基本思路**是：

- 让 current指向 next内核线程initproc；
- 设置 CR3 寄存器的值为 next 内核线程 initproc 的页目录表起始地址 next->cr3，这实际上是完成进程间的页表切换；
- 由 switch_to函数完成具体的两个线程的执行现场切换，即切换各个寄存器，当 switch_to 函数执行完“ret”指令后，就切换到initproc执行了。

值得注意的是，这里我们使用`local_intr_save()`和`local_intr_restore()`，作用分别是屏蔽中断和打开中断，以免进程切换时其他进程再进行调度，保护进程切换不会被中断。

####  问题2

在本实验中，创建且运行了2两个内核线程：

- idleproc：第一个内核进程，完成内核中各个子系统的初始化，之后立即调度，执行其他进程。
- initproc：用于完成实验的功能而调度的内核进程。

###@ .扩展练习 Challenge：

*说明语句 local_intr_save(intr_flag);....local_intr_restore(intr_flag); 是如何 实现开关中断的？*

#### 回答：

这两句分别是kern/sync.h中定义的中断前后使能信号保存和退出的函数。

```c++
#include <defs.h>
#include <intr.h>
#include <riscv.h>

static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);

#endif /* !__KERN_SYNC_SYNC_H__ */
```

这两个函数在当时Lab1中有所涉及，主要作用是首先通过定义两个宏函数local_intr_save和local_intr_restore。这两个宏函数会调用两个内联函数intr_save和intr_restore。其中：

1. **intr_save和local_intr_save：**
   * **intr_save：**通过**读取CSR控制和状态寄存器的sstatus中的值**，并对比其是否被设置为了SIE=1，即中断使能位=1。如果中断使能位SIE是1，那么表示中断是被允许的；为0就是不允许的。因此如果中断本来是允许的，就会调用intr.h中的intr_disable禁用中断，否则直接返回，因此本身也不允许。
   * **local_intr_save：**在这个函数其中，用 do-while循环可以确保 **x变量在__intr_save() 函数调用之后被正确赋值，无论中断是否被禁用。**
2. **intr_restore和local_intr_restore：**
   * **intr_restore：**直接根据flag标志位是否为0，intr_enable()重新启用中断。
   * **local_intr_restore：**与save不同，无需返回任何值，恢复中断即可。
在函数schedule()中，实现了FIFO策略的进程调度，而为了保证进程上下文切换这一操作的原子性，则需要通过local_intr_save(intr_flag);....local_intr_restore(intr_flag);这一对内联函数实现中断状态的关闭、保存以及再开启，形成临界区来保证代码的正确执行。

在riscV的架构下，中断的开关状态由sstatus寄存器中的SIE位（System Interrupt Enable中断使能位）进行控制，通过读取这一字段，可以实现中断的关闭以及恢复操作。具体如下：

由此将两个宏定义函数结合起来，local_intr_save(intr_flag);....local_intr_restore(intr_flag)就可以实现在一个进程发生切换前禁用中断，切换后重新启用中断，以实现开关中断，保证进程切换原子性的目的。

在Kernel_Thread函数中，就设置SSTATUS的SPIE,SIE等标志位实现控制中断开关。

```c++
 local_intr_save(intr_flag);
        {
            current = proc;
            lcr3(next->cr3);
            switch_to(&(prev->context), &(next->context));
       }
local_intr_restore(intr_flag);
```
