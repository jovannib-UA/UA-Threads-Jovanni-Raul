/*
    CYBV 489
    Group 10: Raul Cano and Jovanni Blanco
    Professor: Li Xu
    Last Update: 2/12/2026
*/

#define _CRT_SECURE_NO_WARNINGS
#define STATUS_READY    1
#define STATUS_RUNNING  2
#define STATUS_BLOCKED  3
#define STATUS_QUIT     4
#define TIME_SLICE_MS   10

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include "THREADSLib.h"
#include "Scheduler.h"
#include "Processes.h"

Process processTable[MAX_PROCESSES];
Process* runningProcess = NULL;

interrupt_handler_t* intVector;

int nextPid = 1;
int debugFlag = 1;

static int gChildExited = 0;
static int gChildExitCode = 0;
static int gChildPid = -1;

static int watchdog(char*);
static inline void disableInterrupts();
static inline void enableInterrupts();
void dispatcher();
static int launch(void*);
static void check_deadlock();
static void DebugConsole(char* format, ...);
static void clock_handler(char* devicename, uint8_t command, uint32_t status);
static int isWatchdogName(const char* name);
static Process* readyQ[HIGHEST_PRIORITY + 1];

static int find_free_slot(void);
static int count_children(const Process* p);
void time_slice(void);

static int signalTable[MAX_PROCESSES];

static Process* get_process_by_pid(int pid)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processTable[i].pid == pid)
        {
            return &processTable[i];
        }
    }
    return NULL;
}

static int get_slot_by_process(Process* p)
{
    if (p == NULL) return -1;
    return (int)(p - processTable);
}

static void enqueue_ready(Process* p)
{
    if (p == NULL) return;

    int pr = p->priority;
    if (pr < LOWEST_PRIORITY) pr = LOWEST_PRIORITY;
    if (pr > HIGHEST_PRIORITY) pr = HIGHEST_PRIORITY;

    p->nextReadyProcess = NULL;

    if (readyQ[pr] == NULL)
    {
        readyQ[pr] = p;
        return;
    }

    Process* current = readyQ[pr];
    while (current->nextReadyProcess != NULL)
    {
        current = current->nextReadyProcess;
    }
    current->nextReadyProcess = p;
}

static Process* dequeue_ready_highest(void)
{
    for (int pr = HIGHEST_PRIORITY; pr >= LOWEST_PRIORITY; pr--)
    {
        if (readyQ[pr] != NULL)
        {
            Process* p = readyQ[pr];
            readyQ[pr] = p->nextReadyProcess;
            p->nextReadyProcess = NULL;
            return p;
        }
    }
    return NULL;
}

static int any_non_system_active(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processTable[i].pid != 0)
        {
            if (strcmp(processTable[i].name, "watchdog") != 0 &&
                strcmp(processTable[i].name, "Scheduler") != 0)
            {
                if (processTable[i].status == STATUS_READY ||
                    processTable[i].status == STATUS_RUNNING ||
                    processTable[i].status == STATUS_BLOCKED)
                {
                    return 1;
                }
            }
        }
    }
    return 0;
}

static void unlink_child(Process* parent, Process* child)
{
    if (!parent || !child) return;

    Process** link = &parent->pChildren;
    while (*link)
    {
        if (*link == child)
        {
            *link = child->nextSiblingProcess;
            return;
        }
        link = &(*link)->nextSiblingProcess;
    }
}

/* DO NOT REMOVE */
extern int SchedulerEntryPoint(void* pArgs);
int check_io_scheduler();
check_io_function check_io;

/*************************************************************************
   bootstrap()

   Purpose - This is the first function called by THREADS on startup.

             The function must setup the OS scheduler and primitive
             functionality and then spawn the first two processes.

             The first two process are the watchdog process
             and the startup process SchedulerEntryPoint.

             The statup process is used to initialize additional layers
             of the OS.  It is also used for testing the scheduler
             functions.

   Parameters - Arguments *pArgs - these arguments are unused at this time.

   Returns - The function does not return!

   Side Effects - The effects of this function is the launching of the kernel.

 *************************************************************************/
