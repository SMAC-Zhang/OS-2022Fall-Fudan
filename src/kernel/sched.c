#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <driver/clock.h>
#include <common/spinlock.h>
#include <common/string.h>

extern bool panic_flag;

extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);

static SpinLock sched_lock;
static ListNode rq;

define_early_init(rq) {
    init_spinlock(&sched_lock);
    init_list_node(&rq);
}
define_init(sched) {
    for (int i = 0; i < NCPU; i++) {
        struct proc* p = kalloc(sizeof(struct proc));
        memset(p, 0, sizeof(struct proc));
        p->idle = true;
        p->state = RUNNING;
        cpus[i].sched.thisproc = cpus[i].sched.idle = p;
    }                              
}

struct proc* thisproc() {
    // TODO: return the current process
    return cpus[cpuid()].sched.thisproc;
}

void init_schinfo(struct schinfo* p) {
    // TODO: initialize your customized schinfo for every newly-created process
    init_list_node(&p->rq);
}

void _acquire_sched_lock() {
    // TODO: acquire the sched_lock if need
    _acquire_spinlock(&sched_lock);
}

void _release_sched_lock() {
    // TODO: release the sched_lock if need
    _release_spinlock(&sched_lock);
}

bool is_zombie(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == ZOMBIE;
    _release_sched_lock();
    return r;
}

bool is_unused(struct proc* p)
{
    bool r;
    _acquire_sched_lock();
    r = p->state == UNUSED;
    _release_sched_lock();
    return r;
}

bool activate_proc(struct proc* p)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    _acquire_sched_lock();
    if (p->state == RUNNABLE || p->state == RUNNING || p->state == ZOMBIE) {
        _release_sched_lock();
        return false;
    }

    p->state = RUNNABLE;
    _insert_into_list(&rq, &(p->schinfo.rq));
    _release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state) {
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    thisproc()->state = new_state;
    ASSERT(new_state != RUNNING);
    if (new_state == SLEEPING || new_state == ZOMBIE) {
        _detach_from_list(&(thisproc()->schinfo.rq));
    }
}

static struct proc* pick_next() {
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    _for_in_list(p, &rq) {
        if (p == &rq) {
            continue;
        }
        auto proc = container_of(p, struct proc, schinfo.rq);
        if (proc->state == RUNNABLE) {
            return proc;
        }
    }
    return cpus[cpuid()].sched.idle;
}

static void update_this_proc(struct proc* p) {
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    reset_clock(1000);
    cpus[cpuid()].sched.thisproc = p;
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
static void simple_sched(enum procstate new_state)
{
    auto this = thisproc();
    if (this->killed && new_state != ZOMBIE) {
        _release_sched_lock();
        return;
    }
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this)
    {
        attach_pgdir(&next->pgdir);
        swtch(next->kcontext, &this->kcontext);
    }
    _release_sched_lock();
}

__attribute__((weak, alias("simple_sched"))) void _sched(enum procstate new_state);

u64 proc_entry(void(*entry)(u64), u64 arg)
{
    _release_sched_lock();
    set_return_addr(entry);
    return arg;
}

