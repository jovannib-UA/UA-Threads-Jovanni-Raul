/*
    CYBV 489
<<<<<<< HEAD
    Group 10: Raul Cano & Jovanni Blanco
    Professor: Li Xu
    Last Update: 1/29/2026
=======
    Group 10: Jake Newton & Jose Aguilar
    Professor: Li Xu
    Last Update: 2/12/2026
>>>>>>> upstream/main
*/

#define _CRT_SECURE_NO_WARNINGS
#define STATUS_READY    1
#define STATUS_RUNNING  2
#define STATUS_BLOCKED  3
#define STATUS_QUIT     4

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
<<<<<<< HEAD
=======
static int count_children(const Process* p);
static int find_free_slot(void);
void time_slice(void);
>>>>>>> upstream/main

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
<<<<<<< HEAD
    }
=======
        processTable[i].priority = 0;
        processTable[i].entryPoint = NULL;
        processTable[i].stack = NULL;
        processTable[i].stacksize = 0;
        processTable[i].name[0] = '\0';
        processTable[i].startArgs[0] = '\0';
    }

    runningProcess = NULL;
    nextPid = 1;
>>>>>>> upstream/main

    runningProcess = NULL;
    nextPid = 1;
    /* Initialize the Ready list, etc. */
    for (int p = 0; p <= HIGHEST_PRIORITY; p++)
    {
        readyQ[p] = NULL;
    }
<<<<<<< HEAD
    /* Initialize the clock interrupt handler */
    intVector = get_interrupt_handlers();
    intVector[THREADS_TIMER_INTERRUPT] = clock_handler;
=======

    /* Initialize the clock interrupt handler */
    intVector = get_interrupt_handlers();
    intVector[THREADS_TIMER_INTERRUPT] = clock_handler;

>>>>>>> upstream/main
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

    {
        int slot = 0;
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (processTable[i].pid != 0 && strcmp(processTable[i].name, "Scheduler") == 0)
            {
                slot = i;
                break;
            }
        }

        if (slot != 0)
        {
            Process* saved = runningProcess;
            runningProcess = &processTable[slot];
            runningProcess->status = STATUS_RUNNING;

            SchedulerEntryPoint(NULL);

            runningProcess = saved;
        }
    }

    /* Initialized and ready to go!! */
    console_output(debugFlag, "All processes completed.\n");
    // not a real process, wont return any debug flags

<<<<<<< HEAD
=======
    stop(0);
>>>>>>> upstream/main
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

<<<<<<< HEAD
    if (!(priority < 0 || priority > 5)) //checks if priority is between 0 and 5 
    {
        pNewProc->priority = &priority; //assign address of priority variable to pNewProc priority field
    }
    else
    {
        return -3; //if priority is not between 0 and 5 return -3
    }


    pNewProc->status = "Ready...";
    pNewProc->startArgs[0] = &arg;

    /* Find an empty slot in the process table */
=======
    if (priority < LOWEST_PRIORITY || priority > HIGHEST_PRIORITY)
    {
        console_output(debugFlag, "spawn(): Priority out of range.\n");
        enableInterrupts();
        return -1;
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
        return -4;
    }
>>>>>>> upstream/main

    pNewProc = &processTable[proc_slot];

    /* Setup the entry in the process table. */
    strcpy(pNewProc->name, name);
<<<<<<< HEAD

    pNewProc->pid = gChildPid = nextPid++; //generate a new PID and set pNewProc and gChildPid to it
    pNewProc->entryPoint = entryPoint; //assign entry point with new address

=======
    pNewProc->pid = nextPid++;
    if (priority > 5 || priority < 0) {
        console_output(debugFlag, "spawn(): Priority out of range\n");
        return -3;
    }
    pNewProc->priority = priority;
    pNewProc->status = STATUS_READY;
    pNewProc->entryPoint = entryPoint;
    pNewProc->stacksize = (unsigned int)stacksize;

    if (arg != NULL)
    {
        strncpy(pNewProc->startArgs, (char*)arg, MAXARG - 1);
        pNewProc->startArgs[MAXARG - 1] = '\0';
    }
    else
    {
        pNewProc->startArgs[0] = '\0';
    }
>>>>>>> upstream/main

    /* If there is a parent process,add this to the list of children. */
    if (runningProcess != NULL)
    {
        pNewProc->pParent = runningProcess;
    }

    /* Add the process to the ready list. */

    /* Initialize context for this process, but use launch function pointer for
     * the initial value of the process's program counter (PC)
    */
