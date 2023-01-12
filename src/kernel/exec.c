#include <elf.h>
#include <common/string.h>
#include <common/defines.h>
#include <kernel/console.h>
#include <kernel/proc.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/pt.h>
#include <kernel/mem.h>
#include <kernel/paging.h>
#include <aarch64/trap.h>
#include <fs/file.h>
#include <fs/inode.h>
#include <kernel/printk.h>

//static u64 auxv[][2] = {{AT_PAGESZ, PAGE_SIZE}};
extern int fdalloc(struct file* f);

int execve(const char *path, char *const argv[], char *const envp[]) {
	// TODO
	auto this = thisproc();
	struct pgdir pgdir = {0};
	// open file
	OpContext ctx;
	bcache.begin_op(&ctx);
	Inode* ip = namei(path, &ctx);
	if (ip == NULL) {
		goto bad;
	}

	// Step1: read header
	inodes.lock(ip);
	Elf64_Ehdr header;
	if (inodes.read(ip, (u8*)(&header), 0, sizeof(Elf64_Ehdr)) < sizeof(Elf64_Ehdr)) {
		goto bad;
	}
	if (strncmp((const char*)header.e_ident, ELFMAG, strlen(ELFMAG)) != 0) {
		// check magic number
		goto bad;
	}

	// Step2: read program header
	Elf64_Phdr p_header;
	init_pgdir(&pgdir);
	u64 sp = 0;
	for (Elf64_Half i = 0, offset = header.e_phoff; i < header.e_phnum; offset += sizeof(Elf64_Phdr), ++i) {
		if (inodes.read(ip, (u8*)(&p_header), offset, sizeof(Elf64_Phdr)) < sizeof(Elf64_Phdr)) {
			goto bad;
		}
		if (p_header.p_type == PT_LOAD) { // load and create a section
			// set something
			struct section* st = kalloc(sizeof(struct section));
			if (p_header.p_flags == (PF_R | PF_X)) {
				st->flags = (ST_FILE | ST_RO);
			} else if (p_header.p_flags == (PF_R | PF_W)) {
				st->flags = (ST_FILE);
			} else {
				kfree(st);
				continue;
			}
			init_sleeplock(&(st->sleeplock));
			st->begin = p_header.p_vaddr;
			st->end = p_header.p_vaddr + p_header.p_memsz;
			sp = MAX(sp, st->end);
			init_list_node(&(st->stnode));
			_insert_into_list(&(pgdir.section_head), &(st->stnode));

			// load 
			u64 va = p_header.p_vaddr, va_end = p_header.p_vaddr + p_header.p_filesz;
			for (; va < va_end; va = PAGE_BASE(va + PAGE_SIZE)) {
				void* ka = kalloc_page();
				memset(ka, 0, PAGE_SIZE);
				u64 count = MIN(va_end - va, (u64)PAGE_SIZE - (va - PAGE_BASE(va)));
				if (inodes.read(ip, (u8*)((u64)ka + va - PAGE_BASE(va)), p_header.p_offset + (va - p_header.p_vaddr), count) < count) {
					goto bad;
				}
				vmmap(&pgdir, va, ka, (st->flags & ST_RO) ? PTE_USER_DATA | PTE_RO : PTE_USER_DATA);
			}
			// filesz ~ memsz is BSS section
			// COW
			for (; va < p_header.p_vaddr + p_header.p_memsz; va += PAGE_SIZE) {
				vmmap(&pgdir, va, get_zero_page(), PTE_USER_DATA | PTE_RO);
			}
			// // don't really load
			// // only set file and lazy allocation
			// st->fp = filealloc();
			// if (st->fp == 0) {
			// 	goto bad;
			// }
			// st->fp->type = FD_INODE;
			// st->fp->readable = 1;
			// st->fp->writable = !(st->flags & ST_RO);
			// st->fp->ip = ip;
			// st->fp->off = offset;
			// st->offset = offset;
			// st->length = p_header.p_memsz;
		} else {
			continue;
		}
	}
	inodes.unlock(ip);
	inodes.put(&ctx, ip);
	bcache.end_op(&ctx);

	// Step3: Allocate and initialize user stack.
	u64 sp_end = PAGE_BASE((sp + STACK_SIZE)) + PAGE_SIZE;
	for (; sp < sp_end; sp += PAGE_SIZE) {
		void* ka = kalloc_page();
		vmmap(&pgdir, sp, ka, PTE_USER_DATA);
	}
	// left some space
	sp -= 128;
	
	int argc = 0;
	if (argv) { // push argv
		for (; argv[argc]; argc++) {

		}
		for (int i = argc; i >= 0; --i) {
			sp -= strlen(argv[i]) + 1;
			copyout(&pgdir, (void*)sp, argv[i], strlen(argv[i]) + 1);
		}
	}

	// push argc
	sp -= 8;
	copyout(&pgdir, (void*)sp, &argc, sizeof(int));

	this->ucontext->x[0] = (u64)argc;
	this->ucontext->x[1] = (u64)argv;
	this->ucontext->sp = sp;
	this->ucontext->elr = header.e_entry;
	free_pgdir(&(this->pgdir));
	this->pgdir = pgdir;
	copy_sections(&(pgdir.section_head), &(this->pgdir.section_head));
	attach_pgdir(&pgdir);
	return 0;

bad:
	if (pgdir.pt != NULL) {
        free_pgdir(&pgdir);
	}
    if (ip != NULL) {
        inodes.unlock(ip);
        inodes.put(&ctx, ip);
	}
	bcache.end_op(&ctx);
	i64 xxx = (i64)envp;
	xxx = -1;
	return xxx;
}