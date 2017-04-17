// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/pmap.h>
#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "time", "Display the time", mon_time },
	{ "showmappings", "Display the mapping of given range", mon_showmappings },
	{ "setmappings", "Display the mapping of given range", mon_setmappings },
	{ "dumpmem", "Display the memory in [arg2,arg3)\n arg 1(p or v) present physical or virtual addr\n ", mon_dumpmem },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-entry+1023)/1024);
	return 0;
}

// Lab1 only
// read the pointer to the retaddr on the stack
static uint32_t
read_pretaddr() {
    uint32_t pretaddr;
    __asm __volatile("leal 4(%%ebp), %0" : "=r" (pretaddr)); 
    return pretaddr;
}

void
do_overflow(void)
{
    cprintf("Overflow success\n");
}

void
start_overflow(void)
{
	// You should use a techique similar to buffer overflow
	// to invoke the do_overflow function and
	// the procedure must return normally.

    // And you must use the "cprintf" function with %n specifier
    // you augmented in the "Exercise 9" to do this job.

    // hint: You can use the read_pretaddr function to retrieve 
    //       the pointer to the function call return address;

	char str[256] = {};
	int nstr = 0;
	memset(str, 0xff, 255);
	char *pret_addr;
	//xx815:	push   %ebp
	//xx816:	mov    %esp,%ebp
	//xx818:	sub    $0x14,%esp
	//xx81b:	push   $0xf0101cfe
	pret_addr = (char*)read_pretaddr();//this is the return address, which should be modified into addr(do_overflow)+6
	//cprintf("pretaddr = 0x%x\n",(unsigned int)pret_addr);

	// Your code here.
	int addr = (int)(&do_overflow)+6;
	//cprintf("over addr = 0x%x\n",(unsigned int)addr);
	int i = 0;
	for(;i<4;i++){
		nstr = ((uint32_t)addr>>(i*8)) & 0xff;//((int)(addr)>>24)&0xff;
		str[nstr] = 0;
		//cprintf("nstr = 0x%x\n",(unsigned int)nstr);
		cprintf("%s%n", str, pret_addr+i);
		str[nstr] = 0xff;
	}



	//cprintf("final addr = 0x%x\n",(unsigned int)pret_addr);




}

void
overflow_me(void)
{
        start_overflow();
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	//cprintf("mon backtrace!!!!\n");
	uint32_t *ebp;
	
	ebp = (uint32_t *)read_ebp();
	while(ebp!=0){
		uint32_t *eip = ebp + 1;
		cprintf("eip %x ebp %x args %08x %08x %08x %08x %08x\n", *eip, ebp, *(ebp+2), *(ebp+3), *(ebp+4), *(ebp+5), *(ebp+6));
		
		struct Eipdebuginfo info;
		debuginfo_eip(*eip, &info);
		//delete the str after :
		char namestr[50];
		int i = 0;
		for (;(info.eip_fn_name[i]!=':') && (info.eip_fn_name[i]!='\0') && (i<50); i++)
			namestr[i]=info.eip_fn_name[i];
		namestr[i]='\0';

		cprintf("       %s:%d: %s%+d\n", 
			info.eip_file, 
			info.eip_line, 
			namestr, 
			*eip - info.eip_fn_addr);

		ebp = (uint32_t *)(*ebp);
	}


	overflow_me();
	cprintf("Backtrace success\n");
	return 0;
}

int
mon_time(int argc, char **argv, struct Trapframe *tf)
{
	unsigned long long time_s;
	__asm __volatile("rdtsc": "=r" (time_s));
	
	int i;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[1], commands[i].name) == 0)
			commands[i].func(argc, argv, tf);
	}
	
	unsigned long long time_e;
	__asm __volatile("rdtsc": "=r" (time_e));
	
	cprintf("%s cycles: %d\n", argv[1], time_e - time_s);
	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 3){
		cprintf("Error: show mapping need 2 arg\n");
		return 0;
	}
	
	//get addr
	uint32_t left = strtol(argv[1],0,0);
	uint32_t right = strtol(argv[2],0,0);

	left = ROUNDUP(left, PGSIZE);
	right = ROUNDUP(right, PGSIZE);
	
	//print each entry msg
	pte_t *pte;
	for(; left<right; left += PGSIZE){
		pte = pgdir_walk(kern_pgdir, (void *)left, 0);
		cprintf("[0x%8x, 0x%8x)\t",left,left + PGSIZE);
		if(!pte || !((* pte)&PTE_P))
			cprintf("NO-MAP\n");
		else{
			cprintf("0x%x\t",PTE_ADDR(*pte));
			if(*pte & PTE_U)
				cprintf("user\t");
			else
				cprintf("kernel\t");
			if(*pte & PTE_W)
				cprintf("rw\n");
			else
				cprintf("r\n");
		}
	}
	return 0;
}

int
mon_setmappings(int argc, char **argv, struct Trapframe *tf)
{
	if(argc != 5){
		cprintf("Error: setmappings need 5 args\n");
		return 0;
	}
	uint32_t va = strtol(argv[1],0,0);
	uint32_t size = strtol(argv[2],0,0)*PGSIZE;
	uint32_t pa = strtol(argv[3],0,0);

	va = ROUNDUP(va, PGSIZE);
	pa = ROUNDUP(pa, PGSIZE);
	uint32_t offset = 0;

	uint32_t perm = 0;
	if(argv[4][0] == 'u'){
		perm |= PTE_U;
	}
	if(argv[4][1] == 'w'){
		perm |= PTE_W;
	}

	for(; offset<size;offset+=PGSIZE){
		struct Page *pg=pa2page(pa+offset);
		if(pg->pp_ref==0){
			//cprintf("Error: free page: 0x%x\n",pa+offset);
			//return 0;
		}
	}
	for(offset = 0; offset<size;offset+=PGSIZE){
		page_insert(kern_pgdir, pa2page(pa+offset),(void *)va+offset,perm);
	}
	
	//mem_showmappings(va,va+size);
	return 0;
}

int
mon_dumpmem(int argc, char **argv, struct Trapframe *tf)
{
	if(argc !=4){
		cprintf("Error: dumpmem need 4 args\n");
		return 0;
	}

	uint32_t left = strtol(argv[2],0,0);
	uint32_t right = strtol(argv[3],0,0);

	left = ROUNDUP(left,4);
	right = ROUNDUP(right,4);

	if(argv[1][0]=='p'){
		left += KERNBASE;
		right += KERNBASE;
	}else if(argv[1][0]!='v'){
		cprintf("Error: dumpmem arg1 != p or v\n");
		return 0;
	}
	
	uint32_t tmp = left; 
	
	if(left>right){
		cprintf("the 1st arg should be less than 2nd arg\n");
		return 0;
	}

	while(tmp<right){
		cprintf("0x%x: ",tmp);
		cprintf("0x%8x\t",*((uint32_t *)tmp));
		cprintf("0x%8x\t",*((uint32_t *)tmp+4));
		cprintf("0x%8x\t",*((uint32_t *)tmp+8));
		cprintf("0x%8x\t",*((uint32_t *)tmp+12));
		cprintf("\n");
		tmp+=4;
	}
	
	return 0;
}
/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");


	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
