#include "syscall.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "proc.h"
#include "vm.h" //added 

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d va = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}
 
uint64 sys_gettimeofday(TimeVal *val, int _tz) // TODO: implement sys_gettimeofday in pagetable. (VA to PA)
{
	struct proc *p = curr_proc();
	uint64 pa = useraddr(p->pagetable, (uint64)val);
	if (pa == 0)
		return -1;
	TimeVal *kval = (TimeVal *)pa;
	uint64 cycle = get_cycle();
	kval->sec = cycle / CPU_FREQ;
	kval->usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd)
{
	// Validate: start must be page-aligned
	if (start % PAGE_SIZE != 0)
		return -1;
	// len == 0: return immediately
	if (len == 0)
		return 0;
	// len upper limit 1 GiB
	if (len > 1024ULL * 1024 * 1024)
		return -1;
	// port: other bits must be 0
	if ((port & ~0x7) != 0)
		return -1;
	// port: must have at least one permission
	if ((port & 0x7) == 0)
		return -1;

	struct proc *p = curr_proc();

	// Round len up to page boundary
	uint64 end = start + ((len + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

	// Check that no page in [start, end) is already mapped
	for (uint64 va = start; va < end; va += PAGE_SIZE) {
		if (walkaddr(p->pagetable, va) != 0)
			return -1;
	}
	int perm = PTE_U | PTE_V;

	if (port & 0x1) perm |= PTE_R;
	if (port & 0x2) perm |= PTE_W;
	if (port & 0x4) perm |= PTE_X;

	// map pages
	for (uint64 va = start; va < end; va += PAGE_SIZE) {
		void *pa = kalloc();
		if (pa == 0)
			return -1;

		memset(pa, 0, PAGE_SIZE);

		if (mappages(p->pagetable, va, PAGE_SIZE, (uint64)pa, perm) != 0) {
			kfree(pa);
			return -1;
		}
	}

	return 0;
}

uint64 sys_munmap(uint64 start, uint64 len)
{
	// Validate: start must be page-aligned
	if (start % PAGE_SIZE != 0)
		return -1;
	if (len == 0)
		return 0;

	struct proc *p = curr_proc();

	// Round len up to page boundary
	uint64 end = start + ((len + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

	// Check that all pages in [start, end) are currently mapped
	for (uint64 va = start; va < end; va += PAGE_SIZE) {
		if (walkaddr(p->pagetable, va) == 0)
			return -1;
	}

	// Unmap page by page
	for (uint64 va = start; va < end; va += PAGE_SIZE) {
		uvmunmap(p->pagetable, va, 1, 1);
	}
	return 0;
}


/*
* LAB1: you may need to define sys_task_info here
*/
uint64 sys_getpid()
{
	return curr_proc()->pid; // Returns PID 
}
uint64 sys_task_info(TaskInfo *ti)
{
	struct proc *p = curr_proc(); //operate on current running process
	uint64 pa = useraddr(p->pagetable, (uint64)ti);
	if (pa == 0)
		return -1;
	TaskInfo *kti = (TaskInfo *)pa;
	// Set status to running 
	kti->status = 2; 
	
	// copys syscall counters 
	for (int i = 0; i < MAX_SYSCALL_NUM; i++) { 
		kti->syscall_times[i] = p->syscall_times[i];
	}

	// computes rutime (ms) 
	uint64 now = get_cycle();
	kti->time = (now - p->start_time) * 1000 / CPU_FREQ; // ms conversion 

	return 0;
}
extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	struct proc *p = curr_proc(); // Pointer to process that made syscall 
	if (id >= 0 && id < MAX_SYSCALL_NUM) { // validating syscall #
		p->syscall_times[id]++; // Tracks # of calls 
	}

	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday((TimeVal *)args[0], args[1]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_getpid: // Calls getpid kernel function 
		ret = sys_getpid(); 
		break; 
	case SYS_task_info: // Calls kernel function sys_task_info 
		ret = sys_task_info((TaskInfo *)args[0]);
		break;
	case SYS_mmap: // Calls kernel function sys_mmap
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap: // Calls kernel function sys_munmap
		ret = sys_munmap(args[0], args[1]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
