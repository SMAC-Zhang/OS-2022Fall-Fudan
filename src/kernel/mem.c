#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>
#include <common/spinlock.h>



RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt) {
    init_rc(&alloc_page_cnt);
}


extern char end[];
static QueueNode* pages;
static QueueNode* free_mem[4][12];
define_early_init(init_page) {
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE) {
        add_to_queue(&pages, (QueueNode*)p); 
    }
}

void* kalloc_page() {
    _increment_rc(&alloc_page_cnt);
    // TODO
    return fetch_from_queue(&pages);
}

void kfree_page(void* p) {
    _decrement_rc(&alloc_page_cnt);
    // TODO
    add_to_queue(&pages, (QueueNode*)p);
}

i64 __log2(i64 num) {
    i64 ret = 0;
    i64 t = num;

    while (num) {
        num >>= 1;
        ret++;
    }
    if ((1 << (ret - 1)) == t) {
        ret--;
    }

    return ret;
}

// TODO: kalloc kfree
void* kalloc(isize size) {
    size += 8;
    i64 pos = __log2(size), log_size = __log2(size);
    while (pos < 12 && free_mem[cpuid()][pos] == NULL) {
        pos++;
    }
    
    void *ret_addr;
    if (pos != 12) {
        ret_addr = fetch_from_queue(&free_mem[cpuid()][pos]);
    } else {
        ret_addr = kalloc_page();
    }

    for (int i = pos - 1; i >= log_size; --i) {
        add_to_queue(&free_mem[cpuid()][i], (QueueNode*)(ret_addr + (1 << i)) );
    }

    *((i64*) ret_addr) = log_size;
    return ret_addr + 8;
}

void kfree(void* p) {
    p -= 8;
    i64 size = *(i64*) (p);
    add_to_queue(&free_mem[cpuid()][size], (QueueNode*)p);
}
