#include "my_vm.h"
pthread_mutex_t lock;
pde_t * pageDir;
pte_t ** pageTable;
pte_t frame = 0;
pte_t freePage = 0;
pte_t directEntry = 0;
unsigned char * physicalMemory = NULL;
int numPhysPages; 
int numVirtPages; 
unsigned char * physBitmap;
unsigned char * virtBitmap; 
double offset;
double outerPage; 
double innerPage; 
struct tlb tlb_store;
/*
Function responsible for allocating and setting your physical memory 
*/
void set_physical_mem() {

    //Allocate physical memory using mmap or malloc; this is the total size of
    //your memory you are simulating

    physicalMemory = (unsigned char *) malloc(sizeof(unsigned char) * MEMSIZE);
    
    //HINT: Also calculate the number of physical and virtual pages and allocate
    //virtual and physical bitmaps and initialize them

	numPhysPages = MEMSIZE / PGSIZE;
	physBitmap = (unsigned char *) malloc(sizeof(unsigned char) * (numPhysPages / 8));     
	for(int i = 0; i < (numPhysPages / 8); i++)
		physBitmap[i] = 0;

	numVirtPages = MAX_MEMSIZE / PGSIZE;
	virtBitmap = (unsigned char *) malloc(sizeof(unsigned char) * (numVirtPages / 8));
	for(int i = 0; i < (numVirtPages / 8); i++)
		virtBitmap[i] = 0; //initially all the pages are free

	offset = log2 (PGSIZE);
	double va_bits = log2 (MAX_MEMSIZE);
	int numBits = (va_bits - offset); 
	outerPage = numBits/2;
	innerPage = numBits/2;
    if(numBits%2 != 0)
		innerPage++;    
	pageDir = (pde_t *) malloc(sizeof(pde_t) * outerPage);
	pageTable = (pte_t **) malloc(sizeof(pte_t *) * outerPage);
	for(int i=0; i<outerPage; i++) {
		pageTable[i] = (pte_t *) malloc(sizeof(pte_t) * innerPage);
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

    return -1;
}


/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
pte_t *
check_TLB(void *va) {

    /* Part 2: TLB lookup code here */

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
    /* Part 1 HINT: Get the Page directory index (1st level) Then get the
    * 2nd-level-page table index using the virtual address.  Using the page
    * directory index and page table index get the physical address.*/



	pde_t address = (pde_t) va; 
	pde_t outer = 0xFFFFFFFF; 
	pte_t inner = 0xFFFFFFFF;

    inner = performBitmask(inner, innerPage, offset, address);
    outer = performBitmask(outer, outerPage, innerPage + offset, address);
	
	return (void *) ((pte_t)(physicalMemory) + pageTable[pgdir[outer]][inner]*PGSIZE);

    /* Part 2 HINT: Check the TLB before performing the translation. If
    * translation exists, then you can return physical address from the TLB.
    */


    //If translation not successfull
    //return NULL; 
}


/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
int page_map(pde_t *pgdir, void *va, void *pa){

    /*HINT: Similar to translate(), find the page directory (1st level)
    and page table (2nd-level) indices. If no mapping exists, set the
    virtual to physical mapping */

	pde_t v_addr = (pde_t) va; //get the virtual address
    pde_t inner = performBitmask(0xFFFFFFFF, innerPage, offset, v_addr);
    pte_t outer = performBitmask(0xFFFFFFFF, outerPage, innerPage + offset, v_addr);

	
	pte_t map = (pde_t) pa;
	map >>= (int) offset;
	pageTable[pgdir[outer]][inner] = map;
}


/*Function that gets the next available page
*/
void *get_next_avail(int num_pages) {
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
          return NULL;
      }
   }

   return (void *)&freePage;
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
	if(physicalMemory == NULL) 
		set_physical_mem();
	
    pte_t * page = (pte_t *) get_next_avail((num_bytes / PGSIZE) + 1);
    for (int i = 0; i < (num_bytes / PGSIZE) + 1; i++) {
        unsigned char virt_mask = 0x80 >> ((*page + i) % 8);
        virtBitmap[(*page + i) / 8] |= virt_mask;

        unsigned char phys_mask = 0x80 >> ((frame + i) % 8);
        physBitmap[(frame + i) / 8] |= phys_mask;

        frame = (frame + 1) % numPhysPages;
    }
	
    pde_t outer = (*page) / 1024;
    pte_t inner = (*page) % 1024;
    pageDir[directEntry] = outer;
    directEntry = (directEntry + 1) % 1024;

    pte_t va = (outer << (int)(innerPage + offset)) | (inner << (int)offset);
    pte_t pa = (frame << (int)offset);

    page_map(pageDir, (void*)va, (void*)pa);
    pthread_mutex_unlock(&lock);
    return (void *) va;
}

