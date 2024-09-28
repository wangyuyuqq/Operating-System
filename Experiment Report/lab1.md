# lab0.5和lab1
## 使用GDB验证启动流程
1.**系统初始化**
![系统初始化](pc.png)
`make gdb`启动。`0x1000`是该系统的复位地址，每次系统都会从复位地址开始运行，以执行相关的初始化和准备工作。
输入指令`l`，得到指令执行的源码
![执行源码](1.png)
可以看到，进入`entry.s`，OpenSBI启动后进行内核栈的分配,再调用`kern_init` 进行内核初始化。
2.执行到`0x1010`后跳转到`0x80000000`，开始执行bootloader
![执行源码](2.png)
![执行源码](3.png)
3.**真正的入口`kern_init`**
`break *0x80200000` 在内核初始化函数处打断点。发现将要执行 `la sp,bootstacktop` 。但此处仍然不是真正的入口点。
![执行源码](4.png)
继续单步执行，直到进入`init.c`中的`kern_init`函数，才真正进入程序内核。
![执行源码](5.png)

## 理解内核启动中的程序入口操作
### 问题
阅读 `kern/init/entry.S`内容代码，结合操作系统内核启动流程，说明指令 `la sp, bootstacktop` 完成了什么操作，目的是什么？ `tail kern_init` 完成了什么操作，目的是什么？
### 回答
在操作系统的启动过程中，尤其是在切换到内核模式之前，通常需要设置一个干净的栈空间以供内核使用。`la sp, bootstacktop`：`la` 指令是 `Load Address`（加载地址）的简写，这里用于将地址 `bootstacktop` 加载到栈指针寄存器 `sp`（Stack Pointer）。
这行代码的目的是初始化栈指针 `sp` 为 `bootstacktop`。 `bootstacktop` 是内核启动时为栈（stack）预留的顶端地址，为接下来的内核代码提供一个栈空间，以保证内核代码执行时有足够的空间用于函数调用、参数传递、临时数据存储等。正确初始化栈指针是启动内核的重要步骤。

操作系统在完成基本的硬件设置和栈指针初始化后，会调用内核的初始化函数，完成更复杂的设置，包括内存管理、设备驱动初始化、文件系统挂载等操作。`tail kern_init`：使用 RISC-V 的 `tail` 指令，它相当于一个无返回的跳转，直接跳转到 `kern_init` 函数。与普通的 `jal` 指令不同，`tail` 不会保存返回地址（不会将返回地址保存在寄存器中），因此不会增加函数调用栈的深度。
目的是跳转到 `kern_init` 是为了开始执行内核的初始化代码，`kern_init` 通常负责内核的进一步初始化，例如设置页表、内存管理器、硬件中断等。这种跳转方式在操作系统启动流程中很常见，因为从引导程序到内核初始化不需要再返回到前面的代码,可以节省栈空间并简化控制流。

## 完善中断处理
根据提示信息补全`trap()`，每100次时钟中断打印一次`100 ticks`，每次中断后重新设置下一次的时钟中断，即调用`clock_set_next_event()` 。否则会一次性输出10行 `100 ticks` 。等`ticks`到100后，打印次数加1，`ticks`归零。

```C
case IRQ_S_TIMER:
     /* LAB1 EXERCISE2   our code :  */
    /*(1)设置下次时钟中断- clock_set_next_event()
     *(2)计数器（ticks）加一
     *(3)当计数器加到100的时候，我们会输出一个`100ticks`表示我们触发了100次时钟中断，同时打印次数（num）加一
    * (4)判断打印次数，当打印次数为10时，调用<sbi.h>中的关机函数关机
    */
    clock_set_next_event();
    ticks++;
    if(ticks==100){
        print_ticks();
        num++;
        ticks=0;
    }
    if(num==10){
        sbi_shutdown();
    }
    break;
```
可以看到每隔一秒钟打印一次`100 ticks`，打印10次结束。
![结果](6.png)


## Challenge 1：描述与理解中断流程

### 问题
1. 描述 uCore 中处理中断异常的流程（从异常的产生开始）。
2. `mov a0，sp` 的目的是什么？
3. `SAVE_ALL` 中寄存器保存在栈中的位置是什么确定的？
4. 对于任何中断，`__alltraps` 中都需要保存所有寄存器吗？请说明理由。

### 回答

