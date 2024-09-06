#define _XOPEN_SOURCE 700
#include "mlpt.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdbool.h>
#include <stdalign.h>
#include <math.h>
#include <stdalign.h>


#define PAGE_TABLE_SIZE (1 << POBITS)  // the size of each page table 
#define PTESIZE 8  // the size of each page table entry (declared as 8 bytes in the writeup)
#define INVALID 0xFFFFFFFFFFFFFFFF  // all 1 bits to represent an invalid entry


size_t ptbr = 0;  // Page table base register; initially 0.
int allocated_page_count;  // amount of page table pages that have been allocated with posix_memalign
size_t * allocated_pages;  // an array of page table addresses; for deallocate() to deallocate all page tables within this array


/**
 * Input: a virtual address
 * Output: the offset value of the virtual address
 * 
 * Finds the offset value for a given virtual address.
 * It does this by masking all the lower POBITS.
*/
size_t get_offset_val(size_t va){
    int mask = ((1 << POBITS) - 1);
    size_t offset_val = va & mask;
    return offset_val;
}


/**
 * Input: a virtual address
 * Output: the amount of bits assigned for each vpn
 * 
 * Finds the amount of bits for each vpn (varies by POBITS).
 * It does this by first finding the amount of entries and then taking the log base 2 of this.
*/
size_t get_vpn_bit_size(size_t va) {
    int page_table_entries = PAGE_TABLE_SIZE / PTESIZE;
    size_t vpn_bit_size = log2(page_table_entries);
    return vpn_bit_size;
}


/**
 * Input: a virtual address, an array of size_t's of size LEVELS
 * Output: NA
 * 
 * Splits the virtual address into its separate vpn's, depending on the amount of LEVELS specified.
 * The vpn's are in reverse order, i.e. vpn1 is in the highest significant bits and vpnLEVELS is in
 * the lowest significant bits (not inclduing offset bits).
*/
void get_vpn_segments(size_t va, size_t * vpn_segments) {
    size_t vpn_bit_size = get_vpn_bit_size(va);
    size_t segment = va >> POBITS;  // get rid of offset bits
    int mask = ((1 << vpn_bit_size) - 1);  // mask the size of the bits reserved for the vpn

    for (int i = 0; i < LEVELS; ++i) {
        size_t vpn_num = segment & mask;
        vpn_segments[LEVELS - 1 - i] = vpn_num;  // assign the vpn to the array of vpn's in reverse order 
        segment = segment >> vpn_bit_size;  // shift the segment as to get rid of the vpn we just collected
    }
}


/**
 * Input: a virtual page number, a base address
 * Output: a pointer to a page table entry
 * 
 * Given a base address and a vpn, this function finds the page table entry and returns a pointer to it.
 * It can be thought of as the base_address being the page table, and the vpn being the index of the page 
 * table being accessed to find a page table entry
*/
size_t * get_pte_address_pointer(size_t vpn, size_t base_address) {
    size_t pte_address = vpn * PTESIZE + base_address;
    size_t * pte_address_ptr = (size_t*) pte_address;
    return pte_address_ptr;
}


/**
 * Input: a pointer to a page table entry
 * Output: a physical address; INVALID if the page table entry was invalid
 * 
 * By dereferencing the pointer to a page table entry, the page table entry is found. If this entry is invalid,
 * INVALID is returned. Otherwise, a physical address (i.e. an address to a new page table)
 * is returned. A page table entry is invalid if the bottom bit is not set to 1.
*/
size_t read_pte_pointer(size_t * pte_address_ptr) {
    size_t pte = *pte_address_ptr;

    if ((pte & 1) != 1) {  // if invalid pte
        return INVALID;
    }

    size_t physical_address = --pte;  // must get rid of valid bit for traversals
    return physical_address;
}


/**
 * Input: virtual address
 * Return: physical address; INVALID if translate cannot be complete because the pages are not all allocated
 * 
 * Attempts to find the physical address that corresponds to the given virtual address. This is done by going 
 * through the mapped page tables and reaching a final address. If all the necessary page tables have yet to be
 * allocated, it will fail and return INVALID.
 */
size_t translate(size_t va) {

    // get all vpn's
    size_t vpn_segments[LEVELS];
    get_vpn_segments(va, vpn_segments); 

    // set the first physical address to be the base page table pointer
    size_t physical_address = ptbr;

    if (ptbr == 0) {  // if the first page table has yet to be allocated, return INVALID
        return INVALID;
    }

    for (int index = 0; index < LEVELS; ++index) {
        size_t vpn = vpn_segments[index];
        size_t * pte_address_pointer = get_pte_address_pointer(vpn, physical_address);
        physical_address = read_pte_pointer(pte_address_pointer);

        if (physical_address == INVALID) { // if it was found as invalid, return so
            return INVALID;
        }

    }
    physical_address += get_offset_val(va);  // add the offset value to get the final physical address
    return physical_address;
}


