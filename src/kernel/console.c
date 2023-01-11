#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#include<driver/interrupt.h>

#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r;  // Read index
    usize w;  // Write index
    usize e;  // Edit index
    SpinLock lock;
    SleepLock sem;
} input;
#define C(x)      ((x) - '@')  // Control-x

extern InodeTree inodes;

define_rest_init(console) {
    set_interrupt_handler(IRQ_AUX, console_intr);
    init_spinlock(&(input.lock));
    init_sleeplock(&(input.sem));
}

isize console_write(Inode *ip, char *buf, isize n) {
    // TODO
    inodes.unlock(ip);
    _acquire_spinlock(&(input.lock));
    for (isize i = 0; i < n; ++i) {
        uart_put_char(buf[i]);
    }
    _release_spinlock(&(input.lock));
    inodes.lock(ip);
    return n;
}

isize console_read(Inode *ip, char *dst, isize n) {
    // TODO
    inodes.unlock(ip);
    isize i = 0;
    _acquire_spinlock(&(input.lock));
    for (; i < n; ++i) {
        while (input.r == input.w) {
            _lock_sem(&(input.sem));
            _release_spinlock(&(input.lock));
            if (_wait_sem(&(input.sem), true) == FALSE) {
                inodes.lock(ip);
                return -1;
            }
        }
        char c = input.buf[input.r % INPUT_BUF];
        if (c == C('D')) {
            break;
        }
        dst[i] = c;
        input.r = (input.r + 1) % INPUT_BUF;
    }
    _release_spinlock(&(input.lock));
    inodes.lock(ip);
    return i;
}

static void backspace() {
    if (input.e > 0) {
        input.e--;
    } else {
        input.e = INPUT_BUF - 1;
    }
    uart_put_char('\b'); 
    uart_put_char(' ');
    uart_put_char('\b');
}

void console_intr(char (*getc)()) {
    // TODO
    char c;
    getc = getc;
    while ((c = uart_get_char()) != 0xff) {
        switch (c) {
            case '\b': {
                if (input.e != input.w) {
                    backspace();
                }
                break;
            }
            case C('U'): {
                while (input.e != input.w && input.buf[(input.e - 1) % INPUT_BUF] != '\n') {
                    backspace();
                }
                break;
            }
            case C('D'): 
                if ((input.r + INPUT_BUF) % INPUT_BUF != input.e) {
                    input.buf[input.e] = c;
                    input.e = (input.e + 1) % INPUT_BUF;
                } else {
                    continue;
                }
                input.w = input.e;
                post_all_sem(&(input.sem));
                uart_put_char(c);
                break;
            case C('C'): 
                ASSERT(kill(thisproc()->pid) == 0);
                uart_put_char(c);
                break;
            default: 
                if ((input.r + INPUT_BUF) % INPUT_BUF != input.e) {
                    input.buf[input.e] = c;
                    input.e = (input.e + 1) % INPUT_BUF;
                    if (c == '\n') {
                        input.w = input.e;
                        post_all_sem(&(input.sem));
                    }
                    uart_put_char(c);
                } else {
                    continue;
                }
                break;
        }
    }
}