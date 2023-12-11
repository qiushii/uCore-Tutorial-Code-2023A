#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"

extern pagetable_t kernel_pagetable;

uint64 getPages(uint64 len)
{
	uint64 result = len, multiple = 1;
	if (len % PAGE_SIZE == 0) {
        result = len;
        multiple = len / PAGE_SIZE;
    } else {
        result = (len / PAGE_SIZE + 1) * PAGE_SIZE;
        multiple = result / PAGE_SIZE;
    }
	return multiple;
}

uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
int mmap(void* start, unsigned long long len, int port, int flag, int fd)
{
	if(((port & 0x7) == 0) || ((port & 0xF8) != 0) || (len <= 0))
		return -1;

	if (!PGALIGNED((uint64)start)) {	
		return -1;
	}
	uint64 _R = (port & 1L) == 0 ? 0x00 : PTE_R;
	uint64 _W = (port & (1L<<1)) == 0 ? 0x00 : PTE_W;
	uint64 _X = (port & (1L<<2)) == 0 ? 0x00 : PTE_X;

	int perm =  _R | _W | _X | PTE_U | PTE_V;
	uint64 a = (uint64)start;
	int page_num = getPages(len);
	//printf("*%d*:[%x], (%d = %dk), %x\n", curr_proc()->pid, start, len, page_num, perm);	//for debug
	for(int p = 0; p<page_num; p++)
	{
		uint64 kstart = (uint64)kalloc();
		//printf("*%d*:%x=>%x\n", curr_proc()->pid, a, kstart);			//for debug
		if(mappages(curr_proc()->pagetable, a, PAGE_SIZE, kstart, perm) != 0)
			return -1;

		curr_proc()->max_page++;
		//printf("[mmap]%d: %d\n", curr_proc()->pid, curr_proc()->max_page); 	//for debug
		a += PAGE_SIZE;
	}
	 return 0;
}

int munmap(void* start, unsigned long long len)
{
	uint64 a = (uint64)start;
	int page_num = getPages(len);
	//printf("[munmap]%d:[%x], %d\n", curr_proc()->pid, start, len); 	//for debug
	for(int p = 0; p<page_num; p++)
	{
		if(useraddr(curr_proc()->pagetable, a) == 0)
			return -1;
		if ((a % PGSIZE) != 0)
			return -1;
			
		uvmunmap(curr_proc()->pagetable, a, 1, 1);
		
		curr_proc()->max_page--;
		//printf("[munmap]%d: %d\n", curr_proc()->pid, curr_proc()->max_page); 	//for debug
		a += PAGE_SIZE;
	}
	return 0;
}

/*
* LAB1: you may need to define sys_task_info here
*/
int sys_task_info(TaskInfo *ti)
{
	if(curr_proc()->pid > NPROC || curr_proc()->pid <= 0)
		return -1;

	uint64 address = useraddr(curr_proc()->pagetable, (uint64)ti);
	TaskInfo* k_ti = (TaskInfo*)address;

	k_ti->status = Running;

	uint64 cycle = get_cycle();
	uint64 sec = cycle / CPU_FREQ;
	uint64 usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	int now = (sec * 1000 + usec / 1000);
	k_ti->time = now - curr_proc()->time;

	for(int index = 0; index < MAX_SYSCALL_NUM; index++){
		k_ti->syscall_times[index] = curr_proc()->syscall_times[index];
	}
	
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	// TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	char *strName = (char*)useraddr(p->pagetable, (uint64)va);
	struct proc *np = allocproc();
	if (np == NULL) {
		return -1;
	}
	init_stdio(np);
	np->parent = curr_proc();

	add_task(np);
	bin_loader(namei(strName), np);
	//printf("[debug]:spwan [%d]%s\n", np->pid, strName);	//for debug
	return np->pid;
	return 0;
}

uint64 sys_set_priority(long long prio){
    if(prio <= 1 || !curr_proc())
		return -1;

	curr_proc()->prio = prio;

	return prio;
}


uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_fstat(int fd, uint64 stat)
{
	if(fd < 3 || fd >= FD_BUFFER_SIZE)
		return -1;

	struct Stat* _stat = (struct Stat*)useraddr(curr_proc()->pagetable, (uint64)stat);
	if(_stat == NULL)
		return -1;
	
	struct file* _file = curr_proc()->files[fd];
	if(_file == NULL)return-1;

	struct inode* _inode = _file->ip;
	if(_inode == NULL)return -1;

	_stat->dev = 0;
	_stat->ino = _inode->inum;
	_stat->nlink = _inode->nlink;

	_stat->mode = getdinodeType(_inode) == T_FILE ? FILE : DIR;

	return 0;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath,
	       uint64 flags)
{
	char _oldpath[200], _newpath[200];
	copyinstr(curr_proc()->pagetable, _oldpath, oldpath, 200);
	copyinstr(curr_proc()->pagetable, _newpath, newpath, 200);

	//检查是否存在同名文件
	struct inode* tinode = namei(_newpath);
	if(tinode != 0 && getdinodeType(tinode) == T_FILE)
	{
		printf("sys_linkat:%s is exist\n", _newpath);
		iput(tinode);
		return -1;
	}
	
	//链接
	struct inode* ioldpath = namei(_oldpath);
	if(ioldpath == 0)
	{
		printf("sys_linkat:%s error\n", oldpath);
		return -1;
	}
	if(dirlink(root_dir(), _newpath, ioldpath->inum) != 0)
	{
		iput(ioldpath);
		printf("sys_linkat:dirlink error\n");
		return -1;
	}
	
	//更新
	ioldpath->nlink++;
	iupdate(ioldpath);
	
	return 0;
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags)
{
	char _name[200];
	copyinstr(curr_proc()->pagetable, _name, name, 200);
	return dirunlink(root_dir(), _name);
}

uint64 sys_sbrk(int n)
{
	uint64 addr;
	struct proc *p = curr_proc();
	addr = p->program_brk;
	if (growproc(n) < 0)
		return -1;
	return addr;
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
		
	curr_proc()->syscall_times[id]++;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_task_info:
		ret = sys_task_info((TaskInfo*)args[0]);
		break;
	case SYS_mmap:
		ret = mmap((void*)args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = munmap((void*)args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	case SYS_fstat:
		ret = sys_fstat(args[0], args[1]);
		break;
	case SYS_linkat:
		ret = sys_linkat(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_unlinkat:
		ret = sys_unlinkat(args[0], args[1], args[2]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	case SYS_sbrk:
		ret = sys_sbrk(args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