/**
 * Input: a pointer to a page table
 * Output: NA
 * 
 * Given a pointer to a page table, add this page to the global array 'allocated_pages' as well as update
 * the 'allocated_page_count'. This array is dynamically allocated and as such the function is split into
 * two parts; a part for the initial allocation and a part for additional allocations. This function mainly
 * exists for the implementation of deallocate() by tracking what pages have been allocated.
*/
void add_page_to_allocated_pages(size_t page_pointer) {
    if (allocated_page_count == 0) {  // if allocated_pages does not exist, malloc
        allocated_page_count = 1;
        allocated_pages = (size_t *)malloc(sizeof(&page_pointer));
    }
    else {  // otherwise already exists and realloc
        allocated_page_count += 1;
        size_t * temp_ptr = (size_t *)realloc(allocated_pages, allocated_page_count * sizeof(&page_pointer));
        allocated_pages = temp_ptr;
    }
    allocated_pages[allocated_page_count - 1] = page_pointer;  // update allocated_pages with the page_pointer
}


/**
 * Input: a pointer to a page table entry
 * Output: NA
 * 
 * Allocate a single pages using posix_memalign. It takes in a pointer and assigns the newly allocated page to this pointer.
*/
void allocate_single_page(size_t* pte_address_ptr) {
    size_t resulting_pointer;

    // slightly strange format because of argument types and warnings; just using posix_memalign to create a new pointer to a page
    void * temp_pointer = NULL;
    posix_memalign(&temp_pointer, PAGE_TABLE_SIZE, PAGE_TABLE_SIZE);
    memset(temp_pointer, 0, PAGE_TABLE_SIZE);
    resulting_pointer = (size_t) temp_pointer;
    resulting_pointer |= 1;  // marking the pointer as valid

    *pte_address_ptr = resulting_pointer;  // assign the new page's pointer to the page table entry

    add_page_to_allocated_pages(resulting_pointer);  // make sure to track the page
}


/**
 * Input: virtual address
 * Output: NA
 * 
 * Creating the page tables sufficient to have a mapping between the given virtual address and some physical address.
 * If one of the necessary pages is there, it ignores this page and continues down the traversal.
 */
void page_allocate(size_t va) {
    size_t vpn_segments[LEVELS];  // get vpns
    get_vpn_segments(va, vpn_segments); 
    
    if (ptbr == 0) {  // beginning page allocation for ptbr is slightly different so separate from singel_page_allocate()
        posix_memalign((void**) &ptbr, PAGE_TABLE_SIZE, PAGE_TABLE_SIZE);
        memset((void**) ptbr, 0, PAGE_TABLE_SIZE);
        add_page_to_allocated_pages(ptbr);
    }

    size_t physical_address = ptbr;  // base address

    for (int index = 0; index < LEVELS; ++index) {
        size_t vpn = vpn_segments[index];
        size_t * pte_address_ptr = get_pte_address_pointer(vpn, physical_address);
        physical_address = read_pte_pointer(pte_address_ptr);

        if (physical_address == INVALID) {
            // if not allocated, allocate
            allocate_single_page(pte_address_ptr);
            physical_address = read_pte_pointer(pte_address_ptr);  // update the base address
        }
    }
}


/**
 * Input: a page address
 * Output: NA
 * 
 * Deallocates a specific page as given by the page address. It shifts to clear the bottom bits because some were set 
 * as a result of validating entries.
*/
void deallocate_specific_page(size_t page_address) {
    page_address = page_address >> 1;
    page_address = page_address << 1;

    void * page_address_ptr = (void *) page_address;  // casting to avoid warnings
    free(page_address_ptr);
}


/**
 * Input: NA
 * Ouput: NA
 * 
 * Deallocates any and all things that have been allocated. This includes the global array 'allocated_pages' as well as 
 * the pages that have been allocated with posix_memalign. It also resets the other global values, which include 'ptbr' and 
 * 'allocated_page_count'.
*/
void deallocate(void) {
    if (ptbr != 0)  {  // if ptbr is already 0, nothing to deallocate as it is all clear
        
        for (int index = 0; index < allocated_page_count; ++index) {
            deallocate_specific_page(allocated_pages[index]);
        }

        free(allocated_pages);
        ptbr = 0;
        allocated_page_count = 0;
    }
}
