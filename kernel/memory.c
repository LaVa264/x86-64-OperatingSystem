#include <stddef.h>
#include <string.h>
#include "memory.h"
#include "printk.h"
#include "assert.h"

/* Private define ------------------------------------------------------------*/
#define MEMORY_MAX_FREE_REGIONS                 50
#define MEMORY_REGION_USABLE_RAM_TYPE           1
#define MEMORY_REGION_RESERVED_TYPE             2
#define MEMORY_REGION_ACPI_RECLAIMABLE_TYPE     3
#define MEMORY_REGION_ACPI_NVS_TYPE             4
#define MEMORY_REGION_BAD_MEMORY_TYPE           5

/* We save the memory informations at these addresses by using loader code.   */
#define MEMORY_REGION_COUNT_BASE_ADDR           0x9000
#define MEMORY_REGION_STRUCTURES_BASE_ADDR      0x9008

/* Private variable ----------------------------------------------------------*/
static FreeMemoryRegion s_free_memory_regions[MEMORY_MAX_FREE_REGIONS];
extern char l_kernel_end;
static Page s_free_memory_page_head;
static uint64_t s_free_memory_end_address = 0;
static uint64_t s_total_mem = 0;

/* Private function prototypes -----------------------------------------------*/
static void FreeRegion(uint64_t v_start, uint64_t v_end);

/**
 * @brief   This function find PML4 table entry according to the `v` virtual
 *          address.
 * 
 * @param map 
 * @param v 
 * @param alloc 
 * @param attribute 
 * @return PageDirPointerTable 
 */
static PageDirPointerTable
FindPML4TableEntry(uint64_t map,
                    uint64_t v,
                    int alloc,
                    uint32_t attribute);

/**
 * @brief   This function map the a virtual memory region to physical memory.
 *          the (phys -> end) map to (virtual -> end). And when we load the page
 *          using cr3 register, every access to virtual memory region will be
 *          mapped back to the physical memory. And also, the attributes for
 *          this memory region will be assigned. Violation could be emit a CPU
 *          exception.
 * 
 * @param map 
 * @param v 
 * @param end 
 * @param phys 
 * @param attr 
 * @return true 
 * @return false 
 */
static bool MapPages(uint64_t map,
                        uint64_t v,
                        uint64_t end,
                        uint64_t phys,
                        uint32_t attr);

/**
 * @brief 
 * 
 * @param map           - Page map level 4 table.
 * @param v             - Virtual address.
 * @param alloc         - If true, allocate a page if it doesn't exist.
 * @param attr          - Attribute.
 * @return PageDir
 */
static PageDir FindPageDirPointerTableEntry(uint64_t map,
                                            uint64_t v,
                                            int alloc,
                                            uint32_t attr);

static void FreePages(uint64_t map, uint64_t v_start, uint64_t v_end);

static void FreePML4Table(uint64_t map);

static void FreePDTable(uint64_t map);

static void FreePDPTable(uint64_t map);

/* Public function -----------------------------------------------------------*/
void RetrieveMemoryInfo(void)
{
    int32_t count = *(int32_t *)MEMORY_REGION_COUNT_BASE_ADDR;
    E820 *mem_map = (E820 *)MEMORY_REGION_STRUCTURES_BASE_ADDR;
    int free_memory_region_count = 0;

    ASSERT(count < MEMORY_MAX_FREE_REGIONS);

    for(int32_t i = 0; i < count; i++)
    {
        if(mem_map[i].type == MEMORY_REGION_USABLE_RAM_TYPE) {
            s_free_memory_regions[free_memory_region_count].address =
            mem_map[i].address;

            s_free_memory_regions[free_memory_region_count].length =
            mem_map[i].length;

            s_total_mem += mem_map[i].length;
            free_memory_region_count++;
        }

        printk("Physical Address: %x   size: %uKB   type: %u\n",
                mem_map[i].address,
                mem_map[i].length/1024,
                (uint64_t)mem_map[i].type);
    }
    printk("Total Free Memory: %uKB\n", s_total_mem/1024);

    for (int i = 0; i < free_memory_region_count; i++)
    {
        uint64_t v_start = PHY_TO_VIR(s_free_memory_regions[i].address);
        uint64_t v_end = v_start + s_free_memory_regions[i].length;

        /* We collect the free memory. */
        if (v_start > (uint64_t)&l_kernel_end) {
            FreeRegion(v_start, v_end);
        } else if (v_end > (uint64_t)&l_kernel_end) {
            FreeRegion((uint64_t)&l_kernel_end, v_end);
        }
    }

    s_free_memory_end_address = (uint64_t)s_free_memory_page_head.next 
                                + PAGE_SIZE;

    Page* start_page = NULL;
    for (Page *page = s_free_memory_page_head.next; page != NULL;) {
        if (page != NULL) {
            start_page = page;
        }
        page = page->next;
    }

    printk("Virtual Free Memory: %x->%x\n",
            start_page,
            s_free_memory_end_address);
}

