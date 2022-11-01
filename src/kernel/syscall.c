#include <kernel/syscall.h>
#include <kernel/sched.h>
#include <kernel/printk.h>
#include <common/sem.h>

void* syscall_table[NR_SYSCALL];

void syscall_entry(UserContext* context)
{
    // TODO
    // Invoke syscall_table[id] with args and set the return value.
    // id is stored in x8. args are stored in x0-x5. return value is stored in x0.
    u64 id = context->x[8];
    u64 x0 = context->x[0];
    u64 x1 = context->x[1];
    u64 x2 = context->x[2];
    u64 x3 = context->x[3];
    u64 x4 = context->x[4];
    u64 x5 = context->x[5];
    if (id < NR_SYSCALL) {
        void* func = syscall_table[id];
        if (func != NULL) {
            context->x[0] = ((u64(*)(u64, u64, u64, u64, u64, u64))func)(x0, x1, x2, x3, x4, x5);
        }
    }
}
