// List all group member's name(s): Abid Azad(aa2177), Ghautham Sambabu(gs878) 
// username of iLab: rlab

#ifndef MY_VM_H_INCLUDED
#define MY_VM_H_INCLUDED
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include<pthread.h> 
#include <math.h> 
#include <string.h>

#define PGSIZE 4096

// Maximum size of virtual memory
#define MAX_MEMSIZE 4ULL*1024*1024*1024

// Size of "physcial memory"
#define MEMSIZE 1024*1024*1024

// Represents a page table entry
typedef unsigned long pte_t;

// Represents a page directory entry
typedef unsigned long pde_t;

#define TLB_ENTRIES 512

//Structure to represents TLB
struct tlb_entry {
    void *va;
    void *pa;
};

struct tlb {
    struct tlb_entry entries[TLB_ENTRIES];
    int size;
    int front;
    int rear;
    int misses;
    int accesses;
};



void set_physical_mem();
pte_t* translate(pde_t *pgdir, void *va);
int page_map(pde_t *pgdir, void *va, void* pa);
bool check_in_tlb(void *va);
void put_in_tlb(void *va, void *pa);
void *t_malloc(unsigned int num_bytes);
void t_free(void *va, int size);
int put_value(void *va, void *val, int size);
void get_value(void *va, void *val, int size);
void mat_mult(void *mat1, void *mat2, int size, void *answer);
void print_TLB_missrate();
unsigned long performBitmask(unsigned long page, double shift, double offset, pde_t address);
#endif
