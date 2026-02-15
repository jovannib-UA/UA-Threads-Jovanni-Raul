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
static void scheduler_tick(char* devicename, uint8_t command, uint32_t status);
static int isWatchdogName(const char* name);
static Process* readyQ[HIGHEST_PRIORITY + 1];

static int loc_free_slot(void);
static int child_counter(const Process* p);
void time_slice(void);

static int signalTable[MAX_PROCESSES];

static Process* get_process_by_pid(int pid)
{
    Process* p = processTable;
    Process* end = processTable + MAX_PROCESSES;

    while (p < end)
    {
        if (p->pid == pid)
            return p;
        p++;
    }

    return NULL;
}


static int get_slot_by_process(Process* p)
{
    if (!p) 
    {
        return -1;
    }

    return (int)(p - processTable);
}


static void enqueue_ready(Process* p)
{
    if (!p)
        return;

    int pr = p->priority;

    if (pr < LOWEST_PRIORITY)
        pr = LOWEST_PRIORITY;
    else if (pr > HIGHEST_PRIORITY)
        pr = HIGHEST_PRIORITY;

    p->nextReadyProcess = NULL;

    Process** head = &readyQ[pr];

    if (*head == NULL) {
        *head = p;
        return;
    }

    Process* tail = *head;
    while (tail->nextReadyProcess) {
        tail = tail->nextReadyProcess;
    }

    tail->nextReadyProcess = p;
}


