#include <kernel/proc.h>
#include <kernel/init.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <common/list.h>
#include <common/string.h>
#include <kernel/printk.h>
#define PID_MAX 128 //96

struct proc root_proc;

void kernel_entry();
void proc_entry();

// a map from pid to pcb
typedef struct pid_map_pcb {
    int pid;
    struct proc* pcb;
    struct rb_node_ node;
} pid_map_pcb_t;

typedef struct pid_pcb_tree {
    struct rb_root_ root;
} pid_pcb_tree_t;

static bool _cmp_pid_pcb(rb_node lnode,rb_node rnode) {
    auto lp = container_of(lnode, pid_map_pcb_t, node);
    auto rp = container_of(rnode, pid_map_pcb_t, node);
    return lp->pid < rp->pid;
}

static pid_pcb_tree_t pid_pcb;
static int last_pid = 0;
static bool pid_map[PID_MAX];
static SpinLock ptree_lock, pid_lock, pid_pcb_lock;

static int alloc_pid() {
    _acquire_spinlock(&pid_lock);
    
    int ret = -1;
    for (int i = last_pid + 1; i < PID_MAX; ++i) {
        if (pid_map[i] == false) {
            ret = i;
            pid_map[i] = true;
            break;
        }
    }
    if (ret == -1) {
        for (int i = 1; i < last_pid; ++i) {
            if (pid_map[i] == false) {
                ret = i;
                pid_map[i] = true;
                break;
            }
        }
    }

    ASSERT(ret != -1);
    last_pid = ret;
    _release_spinlock(&pid_lock);
    return ret;
}

static void free_pid(int pid) {
    _acquire_spinlock(&pid_lock);
    pid_map[pid] = false;
    _release_spinlock(&pid_lock);
}

define_early_init(ptree_lock) {
    init_spinlock(&ptree_lock);
    init_spinlock(&pid_lock);
    init_spinlock(&pid_pcb_lock);
}

void set_parent_to_this(struct proc* proc) {
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    _acquire_spinlock(&ptree_lock);
    if (proc->parent == NULL) {
        // insert into rb_tree
        pid_map_pcb_t *in_node = kalloc(sizeof(pid_map_pcb_t));
        in_node->pid = proc->pid;
        in_node->pcb = proc;
        _acquire_spinlock(&pid_pcb_lock);
        _rb_insert(&(in_node->node), &pid_pcb.root, _cmp_pid_pcb);
        _release_spinlock(&pid_pcb_lock);
    }
    proc->parent = thisproc();
    _insert_into_list(&(thisproc()->children), &(proc->ptnode));
    _release_spinlock(&ptree_lock);
}

NO_RETURN void exit(int code) {
    // TODO
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. notify the parent
    // 5. sched(ZOMBIE)
    // NOTE: be careful of concurrency
    _acquire_spinlock(&ptree_lock);
    auto this = thisproc();
    this->exitcode = code;

    // transfer children to the root_proc
    auto p = (this->children).next;
    while (p != &(this->children)) {
        auto q = p->next;
        _insert_into_list(&(root_proc.children), p);
        auto child = container_of(p, struct proc, ptnode);
        child->parent = &root_proc;
        p = q;
    }

    // transfer zombie children to the root_proc
    // _merge_list(&(root_proc.zombie_children), &(this->zombie_children));
    // _detach_from_list(root_proc.zombie_children.next);
    // int sem = get_all_sem(&(this->childexit));
    // for (int i = 0; i < sem; ++i) {
    //     post_sem(&(root_proc.childexit));
    // }
    p = (this->zombie_children).next;
    while (p != &(this->zombie_children)) {
        auto q = p->next;
        _insert_into_list(&(root_proc.zombie_children), p);
        auto child = container_of(p, struct proc, ptnode);
        child->parent = &root_proc;
        p = q;
        post_sem(&(root_proc.childexit));
    }
    // free resource
    free_pgdir(&(this->pgdir));
    
    // notify parent proc
    _detach_from_list(&(this->ptnode));
    _insert_into_list(&(this->parent->zombie_children), &(this->ptnode));
    post_sem(&(this->parent->childexit));
    
    _acquire_sched_lock();
    _release_spinlock(&ptree_lock);
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
    auto this = thisproc();

    // if no children
    if ((this->children).next == &(this->children)
        && (this->zombie_children).next == &(this->zombie_children)
    ) {
        _release_spinlock(&ptree_lock);
        return -1;
    }
    _release_spinlock(&ptree_lock);

    wait_sem(&this->childexit);
    _acquire_spinlock(&ptree_lock);
    auto p = (this->zombie_children).next;
    _detach_from_list(p);
    auto child = container_of(p, struct proc, ptnode);
    int ret = child->pid;
    *exitcode = child->exitcode;
    
    // erase from rb_tree
    pid_map_pcb_t del_node = {child->pid, NULL, {0, 0, 0}};
    _acquire_spinlock(&pid_pcb_lock);
    auto find_node = _rb_lookup(&(del_node.node), &pid_pcb.root, _cmp_pid_pcb);
    _rb_erase(find_node, &pid_pcb.root);
    _release_spinlock(&pid_pcb_lock);
    kfree(container_of(find_node, pid_map_pcb_t, node));
    // free pid resource
    free_pid(child->pid);

    kfree_page(child->kstack);
    kfree(child);
    
    _release_spinlock(&ptree_lock);
    return ret;
}

int kill(int pid)
{
    // TODO
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    _acquire_spinlock(&ptree_lock);
    
    // find from rb_tree 
    pid_map_pcb_t kill_node = {pid, NULL, {0, 0, 0}};
    _acquire_spinlock(&pid_pcb_lock);
    auto find_node = _rb_lookup(&(kill_node.node), &pid_pcb.root, _cmp_pid_pcb);
    _release_spinlock(&pid_pcb_lock);

    if (find_node == NULL) {
        _release_spinlock(&ptree_lock);
        return -1;
    }
    auto p = container_of(find_node, pid_map_pcb_t, node);
    if (is_unused(p->pcb)) {
        _release_spinlock(&ptree_lock);
        return -1;
    }
    p->pcb->killed = true;
    activate_proc(p->pcb);
    _release_spinlock(&ptree_lock);
    return 0;
}

int start_proc(struct proc* p, void(*entry)(u64), u64 arg)
{
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

        // insert into rb_tree
        pid_map_pcb_t *in_node = kalloc(sizeof(pid_map_pcb_t));
        in_node->pid = p->pid;
        in_node->pcb = p;
        _acquire_spinlock(&pid_pcb_lock);
        _rb_insert(&(in_node->node), &pid_pcb.root, _cmp_pid_pcb);
        _release_spinlock(&pid_pcb_lock);
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
    p->pid = alloc_pid();
    init_sem(&p->childexit, 0);
    init_list_node(&p->children);
    init_list_node(&p->ptnode);
    init_list_node(&p->zombie_children);
    init_pgdir(&p->pgdir);
    init_schinfo(&p->schinfo);
    p->kstack = kalloc_page();
    memset(p->kstack, 0, PAGE_SIZE);
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