int bootstrap(void* pArgs)
{
    int result; /* value returned by call to spawn() */

    /* set this to the scheduler version of this function.*/
    check_io = check_io_scheduler;

    /* Initialize the process table. */
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        processTable[i].pid = 0;
        processTable[i].context = NULL;
        processTable[i].nextReadyProcess = NULL;
        processTable[i].nextSiblingProcess = NULL;
        processTable[i].pParent = NULL;
        processTable[i].pChildren = NULL;
        processTable[i].status = 0;
        processTable[i].priority = 0;
        processTable[i].entryPoint = NULL;
        processTable[i].stack = NULL;
        processTable[i].stacksize = 0;
        processTable[i].name[0] = '\0';
        processTable[i].startArgs[0] = '\0';
        processTable[i].exitCode = 0;
        signalTable[i] = 0;
        processTable[1].cpuTime = 0;
    }

    runningProcess = NULL;
    nextPid = 1;

    /* Initialize the Ready list, etc. */
    for (int p = 0; p <= HIGHEST_PRIORITY; p++)
    {
        readyQ[p] = NULL;
    }

    /* Initialize the clock interrupt handler */
    intVector = get_interrupt_handlers();
    intVector[THREADS_TIMER_INTERRUPT] = clock_handler;

    /* startup a watchdog process */
    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n", result);
        stop(1);
    }

    /* start the test process, which is the main for each test program.  */
    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY);
    if (result < 0)
    {
        console_output(debugFlag, "Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n", result);
        stop(1);
    }

    dispatcher();

    /* Initialized and ready to go!! */
    console_output(debugFlag, "All processes completed.\n");
    // not a real process, wont return any debug flags

    stop(0);
    return 0;

}

/*************************************************************************
   k_spawn()

   Purpose - spawns a new process.

             Finds an empty entry in the process table and initializes
             information of the process.  Updates information in the
             parent process to reflect this child process creation.

   Parameters - the process's entry point function, the stack size, and
                the process's priority.

   Returns - The Process ID (pid) of the new child process
             The function must return if the process cannot be created.

************************************************************************ */
int k_spawn(char* name, int (*entryPoint)(void*), void* arg, int stacksize, int priority)
{
    int proc_slot;
    struct _process* pNewProc;

    DebugConsole("spawn(): creating process %s\n", name);

    disableInterrupts();

    /* Validate all of the parameters, starting with the name. */
    if (name == NULL)
    {
        console_output(debugFlag, "spawn(): Name value is NULL.\n");
        enableInterrupts();
        return -1;
    }

    if (strlen(name) >= (MAXNAME - 1))
    {
        console_output(debugFlag, "spawn(): Process name is too long.  Halting...\n");
        stop(1);
    }

    if (priority < LOWEST_PRIORITY || priority > HIGHEST_PRIORITY)
    {
        console_output(debugFlag, "spawn(): Priority out of range.\n");
        enableInterrupts();
        return -3;
    }

    if (stacksize < THREADS_MIN_STACK_SIZE)
    {
        console_output(debugFlag, "spawn(): Stack size is too small\n");
        enableInterrupts();
        return -2;
    }

    /* Find an empty slot in the process table */
    proc_slot = find_free_slot();
    if (proc_slot < 0)
    {
        enableInterrupts();
        return -1;
    }

    pNewProc = &processTable[proc_slot];

    /* Setup the entry in the process table. */
    strcpy(pNewProc->name, name);
    pNewProc->pid = nextPid++;
    pNewProc->priority = priority;
    pNewProc->status = STATUS_READY;
    pNewProc->entryPoint = entryPoint;
    pNewProc->stacksize = (unsigned int)stacksize;
    pNewProc->nextReadyProcess = NULL;
    pNewProc->nextSiblingProcess = NULL;
    pNewProc->pChildren = NULL;
    pNewProc->pParent = NULL;
    pNewProc->cpuTime = 0;
    pNewProc->startTime = read_clock();
    pNewProc->lastReadTime = 0;

    if (arg != NULL)
    {
        strncpy(pNewProc->startArgs, (char*)arg, MAXARG - 1);
        pNewProc->startArgs[MAXARG - 1] = '\0';
    }
    else
    {
        pNewProc->startArgs[0] = '\0';
    }

    /* If there is a parent process,add this to the list of children. */
    if (runningProcess != NULL)
    {
        pNewProc->pParent = runningProcess;

        if (runningProcess->pChildren == NULL)
        {
            runningProcess->pChildren = pNewProc;
        }
        else
        {
            Process* cur = runningProcess->pChildren;
            while (cur->nextSiblingProcess != NULL)
            {
                cur = cur->nextSiblingProcess;
            }
            cur->nextSiblingProcess = pNewProc;
        }
    }

    /* Add the process to the ready list. */

    /* Initialize context for this process, but use launch function pointer for
     * the initial value of the process's program counter (PC)
    */
    pNewProc->context = context_initialize(launch, stacksize, pNewProc);
    enqueue_ready(pNewProc);
    if (!isWatchdogName(name) && strcmp(name, "Scheduler") != 0)
    {
        gChildPid = pNewProc->pid;
    }

    enableInterrupts();
    return pNewProc->pid;


} /* spawn */