static Process* dequeue_ready_highest(void)
{
    for (int pr = HIGHEST_PRIORITY; pr >= LOWEST_PRIORITY; --pr)
    {
        Process** queue = &readyQ[pr];

        if (*queue)
        {
            Process* p = *queue;
            *queue = p->nextReadyProcess;
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
        Process* p = &processTable[i];

        if (p->pid == 0)
            continue;

        if (strcmp(p->name, "watchdog") == 0 || strcmp(p->name, "Scheduler") == 0)
            continue;

        if (p->status == STATUS_READY ||
            p->status == STATUS_RUNNING ||
            p->status == STATUS_BLOCKED)
        {
            return 1;
        }
    }

    return 0;
}


static void unlink_child(Process* parent, Process* child)
{
    if (!parent || !child)
        return;

    Process** current = &parent->pChildren;

    while (*current)
    {
        if (*current == child)
        {
            *current = child->nextSiblingProcess;
            return;
        }
        current = &(*current)->nextSiblingProcess;
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
    int result; //value returned by spawn()

    check_io = check_io_scheduler; //set to scheduler version to check I/O function

    for (int i = 0; i < MAX_PROCESSES; i++) // initialize process table and signal table
    {
        Process* p = &processTable[i];

        p->pid = 0;
        p->context = NULL;
        p->nextReadyProcess = NULL;
        p->nextSiblingProcess = NULL;
        p->pParent = NULL;
        p->pChildren = NULL;
        p->status = 0;
        p->priority = 0;
        p->entryPoint = NULL;
        p->stack = NULL;
        p->stacksize = 0;
        p->name[0] = '\0';
        p->startArgs[0] = '\0';
        p->exitCode = 0;
        p->startTime = 0;
        p->cpuTime = 0;
        p->lastStartTime = 0;
        p->started = 0;

        signalTable[i] = 0;
    }

    processTable[1].cpuTime = 0; //reset cpu time for process 1

    runningProcess = NULL;
    nextPid = 1;

    for (int pr = 0; pr <= HIGHEST_PRIORITY; pr++) //initialize ready queues
        readyQ[pr] = NULL;

    intVector = get_interrupt_handlers(); //setup clock interrupt handler
    intVector[THREADS_TIMER_INTERRUPT] = scheduler_tick;

    result = k_spawn("watchdog", watchdog, NULL, THREADS_MIN_STACK_SIZE, LOWEST_PRIORITY); //spawns watchdog process
    if (result < 0)
    {
        console_output(debugFlag,
            "Scheduler(): spawn for watchdog returned an error (%d), stopping...\n",
            result);
        stop(1);
    }

    result = k_spawn("Scheduler", SchedulerEntryPoint, NULL, 2 * THREADS_MIN_STACK_SIZE, HIGHEST_PRIORITY); //spawns schedulerEntryPoint
    if (result < 0)
    {
        console_output(debugFlag,
            "Scheduler(): spawn for SchedulerEntryPoint returned an error (%d), stopping...\n",
            result);
        stop(1);
    }

    dispatcher(); //starts dipsatcher

    stop(0); //scheduler initialized, running and stops bootstrap
    return 0; //doesnt actually reach out
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
    Process* pNewProc;

    DebugConsole("spawn(): creating process %s\n", name);

    disableInterrupts();

    if (!name) //validates the parameters
    {
        console_output(debugFlag, "spawn(): Name value is NULL.\n");
        enableInterrupts();
        return -1;
    }

    if (strlen(name) >= MAXNAME - 1)
    {
        console_output(debugFlag, "spawn(): Process name is too long. Halting...\n");
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

    proc_slot = loc_free_slot();
    if (proc_slot < 0)
    {
        enableInterrupts();
        return -1;
    }

    pNewProc = &processTable[proc_slot];

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

    if (arg)
    {
        strncpy(pNewProc->startArgs, (char*)arg, MAXARG - 1);
        pNewProc->startArgs[MAXARG - 1] = '\0';
    }
    else
    {
        pNewProc->startArgs[0] = '\0';
    }

    if (runningProcess) //link to parent process
    {
        pNewProc->pParent = runningProcess;

        Process** link = &runningProcess->pChildren;
        while (*link)
        {
            link = &(*link)->nextSiblingProcess;
        }
        *link = pNewProc;
    }

    pNewProc->context = context_initialize(launch, stacksize, pNewProc);

    enqueue_ready(pNewProc); //add to READY queue

    if (!isWatchdogName(name) && strcmp(name, "Scheduler") != 0) //tracks child PID for processes not related to watchdog and scheduler
    {
        gChildPid = pNewProc->pid;
    }

    enableInterrupts();
    return pNewProc->pid;
}

/**************************************************************************
   Name - launch

   Purpose - Utility function that makes sure the environment is ready,
             such as enabling interrupts, for the new process.

   Parameters - none

   Returns - nothing
*************************************************************************/
static int launch(void* args)
{
    if (runningProcess) //markl process as running, rewcord start time if first load
    {
        runningProcess->status = STATUS_RUNNING;

        if (!runningProcess->started)
        {
            runningProcess->startTime = read_clock();
            runningProcess->started = 1;
        }
    }

    DebugConsole("launch(): started: %s\n", runningProcess->name);

    enableInterrupts(); //enable interrupts for regular operations

    //call process entry point
    int rc = 0; 
    if (runningProcess->entryPoint)
    {
        rc = runningProcess->entryPoint(
            (runningProcess->startArgs[0] != '\0') ? (void*)runningProcess->startArgs : NULL);
    }

    DebugConsole("Process %d returned to launch\n", runningProcess->pid);

    //terminate process, return code
    k_exit(rc);

    return 0; // doesnt reach, used for compiler
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
    if (!runningProcess)
        return -4;

    while (1)
    {
        disableInterrupts();
        int hasChild = 0;

        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            Process* child = &processTable[i];

            if (child->pid == 0 || child->pParent != runningProcess)
                continue;

            hasChild = 1;

            if (child->status == STATUS_QUIT)
            {
                int pid = child->pid;

                //if requested, return exit code
                if (code)
                    *code = child->exitCode;

                //unlinks child from parent process
                unlink_child(runningProcess, child);

                //resets child process table
                child->pid = 0;
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
                child->cpuTime = 0;
                child->startTime = 0;
                child->started = 0;
                signalTable[i] = 0;

                enableInterrupts();
                return pid;
            }
        }

        if (!hasChild)
        {
            enableInterrupts();
            return -4;
        }

        //blocks running process until child exits
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
    if (!runningProcess)
        return;

    disableInterrupts();

    //mark current process as quit, storer exit code.
    runningProcess->exitCode = code;
    runningProcess->status = STATUS_QUIT;

    //if the parent becomes blocked, unblock the process and add to ready queue
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

    //find process
    Process* p = get_process_by_pid(pid);
    if (!p)
    {
        enableInterrupts();
        return -1;
    }

    //validate process
    int slot = get_slot_by_process(p);
    if (slot < 0 || slot >= MAX_PROCESSES)
    {
        enableInterrupts();
        return -1;
    }

    //set signal in the signal table
    signalTable[slot] = signal;

    //terminate process if active
    if (p->status != STATUS_QUIT)
    {
        p->exitCode = -5;
        p->status = STATUS_QUIT;

        //unblock parent if waiting
        Process* parent = p->pParent;
        if (parent && parent->status == STATUS_BLOCKED)
        {
            parent->status = STATUS_READY;
            enqueue_ready(parent);
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
    if (!runningProcess)
        return -4;

    while (1)
    {
        disableInterrupts();

        //with PID, find child process
        int childSlot = -1;
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
            Process* child = &processTable[i];
            if (child->pid == pid && child->pParent == runningProcess)
            {
                childSlot = i;
                break;
            }
        }

        //return error if child not found
        if (childSlot < 0)
        {
            enableInterrupts();
            return -4;
        }

        Process* child = &processTable[childSlot];

        //if child quits, exit code and cleanup
        if (child->status == STATUS_QUIT)
        {
            if (pChildExitCode)
                *pChildExitCode = child->exitCode;

            unlink_child(runningProcess, child);

            //reset child process table
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

        //block running process until child quits
        runningProcess->status = STATUS_BLOCKED;

        enableInterrupts();
        dispatcher();

        //check if running process recieves signal while waiting
        if (signaled())
        {
            if (pChildExitCode)
                *pChildExitCode = -5;

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
    if (runningProcess == NULL) return 0;

    uint32_t now = system_clock();

    uint32_t cpu_us = runningProcess->cpuTime;

    if (runningProcess->status == STATUS_RUNNING && runningProcess->lastStartTime != 0)
        cpu_us += (now - runningProcess->lastStartTime);

    return (int)(cpu_us / 1000u);
}

/*************************************************************************
   Name - readClock
*************************************************************************/
uint32_t read_clock(void)
{
    return system_clock();
}

void display_process_table(void)
{
    console_output(FALSE, "\n%-7s %-8s %-9s %-12s %-7s %-8s %s\n","PID", "Parent", "Priority", "Status", "# Kids", "CPUtime", "Name");

    uint32_t now = system_clock();

    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        Process* p = &processTable[i];

        //pass over empty slots
        if (p->pid == 0)
            continue;

        //calculate cpu time in microseconds, include current time slice
        uint32_t cpu_us = p->cpuTime;
        if (p->status == STATUS_RUNNING && p->lastStartTime != 0)
        {
            cpu_us += (now - p->lastStartTime);
        }

        uint32_t cpu_ms = cpu_us / 1000u;

        //parent ID determination
        int parentPid = (p->pParent) ? p->pParent->pid : -1;

        //tie status to string
        const char* statusStr;
        switch (p->status)
        {
        case STATUS_READY:   statusStr = "READY"; break;
        case STATUS_RUNNING: statusStr = "RUNNING"; break;
        case STATUS_BLOCKED: statusStr = "WAIT BLOCK"; break;
        case STATUS_QUIT:    statusStr = "QUIT"; break;
        default:             statusStr = "UNKNOWN"; break;
        }

            //print processes info
            console_output(FALSE,
                "%-7d %-8d %-9d %-12s %-7d %-8u %s\n",
                p->pid,
                parentPid,
                p->priority,
                statusStr,
                child_counter(p),
                cpu_ms,
                (p->name[0] ? p->name : "(noname)"));
    }
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

    uint32_t now = read_clock();
    Process* prev = runningProcess;

    if (prev && prev->status == STATUS_RUNNING)
    {
        prev->cpuTime += (now - prev->lastStartTime);
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

    if (!runningProcess->started)
    {
        runningProcess->startTime = now;
        runningProcess->started = 1;
    }

    runningProcess->lastStartTime = now;
    runningProcess->status = STATUS_RUNNING;

    enableInterrupts();

    if (runningProcess->context == NULL)
    {
        console_output(TRUE, "FATAL: NULL context for pid %d\n",
            runningProcess->pid);
        stop(1);
    }

    context_switch(runningProcess->context);
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
        //checks for any active processes not related to system
        if (!any_non_system_active())
        {
            console_output(debugFlag, "All processes completed.\n");
            stop(0);
        }

        //perform deadlock handling and detection
        check_deadlock();

        dispatcher();
    }

    return 0;
}


//check if deadlock is occured
static void check_deadlock()
{

}

//disable cpu interrupts
static inline void disableInterrupts(void)
{
    int psr = get_psr();
    psr &= ~PSR_INTERRUPTS;  //clear interrupt flag
    set_psr(psr);
}

//enable cpu interrupts
static inline void enableInterrupts(void)
{
    int psr = get_psr();
    psr |= PSR_INTERRUPTS;   //set flag
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
    if (!debugFlag)
        return;

    char buffer[2048];
    va_list argptr;

    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);

    console_output(TRUE, buffer);
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

//returns fale, no I/O
int check_io_scheduler()
{
    return false;
}

static void scheduler_tick(char* devicename, uint8_t command, uint32_t status)
{
    time_slice();
}


void time_slice(void)
{
    disableInterrupts();

    if (runningProcess && runningProcess->status == STATUS_RUNNING)
    {
        uint32_t now = read_clock();
        runningProcess->cpuTime += (now - runningProcess->lastStartTime);
        runningProcess->status = STATUS_READY;
        enqueue_ready(runningProcess);
    }

    enableInterrupts();
    dispatcher();
}

//returns 1 if true, 0 if not
static int isWatchdogName(const char* name)
{
    return (name != NULL && strcmp(name, "watchdog") == 0);
}

static int loc_free_slot(void)
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

static int child_counter(const Process* p)
{
    int count = 0;

    for (Process* child = p->pChildren; child != NULL; child = child->nextSiblingProcess)
    {
        count++;
    }

    return count;
}
