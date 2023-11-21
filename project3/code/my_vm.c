// List all group member's name(s): Abid Azad(aa2177), Ghautham Sambabu(gs878) 
// username of iLab: rlab

#include "my_vm.h"
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t secondLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t translateLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t memLock = PTHREAD_MUTEX_INITIALIZER;
pde_t * pageDir;
pte_t ** pageTable;
pte_t frame = 0;
pte_t freePage = 0;
pte_t directEntry = 0;
unsigned char * physicalMemory = NULL;
int numPhysPages; 
int numVirtPages; 
int checkBits;
unsigned char * physBitmap;
unsigned char * virtBitmap; 
double offset;
double outerPage; 
double innerPage; 
struct tlb tlb_store = { .size = 0, .front = 0, .rear = 0, .misses = 0, .accesses = 0 };
struct allocation_info {
    void *start_address;
    int size;
};

// Maintain a list of allocated pages and their sizes in an attempt to reduce internal fragmentation.
struct allocation_info allocated_pages[MAX_MEMSIZE / PGSIZE];
int num_allocated_pages = 0;


/*
Function responsible for allocating and setting your physical memory 
*/
void set_physical_mem() {


    pthread_mutex_lock(&memLock);
    physicalMemory = (unsigned char *) malloc(sizeof(unsigned char) * MEMSIZE);

	numPhysPages = MEMSIZE / PGSIZE;
	physBitmap = (unsigned char *) malloc(sizeof(unsigned char) * (numPhysPages / 8));     
	for(int i = 0; i < (numPhysPages / 8); i++)
		physBitmap[i] = 0;

	numVirtPages = MAX_MEMSIZE / PGSIZE;
	virtBitmap = (unsigned char *) malloc(sizeof(unsigned char) * (numVirtPages / 8));
	for(int i = 0; i < (numVirtPages / 8); i++)
		virtBitmap[i] = 0;

	offset = log2 (PGSIZE);
	double va_bits = log2 (MAX_MEMSIZE);
	int numBits = (va_bits - offset);
    checkBits = numBits/2; 
	outerPage = numBits/2;
	innerPage = numBits/2;
    if(numBits%2 != 0)
		innerPage++;    
	pageDir = (pde_t *) malloc(sizeof(pde_t) * outerPage);
	pageTable = (pte_t **) malloc(sizeof(pte_t *) * outerPage);
	for(int i=0; i<outerPage; i++) {
		pageTable[i] = (pte_t *) malloc(sizeof(pte_t) * innerPage);
        
	}
    pthread_mutex_unlock(&memLock);
}


/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 */
pthread_mutex_t tlbLock = PTHREAD_MUTEX_INITIALIZER;

int
add_TLB(void *va, void *pa)
{
    pthread_mutex_lock(&tlbLock);

    // Check if the entry already exists, and update it
    for (int i = 0; i < tlb_store.size; i++) {
        int index = (tlb_store.front + i) % TLB_ENTRIES;
        if (tlb_store.entries[index].va == va) {
            tlb_store.entries[index].pa = pa;
            pthread_mutex_unlock(&tlbLock);
            return 0;
        }
    }

    // If TLB is full, remove the oldest entry
    if (tlb_store.size == TLB_ENTRIES) {
        tlb_store.front = (tlb_store.front + 1) % TLB_ENTRIES;
        tlb_store.size--;
    }

    // Add the new entry at the end
    tlb_store.entries[tlb_store.rear].va = va;
    tlb_store.entries[tlb_store.rear].pa = pa;
    tlb_store.rear = (tlb_store.rear + 1) % TLB_ENTRIES;
    tlb_store.size++;
    pthread_mutex_unlock(&tlbLock);
    return 0;
}


/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
pte_t *
check_TLB(void *va) {

    pthread_mutex_lock(&tlbLock);
    for (int i = 0; i < tlb_store.size; i++) {
        int index = (tlb_store.front + i) % TLB_ENTRIES;
        if (tlb_store.entries[index].va == va) {
            pthread_mutex_unlock(&tlbLock);
            tlb_store.accesses++;
            return tlb_store.entries[index].pa;
        }
    }
    tlb_store.misses++;
    pthread_mutex_unlock(&tlbLock);
    return NULL;
}


/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void
print_TLB_missrate()
{
    double miss_rate = 0;	

    if (tlb_store.accesses > 0) {
        miss_rate = (double) tlb_store.misses / (double) tlb_store.accesses;
    }
    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}