/**************************************************************************
   Name - launch

   Purpose - Utility function that makes sure the environment is ready,
             such as enabling interrupts, for the new process.

   Parameters - none

   Returns - nothing
*************************************************************************/
static int launch(void* args)
{
    if (runningProcess != NULL)
    {
        runningProcess->status = STATUS_RUNNING;
    }

    DebugConsole("launch(): started: %s\n", runningProcess->name);

    /* Enable interrupts */
    enableInterrupts();

    /* Call the function passed to spawn and capture its return value */
    int rc = 0;
    if (runningProcess->entryPoint != NULL)
    {
        void* callArg = NULL;
        if (runningProcess->startArgs[0] != '\0')
        {
            callArg = (void*)runningProcess->startArgs;
        }
        rc = runningProcess->entryPoint(callArg);
    }

    DebugConsole("Process %d returned to launch\n", runningProcess->pid);

    /* Stop the process gracefully */
    k_exit(rc);

    return 0;
}

/**************************************************************************
   Name - k_wait

   Purpose - Wait for a child process to quit.  Return right away if
             a child has already quit.

   Parameters - Output parameter for the child's exit code.

   Returns - the pid of the quitting child, or
        -4 if the process has no children
        -5 if the process was signaled in the join

************************************************************************ */
int k_wait(int* code)
{
    if (runningProcess == NULL)
    {
        return -4;
    }

    while (1)
    {
        disableInterrupts();
        int hasChild = 0;

        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if ((processTable[i].pid != 0) && (processTable[i].pParent == runningProcess))
            {
                hasChild = 1;

                if (processTable[i].status == STATUS_QUIT)
                {
                    Process* child = &processTable[i];
                    int pid = child->pid;

                    if (code != NULL)
                    {
                        *code = child->exitCode;
                    }

                    unlink_child(runningProcess, child);

                    child->pid = 0;
                    child->context = NULL;
                    child->nextReadyProcess = NULL;
                    child->nextSiblingProcess = NULL;
                    child->pParent = NULL;
                    child->pChildren = NULL;
                    child->status = 0;
                    child->priority = 0;
                    child->entryPoint = NULL;
                    child->stack = NULL;
                    child->stacksize = 0;
                    child->name[0] = '\0';
                    child->startArgs[0] = '\0';
                    child->exitCode = 0;
                    signalTable[i] = 0;

                    enableInterrupts();
                    return pid;
                }
            }
        }

        if (!hasChild)
        {
            enableInterrupts();
            return -4;
        }

        runningProcess->status = STATUS_BLOCKED;
        enableInterrupts();
        dispatcher();
    }
}


/**************************************************************************
   Name - k_exit

   Purpose - Exits a process and coordinates with the parent for cleanup
             and return of the exit code.

   Parameters - the code to return to the grieving parent

   Returns - nothing

*************************************************************************/
void k_exit(int code)
{
    if (!runningProcess) return;
    disableInterrupts();
    runningProcess->exitCode = code;
    runningProcess->status = STATUS_QUIT;
    Process* parent = runningProcess->pParent;
    if (parent && parent->status == STATUS_BLOCKED)
    {
        parent->status = STATUS_READY;
        enqueue_ready(parent);
    }

    enableInterrupts();
    dispatcher();
}

