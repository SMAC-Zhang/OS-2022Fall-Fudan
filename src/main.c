#include <aarch64/intrinsic.h>
#include <kernel/init.h>
#include <driver/uart.h>
#include <common/string.h>

static char hello[16];

define_early_init(set_hello) {
    memcpy(hello, "Hello world!", 12);
}

define_init(put_hello) {
    for (int i = 0; i < 16; ++i) {
        uart_put_char(hello[i]);
    }
}

extern char edata[], end[];
NO_RETURN void main() {
    if (cpuid() == 0) {
        memset(edata, 0, end - edata);
        do_early_init();
        do_init();
    }
    arch_stop_cpu();
}