void InitMemory(void)
{
    uint64_t kernel_map = SetupKVM();
    ASSERT(kernel_map);

    SwitchVM(kernel_map);
    printk("Memory Manage is working now.\n");
}

void SwitchVM(uint64_t map)
{
    LoadCR3(VIR_TO_PHY(map));
}

void FreeVM(uint64_t map)
{
    /* we will free from lower level to higher level tables of paging
     * hierarchical: Free Physical Page -> Page Directory -> Page Directory
     * Pointer Table -> Page Map Level 4 Table. */
    FreePages(map,
            USER_VIRTUAL_ADDRESS_BASE,
            USER_VIRTUAL_ADDRESS_BASE + PAGE_SIZE);
    FreePDTable(map);
    FreePDPTable(map);
    FreePML4Table(map);
}


bool SetupUVM(uint64_t map, uint64_t start_location, int size)
{
    bool status = false;
    
    /* 1. Allocation a page for user program. 
     * TODO: Expend to allow user program using more memory pages.
     */
    void *page = kalloc();
    if (page == NULL) {
        return false;
    }

    memset(page, 0, PAGE_SIZE);

    /* 2. Map the page to user virtual address. */
    status = MapPages(map,
                    USER_VIRTUAL_ADDRESS_BASE,
                    USER_VIRTUAL_ADDRESS_BASE + PAGE_SIZE,
                    VIR_TO_PHY(page),
                    TABLE_ENTRY_PRESENT_ATTRIBUTE 
                    | TABLE_ENTRY_WRITABLE_ATTRIBUTE
                    | TABLE_ENTRY_USER_ATTRIBUTE);

    /* 3. Copy user program to the page. */
    if (status == true) {
        memcpy(page, (void *)start_location, size);
    } else {
        kfree((uint64_t)page);
        FreeVM(map);
    }

    return status;
}

void kfree(uint64_t addr)
{
    /* Check the address is aligned. */
    ASSERT_ADDR_IS_ALIGNED(addr);

     /* Check the address is not within kernel and not out of memory. */
    ASSERT(addr >= (uint64_t)&l_kernel_end);
    ASSERT(addr + PAGE_SIZE <= VIRTUAL_ADDRESS_END);

    /* To free memory, we just add it to the free memory page linked list. */
    Page *page_address = (Page *)addr;
    page_address->next = s_free_memory_page_head.next;
    s_free_memory_page_head.next = page_address;
}

void* kalloc(void)
{
    Page *page_address = s_free_memory_page_head.next;

    if (page_address != NULL) {
        /* Check the address is aligned. */
        ASSERT_ADDR_IS_ALIGNED((uint64_t)page_address);

        /* Check the address is not within kernel and not out of memory. */
        ASSERT((uint64_t)page_address >= (uint64_t)&l_kernel_end);
        ASSERT((uint64_t)page_address + PAGE_SIZE <= VIRTUAL_ADDRESS_END);

        s_free_memory_page_head.next = page_address->next;
    }

    return (void *)page_address;
}

uint64_t SetupKVM(void)
{
    uint64_t kernel_page_map = (uint64_t)kalloc();

    if (kernel_page_map != 0) {
        memset((void *)kernel_page_map, 0, PAGE_SIZE);

        /* Map the kernel to the same physical address. */
        bool status =
        MapPages(kernel_page_map,
                KERNEL_VIRTUAL_ADDRESS_BASE,   /* Start kernel address.   */
                s_free_memory_end_address,     /* End kernel address.     */
                VIR_TO_PHY(KERNEL_VIRTUAL_ADDRESS_BASE),
                TABLE_ENTRY_PRESENT_ATTRIBUTE | TABLE_ENTRY_WRITABLE_ATTRIBUTE);

        if (!status) {
            FreeVM(kernel_page_map);
            kernel_page_map = 0;
        }
    }

    return kernel_page_map;
}