/**************************************************************************
   Name - k_kill

   Purpose - Signals a process with the specified signal

   Parameters - Signal to send

   Returns -
*************************************************************************/
int k_kill(int pid, int signal)
{
    disableInterrupts();

    Process* p = get_process_by_pid(pid);
    if (p == NULL)
    {
        enableInterrupts();
        return -1;
    }

    int slot = get_slot_by_process(p);
    if (slot < 0 || slot >= MAX_PROCESSES)
    {
        enableInterrupts();
        return -1;
    }

    signalTable[slot] = signal;

    if (p->status != STATUS_QUIT)
    {
        p->exitCode = -5;
        p->status = STATUS_QUIT;

        if (p->pParent != NULL && p->pParent->status == STATUS_BLOCKED)
        {
            p->pParent->status = STATUS_READY;
            enqueue_ready(p->pParent);
        }
    }

    enableInterrupts();
    return 0;
}

/**************************************************************************
   Name - k_getpid
*************************************************************************/
int k_getpid(void)
{
    if (runningProcess == NULL) return -1;
    return runningProcess->pid;
}

/**************************************************************************
   Name - k_join
***************************************************************************/
int k_join(int pid, int* pChildExitCode)
{
    if (runningProcess == NULL)
    {
        return -4;
    }

    while (1)
    {
        disableInterrupts();

        int found = 0;
        int childSlot = -1;

        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (processTable[i].pid == pid && processTable[i].pParent == runningProcess)
            {
                found = 1;
                childSlot = i;
                break;
            }
        }

        if (!found)
        {
            enableInterrupts();
            return -4;
        }

        if (processTable[childSlot].status == STATUS_QUIT)
        {
            Process* child = &processTable[childSlot];

            if (pChildExitCode != NULL)
            {
                *pChildExitCode = child->exitCode;
            }

            unlink_child(runningProcess, child);

            child->pid = 0;
            child->context = NULL;
            child->nextReadyProcess = NULL;
            child->nextSiblingProcess = NULL;
            child->pParent = NULL;
            child->pChildren = NULL;
            child->status = 0;
            child->priority = 0;
            child->entryPoint = NULL;
            child->stack = NULL;
            child->stacksize = 0;
            child->name[0] = '\0';
            child->startArgs[0] = '\0';
            child->exitCode = 0;
            signalTable[childSlot] = 0;

            enableInterrupts();
            return pid;
        }


        runningProcess->status = STATUS_BLOCKED;

        enableInterrupts();
        dispatcher();

        if (signaled())
        {
            if (pChildExitCode != NULL)
            {
                *pChildExitCode = -5;
            }
            return -5;
        }
    }
}

/**************************************************************************
   Name - unblock
*************************************************************************/
int unblock(int pid)
{
    disableInterrupts();

    Process* p = get_process_by_pid(pid);
    if (p == NULL)
    {
        enableInterrupts();
        return -1;
    }

    if (p->status == STATUS_BLOCKED)
    {
        p->status = STATUS_READY;
        enqueue_ready(p);
    }

    enableInterrupts();
    return 0;
}

/*************************************************************************
   Name - block
*************************************************************************/
int block(int newStatus)
{
    disableInterrupts();

    if (runningProcess == NULL)
    {
        enableInterrupts();
        return -1;
    }

    runningProcess->status = newStatus;

    enableInterrupts();
    dispatcher();

    return 0;
}

/*************************************************************************
   Name - signaled
*************************************************************************/
int signaled(void)
{
    if (runningProcess == NULL) return 0;

    int slot = get_slot_by_process(runningProcess);
    if (slot < 0 || slot >= MAX_PROCESSES) return 0;

    if (signalTable[slot] != 0)
    {
        signalTable[slot] = 0;
        return 1;
    }

    return 0;
}
/*************************************************************************
   Name - readtime
*************************************************************************/
int read_time(void)
{
    return 0;
}

/*************************************************************************
   Name - readClock
*************************************************************************/
DWORD read_clock(void)
{
    return system_clock();
}

void display_process_table(void)
{
    const char* statusStr;
    console_output(FALSE,
        "\n%-7s %-8s %-9s %-12s %-7s %-8s %s\n",
        "PID", "Parent", "Priority", "Status", "# Kids", "CPUtime", "Name");

    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        Process* p = &processTable[i];

        if (p->pid == 0)
            continue;

        int parentPid = (p->pParent != NULL) ? p->pParent->pid : -1;
        switch (p->status)
        {
        case STATUS_READY:
            statusStr = "READY";
            break;
        case STATUS_RUNNING:
            statusStr = "RUNNING";
            break;
        case STATUS_BLOCKED:
            statusStr = "BLOCKED";
            break;
        case STATUS_QUIT:
            statusStr = "QUIT";
            break;
        default:
            statusStr = "UNKNOWN";
            break;
        }

        console_output(FALSE,
            "%-7d %-8d %-9d %-12s %-7d %-8u %s\n",
            p->pid,
            parentPid,
            p->priority,
            statusStr,
            count_children(p),
            p->cpuTime,
            (p->name[0] ? p->name : "(noname)"));
    }
    return;
}

