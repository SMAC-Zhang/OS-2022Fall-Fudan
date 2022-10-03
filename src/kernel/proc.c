#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>

struct proc root_proc;

void kernel_entry();
void proc_entry();

static int pid;
static SpinLock ptree_lock, pid_lock;

define_early_init(ptree_lock) {
    init_spinlock(&ptree_lock);
    init_spinlock(&pid_lock);
}

void set_parent_to_this(struct proc* proc) {
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    _acquire_spinlock(&ptree_lock);
    proc->parent = thisproc();
    _insert_into_list(&(thisproc()->children), &(proc->ptnode));
    _release_spinlock(&ptree_lock);
}

NO_RETURN void exit(int code) {
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    _acquire_spinlock(&ptree_lock);
    auto this = thisproc();
    this->exitcode = code;
    auto p = (this->children).next;
    while (p != &(this->children)) {
        auto q = p->next;
        _insert_into_list(&(root_proc.children), p);
        auto child = container_of(p, struct proc, ptnode);
        child->parent = &root_proc;
        if (is_zombie(child)) {
            activate_proc(&root_proc);
        }
        p = q;
    }
    _release_spinlock(&ptree_lock);
    
    activate_proc(this->parent);
    _acquire_sched_lock();
    _sched(ZOMBIE);
    PANIC(); // prevent the warning of 'no_return function returns'
}

int wait(int* exitcode) {
    // TODO
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency
    _acquire_spinlock(&ptree_lock);
    if ((thisproc()->children).next == &(thisproc()->children)) {
        _release_spinlock(&ptree_lock);
        return -1;
    }

    while (1) {
        _for_in_list(p, &(thisproc()->children)) {
            auto child = container_of(p, struct proc, ptnode);
            if (is_zombie(child)) {
                int ret = child->pid;
                *exitcode = child->exitcode;
                _detach_from_list(p);
                kfree(child);
                _release_spinlock(&ptree_lock);
                return ret;
            }
        }

        _release_spinlock(&ptree_lock);
        _acquire_sched_lock();
        _sched(SLEEPING);
        _acquire_spinlock(&ptree_lock);
    }
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg) {
    // TODO
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    if (p->parent == NULL) {
        _acquire_spinlock(&ptree_lock);
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
        _release_spinlock(&ptree_lock);
    }
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    int id = p->pid;
    activate_proc(p);
    return id;
}

void init_proc(struct proc* p) {
    // TODO
    // setup the struct proc with kstack and pid allocated
    // NOTE: be careful of concurrency
    memset(p, 0, sizeof(*p));
    _acquire_spinlock(&pid_lock);
    p->pid = ++pid;
    _release_spinlock(&pid_lock);
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    p->kstack = kalloc_page();
    init_schinfo(&p->schinfo);
    p->kcontext = (KernelContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(KernelContext) - sizeof(UserContext));
    p->ucontext = (UserContext*)((u64)p->kstack + PAGE_SIZE - 16 - sizeof(UserContext));
}

struct proc* create_proc()
{
    struct proc* p = kalloc(sizeof(struct proc));
    init_proc(p);
    return p;
}

define_init(root_proc)
{
    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}
