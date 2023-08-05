#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "printk.h"
#include "trap.h"
#include "assert.h"
#include "memory.h"
#include "process.h"

void KMain(void)
{
    InitIDT();
    printk("Retrieve memory map:\n");
    RetrieveMemoryInfo();
    InitMemory();
    InitProcess();
    StartScheduler();
}