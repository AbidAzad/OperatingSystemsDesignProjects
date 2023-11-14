#include "my_vm.h"
pthread_mutex_t lock;
char **physMem = NULL;
int *physBitMap;

directoryBitMap *dirBitMap;
pde_t *pgdir;
struct tlb tlb_store;
/*
Function responsible for allocating and setting your physical memory 
*/
void set_physical_mem() {

    //Allocate physical memory using mmap or malloc; this is the total size of
    //your memory you are simulating

    
    //HINT: Also calculate the number of physical and virtual pages and allocate
    //virtual and physical bitmaps and initialize them
    int numberOfPages = MEMSIZE/PGSIZE;
    physMem = malloc(numberOfPages * sizeof(char*));
    physBitMap = malloc(numberOfPages * sizeof(int));
    for(int i = 0; i < numberOfPages; i++){
        physMem[i] = malloc(PGSIZE * sizeof(char));
        physBitMap[i] = -1;
    }
}


/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 */
int
add_TLB(void *va, void *pa)
{

    /*Part 2 HINT: Add a virtual to physical page translation to the TLB */
   /* pthread_mutex_lock(&lock);

    // Check if TLB is full, evict the oldest entry if necessary
    if (tlb_store.usedEntries == TLB_ENTRIES)
    {
        int oldestIndex = 0;
        for (int i = 1; i < TLB_ENTRIES; i++)
        {
            if (tlb_store.lastAccessed[i] < tlb_store.lastAccessed[oldestIndex])
            {
                oldestIndex = i;
            }
        }

        // Evict the oldest entry
        tlb_store.virtualPage[oldestIndex] = (pte_t)NULL;
        tlb_store.physicalPage[oldestIndex] = (pte_t)NULL;
        tlb_store.lastAccessed[oldestIndex] = 0;
        tlb_store.usedEntries--;
    }

    // Add the new translation to the TLB
    tlb_store.virtualPage[tlb_store.usedEntries] = (pte_t)va;
    tlb_store.physicalPage[tlb_store.usedEntries] = (pte_t)pa;
    tlb_store.lastAccessed[tlb_store.usedEntries] = tlb_store.usedEntries;

    tlb_store.usedEntries++;

    pthread_mutex_unlock(&lock);
*/
    return 0;
}


/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
pte_t *
check_TLB(void *va) {

    /* Part 2: TLB lookup code here */
 /*pthread_mutex_lock(&lock);

    // Check if the translation exists in the TLB
    for (int i = 0; i < tlb_store.usedEntries; i++)
    {
        if (tlb_store.virtualPage[i] == (pte_t)va)
        {
            pthread_mutex_unlock(&lock);
            return &(tlb_store.physicalPage[i]);
        }
    }

    pthread_mutex_unlock(&lock);

    // If translation does not exist in TLB*/
    return NULL;


   /*This function should return a pte_t pointer*/
}


/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void
print_TLB_missrate()
{
    double miss_rate = 0;	

    /*Part 2 Code here to calculate and print the TLB miss rate*/




    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}



/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address
*/
pte_t *translate(pde_t *pgdir, void *va) {

    // Get the Page Directory index (1st level)
    unsigned long pdIndex = bitConversion((unsigned long)va, 10, 1);
    pde_t pgdirEntry = pgdir[pdIndex];

    // Check if the page directory entry is valid
    if (!(pgdirEntry & 0x1)) {
        return NULL;  // Page directory entry is not valid, translation failed
    }

    // Get the Page Table index (2nd level) using the virtual address
    unsigned long ptIndex = bitConversion((unsigned long)va, 10, 11);
    pte_t *pageTable = dirBitMap[pgdirEntry].pageTable;

    // Check if the page table entry is valid
    if (pageTable[ptIndex] & 0x1) {
        return &pageTable[ptIndex];  // Return the physical address
    } else {
        return NULL;  // Page table entry is not valid, translation failed
    }

}


/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
int
page_map(pde_t *pgdir, void *va, void *pa)
{

    /*HINT: Similar to translate(), find the page directory (1st level)
    and page table (2nd-level) indices. If no mapping exists, set the
    virtual to physical mapping */
    pthread_mutex_lock(&lock);
    pde_t pgdirEntry = pgdir[bitConversion((unsigned long) va, 10, 1)];
    pte_t *pageTable = dirBitMap[pgdirEntry].pageTable;
    if(pageTable == NULL){
		dirBitMap[pgdirEntry].pageTable = malloc(1024 * sizeof(pte_t *));
		for(int i = 0; i < 1024; i++){
			dirBitMap[pgdirEntry].pageTable[i] = -1;	
		}
	} 
    dirBitMap[pgdirEntry].pageTable[bitConversion((unsigned long) va, 10, 11)] = (unsigned long) pa;
    physBitMap[(unsigned long) pa] = 1;
    pthread_mutex_unlock(&lock);
    return 0;

}


