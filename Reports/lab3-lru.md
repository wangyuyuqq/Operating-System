# lab3 swap-lru 设计文档

### 王昱

## 设计原理

LRU（Least Recently Used）是一种常见的页面替换算法，用于内存管理，特别是在虚拟内存系统中。当物理内存不足时，LRU算法会选择最长时间未被访问的页面进行替换，以腾出空间给新的页面。LRU算法的核心设计原理是，优先替换在一段时间内最长时间未被访问的页面，因为我们认为最长时间未被访问的页面在未来被访问的可能性最低，因此优先替换这些页面。

本次我使用了两种方法实现LRU，第二种基于时钟更加接近LRU。

## 设计过程说明（一）

在此设计中，通过使用双向链表来实现 LRU 算法。链表中的每个节点表示一个页面，节点的顺序反映页面的访问顺序，尾部为最新访问，头部为最久未访问。具体实现步骤如下：

1. **初始化链表结构**：在`_lru_init_mm`函数中，初始化双向链表`pra_list_head`，并将`curr_ptr`指向链表头。同时，将`mm->sm_priv`指向`pra_list_head`，便于其他函数访问 LRU 链表。

```c
_lru_init_mm(struct mm_struct *mm)
{
    list_init(&pra_list_head);
    curr_ptr = &pra_list_head;
    mm->sm_priv = &pra_list_head;
    return 0;
}
```
2. **页面访问管理**：在每次将页面设置为可交换时（通过`_lru_map_swappable`），将最近访问的页面添加到链表末尾。链表尾部表示最近被访问的页面，而头部表示最久未被访问的页面。
```c
static int _lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in) {
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    list_entry_t *entry = &(page->pra_page_link);
    assert(entry != NULL && head != NULL);
    list_entry_t *le;
    bool found = false;
    for (le = list_next(head); le != head; le = list_next(le)) {
        if (le == entry) {
            found = true;
            break;
        }
    }
    if (!found) {
        list_add_before(head, entry);
    } else {
        list_del(entry);
        list_add_before(head, entry);
    }
    return 0;
}

```
3. **页面置换选择**：在`_lru_swap_out_victim`函数中，通过遍历链表选择最久未被访问的页面（即链表头部的页面）进行置换。
```c
static int _lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in) {
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    list_entry_t *entry = &(page->pra_page_link);

    assert(entry != NULL && head != NULL);
    list_entry_t *le;
    bool found = false;
    for (le = list_next(head); le != head; le = list_next(le)) {
        if (le == entry) {
            found = true;
            break;
        }
    }
    if (!found) {
        list_add_before(head, entry);
    } else {
        list_del(entry);
        list_add_before(head, entry);
    }
    return 0;
}

```
## 设计过程说明（二）

在本设计的 LRU 实现中，通过维护一个双向链表来记录页面的访问顺序。具体操作思路如下：模拟时钟中断的方式，每次触发时对链表进行刷新，遍历链表中的所有页面，检查其visited标志位。visited标志位相当于硬件中 A 位（Access 位）的作用，用于标记页面是否在当前时间段内被访问过。如果页面的visited位为1，表示该页面在最近被访问过，将其移动到链表尾部，代表最新被访问；否则，页面位置不变，意味着它是较少被访问的页面。

这样一来，链表的头部自然会保持最久未被访问的页面，而尾部则是最近访问的页面。当需要进行页面置换时，只需从链表头部选择页面进行换出，即实现了 LRU 的页面替换策略。在产生缺页异常（pgfault）时，会触发页面换入或换出的操作，从而实现对内存的有效管理。

这里主要修改__lru_tick_event函数和_lru_map_swappable函数：

1. **页面访问**：通过在访问的时候手动设置当前page页表项的visit位，说明该页被访问 使其在下文的刷新中可以被发现并放置到最前端。
```c
static int
_lru_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    list_entry_t *entry=&(page->pra_page_link);
 
    assert(entry != NULL && head != NULL);
    //record the page access situlation

    //(1)link the most recent arrival page at the back of the pra_list_head qeueue.
    list_add(head, entry);
    page->visited=1;
    return 0;
}
```

