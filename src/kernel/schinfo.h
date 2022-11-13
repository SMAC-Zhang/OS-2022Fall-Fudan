#pragma once

#include <common/list.h>
#include <common/rbtree.h>
struct proc; // dont include proc.h here

// embedded data for cpus
struct sched {
    // TODO: customize your sched info
    struct proc* thisproc;
    struct proc* idle;
};

// embeded data for procs
struct schinfo {
    // TODO: customize your sched info
    u64 vruntime;
    int prio;
    int weight;
    struct rb_node_ node;
};
