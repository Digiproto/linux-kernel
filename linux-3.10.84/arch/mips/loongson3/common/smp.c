/*
 * Copyright (C) 2010, 2011, 2012, Lemote, Inc.
 * Author: Chen Huacai, chenhc@lemote.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/clock.h>
#include <asm/tlbflush.h>
#include <loongson.h>

#include "smp.h"

DEFINE_PER_CPU(int, cpu_state);
DEFINE_PER_CPU(uint32_t, core0_c0count);

/* read a 64bit value from ipi register */
uint64_t loongson3_ipi_read64(void * addr)
{
	return *((volatile uint64_t *)addr);
};

/* write a 64bit value to ipi register */
void loongson3_ipi_write64(uint64_t action, void * addr)
{
	*((volatile uint64_t *)addr) = action;
	__wbflush();
};

/* read a 32bit value from ipi register */
uint32_t loongson3_ipi_read32(void * addr)
{
	return *((volatile uint32_t *)addr);
};

/* write a 32bit value to ipi register */
void loongson3_ipi_write32(uint32_t action, void * addr)
{
	*((volatile uint32_t *)addr) = action;
	__wbflush();
};

/*
 * Simple enough, just poke the appropriate ipi register
 */
static void loongson3_send_ipi_single(int cpu, unsigned int action)
{
	unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(cpu));
	loongson3_ipi_write32((u32)action, (void *)(base + IPI_OFF_SET));
}

static void loongson3_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;
	for_each_cpu(i, mask){
	unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(i));
		loongson3_ipi_write32((u32)action, (void *)(base + IPI_OFF_SET));
	}
}

#define IPI_IRQ_OFFSET 6

void loongson3_send_irq_by_ipi(int cpu, int irqs)
{
        unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(cpu));
        loongson3_ipi_write32((u32)(irqs << IPI_IRQ_OFFSET), (void *)(base + IPI_OFF_SET));
}

void loongson3_ipi_interrupt(struct pt_regs *regs)
{
	int i, cpu = smp_processor_id();
	unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(cpu));
	unsigned int action, c0count, irqs, irq;

	/* Load the ipi register to figure out what we're supposed to do */
	action = loongson3_ipi_read32((void *)(base + IPI_OFF_STATUS));

	/* Clear the ipi register to clear the interrupt */
	loongson3_ipi_write32((u32)action,(void *)(base + IPI_OFF_CLEAR));

	if (action & SMP_RESCHEDULE_YOURSELF) {
		scheduler_ipi();
	}

	if (action & SMP_CALL_FUNCTION) {
		smp_call_function_interrupt();
	}

	if (action & SMP_ASK_C0COUNT) {
		BUG_ON(cpu != 0);
		c0count = read_c0_count();
		for (i=1; i < nr_cpus_loongson; i++)
			per_cpu(core0_c0count, i) = c0count;
	}

	irqs = action >> IPI_IRQ_OFFSET;
	if (irqs) {
		switch (board_type) {
			case RS780E:
				while ((irq = ffs(irqs))) {
					do_IRQ(irq-1);
					irqs &= ~(1<<(irq-1));
				}
				break;
			case LS2H:
				do_IRQ(irqs);
				break;
			default:
				break;
		}
	}
}

#define MAX_LOOPS 1200
/*
 * SMP init and finish on secondary CPUs
 */
void loongson3_init_secondary(void)
{
	int i;
	uint32_t initcount;
	unsigned int cpu = smp_processor_id();
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 |
			     STATUSF_IP3 | STATUSF_IP2;

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);

	for (i = 0; i < nr_cpus_loongson; i++) {
		unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(i));
		loongson3_ipi_write32(0xffffffff,(void *)(base + IPI_OFF_ENABLE));
	}

	per_cpu(cpu_state, cpu) = CPU_ONLINE;

	i = 0;
	__get_cpu_var(core0_c0count) = 0;
	loongson3_send_ipi_single(0, SMP_ASK_C0COUNT);
	while (!__get_cpu_var(core0_c0count))
		i++;

	if (i > MAX_LOOPS)
		i = MAX_LOOPS;
	initcount = __get_cpu_var(core0_c0count) + i;
	write_c0_count(initcount);
}

