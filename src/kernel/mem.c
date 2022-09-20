#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>


RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt) {
    init_rc(&alloc_page_cnt);
}


extern char end[];
static QueueNode* pages;
static QueueNode* free_mem[12];
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

int __log2(i64 num) {
    int ret = 0;

    while (num & 1) {
        num >>= 1;
        ret++;
    }

    return ret;
}

// TODO: kalloc kfree
void* kalloc(isize size) {
    size += 1;
    int pos = __log2(size), log_size = __log2(size);
    while (pos < 12 && free_mem[pos] == NULL) {
        pos++;
    }

    if (pos != 12) {
        return (void*)fetch_from_queue(&free_mem[pos]);
    }

    void* new_addr = kalloc_page();
    for (int i = 11; i > log_size; --i) {
        add_to_queue(&free_mem[i], (QueueNode*) (new_addr + (1 << i)) );
    }

    *((char*) new_addr) = (char)log_size;
    return new_addr;
}

void kfree(void* p) {
    i64 size = (i64)(*(char*) (p));
    add_to_queue(&free_mem[size], (QueueNode*) (p) );
}
