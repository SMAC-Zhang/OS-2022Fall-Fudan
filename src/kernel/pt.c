#include <kernel/pt.h>
#include <kernel/mem.h>
#include <common/string.h>
#include <aarch64/intrinsic.h>

PTEntriesPtr get_pte(struct pgdir* pgdir, u64 va, bool alloc)
{
    // TODO
    // Return a pointer to the PTE (Page Table Entry) for virtual address 'va'
    // If the entry not exists (NEEDN'T BE VALID), allocate it if alloc=true, or return NULL if false.
    // THIS ROUTINUE GETS THE PTE, NOT THE PAGE DESCRIBED BY PTE.
    if (pgdir->pt == NULL) {
        if (alloc) {
            pgdir->pt = kalloc_page();
            memset(pgdir->pt, 0, PAGE_SIZE);
        } else {
            return NULL;
        }
    }
    PTEntriesPtr pt0 = pgdir->pt;
    if (pt0[VA_PART0(va)] == 0) {
        if (alloc) {
            PTEntriesPtr pt1 = kalloc_page();
            memset(pt1, 0, PAGE_SIZE);
            pt0[VA_PART0(va)] = K2P(pt1) | PTE_TABLE;
        } else {
            return NULL;
        }
    }
    PTEntriesPtr pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[VA_PART0(va)]));
    if (pt1[VA_PART1(va)] == 0) {
        if (alloc) {
            PTEntriesPtr pt2 = kalloc_page();
            memset(pt2, 0, PAGE_SIZE);
            pt1[VA_PART1(va)] = K2P(pt2) | PTE_TABLE;
        } else {
            return NULL;
        }
    }
    PTEntriesPtr pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[VA_PART1(va)]));
    if (pt2[VA_PART2(va)] == 0) {
        if (alloc) {
            PTEntriesPtr pt3 = kalloc_page();
            memset(pt3, 0, PAGE_SIZE);
            pt2[VA_PART2(va)] = K2P(pt3) | PTE_TABLE;
        } else {
            return NULL;
        }
    }
    PTEntriesPtr pt3 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt2[VA_PART2(va)]));
    if (pt3[VA_PART3(va)] == 0) {
        if (alloc) {
            pt3[VA_PART3(va)] = K2P(va) | PTE_TABLE;
            return (PTEntriesPtr)(pt3 + VA_PART3(va));
        } else {
            return NULL;
        }
    }
    return (PTEntriesPtr)(pt3 + VA_PART3(va));
}

void init_pgdir(struct pgdir* pgdir)
{
    pgdir->pt = NULL;
}

void free_pgdir(struct pgdir* pgdir)
{
    // TODO
    // Free pages used by the page table. If pgdir->pt=NULL, do nothing.
    // DONT FREE PAGES DESCRIBED BY THE PAGE TABLE
    if (pgdir->pt == NULL) {
        return;
    }
    auto pt0 = pgdir->pt;
    for (int i = 0; i < N_PTE_PER_TABLE; ++i) {
        if (pt0[i] != 0) {
            PTEntriesPtr pt1 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt0[i]));
            for (int j = 0; j < N_PTE_PER_TABLE; ++j) {
                if (pt1[j] != 0) {
                    PTEntriesPtr pt2 = (PTEntriesPtr)P2K(PTE_ADDRESS(pt1[j]));
                    for (int k = 0; k < N_PTE_PER_TABLE; ++k) {
                        if (pt2[k] != 0) {
                            kfree_page((void*)P2K(PTE_ADDRESS(pt2[k])));
                        }
                    }
                    kfree_page((void*)pt2);
                }
            }
            kfree_page((void*)pt1);
        }
    }
    kfree_page((void*)pgdir->pt);
    pgdir->pt = NULL;
}

void attach_pgdir(struct pgdir* pgdir)
{
    extern PTEntries invalid_pt;
    if (pgdir->pt)
        arch_set_ttbr0(K2P(pgdir->pt));
    else
        arch_set_ttbr0(K2P(&invalid_pt));
}