void loongson3_smp_finish(void)
{
	int cpu = smp_processor_id();
	unsigned long base = LOONGSON3_TO_BASE(cpu_logical_map(cpu));
	write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);
	local_irq_enable();
	loongson3_ipi_write64(0, (void *)(base + IPI_OFF_MAILBOX0));
	printk("CPU#%d finished, CP0_ST=%x\n",
			smp_processor_id(), read_c0_status());
}

void __init loongson3_smp_setup(void)
{
	int num = 0; /* num: logical id */

	init_cpu_possible(cpu_none_mask);

	/* For unified kernel, NR_CPUS is the maximum possible value,
	 * nr_cpus_loongson is the really present value */
	while (num < nr_cpus_loongson) {
		set_cpu_possible(num, true);
		num++;
	}
	printk("Detected %i available CPU(s)\n", num);
}

void __init loongson3_prepare_cpus(unsigned int max_cpus)
{
	init_cpu_present(cpu_possible_mask);
	per_cpu(cpu_state, smp_processor_id()) = CPU_ONLINE;
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it runing!
 */
void loongson3_boot_secondary(int cpu, struct task_struct *idle)
{
	volatile unsigned long startargs[4];
	unsigned long coreid = cpu_logical_map(cpu);
	unsigned long base = LOONGSON3_TO_BASE(coreid);

	printk("Booting CPU#%d...\n", cpu);

	/* startargs[] are initial PC, SP and GP for secondary CPU */
	startargs[0] = (unsigned long)&smp_bootstrap;
	startargs[1] = (unsigned long)__KSTK_TOS(idle);
	startargs[2] = (unsigned long)task_thread_info(idle);
	startargs[3] = 0;

	printk("CPU#%d, func_pc=%lx, sp=%lx, gp=%lx\n",
			cpu, startargs[0], startargs[1], startargs[2]);

	loongson3_ipi_write64(startargs[3], (void *)(base + IPI_OFF_MAILBOX3));
	loongson3_ipi_write64(startargs[2], (void *)(base + IPI_OFF_MAILBOX2));
	loongson3_ipi_write64(startargs[1], (void *)(base + IPI_OFF_MAILBOX1));
	loongson3_ipi_write64(startargs[0], (void *)(base + IPI_OFF_MAILBOX0));
}

/*
 * Final cleanup after all secondaries booted
 */
void __init loongson3_cpus_done(void)
{
}

#ifdef CONFIG_HOTPLUG_CPU

extern void fixup_irqs(void);
extern void (*flush_cache_all)(void);

static int loongson3_cpu_disable(void)
{
	unsigned long flags;
	unsigned int cpu = smp_processor_id();

	if (cpu == 0)
		return -EBUSY;

	set_cpu_online(cpu, false);
	cpu_clear(cpu, cpu_callin_map);
	local_irq_save(flags);
	fixup_irqs();
	local_irq_restore(flags);
	flush_cache_all();
	local_flush_tlb_all();

	return 0;
}


static void loongson3_cpu_die(unsigned int cpu)
{
	while (per_cpu(cpu_state, cpu) != CPU_DEAD)
		cpu_relax();

	mb();
}

/* To shutdown a core in Loongson 3, the target core should go to CKSEG1 and
 * flush all L1 entries at first. Then, another core (usually Core 0) can
 * safely disable the clock of the target core. loongson3_play_dead() is
 * called via CKSEG1 (uncached and unmmaped) */
void loongson3a_play_dead(int *state_addr)
{
	__asm__ __volatile__(
		"      .set push                         \n"
		"      .set noreorder                    \n"
		"      li $t0, 0x80000000                \n" /* KSEG0 */
		"      li $t1, 512                       \n" /* num of L1 entries */
		"1:    cache 0, 0($t0)                   \n" /* flush L1 ICache */
		"      cache 0, 1($t0)                   \n"
		"      cache 0, 2($t0)                   \n"
		"      cache 0, 3($t0)                   \n"
		"      cache 1, 0($t0)                   \n" /* flush L1 DCache */
		"      cache 1, 1($t0)                   \n"
		"      cache 1, 2($t0)                   \n"
		"      cache 1, 3($t0)                   \n"
		"      addiu $t0, $t0, 0x20              \n"
		"      bnez  $t1, 1b                     \n"
		"      addiu $t1, $t1, -1                \n"
		"      li    $t0, 0x7                    \n" /* *state_addr = CPU_DEAD; */
		"      sw    $t0, 0($a0)                 \n"
		"      sync                              \n"
		"      cache 21, 0($a0)                  \n" /* flush entry of *state_addr */
		"      .set pop                          \n");

	__asm__ __volatile__(
		"      .set push                         \n"
		"      .set noreorder                    \n"
		"      .set mips64                       \n"
		"      mfc0  $t2, $15, 1                 \n"
		"      andi  $t2, 0x3ff                  \n"
		"      dli   $t0, 0x900000003ff01000     \n"
		"      andi  $t3, $t2, 0x3               \n"
		"      sll   $t3, 8                      \n"  /* get cpu id */
		"      or    $t0, $t0, $t3               \n"
		"      andi  $t1, $t2, 0xc               \n"
		"      dsll  $t1, 42                     \n"  /* get node id */
		"      or    $t0, $t0, $t1               \n"
		"1:    li    $a0, 0x100                  \n"  /* wait for init loop */
		"2:    bnez  $a0, 2b                     \n"  /* idle loop */
		"      addiu $a0, -1                     \n"
		"      lw    $v0, 0x20($t0)              \n"  /* get PC via mailbox */
		"      beqz  $v0, 1b                     \n"
		"      nop                               \n"
		"      ld    $sp, 0x28($t0)              \n"  /* get SP via mailbox */
		"      ld    $gp, 0x30($t0)              \n"  /* get GP via mailbox */
		"      ld    $a1, 0x38($t0)              \n"
		"      jr  $v0                           \n"  /* jump to initial PC */
		"      nop                               \n"
		"      .set pop                          \n");
}
void loongson3b_play_dead(int *state_addr)

{
	__asm__ __volatile__(
		"      .set push                         \n"
		"      .set noreorder                    \n"
		"      li $t0, 0x80000000                \n" /* KSEG0 */
		"      li $t1, 512                       \n" /* num of L1 entries */
		"1:    cache 0, 0($t0)                   \n" /* flush L1 ICache */
		"      cache 0, 1($t0)                   \n"
		"      cache 0, 2($t0)                   \n"
		"      cache 0, 3($t0)                   \n"
		"      cache 1, 0($t0)                   \n" /* flush L1 DCache */
		"      cache 1, 1($t0)                   \n"
		"      cache 1, 2($t0)                   \n"
		"      cache 1, 3($t0)                   \n"
		"      addiu $t0, $t0, 0x20              \n"
		"      bnez  $t1, 1b                     \n"
		"      addiu $t1, $t1, -1                \n"
		"      li    $t0, 0x7                    \n" /* *state_addr = CPU_DEAD; */
		"      sw    $t0, 0($a0)                 \n"
		"      sync                              \n"
		"      cache 21, 0($a0)                  \n" /* flush entry of *state_addr */
		"      .set pop                          \n");

	__asm__ __volatile__(
		"      .set push                         \n"
		"      .set noreorder                    \n"
		"      .set mips64                       \n"
		"      mfc0  $t2, $15, 1                 \n"
		"      andi  $t2, 0x3ff                  \n"
		"      dli   $t0, 0x900000003ff01000     \n"
		"      andi  $t3, $t2, 0x3               \n"
		"      sll   $t3, 8                      \n"  /* get cpu id */
		"      or    $t0, $t0, $t3               \n"
		"      andi  $t1, $t2, 0xc               \n"
		"      dsll  $t1, 42                     \n"  /* get node id */
		"      or    $t0, $t0, $t1               \n"
		"      dsrl  $t1, 30                     \n"  /* 15:14 */
		"      or    $t0, $t0, $t1               \n"
		"1:    li    $a0, 0x100                  \n"  /* wait for init loop */
		"2:    bnez  $a0, 2b                     \n"  /* idle loop */
		"      addiu $a0, -1                     \n"
		"      lw    $v0, 0x20($t0)              \n"  /* get PC via mailbox */
		"      beqz  $v0, 1b                     \n"
		"      nop                               \n"
		"      ld    $sp, 0x28($t0)              \n"  /* get SP via mailbox */
		"      ld    $gp, 0x30($t0)              \n"  /* get GP via mailbox */
		"      ld    $a1, 0x38($t0)              \n"
		"      jr  $v0                           \n"  /* jump to initial PC */
		"      nop                               \n"
		"      .set pop                          \n");
}

void loongson3a2000_play_dead(int *state_addr)
{
	__asm__ __volatile__(
		"      .set push                         \n"
		"      .set noreorder                    \n"
		"      li $t0, 0x80000000                \n" /* KSEG0 */
		"      li $t1, 256                       \n" /* num of L1 entries */
		"1:    cache 0, 0($t0)                   \n" /* flush L1 ICache */
		"      cache 0, 1($t0)                   \n"
		"      cache 0, 2($t0)                   \n"
		"      cache 0, 3($t0)                   \n"
		"      cache 1, 0($t0)                   \n" /* flush L1 DCache */
		"      cache 1, 1($t0)                   \n"
		"      cache 1, 2($t0)                   \n"
		"      cache 1, 3($t0)                   \n"
		"      addiu $t0, $t0, 0x40              \n"
		"      bnez  $t1, 1b                     \n"
		"      addiu $t1, $t1, -1                \n"
		"      li $t0, 0x80000000                \n" /* KSEG0 */
		"      li $t1, 256                       \n" /* num of L2 entries */
		"2:    cache 2, 0($t0)                   \n" /* flush L2 VCache */
		"      cache 2, 1($t0)                   \n"
		"      cache 2, 2($t0)                   \n"
		"      cache 2, 3($t0)                   \n"
		"      cache 2, 4($t0)                   \n"
		"      cache 2, 5($t0)                   \n"
		"      cache 2, 6($t0)                   \n"
		"      cache 2, 7($t0)                   \n"
		"      cache 2, 8($t0)                   \n"
		"      cache 2, 9($t0)                   \n"
		"      cache 2, 10($t0)                   \n"
		"      cache 2, 11($t0)                   \n"
		"      cache 2, 12($t0)                   \n"
		"      cache 2, 13($t0)                   \n"
		"      cache 2, 14($t0)                   \n"
		"      cache 2, 15($t0)                   \n"
		"      addiu $t0, $t0, 0x40              \n"
		"      bnez  $t1, 2b                     \n"
		"      addiu $t1, $t1, -1                \n"
		"      li    $t0, 0x7                    \n" /* *state_addr = CPU_DEAD; */
		"      sw    $t0, 0($a0)                 \n"
		"      sync                              \n"
		"      cache 21, 0($a0)                  \n" /* flush entry of *state_addr */
		"      .set pop                          \n");

	__asm__ __volatile__(
		"      .set push                         \n"
		"      .set noreorder                    \n"
		"      .set mips64                       \n"
		"      mfc0  $t2, $15, 1                 \n"
		"      andi  $t2, 0x3ff                  \n"
		"      dli   $t0, 0x900000003ff01000     \n"
		"      andi  $t3, $t2, 0x3               \n"
		"      sll   $t3, 8                      \n"  /* get cpu id */
		"      or    $t0, $t0, $t3               \n"
		"      andi  $t1, $t2, 0xc               \n"
		"      dsll  $t1, 42                     \n"  /* get node id */
		"      or    $t0, $t0, $t1               \n"
		"1:    li    $a0, 0x100                  \n"  /* wait for init loop */
		"2:    bnez  $a0, 2b                     \n"  /* idle loop */
		"      addiu $a0, -1                     \n"
		"      lw    $v0, 0x20($t0)              \n"  /* get PC via mailbox */
		"      beqz  $v0, 1b                     \n"
		"      nop                               \n"
		"      ld    $sp, 0x28($t0)              \n"  /* get SP via mailbox */
		"      ld    $gp, 0x30($t0)              \n"  /* get GP via mailbox */
		"      ld    $a1, 0x38($t0)              \n"
		"      jr  $v0                           \n"  /* jump to initial PC */
		"      nop                               \n"
		"      .set pop                          \n");
}

void play_dead(void)
{
	int *state_addr;
	unsigned int cpu = smp_processor_id();
	void (*play_dead_at_ckseg1)(int *);

	idle_task_exit();
	switch (cputype) {
	case Loongson_3A:
		if ((read_c0_prid() & 0xf) == PRID_REV_LOONGSON3A2000)
			play_dead_at_ckseg1 =
				(void *)CKSEG1ADDR((unsigned long)loongson3a2000_play_dead);
		else
			play_dead_at_ckseg1 =
				(void *)CKSEG1ADDR((unsigned long)loongson3a_play_dead);
		break;
	case Loongson_3B:
		play_dead_at_ckseg1 = (void *)CKSEG1ADDR((unsigned long)loongson3b_play_dead);
		break;
	default:
		play_dead_at_ckseg1 =
			(void *)CKSEG1ADDR((unsigned long)loongson3a_play_dead);
		break;
	}
	state_addr = &per_cpu(cpu_state, cpu);
	mb();
	play_dead_at_ckseg1(state_addr);
}

void loongson3_disable_clock(int cpu)
{
	uint64_t core_id = cpu_data[cpu].core;
	uint64_t package_id = cpu_data[cpu].package;

	if (cputype == Loongson_3A) {
		switch (read_c0_prid() & 0xf) {
			case PRID_REV_LOONGSON3A:
				LOONGSON_CHIPCFG(package_id) &= ~(1 << (12 + core_id));
				break;
			case PRID_REV_LOONGSON3A2000:
				LOONGSON_CHIPCFG(package_id) &= ~(1 << (core_id * 4 + 3));
				break;
		}
	}
	else if (cputype == Loongson_3B) {
		switch (read_c0_prid() & 0xf) {
			case PRID_REV_LOONGSON3B_R1:
			case PRID_REV_LOONGSON3B_R2:
				break;
			default:
				LOONGSON_CHIPCFG(package_id) &= ~(1 << (core_id * 4 + 3));
				break;
		}
	}
}

void loongson3_enable_clock(int cpu)
{
	uint64_t core_id = cpu_data[cpu].core;
	uint64_t package_id = cpu_data[cpu].package;

	if (cputype == Loongson_3A) {
		switch (read_c0_prid() & 0xf) {
			case PRID_REV_LOONGSON3A:
				LOONGSON_CHIPCFG(package_id) |= 1 << (12 + core_id);
				break;
			case PRID_REV_LOONGSON3A2000:
				LOONGSON_CHIPCFG(package_id) |= 1 << (core_id * 4 + 3);
				break;
		}
	}
	else if (cputype == Loongson_3B) {
		switch (read_c0_prid() & 0xf) {
			case PRID_REV_LOONGSON3B_R1:
			case PRID_REV_LOONGSON3B_R2:
				break;
			default:
				LOONGSON_CHIPCFG(package_id) |= 1 << (cpu * 4 + 3);
				break;
		}
	}
}

#define CPU_POST_DEAD_FROZEN	(CPU_POST_DEAD | CPU_TASKS_FROZEN)
static int loongson3_cpu_callback(struct notifier_block *nfb,
	unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_POST_DEAD:
	case CPU_POST_DEAD_FROZEN:
		printk("Disable clock for CPU#%d\n", cpu);
		loongson3_disable_clock(cpu);
		break;
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		printk("Enable clock for CPU#%d\n", cpu);
		loongson3_enable_clock(cpu);
		break;
	}
	return NOTIFY_OK;
}

