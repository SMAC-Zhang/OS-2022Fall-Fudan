#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>
#include <common/defines.h>

#define get_page_num(addr)  ((i64)(addr) & 0xfffffffffffff000)

RefCount alloc_page_cnt;

define_early_init(alloc_page_cnt) {
    init_rc(&alloc_page_cnt);
}

extern char end[];
static QueueNode* pages;
static struct my_list_node* free_list[4][12];
define_early_init(init_page) {
    for (u64 p = PAGE_BASE((u64)&end) + PAGE_SIZE; p < P2K(PHYSTOP); p += PAGE_SIZE) {
        add_to_queue(&pages, (QueueNode*)p); 
    }
}

struct my_list_node {
    u64    length;
    struct my_list_node *prev,  *next;
};

// add and merge
// if merge, return new pointer
// or return NULL
struct my_list_node* add_to_list(struct my_list_node** head, struct my_list_node* node, u64 size) {
    if (*head == NULL) {
        node->next = NULL;
        node->prev = NULL;
        *head = node;
        node->length = size * 2 + 1;
        return NULL;
    }

    struct my_list_node* next_addr = (void*)node + size;
    if (((u64)node & size) == 0 && get_page_num(next_addr) == get_page_num(node) && next_addr->length == size * 2 + 1) {
        if (next_addr == *head) {
            *head = (*head)->next;
        } else {
            next_addr->prev->next = next_addr->next;
        }
        next_addr->length = 0;
        return node;
    }

    struct my_list_node* prev_addr = (void*)node - size;
    if (((u64)prev_addr & size) == 0 && get_page_num(prev_addr) == get_page_num(node) && prev_addr->length == size * 2 + 1) {
        if (prev_addr == *head) {
            (*head) = (*head)->next;
        } else {
            prev_addr->prev->next = prev_addr->next;
        }
        prev_addr->length = 0;
        return prev_addr;
    }
    
    (*head)->prev = node;
    node->next = (*head);
    node->prev = NULL;
    (*head) = node;
    node->length = size * 2 + 1;
    return NULL;
}

// a function that wraps the add operation
// merge up the way if it could
void wrap_add(struct my_list_node* node, u64 log_size) {
    auto p = node;
    auto i = log_size;
    do {
        if (i == 12) {
            kfree_page(p);
            // static int cnt = 0;
            // printk("%d ", ++cnt); //print the number of page_free because of merge
            return;
        }
        p->length = 0;
        p = add_to_list(&free_list[cpuid()][i], p, 1 << i);
        i++;
    } while (p != NULL);
}

struct my_list_node* fetch_from_list(struct my_list_node** head) {
    struct my_list_node *ret = NULL;
    if (*head == NULL || (*head)->next == NULL) {
        ret = *head;
        *head = NULL;
        return ret;
    }
    ret = (*head);
    (*head) = (*head)->next;
    return ret;
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


u64 __log2(i64 num) {
    i64 ret = 0;
    i64 t = num;

    while (num) {
        num >>= 1;
        ret++;
    }
    if ((1 << (ret - 1)) == t) {
        ret--;
    }

    return (u64)ret;
}

// TODO: kalloc kfree
void* kalloc(isize size) {
    size += 8;

    size = (size + 31) / 32 * 32;

    // find a free block
    u64 pos = __log2(size), log_size = __log2(size);
    while (pos < 12 && free_list[cpuid()][pos] == NULL) {
        pos++;
    }
    
    // if found, get it
    // or kalloc a new page
    void *ret_addr;
    if (pos != 12) {
        ret_addr = fetch_from_list(&free_list[cpuid()][pos]);
    } else {
        ret_addr = kalloc_page();
    }

    // cut off the block to have a fit space 
    // the rest space would be added to the list
    // void *rest_addr = ret_addr + size;
    // u64 rest_size = (1 << pos) - size;
    // for (int i = 0; i < 12; ++i) {
    //     if ((rest_size >> i) & 1) {
    //         wrap_add((struct my_list_node*)rest_addr, (u64)i);
    //         rest_addr += 1 << i;
    //     }
    // }
    // *((u64*) ret_addr) = size * 2;

    for (int i = pos - 1; i >= (i64)log_size; --i) {
        wrap_add((struct my_list_node*)(ret_addr + (1 << i)), (u64)i);
    }
    *((u64*) ret_addr) = log_size;
    
    return ret_addr + 8;
}

void kfree(void* p) {
    p -= 8;

    u64 log_size = *(u64*) (p);
    wrap_add((struct my_list_node*)p, log_size);
    // u64 size = *(u64*) (p) / 2;
    // for (int i = 11; i >= 0; --i) {
    //     if ((size >> i) & 1) {
    //         wrap_add((struct my_list_node*)p, i);
    //         p += 1 << i;
    //     }
    // }
}