/*Function that gets the next available page
*/
void *get_next_avail(int num_pages) {
 
    for(int i = 0; i < num_pages; i++){
		if(physBitMap[i] == -1){
			return (void *)i;
		}
	}
	return (void *)-1;
}


/* Function responsible for allocating pages
and used by the benchmark
*/
void *t_malloc(unsigned int num_bytes) {

    /* 
     * HINT: If the physical memory is not yet initialized, then allocate and initialize.
     */

   /* 
    * HINT: If the page directory is not initialized, then initialize the
    * page directory. Next, using get_next_avail(), check if there are free pages. If
    * free pages are available, set the bitmaps and map a new page. Note, you will 
    * have to mark which physical pages are used. 
    */
    pthread_mutex_lock(&lock);
    
    if(physMem == NULL){
		set_physical_mem();
	}
    if(pgdir == NULL){
		pgdir = malloc(1024 * sizeof(pde_t));
		dirBitMap = malloc(1024 * sizeof(dirBitMap));
	}
    int index = (int)get_next_avail(MEMSIZE/PGSIZE);
    physBitMap[index] = 1;
    pthread_mutex_unlock(&lock);
    return &physMem[index];
}

/* Responsible for releasing one or more memory pages using virtual address (va)
*/
void t_free(void *va, int size) {

    /* Part 1: Free the page table entries starting from this virtual address
     * (va). Also mark the pages free in the bitmap. Perform free only if the 
     * memory from "va" to va+size is valid.
     *
     * Part 2: Also, remove the translation from the TLB
     */
    pthread_mutex_lock(&lock);
    int index = (pte_t) translate(pgdir, va);
    physBitMap[index] = -1;
    for(int i = 0; i < size; i++){
		memset(physMem[index], 0, size);
	}
    /*for (int i = 0; i < tlb_store.usedEntries; i++)
    {
        if (tlb_store.virtualPage[i] == (pte_t)va)
        {
            tlb_store.virtualPage[i] = (pte_t)NULL;
            tlb_store.physicalPage[i] = (pte_t)NULL;
            tlb_store.lastAccessed[i] = 0;
            tlb_store.usedEntries--;
            break;
        }
    }*/
    pthread_mutex_unlock(&lock);	
}


/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
*/
int put_value(void *va, void *val, int size) {

    /* HINT: Using the virtual address and translate(), find the physical page. Copy
     * the contents of "val" to a physical page. NOTE: The "size" value can be larger 
     * than one page. Therefore, you may have to find multiple pages using translate()
     * function.
     */
    pthread_mutex_lock(&lock);

    // Translate the virtual address to get the physical address
    pte_t physicalAddress = translate(pgdir, va);

    // Check if the translation was successful
    if (physicalAddress == -1) {
        pthread_mutex_unlock(&lock);
        return -1;  // Translation failed
    }

    // Check if the copy will go beyond the boundaries of physMem
    if (physicalAddress + size > sizeof(physMem)) {
        pthread_mutex_unlock(&lock);
        return -1;  // Out of bounds
    }

    // Copy the data from val to the physical memory
    memcpy(&physMem[physicalAddress], val, size);

    pthread_mutex_unlock(&lock);

    return 0;  // Success
}


/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {
    pthread_mutex_lock(&lock);

    pte_t index = (pte_t)translate(pgdir, va);

    // Check if index is within bounds of physMem
    if (index >= 0 && index < sizeof(physMem)) {
        memcpy(val, &physMem[index], size);
    } else {
        // Handle error: index out of bounds
    }

    pthread_mutex_unlock(&lock);;	

}



/*
This function receives two matrices mat1 and mat2 as an argument with size
argument representing the number of rows and columns. After performing matrix
multiplication, copy the result to answer.
*/
void mat_mult(void *mat1, void *mat2, int size, void *answer) {

    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
     * matrix accessed. Similar to the code in test.c, you will use get_value() to
     * load each element and perform multiplication. Take a look at test.c! In addition to 
     * getting the values from two matrices, you will perform multiplication and 
     * store the result to the "answer array"
     */
    int x, y, val_size = sizeof(int);
    int i, j, k;
    for (i = 0; i < size; i++) {
        for(j = 0; j < size; j++) {
            unsigned int a, b, c = 0;
            for (k = 0; k < size; k++) {
                int address_a = (unsigned int)mat1 + ((i * size * sizeof(int))) + (k * sizeof(int));
                int address_b = (unsigned int)mat2 + ((k * size * sizeof(int))) + (j * sizeof(int));
                get_value( (void *)address_a, &a, sizeof(int));
                get_value( (void *)address_b, &b, sizeof(int));
                // printf("Values at the index: %d, %d, %d, %d, %d\n", 
                //     a, b, size, (i * size + k), (k * size + j));
                c += (a * b);
            }
            int address_c = (unsigned int)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            // printf("This is the c: %d, address: %x!\n", c, address_c);
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
}


unsigned long bitConversion(unsigned long va, unsigned long bitNum, unsigned long pos) { 
    return (((1 << bitNum) - 1) & (va >> (pos - 1))); 
} 
