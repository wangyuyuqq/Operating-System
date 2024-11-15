#include <defs.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_fifo.h>
#include <swap_lru.h>
#include <list.h>

extern list_entry_t pra_list_head, *curr_ptr;

/*
 * (2) _lru_init_mm: 初始化pra_list_head并让mm->sm_priv指向它
 */
static int
_lru_init_mm(struct mm_struct *mm)
{
    list_init(&pra_list_head);
    curr_ptr = &pra_list_head;
    mm->sm_priv = &pra_list_head;
    return 0;
}

/*
 * (3) _lru_map_swappable: 按照LRU算法将最最近访问的页面添加到队列尾部
 */
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


/*
 * (4) _lru_swap_out_victim: 按照LRU算法选择一个待换出的页面
 */
static int _lru_swap_out_victim(struct mm_struct *mm, struct Page **ptr_page, int in_tick) {
    list_entry_t *head = (list_entry_t *)mm->sm_priv;
    assert(head != NULL);
    assert(in_tick == 0);
    list_entry_t *entry = list_next(head);
    if (entry != head) {
        cprintf("curr_ptr %p\n",entry);  
        list_del(entry);                 
        *ptr_page = le2page(entry, pra_page_link);  
    } else {
        *ptr_page = NULL;
    }
    return 0;
}


/*
 * (5) _lru_check_swap: 检查交换的正确性
 */
static int
_lru_check_swap(void)
{
    cprintf("write Virt Page c in fifo_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 4);
    cprintf("write Virt Page a in fifo_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 4);
    cprintf("write Virt Page d in fifo_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 4);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 4);
    cprintf("write Virt Page e in fifo_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 5);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 5);
    cprintf("write Virt Page a in fifo_check_swap\n");
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 6);
    cprintf("write Virt Page b in fifo_check_swap\n");
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num == 7);
    cprintf("write Virt Page c in fifo_check_swap\n");
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num == 8);
    cprintf("write Virt Page d in fifo_check_swap\n");
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num == 9);
    cprintf("write Virt Page e in fifo_check_swap\n");
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num == 10);
    cprintf("write Virt Page a in fifo_check_swap\n");
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num == 11);
    return 0;
}


static int
_lru_init(void)
{
    return 0;
}

static int
_lru_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int 
_lru_tick_event(struct mm_struct *mm) {
    list_entry_t *head = (list_entry_t*)mm->sm_priv;
    list_entry_t *entry = list_next(head);

    while (entry != head) {
        struct Page *page = le2page(entry, pra_page_link);

        if (page->visited) {
            // 如果页面被访问过，将其移动到队列末尾
            list_del(entry);                 // 从链表中移除
            list_add_before(head, entry);   // 插入到队列末尾
            page->visited = 0;              // 重置访问标志
            // 移动后继续处理下一个页面，跳过当前 entry
            entry = list_next(head);
        } else {
            // 如果页面未被访问过，不做任何移动
            entry = list_next(entry);
        }
    }
    return 0;
}


/*
 * LRU Swap Manager 实现
 */
struct swap_manager swap_manager_lru = {
    .name            = "lru swap manager",
    .init            = &_lru_init,
    .init_mm         = &_lru_init_mm,
    .tick_event      = &_lru_tick_event,
    .map_swappable   = &_lru_map_swappable,
    .set_unswappable = &_lru_set_unswappable,
    .swap_out_victim = &_lru_swap_out_victim,
    .check_swap      = &_lru_check_swap,
};
