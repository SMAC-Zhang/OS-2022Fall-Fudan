#include <common/rc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <common/list.h>
#include <driver/memlayout.h>
#include <kernel/printk.h>
#include <common/defines.h>



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
    struct my_list_node *next;
};

// add and merge
// if merge, return new_addr
// or return NULL
struct my_list_node* add_to_list(struct my_list_node** head, struct my_list_node* node, u64 size) {
    if (*head == NULL) {
        *head = node;
        (*head)->next = NULL;
        return NULL;
    }

    if ((u64)(*head) > (u64)node) {
        node->next = *head;
        *head = node;
        if ((u64)node - (u64)(*head) == size) {
            *head = (*head)->next;
            return node;
        }
        return NULL;
    }

    struct my_list_node *p = *head, *prev = *head;
    while (p) {
        if ((u64)p < (u64)node && (
        (p->next && (u64)(p->next) > (u64)node)
        || (p->next == NULL))
        ) {
            node->next = p->next;
            p->next = node;
            break;
        }
        p = p->next;
        if (prev->next != p) {
            prev = prev->next;
        }
    }
    if ((u64)node - (u64)p == size) {
        prev->next = node->next;
        return p;
    }
    if (node->next && (u64)(node->next) - (u64)node == size) {
        p->next = node->next->next;
        return node;
    }

    return NULL;
    // node->next = (*head)->next;
    // (*head)->next = node;
}

void wrap_add(struct my_list_node* node, u64 log_size) {
    auto p = node;
    auto i = log_size;
    do {
        if (i == 12) {
            kfree_page(p);
            break;
        }
        p = add_to_list(&free_list[cpuid()][log_size], p, 1 << i);
        i++;
    } while (p);
}

struct my_list_node* fetch_from_list(struct my_list_node** head) {
    struct my_list_node *ret = NULL;
    if (*head == NULL || (*head)->next == NULL) {
        ret = *head;
        *head = NULL;
        return ret;
    }
    ret = (*head)->next;
    (*head)->next = (*head)->next->next;
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
    while (pos < 12 && free_list[cpuid()][pos] == NULL) {
        pos++;
    }
    
    void *ret_addr;
    if (pos != 12) {
        ret_addr = fetch_from_list(&free_list[cpuid()][pos]);
    } else {
        ret_addr = kalloc_page();
    }

    for (int i = pos - 1; i >= log_size; --i) {
        wrap_add((struct my_list_node*)(ret_addr + (1 << i)), (u64)i);
        //add_to_list(&free_list[cpuid()][i], (struct my_list_node*)(ret_addr + (1 << i)));
    }

    *((i64*) ret_addr) = log_size;
    return ret_addr + 8;
}

void kfree(void* p) {
    p -= 8;
    i64 log_size = *(i64*) (p);
    wrap_add((struct my_list_node*)p, log_size);
    //add_to_list(&free_list[cpuid()][log_size], (struct my_list_node*)p);
}