/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address
*/
pte_t *translate(pde_t *pgdir, void *va) {
    pthread_mutex_lock(&translateLock);
    void *test = (void*)((unsigned long) va >> (int)offset);
    pte_t *translation = check_TLB(test);
    if(translation != NULL){
        pthread_mutex_unlock(&translateLock);
        return translation;
    }
    
	pde_t address = (pde_t) va; 
	pde_t outer = 0xFFFFFFFF; 
	pte_t inner = 0xFFFFFFFF;

    inner = performBitmask(inner, innerPage, offset, address);
    outer = performBitmask(outer, outerPage, innerPage + offset, address);
    if (pageTable[pgdir[outer]] == NULL) {
        return NULL;
    }
	add_TLB(test,  (void *) ((pte_t)(physicalMemory) + pageTable[pgdir[outer]][inner]*PGSIZE));
    pthread_mutex_unlock(&translateLock);
	return (void *) ((pte_t)(physicalMemory) + pageTable[pgdir[outer]][inner]*PGSIZE);




}


/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
pthread_mutex_t pageMapLock = PTHREAD_MUTEX_INITIALIZER;
int page_map(pde_t *pgdir, void *va, void *pa){

    pthread_mutex_lock(&pageMapLock);
    pde_t address = (pde_t) va;
    pde_t inner = performBitmask(0xFFFFFFFF, innerPage, offset, address);
    pde_t outer = performBitmask(0xFFFFFFFF, outerPage, innerPage + offset, address);
    
    pte_t map = (pde_t) pa;
    map >>= (int) offset;
     
  if (pageTable[pgdir[outer]] == NULL) {
        printf("this one above!\n");
        pageTable[pgdir[outer]] = (pte_t *)malloc(sizeof(pte_t) * innerPage);
  }

    

        pageTable[pgdir[outer]][inner] = map;
    pthread_mutex_unlock(&pageMapLock);

}


/*
* Function that gets the next available page
*/
void *get_next_avail(int num_pages) {
    pthread_mutex_lock(&pageMapLock);
   pte_t current_page = freePage;
   pte_t start_page = freePage;

   for (int i = 0; i < num_pages; i++) {
       int byte_index = current_page / 8;
       int bit_offset = current_page % 8;

       if (!(virtBitmap[byte_index] & (0x80 >> bit_offset))) {
          current_page++;
          freePage = current_page++;
          continue;
       }

      freePage = (freePage + 1) % numVirtPages;

      if (freePage == start_page) {
         pthread_mutex_unlock(&pageMapLock);
          return NULL;
      }
   }
    pthread_mutex_unlock(&pageMapLock);
   return (void *)&freePage;
}


/* 
 Function responsible for allocating pages and used by the benchmark
*/
void *t_malloc(unsigned int num_bytes) {
    pthread_mutex_lock(&lock);
    if(physicalMemory == NULL) 
        set_physical_mem();
    
    // Check if combining small allocations into one page is possible
    int combined_allocation = 0;
    if (num_allocated_pages > 0) {
        struct allocation_info last_allocation = allocated_pages[num_allocated_pages - 1];
        if (last_allocation.size + num_bytes <= PGSIZE) {
            num_bytes = PGSIZE - last_allocation.size;
            combined_allocation = 1;
        }
    }

    pte_t * page = (pte_t *) get_next_avail((num_bytes / PGSIZE) + 1);
    if (page == NULL) {
        pthread_mutex_unlock(&lock);
        return NULL;
    }

    // Update the allocation information
    if (!combined_allocation) {
        allocated_pages[num_allocated_pages].start_address = page;
        allocated_pages[num_allocated_pages].size = num_bytes;
        num_allocated_pages++;
    }

    for (int i = 0; i < (num_bytes / PGSIZE) + 1; i++) {
        unsigned char virt_mask = 0x80 >> ((*page + i) % 8);
        virtBitmap[(*page + i) / 8] |= virt_mask;

        unsigned char phys_mask = 0x80 >> ((frame + i) % 8);
        physBitmap[(frame + i) / 8] |= phys_mask;

        frame = (frame + 1) % numPhysPages;
    }

    int limit = 1;
    for (int i = 0; i < checkBits; i++) {
        limit *= 2;
    }
    pde_t outer = (*page) / limit;
    pte_t inner = (*page) % limit;
    if(directEntry == offset)
        directEntry = 0x0;

    pageDir[directEntry] = outer;
    directEntry = (directEntry + 1) % limit;

    pte_t va = (outer << (int)(innerPage + offset)) | (inner << (int)offset);
    pte_t pa = (frame << (int)offset);
    
    page_map(pageDir, (void*)va, (void*)pa);
    pthread_mutex_unlock(&lock);
    return (void *) va;
}