uint64_t GetTotalMem(void)
{
    return s_total_mem;
}

bool CopyUVM(uint64_t new_page, uint64_t current_page, int size)
{
    bool status = false;
    unsigned int index = 0;
    PageDir pd = NULL;
    uint64_t start = 0;

    void * page = kalloc();
    if (page != NULL) {
        memset(page, 0, PAGE_SIZE);
        status = MapPages(new_page,
                            USER_VIRTUAL_ADDRESS_BASE,
                            USER_VIRTUAL_ADDRESS_BASE + PAGE_SIZE,
                            VIR_TO_PHY(page),
                            TABLE_ENTRY_PRESENT_ATTRIBUTE 
                            | TABLE_ENTRY_WRITABLE_ATTRIBUTE
                            | TABLE_ENTRY_USER_ATTRIBUTE);

        if (status == true) {
            pd = FindPageDirPointerTableEntry(current_page,
                                              USER_VIRTUAL_ADDRESS_BASE,
                                              0,
                                              0);
            if (pd != NULL) {
                index = (USER_VIRTUAL_ADDRESS_BASE >> 21) & 0x1FF;
                ASSERT(((uint64_t)pd[index] & TABLE_ENTRY_PRESENT_ATTRIBUTE)
                        == 1);

                start = PHY_TO_VIR(PAGE_ADDRESS(pd[index]));

                memcpy(page, (void*)start, size);
            } else {
                kfree((uint64_t)page);
                FreeVM(new_page);
                status = false;
            }

        } else {
            kfree((uint64_t)page);
            FreeVM(new_page);
        }
    }

    return status;
}

/* Private function ----------------------------------------------------------*/
static void FreeRegion(uint64_t v_start, uint64_t v_end)
{
    for (uint64_t start = PAGE_ALIGN_UP(v_start);
            start + PAGE_SIZE <= v_end;
            start += PAGE_SIZE) {

        if (start + PAGE_SIZE <= VIRTUAL_ADDRESS_END)
        {
            kfree(start);
        }
    }
}

static PageDirPointerTable
FindPML4TableEntry(uint64_t map,
                    uint64_t v,
                    int alloc,
                    uint32_t attribute)
{
    PageDirPointerTable *map_entry = (PageDirPointerTable *)map;
    PageDirPointerTable pdptr = NULL;
    unsigned int index = (v >> 39) & 0x1FF;

    if ((uint64_t)map_entry[index] & TABLE_ENTRY_PRESENT_ATTRIBUTE) {
        pdptr = (PageDirPointerTable) 
                PHY_TO_VIR(PAGE_DIRECTORY_TABLE_ADDRESS(map_entry[index]));
    } else if (alloc == 1) {
        /* New Page Directory not exist, we create new one. */
        pdptr = (PageDirPointerTable)kalloc();
        if (pdptr != NULL) {
            memset(pdptr, 0, PAGE_SIZE);
            map_entry[index] = (PageDirPointerTable)
                                (VIR_TO_PHY(pdptr) | attribute);
        }
    }

    return pdptr;
}

static bool MapPages(uint64_t map,
                        uint64_t v,
                        uint64_t end,
                        uint64_t phys,
                        uint32_t attr)
{
    uint64_t v_start = PAGE_ALIGN_DOWN(v);
    uint64_t v_end = PAGE_ALIGN_UP(end);
    PageDir pd = NULL;
    unsigned int index = 0;

    ASSERT(v < end);
    ASSERT_ADDR_IS_ALIGNED(phys);
    /* Check out of range memory. */
    ASSERT(phys + v_end - v_start <= VIRTUAL_ADDRESS_END);

    do {
        /* Find the page directory pointer table entry which points to page
         * directory table. */
        pd = FindPageDirPointerTableEntry(map, v_start, 1, attr);
        if (pd == NULL) {
            return false;
        }

        /* We get the index to locate the correct page entry according to the
         * virtual address. The index value is 9 bits in total starting from 
         * bits 21. So we shift right 21 bits and clear other bits except the
         * lower 9 bits of the result. */
        index = (v_start >> 21) & 0x1FF;

        /* The value in index is used to find the entry in the page directory
         * table. We check present bit, if it is set means that we remapping to
         * the used page. So we don't allow this. */
        ASSERT(((uint64_t)pd[index] & TABLE_ENTRY_PRESENT_ATTRIBUTE) == 0);

        /* We add physical address and atrributes, and entry bit to indicate
         * this is 2MB page translation. */
        pd[index] = (PageDirEntry) (phys | attr | TABLE_ENTRY_ENTRY_ATTRIBUTE);

        /* Map the next page. */
        v_start += PAGE_SIZE;
        phys += PAGE_SIZE;

    } while (v_start + PAGE_SIZE <= v_end);

    return true;
}

