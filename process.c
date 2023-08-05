#include "process.h"
#include "printk.h"
#include "assert.h"
#include <string.h>

/* Private variable ----------------------------------------------------------*/

extern TSS TaskStateSegment; /* Extern from ASM. */
static Process s_process_manager[MAXIMUM_NUMBER_OF_PROCESS];
static int s_pid_num = 1;

/* Private function prototypes -----------------------------------------------*/

static Process *FindFreeProcessSlot(void);
static void SetProcessEntry(Process* proc);

/**
 * @brief   Set TaskStateSegment point to top of the process's kernel stack. So
 *          when we jump from ring 3 to ring 0, the kernel stack will be used.
 * 
 * @param   proc 
 * @return  none
 */
static void SetTSS(Process *proc);

/* Public function -----------------------------------------------------------*/
void InitProcess(void)
{
    Process *proc = FindFreeProcessSlot();
    ASSERT (proc == &s_process_manager[0]);

    SetProcessEntry(proc);
}

void StartScheduler(void)
{
    /* Test switch to first process. */

    /* 1. Set TaskStateSegment point to it's kernel stack. */
    SetTSS(&s_process_manager[0]);

    /* 2. Switch to user virtual memory. */
    SwitchVM(s_process_manager[0].page_map);

    /* 3. Start user program. */
    ProcessStart(s_process_manager[0].tf);
}

/* Private function ----------------------------------------------------------*/
static Process *FindFreeProcessSlot(void)
{
    Process *proc = NULL;

    for (int i = 0; i < MAXIMUM_NUMBER_OF_PROCESS; i++)
    {
        if (s_process_manager[i].state == PROCESS_SLOT_UNUSED) {
            proc = &s_process_manager[i];
            break;
        }
    }

    return proc;
}

static void SetProcessEntry(Process* proc)
{
    uint64_t stack_top;

    proc->state = PROCESS_SLOT_INITIALIZED;
    proc->pid = s_pid_num++;

    /* Each process has 2MB its own kernel stack. */
    proc->stack = (uint64_t)kalloc();
    ASSERT(proc->stack != 0);

    memset((void *)proc->stack, 0, STACK_SIZE);
    stack_top = proc->stack + STACK_SIZE;

    /* We save stack frame at top of kernel stack. */
    proc->tf = (TrapFrame *)(stack_top - sizeof(TrapFrame));

    proc->tf->cs = 0x10 | 3;
    proc->tf->rip = USER_VIRTUAL_ADDRESS_BASE;
    proc->tf->ss = 0x18 | 3;
    proc->tf->rsp = USER_STACK_START;
    proc->tf->rflags = 0x202;

    /* We create a virtual memory that is mapped with kernel, so the kernel will
     * reside at the same address in every user virtual memory. */
    proc->page_map = SetupKVM();
    ASSERT(proc->page_map != 0);

    /* Map user memory (2MB) to the kernel virtual memory we just made. */
    /* We use 0x200000 to test first process. */
    ASSERT(SetupUVM(proc->page_map, (uint64_t)PHY_TO_VIR(0x20000), 6000));
}

static void SetTSS(Process *proc)
{
    /* We set TSS structure by assigning the top of the kernel stack to rsp0 in
     * the TaskStateSegment. */
    TaskStateSegment.rsp0 = proc->stack + STACK_SIZE;
}