/* 
Responsible for releasing one or more memory pages using virtual address (va)
*/
void t_free(void *va, int size) {
    pthread_mutex_lock(&lock);
    int limit = 1;
    for (int i = 0; i < checkBits; i++) {
        limit *= 2;
    }
    pde_t address = (pde_t) va;
    pde_t inner = performBitmask(0xFFFFFFFF, innerPage, offset, address);
    pde_t outer = performBitmask(0xFFFFFFFF, outerPage, innerPage + offset, address);
    
    pte_t page = (outer*limit) + inner;
    directEntry--;
    freePage -=  (size / PGSIZE) + 1;
    frame -=  (size / PGSIZE) + 1;
    
    // Update the allocation information
    for (int i = 0; i < num_allocated_pages; i++) {
        if (allocated_pages[i].start_address == va) {
            allocated_pages[i].start_address = NULL;
            allocated_pages[i].size = 0;
            break;
        }
    }

    for(int i=0; i<(size / PGSIZE) + 1; i++) {
        unsigned char virt_mask = ~(0x80 >> ((page + i) % 8));
        virtBitmap[(page + i) / 8] &= virt_mask;

        unsigned char phys_mask = ~(0x80 >> ((frame + i) % 8));
        physBitmap[(frame + i) / 8] &= phys_mask;

        frame = (frame + 1) % numPhysPages;
    }

    for (int i = 0; i < tlb_store.size; i++) {
        int index = (tlb_store.front + i) % TLB_ENTRIES;
        if (tlb_store.entries[index].va == va) {
            for (int j = i; j < tlb_store.size - 1; j++) {
                int current_index = (tlb_store.front + j) % TLB_ENTRIES;
                int next_index = (tlb_store.front + j + 1) % TLB_ENTRIES;
                tlb_store.entries[current_index] = tlb_store.entries[next_index];
            }

            tlb_store.rear = (tlb_store.rear - 1 + TLB_ENTRIES) % TLB_ENTRIES;
            tlb_store.size--;
            pthread_mutex_unlock(&lock);
            return;
        }
    }
    pthread_mutex_unlock(&lock);
}

/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
*/
int put_value(void *va, void *val, int size) {

    pthread_mutex_lock(&lock);
    if(translate(pageDir, va) == NULL){
        pthread_mutex_unlock(&lock);
    return -1;
    }

	unsigned int pages = (size / PGSIZE) + 1;
    for (int i = 0; i < pages; i++) {
       int copy_size = (i == (pages - 1)) ? size % PGSIZE : PGSIZE;
       memcpy(translate(pageDir, va), val, copy_size);
       va += PGSIZE;
    }
    pthread_mutex_unlock(&lock);
    return 0;
}

/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {
    pthread_mutex_lock(&lock);
        if(translate(pageDir, va) == NULL){
        pthread_mutex_unlock(&lock);
    return;
    }
    unsigned int pages = (size / PGSIZE) + 1;
    for (int i = 0; i < pages; i++) {
       void* pa = translate(pageDir, va);
       unsigned int address = (unsigned int)val + (i * PGSIZE);
       size_t copy_size = (i == (pages - 1)) ? size % PGSIZE : PGSIZE;
       memcpy((void*)address, pa, copy_size);
       va += PGSIZE;
    }
    pthread_mutex_unlock(&lock);
}

void mat_mult(void *mat1, void *mat2, int size, void *answer) {
    pthread_mutex_lock(&secondLock);
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
                 //printf("Values at the index: %d, %d, %d, %d, %d\n", 
                    // a, b, size, (i * size + k), (k * size + j));
                c += (a * b);
            }
            int address_c = (unsigned int)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            // printf("This is the c: %d, address: %x!\n", c, address_c);
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
        pthread_mutex_unlock(&secondLock);

   
}
unsigned long performBitmask(unsigned long page, double shift, double offset, pde_t address) { 
	page <<= (int) offset;
	page <<= (int) (32 - (innerPage + offset));
	page >>= (int) (32 - (innerPage + offset));
	page = page & address;
	return page >>= (int) offset;
} 
