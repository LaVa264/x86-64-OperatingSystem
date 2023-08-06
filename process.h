#pragma once
#include <stdint.h>
#include <list.h>
#include "common.h"
#include "trap.h"
#include "memory.h"

/* Public define -------------------------------------------------------------*/
#define STACK_SIZE                      PAGE_SIZE    /* 2MB. */
#define MAXIMUM_NUMBER_OF_PROCESS       10
#define USER_STACK_START                (USER_VIRTUAL_ADDRESS_BASE + STACK_SIZE)
#define NORMAL_PROCESS_WAIT_ID          -1
#define INIT_PROCESS_WAIT_ID             1

/* Public type ---------------------------------------------------------------*/
typedef enum  {
    PROCESS_SLOT_UNUSED = 0,
    PROCESS_SLOT_INITIALIZED,
    PROCESS_SLOT_READY,
    PROCESS_SLOT_RUNNING,
    PROCESS_SLOT_SLEEPING,
    PROCESS_SLOT_KILLED
} ProcessState;


/**
 * @brief   Process Control Block structure. This structure is used to store the
 *          essential data of the process. It is maintained in the kernel space,
 *          user program is not allowed to access it.
 *
 * @property next       - Next process to run.
 * @property pid        - Process Identification number of a process.
 * @property wait_id    - Save the process wait id.
 * @property state      - Current state of process
 * @property page_map   - Saves the address of page map level 4 table, when we
 *                        run the process, we use this to switch to the process
 *                        's virtual memory.
 * @property stack      - Stack pointer is used when enter the kernel mode. A
 *                        Process has two stack, one for user code, and one for
 *                        kernel code. The one for user code is saved in trap
 *                        frame.
 * @property tf         - 
 */
typedef struct {
    List *next;
    int pid;
    int wait_id;
    ProcessState state;
    uint64_t page_map;
    uint64_t context;
    uint64_t stack;
    TrapFrame *tf;
} Process;

/**
 * @brief   The TSS (Task state segment) structure is used only for setting up
 *          stack pointer for ring 0.
 */
typedef struct {
    uint32_t res0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t res1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t res2;
    uint16_t res3;
    uint16_t iopb;
} __attribute__ ((packed)) TSS;

typedef struct {
    Process *current_proc;
    HeadList ready_proc_list;
    HeadList wait_proc_list;
    HeadList kill_proc_list;
} Scheduler;

/* Public function prototype -------------------------------------------------*/

void InitProcess(void);
void StartScheduler(void);
void ProcessStart(TrapFrame *tf);
Scheduler *GetScheduler(void);
void Yield(void);

/**
 * @brief       Switch context between two process, save old context to `old`,
 *              And retrieve new context from `new`.
 * 
 * @param[in]   old   The address of context field in process.
 * @param[in]   new   The context value in the next process.
 * @return none
 */
void ContextSwitch(uint64_t *old, uint64_t new);

/**
 * @brief       Add current process to wait list wih wait_id.
 * 
 * @param[in]   wait_id     - Current process wait id.
 */
void Sleep(int wait_id);

/**
 * @brief       Wakeup processes which match wait_id.
 * 
 * @param[in]   wait_id     - Wait id to wake up processes matching.
 */
void Wakeup(int wait_id);

/**
 * @brief       Exit current process, remove it from ready list.
 * 
 */
void Exit(void);

/**
 * @brief       Waiting for children process exit, the init process call this to
 *              cleanup exiting processes. This system call never return from
 *              kernel mode, so, this function should be called when the init
 *              process has done it's job in user space.
 * 
 */
void Wait(void);