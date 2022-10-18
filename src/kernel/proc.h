#pragma once

#include <common/defines.h>
#include <common/list.h>
#include <common/sem.h>
#include <kernel/schinfo.h>
#include <kernel/pt.h>
#include <common/rbtree.h>

enum procstate { UNUSED, RUNNABLE, RUNNING, SLEEPING, ZOMBIE };

typedef struct pid_map_pcb {
    int pid;
    struct proc* pcb;
    struct rb_node_ node;
} pid_map_pcb_t;

typedef struct pid_pcb_tree {
    struct rb_root_ root;
} pid_pcb_tree_t;

typedef struct UserContext {
    // TODO: customize your trap frame
    u64 spsr, elr;
    u64 sp, fuck_gcc;
    u64 x[18];
} UserContext;

typedef struct KernelContext {
    // TODO: customize your context
    u64 lr, x0, x1; 
    u64 x[11]; // x19 - x29
} KernelContext;

struct proc
{
    bool killed;
    bool idle;
    int pid;
    int exitcode;
    enum procstate state;
    Semaphore childexit;
    ListNode children;
    ListNode zombie_children;
    ListNode ptnode;
    struct proc* parent;
    struct schinfo schinfo;
    struct pgdir pgdir;
    void* kstack;
    UserContext* ucontext;
    KernelContext* kcontext;
};

// void init_proc(struct proc*);
struct proc* create_proc();
int start_proc(struct proc*, void(*entry)(u64), u64 arg);
NO_RETURN void exit(int code);
int wait(int* exitcode);
int kill(int pid);
