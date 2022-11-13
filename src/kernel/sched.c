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
#include <common/defines.h>

static const int prio_to_weight[40] = {
    /* -20 */ 88761, 71755, 56483, 46273, 36291,
    /* -15 */ 29154, 23254, 18705, 14949, 11916,
    /* -10 */ 9548, 7620, 6100, 4904, 3906,
    /* -5 */ 3121, 2501, 1991, 1586, 1277,
    /* 0 */ 1024, 820, 655, 526, 423,
    /* 5 */ 335, 272, 215, 172, 137,
    /* 10 */ 110, 87, 70, 56, 45,
    /* 15 */ 36, 29, 23, 18, 15
};

extern bool panic_flag;

extern void swtch(KernelContext* new_ctx, KernelContext** old_ctx);

static SpinLock sched_lock;
//static ListNode rq;
static struct rb_root_ rq;
static struct timer proc_cpu_timer[NCPU];
static u64 thisproc_start_time[NCPU];
static const u64 sched_latency = 20;
static u64 weight_sum = 15;

static bool _ptree_cmp(rb_node lnode, rb_node rnode) {
    auto l = container_of(lnode, struct schinfo, node);
    auto r = container_of(rnode, struct schinfo, node);
    if (l->vruntime == r->vruntime) {
        auto lproc = container_of(l, struct proc, schinfo);
        auto rproc = container_of(r, struct proc, schinfo);
        if (lproc->schinfo.prio == rproc->schinfo.prio) {
            return lproc->pid < rproc->pid;
        }
        return lproc->schinfo.prio < rproc->schinfo.prio;
    }
    return l->vruntime < r->vruntime;
}

define_early_init(rq) {
    init_spinlock(&sched_lock);
}

define_init(sched) {
    for (int i = 0; i < NCPU; i++) {
        struct proc* p = kalloc(sizeof(struct proc));
        memset(p, 0, sizeof(struct proc));
        p->idle = true;
        p->state = RUNNING;
        cpus[i].sched.thisproc = cpus[i].sched.idle = p;
        p->schinfo.prio = 19;
        p->schinfo.weight = prio_to_weight[19 + 20];
    }                              
}

struct proc* thisproc() {
    // TODO: return the current process
    return cpus[cpuid()].sched.thisproc;
}

void init_schinfo(struct schinfo* p) {
    // TODO: initialize your customized schinfo for every newly-created process
    p->vruntime = 0;
    p->prio = 1;
    p->weight = prio_to_weight[p->prio + 20];
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

bool _activate_proc(struct proc* p, bool onalert)
{
    // TODO
    // if the proc->state is RUNNING/RUNNABLE, do nothing and return false
    // if the proc->state is SLEEPING/UNUSED, set the process state to RUNNABLE, add it to the sched queue, and return true
    // if the proc->state is DEEPSLEEING, do nothing if onalert or activate it if else, and return the corresponding value.
    _acquire_sched_lock();
    
    // up kill proc's prio
    if (onalert) {
        p->schinfo.prio = -20;
        p->schinfo.weight = prio_to_weight[p->schinfo.prio + 20];
    }
    if (p->state == RUNNABLE || p->state == RUNNING || p->state == ZOMBIE) {
        _release_sched_lock();
        return false;
    }

    if (p->state == DEEPSLEEPING && onalert) {
        _release_sched_lock();
        return false;
    }

    if (p->state == SLEEPING || p->state == UNUSED || p->state == DEEPSLEEPING) {
        auto first = _rb_first(&rq);
        if (first != NULL) {
            auto sch = container_of(first, struct schinfo, node);
            p->schinfo.vruntime = sch->vruntime;
        } else {
            p->schinfo.vruntime = thisproc()->schinfo.vruntime;
        }
    }

    p->state = RUNNABLE;
    ASSERT(_rb_insert(&(p->schinfo.node), &rq, _ptree_cmp) == 0);
    weight_sum += p->schinfo.weight;
    _release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state) {
    // TODO: if using simple_sched, you should implement this routinue
    // update the state of current process to new_state, and remove it from the sched queue if new_state=SLEEPING/ZOMBIE
    auto this = thisproc();
    this->state = new_state;
    this->schinfo.vruntime += (get_timestamp_ms() - thisproc_start_time[cpuid()]) * prio_to_weight[21] / this->schinfo.weight;
    if (this->state == RUNNABLE && this->idle == false) {
        ASSERT(_rb_insert(&(this->schinfo.node), &rq, _ptree_cmp) == 0);
    }
    if (this->state == ZOMBIE) {
        weight_sum -= this->schinfo.weight;
    }
}

static struct proc* pick_next() {
    // TODO: if using simple_sched, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    auto p = _rb_first(&rq);
    if (p != NULL) {
        _rb_erase(p, &rq);
        auto sch = container_of(p, struct schinfo, node);
        auto ret = container_of(sch, struct proc, schinfo);
        ASSERT(ret->state == RUNNABLE);
        return ret;
    }
    if (thisproc()->state == RUNNABLE) {
        return thisproc();
    }
    return cpus[cpuid()].sched.idle;
}

static void proc_interrupt(struct timer* t) {
    _acquire_sched_lock();
    t->data--;
    _sched(RUNNABLE);
}

static void update_this_proc(struct proc* p) {
    // TODO: if using simple_sched, you should implement this routinue
    // update thisproc to the choosen process, and reset the clock interrupt if need
    cpus[cpuid()].sched.thisproc = p;
    if (proc_cpu_timer[cpuid()].data > 0) {
        cancel_cpu_timer(&proc_cpu_timer[cpuid()]);
        proc_cpu_timer[cpuid()].data--;
    }
    proc_cpu_timer[cpuid()].elapse = MAX(sched_latency * p->schinfo.weight / weight_sum, (u64)1);
    proc_cpu_timer[cpuid()].handler = proc_interrupt;
    set_cpu_timer(&proc_cpu_timer[cpuid()]);
    proc_cpu_timer[cpuid()].data++;
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
        thisproc_start_time[cpuid()] = get_timestamp_ms();
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