static int register_loongson3_notifier(void)
{
	hotcpu_notifier(loongson3_cpu_callback, 0);
	return 0;
}
early_initcall(register_loongson3_notifier);

#if     defined(CONFIG_CPU_LOONGSON3)&&defined(CONFIG_SUSPEND)
void __cpuinit disable_unused_cpus(void)
{
	int cpu;
	struct cpumask tmp;

	cpumask_complement(&tmp, cpu_online_mask);
	cpumask_and(&tmp, &tmp, cpu_possible_mask);

	for_each_cpu(cpu, &tmp)
		cpu_up(cpu);

	for_each_cpu(cpu, &tmp)
		cpu_down(cpu);
}
#endif
#endif

struct plat_smp_ops loongson3_smp_ops = {
	.send_ipi_single = loongson3_send_ipi_single,
	.send_ipi_mask = loongson3_send_ipi_mask,
	.init_secondary = loongson3_init_secondary,
	.smp_finish = loongson3_smp_finish,
	.cpus_done = loongson3_cpus_done,
	.boot_secondary = loongson3_boot_secondary,
	.smp_setup = loongson3_smp_setup,
	.prepare_cpus = loongson3_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable = loongson3_cpu_disable,
	.cpu_die = loongson3_cpu_die,
#endif
};
