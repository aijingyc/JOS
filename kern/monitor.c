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
	{ "backtrace", "Display information about the backtrace", mon_backtrace },
	{ "showmappings", "Display memory mappings for a range of virtual addresses", mon_showmappings },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

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
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp, eip;
	int i;

	cprintf("Stack backtrace:\n");

	ebp = read_ebp();
	while (ebp) {
		eip = ((uint32_t *) ebp)[1];
		cprintf("  ebp %08x eip %08x ", ebp, eip);
	
		cprintf("args ");
		for (i = 1; i <= 5; i++) {
			cprintf("%08x ", ((uint32_t *) ebp)[1 + i]);
		}
		cprintf("\n");

		struct Eipdebuginfo info;
		if (!debuginfo_eip(eip, &info))
			cprintf("\t%s:%d: %.*s+%d\n", info.eip_file, info.eip_line,
				info.eip_fn_namelen, info.eip_fn_name,
				eip - info.eip_fn_addr);

		ebp = ((uint32_t *) ebp)[0];
	}
	return 0;
}

int
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	int i;
	uintptr_t va_arg[2], va;
	pde_t *pgdir;
	pte_t *pte;

	if (argc != 3) {
		cprintf("usage: showmappings begin_va end_va\n");
		return 0;
	}

	for (i = 1; i < argc; i++) {
		va_arg[i - 1] = (uintptr_t) strtol(argv[i], NULL, 0);
	}

	if (va_arg[0] > va_arg[1]) {
		cprintf("begin va (0x%x) is greater than end va (0x%x)\n", va_arg[0], va_arg[1]);
		return 0;
	}

	pgdir = (pde_t *) KADDR(rcr3());
	for (va = va_arg[0]; va <= va_arg[1]; va += PGSIZE) {
		pte = pgdir_walk(pgdir, (void *) va, 0);
		if (!pte || !(*pte & PTE_P)) {
			cprintf("va 0x%x is not mapped\n", va);
			continue;
		}

		cprintf("va 0x%x: 0x%x PTE_P %d PTE_W %d PTE_U %d\n", va, PTE_ADDR(*pte), *pte & PTE_P, *pte & PTE_W, *pte & PTE_U);
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