1. **uCore 中处理中断异常的流程**  
   当中断或异常发生时，处理器会将当前的 CPU 状态（包括通用寄存器和部分 CSR 寄存器）保存在栈中，并转移控制权至内核的中断或异常处理程序。对于 RISC-V 架构，处理器首先跳转到 `__alltraps` 汇编代码入口，进入后，先通过 `SAVE_ALL` 宏保存当前的上下文。然后，程序会进入 `trap()` 函数，检查 `scause` 寄存器的值，判断中断或异常类型，调用相应的处理函数。处理完成后，控制流回到 `__alltraps`，通过 `RESTORE_ALL` 宏恢复保存的上下文，最后执行 `sret` 返回用户态继续执行。

2. **`mov a0, sp` 的目的**  
   `mov a0, sp` 是将当前栈指针寄存器的值存储到 `a0` 寄存器中。`a0` 是第一个函数参数寄存器，在进入 C 语言的中断处理函数时，内核会将 `trapframe` 传递给处理函数，因此需要将栈指针传递过去。`trapframe` 结构体位于栈上，通过 `sp` 可以访问它。

3. **`SAVE_ALL` 中寄存器保存在栈中的位置**  
   每个寄存器保存的位置是根据栈指针 `sp` 和固定的偏移量计算得出。例如，`STORE x1, 1*REGBYTES(sp)` 将寄存器 `x1` 保存到当前栈指针偏移 `1*REGBYTES` 处。每个寄存器的保存位置在栈中的偏移量根据寄存器编号固定，以便处理完中断后能够按照对应的顺序恢复。

4. **`__alltraps` 中是否需要保存所有寄存器？**  
   是的，通常情况下所有寄存器都需要保存。中断或异常处理可能需要较长时间处理其他任务，因此为了保证中断返回后程序能够继续正确执行，必须保存所有的寄存器内容，以便在处理完成后恢复现场。

## Challenge 2：理解上下文切换机制

### 问题
1. `csrw sscratch, sp; csrrw s0, sscratch, x0` 实现了什么操作？目的是什么？
2. `SAVE_ALL` 里面保存了 `stval` 和 `scause` 这些 CSR，而在 `RESTORE_ALL` 里面却不还原它们？这样 `STORE` 的意义何在呢？

### 回答

1. **`csrw sscratch, sp; csrrw s0, sscratch, x0` 实现的操作**  
   - `csrw sscratch, sp`: 将当前的栈指针 `sp` 保存到 `sscratch` 寄存器中。`sscratch` 是一个临时寄存器，用于保存 CPU 状态（栈指针）的某些信息。
   - `csrrw s0, sscratch, x0`: 将 `sscratch` 的值写入 `s0` 寄存器（也就是保存的栈指针），并将 `x0` 写入 `sscratch`，清空 `sscratch`。

   这样做的目的是为了安全地保存栈指针，以便在中断处理期间不丢失原始的栈指针。

2. **`stval` 和 `scause` 的 `STORE` 意义**  
   保存 `stval` 和 `scause` 是为了记录当前中断或异常的原因信息。这些 CSR 在处理中断或异常时是有用的，特别是在调试和诊断问题时。不过，`stval` 和 `scause` 不需要恢复，因为处理完后它们的值已经无关紧要。保存它们仅是为了在中断处理函数中使用，而不是为了恢复原有值。

## Challenge 3：完善异常中断处理

### 问题
编写代码捕获并处理非法指令和断点异常，输出以下信息：
- "Illegal instruction caught at 0x(地址)"
- "ebreak caught at 0x（地址）"
- "Exception type: Illegal instruction"
- "Exception type: Breakpoint"

### 代码

```c
void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
        case CAUSE_ILLEGAL_INSTRUCTION:
            // 非法指令异常处理
            /* LAB1 CHALLENGE3 */
            /*(1)输出指令异常类型（Illegal instruction）
             *(2)输出异常指令地址
             *(3)更新 tf->epc寄存器
            */
            cprintf("Exception type: Illegal instruction \n");
            cprintf("Illegal instruction caught at 0x%016llx\n", tf->epc);
            // %016llx 格式化为16位的十六进制数字
            tf->epc += 4;  // 更新epc寄存器，跳过当前非法指令
            break;

        case CAUSE_BREAKPOINT:
            // 断点异常处理
            /* LAB1 CHALLENGE3 */
            /*(1)输出指令异常类型（breakpoint）
             *(2)输出异常指令地址
             *(3)更新 tf->epc寄存器
            */
            cprintf("Exception type: Breakpoint \n");
            cprintf("ebreak caught at 0x%016llx\n", tf->epc);
            // ebreak 指令长度为2字节
            tf->epc += 2;  // 更新epc，跳过ebreak指令
            break;

        default:
            print_trapframe(tf);
            break;
    }
}