static PageDir FindPageDirPointerTableEntry(uint64_t map,
                                            uint64_t v,
                                            int alloc,
                                            uint32_t attr)
{
    PageDirPointerTable pdptr = NULL;
    PageDir pd = NULL;
    unsigned int index = (v >> 30) & 0x1FF;

    pdptr = FindPML4TableEntry(map, v, alloc, attr);
    if (pdptr == NULL) {
        return NULL;
    }

    if ((uint64_t)pdptr[index] & TABLE_ENTRY_PRESENT_ATTRIBUTE) {
        pd = (PageDir) PHY_TO_VIR(PAGE_DIRECTORY_TABLE_ADDRESS(pdptr[index]));
    } else if (alloc == 1) {
        /* If Page Directory does not exist, we create new one. */
        pd = (PageDir)kalloc();
        if (pd != NULL) {
            memset(pd, 0, PAGE_SIZE);
            pdptr[index] = (PageDir)(VIR_TO_PHY(pd) | attr);
        }
    }

    return pd;
}

static void FreePages(uint64_t map, uint64_t v_start, uint64_t v_end)
{
    unsigned int index;
    ASSERT_ADDR_IS_ALIGNED(v_start);
    ASSERT_ADDR_IS_ALIGNED(v_end);

    do {
        /* Get all Page Directory Pointer Table Entry and free them. */
        PageDir pd = FindPageDirPointerTableEntry(map, v_start, 0, 0);
        if (pd != NULL) {
            index = (v_start >> 21) & 0x1FF;

            /* The present bit should be set when we free it because the PDPT,
             * the entry in the PDPT is pointing to 2MB physical address. */
            if (pd[index] & TABLE_ENTRY_PRESENT_ATTRIBUTE) {
                kfree(PHY_TO_VIR(PAGE_ADDRESS(pd[index])));
                pd[index] = 0;
            }
        }

        v_start += PAGE_SIZE;
    } while (v_start + PAGE_SIZE <= v_end);
}

static void FreePML4Table(uint64_t map)
{
    kfree(map);
}

static void FreePDTable(uint64_t map)
{
    PageDirPointerTable *map_entry = (PageDirPointerTable *)map;

    /* Each memory map have 512 page directory pointer tables. */
    for (int i = 0; i < TOTAL_PAGE_DIR_POINTER_TABLE; i++) {
        if ((uint64_t)map_entry[i] & TABLE_ENTRY_PRESENT_ATTRIBUTE) {
            PageDir *pdptr = (PageDir *)
                         PHY_TO_VIR(PAGE_DIRECTORY_TABLE_ADDRESS(map_entry[i]));
            
            /* Each PDP table also include 512 entries which point to page
             * directory tables. */
            for (int j = 0; j < TOTAL_PAGE_DIR_TABLE_OF_EACH_PDPT; j++) {
                if ((uint64_t)pdptr[j] & TABLE_ENTRY_PRESENT_ATTRIBUTE) {
                    kfree(PHY_TO_VIR(PAGE_DIRECTORY_TABLE_ADDRESS(pdptr[j])));
                    pdptr[j] = 0;
                }
            }
        }
    }
}

static void FreePDPTable(uint64_t map)
{
    PageDirPointerTable *map_entry = (PageDirPointerTable *)map;
    for (int i = 0; i < TOTAL_PAGE_DIR_POINTER_TABLE; i++) {
        if ((uint64_t)map_entry[i] & TABLE_ENTRY_PRESENT_ATTRIBUTE) {
            kfree(PHY_TO_VIR(PAGE_DIRECTORY_TABLE_ADDRESS(map_entry[i])));
            map_entry[i] = 0;
        }
    }
}