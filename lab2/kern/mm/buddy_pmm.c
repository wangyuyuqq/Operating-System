#include <pmm.h>
#include <list.h>
#include <string.h>
#include <buddy_pmm.h>
#include <stdio.h>
#include <memlayout.h>

free_area_t free_area[11]; 

#define MAX_ORDER 10

static void buddy_init(void) {
    for (int i = 0; i <= MAX_ORDER; i++) {
        list_init(&free_area[i].free_list);
        free_area[i].nr_free = 0;
    }
}

static void buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    int order = MAX_ORDER;
    while ((1 << order) > n) {
        order--;
    }
    list_add(&free_area[order].free_list, &(base->page_link));
    base->property = (1 << order);
    SetPageProperty(base);
    free_area[order].nr_free++;
}

static struct Page *buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > (1 << MAX_ORDER)) return NULL;

    int order = 0;
    while ((1 << order) < n) {
        order++;
    }
    // 查找适合的块
    for (int current_order = order; current_order <= MAX_ORDER; current_order++) {
        if (!list_empty(&free_area[current_order].free_list)) {
            list_entry_t *le = list_next(&free_area[current_order].free_list);
            list_del(le);  
            free_area[current_order].nr_free--;
            struct Page *page = le2page(le, page_link);
            // 拆分较大的块，直到我们找到合适的大小
            while (current_order > order) {
                current_order--;
                // 创建一个 buddy 块
                struct Page *buddy = page + (1 << current_order);
                buddy->property = (1 << current_order);
                SetPageProperty(buddy);
                // 将 buddy 块放回空闲列表中
                list_add(&free_area[current_order].free_list, &(buddy->page_link));
                free_area[current_order].nr_free++;
            }
            // 拆分完成后，再从列表中删除该块
            list_del(le);  
            free_area[current_order].nr_free--;
            ClearPageProperty(page);
            page->property = n;
            return page;
        }
    }
    return NULL;  
}


static void buddy_free_pages(struct Page *base, size_t n) {
    int order = 0;
    while ((1 << order) < n) {
        order++;
    }

    base->property = (1 << order);
    SetPageProperty(base);

    // 开始处理释放和合并
    while (order <= MAX_ORDER) {
        uintptr_t addr = page2pa(base);
        uintptr_t buddy_addr = addr ^ (1 << (PGSHIFT + order));
        struct Page *buddy = pa2page(buddy_addr);
        if (buddy_addr >= npage * PGSIZE || !PageProperty(buddy) || buddy->property != (1 << order)) {
            break;
        }
        list_del(&(buddy->page_link));
        ClearPageProperty(buddy);
        ClearPageProperty(base);
        base = (base < buddy) ? base : buddy;  // 合并到较小地址的块
        order++;
    }
    base->property = (1 << order);
    SetPageProperty(base);
    list_add(&free_area[order].free_list, &(base->page_link));
    free_area[order].nr_free++;  // 更新空闲块数量s
}

static size_t buddy_nr_free_pages(void) {
    size_t total = 0;
    for (int i = 0; i <= MAX_ORDER; i++) {
        total += free_area[i].nr_free * (1 << i);
    }
    return total;
}

static void basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store[MAX_ORDER + 1];
    unsigned int nr_free_store[MAX_ORDER + 1];

    for (int order = 0; order <= MAX_ORDER; order++) {
        free_list_store[order] = free_area[order].free_list;
        nr_free_store[order] = free_area[order].nr_free;
        list_init(&free_area[order].free_list);
        free_area[order].nr_free = 0;
    }

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);

    int total_free = 0;
    for (int order = 0; order <= MAX_ORDER; order++) {
        total_free += free_area[order].nr_free;
    }
    assert(total_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
}


static void buddy_check(void) {
    int count = 0, total = 0;
    // 遍历所有 order，检查每个阶次的空闲页块
    for (int order = 0; order <= MAX_ORDER; order++) {
        list_entry_t *le = &free_area[order].free_list;
        while ((le = list_next(le)) != &free_area[order].free_list) {
            struct Page *p = le2page(le, page_link);
            count++;
            total += p->property;  // 统计空闲块的总大小
        }
    }
    assert(total == buddy_nr_free_pages());
    struct Page *p0 = alloc_pages(26); //32- 32+
    assert(p0!= NULL);
    assert(!PageProperty(p0));
    assert((p0 + 32)->property == 32);
    struct Page *p1 = alloc_pages(26); //32- 32-
    free_pages(p0, 26);
    //这时候邻不是空闲状态就不会合并
    assert(p0->property==32); //32+ 32-
    free_pages(p1, 26);
    //这时候再放开就会合并了
    assert(p0->property==64); //64+
    basic_check();
}

const struct pmm_manager buddy_system_pmm_manager = {
    .name = "buddy_system_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