2. **访问状态刷新**：在没有真实的时钟中断机制的情况下，我们模拟了时钟事件。在每次调用`_lru_tick_event`时，遍历链表，对每个页面的`visited`位进行检查：
   - 如果页面的`visited`位为1，说明该页面在最近时间段内被访问过，需将其移动到链表末尾，表示最新访问。
   - 如果页面的`visited`位为0，则不移动页面位置，表明其为较少被访问的页面，仍然留在链表前部。
```c
static int 
_lru_tick_event(struct mm_struct *mm) {
    list_entry_t *head = (list_entry_t*)mm->sm_priv;
    list_entry_t *entry = list_next(head);

    while (entry != head) {
        struct Page *page = le2page(entry, pra_page_link);

        if (page->visited) {
            list_del(entry);                
            list_add_before(head, entry);   
            page->visited = 0;             
            entry = list_next(head);
        } else {
            entry = list_next(entry);
        }
    }
    return 0;
}
```
该设计更加接近LRU的实现，其刷新机制相当于在每个时钟周期内清理和更新页面的访问状态，既防止了 visited 位累积影响精度，也更加精确地模拟了页面的访问顺序，使 LRU 替换算法的实现更加高效和准确。

## 验证LRU正确性
```c
static int
_lru_check_swap(void) {
    cprintf("write Virt Page c in lru_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==4);
    cprintf("write Virt Page a in lru_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==4);
    cprintf("write Virt Page d in lru_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==4);
    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==4);
    cprintf("write Virt Page e in lru_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==5);
    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==5);
    cprintf("write Virt Page a in lru_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==6);
    cprintf("write Virt Page b in lru_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==7);
    cprintf("write Virt Page c in lru_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==8);
    cprintf("write Virt Page d in lru_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==9);
    cprintf("write Virt Page e in lru_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==10);
    cprintf("write Virt Page a in lru_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==11);
    cprintf("LRU test passed!\n");
    return 0;
}

````

在 `_lru_check_swap` 函数中，通过一系列内存访问操作和断言来验证 LRU（最近最少使用）页面替换算法的正确性。

访问顺序依次为：`c, a, d, b, e, b, a, b, c, d, e, a`。以下是各步的详细分析：

1. **前四次访问（c, a, d, b）：**
   - 这些页面初始状态已在内存中，访问时不会触发缺页异常，因此 `pgfault_num` 保持不变，为 `4`。
   - LRU 算法会将每次访问的页面移至链表的尾部，表示该页面为最近访问的页面。此时，链表顺序更新为 `cadb`。

2. **第五次访问 `e`：**
   - 页面 `e` 不在内存中，触发缺页异常，因此 `pgfault_num` 增加至 `5`。
   - 根据 LRU 逻辑，链表首部的 `c` 是最久未被访问的页面，因此 `c` 被换出，链表更新为 `adbe`。

3. **第六至第八次访问（b, a, b）：**
   - `b` 和 `a` 已在内存中，访问不会触发缺页异常，`pgfault_num` 保持不变，但链表更新顺序。此时链表顺序更新为 `abe`。

4. **第九次访问 `c`：**
   - 页面 `c` 不在内存中，触发缺页异常，`pgfault_num` 增加至 `8`。
   - 根据 LRU 逻辑，链表首部的 `d` 是最久未被访问的页面，因此 `d` 被换出，链表更新为 `abec`。

5. **第十至第十二次访问（d, e, a）：**
   - 访问 `d` 时触发缺页异常，`pgfault_num` 增加至 `9`，链表进一步调整。
   - 访问 `e` 和 `a` 时，页面在内存中，不触发缺页异常，但顺序更新。


设置 `assert(pgfault_num == 11)`。通过该访问顺序和缺页次数，可以验证当前实现的是 LRU 页面替换算法，而非 FIFO 算法，因为替换策略正确地按照 LRU 逻辑工作。


## 设计思路总结

通过链表顺序和`visited`标志位结合，完成了 LRU 页面置换。在页面置换时，仅需移除链表头的页面。在刷新时，通过将活跃页面移到链表末尾来维护最近访问的页面顺序，从而实现有效的 LRU 页面管理。