<<<<<<< HEAD

    pNewProc->context = context_initialize(launch, stacksize, arg);

    if (!isWatchdogName(name)) //checks if watchdog process is being created
    {
        Process* psave = runningProcess; // saves running process in psave variable
        runningProcess = pNewProc; //points to pNewProc treating the new process as current running process
        entryPoint(arg); //calls run process
        runningProcess = psave; //after function completes, runningProcess points to psave variable
=======
    pNewProc->context = context_initialize(launch, stacksize, arg);

    if (!isWatchdogName(name) && strcmp(name, "Scheduler") != 0)
    {
        gChildPid = pNewProc->pid;
>>>>>>> upstream/main
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
    Process* p = (Process*)args;

    DebugConsole("launch(): started: %s\n", runningProcess->name);

    /* Enable interrupts */

    /* Call the function passed to spawn and capture its return value */
    DebugConsole("Process %d returned to launch\n", runningProcess->pid);

    /* Stop the process gracefully */
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
<<<<<<< HEAD
    while (!gChildExited) //busy wait loop for created child process
=======
    if (gChildPid < 0)
>>>>>>> upstream/main
    {
        return -1;
    }

<<<<<<< HEAD
    if (code != NULL) //if the process is Null, store exit code
=======
    if (!gChildExited)
    {
        Process* child = NULL;
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            if (processTable[i].pid == gChildPid)
            {
                child = &processTable[i];
                break;
            }
        }

        if (child != NULL)
        {
            Process* saved = runningProcess;

            gChildExited = 0;
            gChildExitCode = 0;

            runningProcess = child;
            runningProcess->status = STATUS_RUNNING;

            int rc = 0;
            if (runningProcess->entryPoint != NULL)
            {
                rc = runningProcess->entryPoint((void*)runningProcess->startArgs);
            }

            if (!gChildExited)
            {

                k_exit(rc);
            }

            runningProcess = saved;
        }
        else
        {
            gChildExitCode = -3;
            gChildExited = 1;
        }
    }

    if (code != NULL)
>>>>>>> upstream/main
    {
        *code = gChildExitCode;
    }

<<<<<<< HEAD
    return gChildPid; //return PID of terminated child process
=======
    return gChildPid;
>>>>>>> upstream/main
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
<<<<<<< HEAD
    gChildExitCode = code; //returns exit code value on exit
    gChildExited = 1; //indicates completion

=======
    gChildExitCode = code;
    gChildExited = 1;
>>>>>>> upstream/main
}

/**************************************************************************
   Name - k_kill

   Purpose - Signals a process with the specified signal

   Parameters - Signal to send

   Returns -
*************************************************************************/
int k_kill(int pid, int signal)
{
    int result = 0;
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
    return 0;
}

/**************************************************************************
   Name - unblock
*************************************************************************/
int unblock(int pid)
{
    return 0;
}

/*************************************************************************
   Name - block
*************************************************************************/
int block(int newStatus)
{
    return 0;
}

/*************************************************************************
   Name - signaled
*************************************************************************/
int signaled(void)
{
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
        switch (p->status) {
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
            0,
            (p->name[0] ? p->name : "(noname)")
        );
    }

    console_output(FALSE, "\n");
}

/**************************************************************************
   Name - dispatcher

   Purpose - This is where context changes to the next process to run.

   Parameters - none

   Returns - nothing

*************************************************************************/

void dispatcher(void)
{
<<<<<<< HEAD
    Process* nextProcess = NULL;
    disableInterrupts();

    if (nextProcess == NULL)
    {
        enableInterrupts();
        return;
    }
    if (runningProcess != NULL && runningProcess->status == STATUS_RUNNING)
    {
        runningProcess->status = STATUS_READY;
        ready_enqueue(runningProcess);
    }

    runningProcess = nextProcess;
    runningProcess->status = STATUS_RUNNING;

    enableInterrupts();

    /* IMPORTANT: context switch enables interrupts. */
=======
    return;

    Process* nextProcess = NULL;
    disableInterrupts();

    if (nextProcess == NULL)
    {
        enableInterrupts();
        return;
    }

    runningProcess = nextProcess;
    runningProcess->status = STATUS_RUNNING;

    enableInterrupts();

 /* IMPORTANT: context switch enables interrupts. */
>>>>>>> upstream/main
    context_switch(nextProcess->context);
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
        check_deadlock();
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


/* there is no I/O yet, so return false. */
int check_io_scheduler()
{
    return false;
}

static void clock_handler(char* devicename, uint8_t command, uint32_t status)
{
    time_slice();
}

<<<<<<< HEAD
=======
void time_slice(void)
{
}

>>>>>>> upstream/main
/* This returns 1(true) if name is "watchdog", if not it returns 0.*/
static int isWatchdogName(const char* name)
{
    return (name != NULL && strcmp(name, "watchdog") == 0);
}

static int find_free_slot(void)
{
    for (int i = 1; i < MAX_PROCESSES; i++)
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