/* Responsible for releasing one or more memory pages using virtual address (va)
*/
void t_free(void *va, int size) {

    /* Part 1: Free the page table entries starting from this virtual address
     * (va). Also mark the pages free in the bitmap. Perform free only if the 
     * memory from "va" to va+size is valid.
     */
	pthread_mutex_lock(&lock);
	pde_t address = (pde_t) va; //get the virtual address
    pde_t inner = performBitmask(0xFFFFFFFF, innerPage, offset, address);
    pde_t outer = performBitmask(0xFFFFFFFF, outerPage, innerPage + offset, address);
	
	pte_t page = (outer*1024) + inner;
	directEntry--;
	freePage -=  (size / PGSIZE) + 1;
	frame -=  (size / PGSIZE) + 1;
	
	for(int i=0; i<(size / PGSIZE) + 1; i++) {
        unsigned char virt_mask = ~(0x80 >> ((page + i) % 8));
        virtBitmap[(page + i) / 8] &= virt_mask;

        unsigned char phys_mask = ~(0x80 >> ((frame + i) % 8));
        physBitmap[(frame + i) / 8] &= phys_mask;

        frame = (frame + 1) % numPhysPages;
	}
	pthread_mutex_unlock(&lock);
	/*
     * Part 2: Also, remove the translation from the TLB
     */
}


/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
*/
int put_value(void *va, void *val, int size) {

    /* HINT: Using the virtual address and translate(), find the physical page. Copy
     * the contents of "val" to a physical page. NOTE: The "size" value can be larger 
     * than one page. Therefore, you may have to find multiple pages using translate()
     * function.
     */
    pthread_mutex_lock(&lock);
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

    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
     * matrix accessed. Similar to the code in test.c, you will use get_value() to
     * load each element and perform multiplication. Take a look at test.c! In addition to 
     * getting the values from two matrices, you will perform multiplication and 
     * store the result to the "answer array"
     */
     int i=0, j=0, k=0;
    for (i = 0; i < size; ++i) {
        for (j = 0; j < size; ++j) {
		int a = 0;
		put_value(answer + (i*size*sizeof(int)) + (j*sizeof(int)), &a, sizeof(int));
        }
    }

	// Multiplying first and second matrices and storing in mult.
	int res =0;
    for (i = 0; i < size; ++i) {
        for (j = 0; j < size; ++j) {
            for (k = 0; k < size; ++k) {
		int first, second, prev;
		get_value(mat1 + (i*size*sizeof(int)) + (k*sizeof(int)), &first, sizeof(int));
		get_value(mat2 + (k*size*sizeof(int)) + (j*sizeof(int)), &second, sizeof(int));
		//get_value(answer + (i*size*sizeof(int)) + (j*sizeof(int)), &prev, sizeof(int));
		//int res = prev + (first*second);
		res+=(first*second);
            }
            put_value(answer + (i*size*sizeof(int)) + (j*sizeof(int)), &res, sizeof(int));
            res = 0;
        }
    }
   
}
unsigned long performBitmask(unsigned long page, double shift, double offset, pde_t address) { 
	page <<= (int) offset;
	page <<= (int) (32 - (innerPage + offset));
	page >>= (int) (32 - (innerPage + offset));
	page = page & address;
	return page >>= (int) offset;
} 