/**************************************************************************
   Name - dispatcher

   Purpose - This is where context changes to the next process to run.

   Parameters - none

   Returns - nothing

*************************************************************************/

void dispatcher(void)
{
    disableInterrupts();

    Process* prev = runningProcess;

    if (prev && prev->status == STATUS_RUNNING)
    {
        // accumulate CPU time
        uint32_t now = read_clock();
        prev->cpuTime += now - prev->lastReadTime;

        prev->status = STATUS_READY;
        enqueue_ready(prev);
    }

    Process* next = dequeue_ready_highest();

    if (!next)
    {
        enableInterrupts();
        return;
    }

    runningProcess = next;
    runningProcess->status = STATUS_RUNNING;
    runningProcess->lastReadTime = read_clock();  // start timing

    enableInterrupts();
    context_switch(next->context);
}

/**************************************************************************
   Name - watchdog

   Purpose - The watchdoog keeps the system going when all other
         processes are blocked.  It can be used to detect when the system
         is shutting down as well as when a deadlock condition arises.

   Parameters - none

   Returns - nothing
   *************************************************************************/
static int watchdog(char* dummy)
{
    DebugConsole("watchdog(): called\n");
    while (1)
    {
        if (!any_non_system_active())
        {
            console_output(FALSE, "watchdog(): no non-system active processes; stopping.\n");
            stop(0);
        }
        check_deadlock();
        dispatcher();
    }
    return 0;
}

/* check to determine if deadlock has occurred... */
static void check_deadlock()
{
}

/*
 * Disables the interrupts.
 */
static inline void disableInterrupts(void)
{

    /* We ARE in kernel mode */


    int psr = get_psr();

    psr = psr & ~PSR_INTERRUPTS;

    set_psr(psr);

} /* disableInterrupts */

static inline void enableInterrupts(void)
{
    int psr = get_psr();
    psr = psr | PSR_INTERRUPTS;
    set_psr(psr);
}

/**************************************************************************
   Name - DebugConsole
   Purpose - Prints  the message to the console_output if in debug mode
   Parameters - format string and va args
   Returns - nothing
   Side Effects -
*************************************************************************/
static void DebugConsole(char* format, ...)
{
    char buffer[2048];
    va_list argptr;

    if (debugFlag)
    {
        va_start(argptr, format);
        vsprintf(buffer, format, argptr);
        console_output(TRUE, buffer);
        va_end(argptr);

    }
}

/*************************************************************************
   Name - Get Start Time
   purpose- returns the start tiume of a process in microseconds
   Parameters -
   Returns -
*************************************************************************/

int get_start_time(void)
{
    if (runningProcess == NULL)
        return 0;
        
    return runningProcess->startTime;
}

/* there is no I/O yet, so return false. */
int check_io_scheduler()
{
    return false;
}

static void clock_handler(char* devicename, uint8_t command, uint32_t status)
{
    time_slice();
}


void time_slice(void)
{
    disableInterrupts();

    if (runningProcess && runningProcess->status == STATUS_RUNNING)
    {
        uint32_t now = read_clock();
        runningProcess->cpuTime += now - runningProcess->lastReadTime;

        runningProcess->status = STATUS_READY;
        enqueue_ready(runningProcess);
    }

    enableInterrupts();
    dispatcher();
}


/* This returns 1(true) if name is "watchdog", if not it returns 0.*/
static int isWatchdogName(const char* name)
{
    return (name != NULL && strcmp(name, "watchdog") == 0);
}

static int find_free_slot(void)
{
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        if (processTable[i].pid == 0)
        {
            return i;
        }
    }
    return -1;
}

static int count_children(const Process* p)
{
    int count = 0;
    for (Process* c = p->pChildren; c != NULL; c = c->nextSiblingProcess) {
        count++;
    }
    return count;
}
