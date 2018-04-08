/*
 * Copyright (c) 2000-2008 Apple Inc. All rights reserved.
 *
 * @APPLE_OSREFERENCE_LICENSE_HEADER_START@
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. The rights granted to you under the License
 * may not be used to create, or enable the creation or redistribution of,
 * unlawful or unlicensed copies of an Apple operating system, or to
 * circumvent, violate, or enable the circumvention or violation of, any
 * terms of an Apple operating system software license agreement.
 * 
 * Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_OSREFERENCE_LICENSE_HEADER_END@
 */
/*
 * @OSF_COPYRIGHT@
 */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

/*
 */
#ifdef	KERNEL_PRIVATE

#ifndef _I386_MP_H_
#define _I386_MP_H_

#ifndef	DEBUG
#include <debug.h>
#endif
//#define	MP_DEBUG 1

#include <i386/apic.h>
#include <i386/mp_events.h>

#define MAX_CPUS	32		/* (8*sizeof(long)) */	

#ifndef	ASSEMBLER
#include <stdint.h>
#include <sys/cdefs.h>
#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/i386/thread_status.h>
#include <kern/lock.h>

__BEGIN_DECLS

extern kern_return_t intel_startCPU(int slot_num);
extern kern_return_t intel_startCPU_fast(int slot_num);
extern void i386_init_slave(void);
extern void i386_init_slave_fast(void);
extern void smp_init(void);

extern void cpu_interrupt(int cpu);
__END_DECLS

extern	unsigned int	real_ncpus;		/* real number of cpus */
extern	unsigned int	max_ncpus;		/* max number of cpus */
decl_simple_lock_data(extern,kdb_lock)	/* kdb lock		*/

__BEGIN_DECLS

extern  void	console_init(void);
extern	void	*console_cpu_alloc(boolean_t boot_cpu);
extern	void	console_cpu_free(void *console_buf);

extern	int	kdb_cpu;		/* current cpu running kdb	*/
extern	int	kdb_debug;
extern	int	kdb_active[];

extern	volatile boolean_t mp_kdp_trap;
extern  volatile boolean_t force_immediate_debugger_NMI;
extern  volatile boolean_t pmap_tlb_flush_timeout;

extern	void	mp_kdp_enter(void);
extern	void	mp_kdp_exit(void);

#if MACH_KDB
extern void mp_kdb_exit(void);
#endif

/*
 * All cpu rendezvous:
 */
extern void mp_rendezvous(
		void (*setup_func)(void *),
		void (*action_func)(void *),
		void (*teardown_func)(void *),
		void *arg);
extern void mp_rendezvous_no_intrs(
		void (*action_func)(void *),
		void *arg);
extern void mp_rendezvous_break_lock(void);

/*
 * All cpu broadcast.
 * Called from thread context, this blocks until all active cpus have
 * run action_func:
 */
extern void mp_broadcast(
		void (*action_func)(void *),
		void *arg);

typedef uint32_t cpu_t;
typedef uint32_t cpumask_t;
static inline cpumask_t
cpu_to_cpumask(cpu_t cpu)
{
	return (cpu < 32) ? (1 << cpu) : 0;
}
#define CPUMASK_ALL	0xffffffff
#define CPUMASK_SELF	cpu_to_cpumask(cpu_number())
#define CPUMASK_OTHERS	(CPUMASK_ALL & ~CPUMASK_SELF)

/*
 * Invoke a function (possibly NULL) on a set of cpus specified by a mask.
 * The mask may include the local cpu.
 * If the mode is:
 *	- ASYNC: other cpus make their calls in parallel.
 * 	- SYNC: the calls are performed serially in logical cpu order.
 * This call returns when the function has been run on all specified cpus.
 * The return value is the number of cpus on which the call was made.
 * The action function is called with interrupts disabled.
 */
extern cpu_t mp_cpus_call(
		cpumask_t	cpus,
		mp_sync_t	mode,
		void		(*action_func)(void *),
		void		*arg);

__END_DECLS

#if MP_DEBUG
typedef struct {
	uint64_t	time;
	int		cpu;
	mp_event_t	event;
} cpu_signal_event_t;

#define	LOG_NENTRIES	100
typedef struct {
	uint64_t		count[MP_LAST];
	int			next_entry;
	cpu_signal_event_t	entry[LOG_NENTRIES];
} cpu_signal_event_log_t;

extern cpu_signal_event_log_t	*cpu_signal[];
extern cpu_signal_event_log_t	*cpu_handle[];

#define DBGLOG(log,_cpu,_event) {					\
	boolean_t		spl = ml_set_interrupts_enabled(FALSE);	\
	cpu_signal_event_log_t	*logp = log[cpu_number()];		\
	int			next = logp->next_entry;		\
	cpu_signal_event_t	*eventp = &logp->entry[next];		\
									\
	logp->count[_event]++;						\
									\
	eventp->time = rdtsc64();					\
	eventp->cpu = _cpu;						\
	eventp->event = _event;						\
	if (next == (LOG_NENTRIES - 1))					\
		logp->next_entry = 0;					\
	else								\
		logp->next_entry++;					\
									\
	(void) ml_set_interrupts_enabled(spl);				\
}

#define DBGLOG_CPU_INIT(cpu)	{					\
	cpu_signal_event_log_t	**sig_logpp = &cpu_signal[cpu];		\
	cpu_signal_event_log_t	**hdl_logpp = &cpu_handle[cpu];		\
									\
	if (*sig_logpp == NULL &&					\
		kmem_alloc(kernel_map,					\
			(vm_offset_t *) sig_logpp,			\
			sizeof(cpu_signal_event_log_t)) != KERN_SUCCESS)\
		panic("DBGLOG_CPU_INIT cpu_signal allocation failed\n");\
	bzero(*sig_logpp, sizeof(cpu_signal_event_log_t));		\
	if (*hdl_logpp == NULL &&					\
		kmem_alloc(kernel_map,					\
			(vm_offset_t *) hdl_logpp,			\
			sizeof(cpu_signal_event_log_t)) != KERN_SUCCESS)\
		panic("DBGLOG_CPU_INIT cpu_handle allocation failed\n");\
	bzero(*sig_logpp, sizeof(cpu_signal_event_log_t));		\
}
#else	/* MP_DEBUG */
#define DBGLOG(log,_cpu,_event)
#define DBGLOG_CPU_INIT(cpu)
#endif	/* MP_DEBUG */

#endif	/* ASSEMBLER */

#define i_bit(bit, word)	((long)(*(word)) & ((long)1 << (bit)))


/* 
 *	Device driver synchronization. 
 *
 *	at386_io_lock(op) and at386_io_unlock() are called
 *	by device drivers when accessing H/W. The underlying 
 *	Processing is machine dependant. But the op argument
 *	to the at386_io_lock is generic
 */

#define MP_DEV_OP_MAX	  4
#define MP_DEV_WAIT	  MP_DEV_OP_MAX	/* Wait for the lock */

/*
 * If the caller specifies an op value different than MP_DEV_WAIT, the
 * at386_io_lock function must return true if lock was successful else
 * false
 */

#define MP_DEV_OP_START 0	/* If lock busy, register a pending start op */
#define MP_DEV_OP_INTR	1	/* If lock busy, register a pending intr */
#define MP_DEV_OP_TIMEO	2	/* If lock busy, register a pending timeout */
#define MP_DEV_OP_CALLB	3	/* If lock busy, register a pending callback */

#if	MACH_RT
#define _DISABLE_PREEMPTION 					\
	incl	%gs:CPU_PREEMPTION_LEVEL

#define _ENABLE_PREEMPTION 					\
	decl	%gs:CPU_PREEMPTION_LEVEL		;	\
	jne	9f					;	\
	pushl	%eax					;	\
	pushl	%ecx					;	\
	pushl	%edx					;	\
	call	EXT(kernel_preempt_check)		;	\
	popl	%edx					;	\
	popl	%ecx					;	\
	popl	%eax					;	\
9:	

#define _ENABLE_PREEMPTION_NO_CHECK				\
	decl	%gs:CPU_PREEMPTION_LEVEL

#if	MACH_ASSERT
#define DISABLE_PREEMPTION					\
	pushl	%eax;						\
	pushl	%ecx;						\
	pushl	%edx;						\
	call	EXT(_disable_preemption);			\
	popl	%edx;						\
	popl	%ecx;						\
	popl	%eax
#define ENABLE_PREEMPTION					\
	pushl	%eax;						\
	pushl	%ecx;						\
	pushl	%edx;						\
	call	EXT(_enable_preemption);			\
	popl	%edx;						\
	popl	%ecx;						\
	popl	%eax
#define ENABLE_PREEMPTION_NO_CHECK				\
	pushl	%eax;						\
	pushl	%ecx;						\
	pushl	%edx;						\
	call	EXT(_enable_preemption_no_check);		\
	popl	%edx;						\
	popl	%ecx;						\
	popl	%eax
#define MP_DISABLE_PREEMPTION					\
	pushl	%eax;						\
	pushl	%ecx;						\
	pushl	%edx;						\
	call	EXT(_mp_disable_preemption);			\
	popl	%edx;						\
	popl	%ecx;						\
	popl	%eax
#define MP_ENABLE_PREEMPTION					\
	pushl	%eax;						\
	pushl	%ecx;						\
	pushl	%edx;						\
	call	EXT(_mp_enable_preemption);			\
	popl	%edx;						\
	popl	%ecx;						\
	popl	%eax
#define MP_ENABLE_PREEMPTION_NO_CHECK				\
	pushl	%eax;						\
	pushl	%ecx;						\
	pushl	%edx;						\
	call	EXT(_mp_enable_preemption_no_check);		\
	popl	%edx;						\
	popl	%ecx;						\
	popl	%eax
#else	/* MACH_ASSERT */
#define DISABLE_PREEMPTION		_DISABLE_PREEMPTION
#define ENABLE_PREEMPTION		_ENABLE_PREEMPTION
#define ENABLE_PREEMPTION_NO_CHECK	_ENABLE_PREEMPTION_NO_CHECK
#define MP_DISABLE_PREEMPTION		_DISABLE_PREEMPTION
#define MP_ENABLE_PREEMPTION		_ENABLE_PREEMPTION
#define MP_ENABLE_PREEMPTION_NO_CHECK 	_ENABLE_PREEMPTION_NO_CHECK
#endif	/* MACH_ASSERT */

#else	/* MACH_RT */
#define DISABLE_PREEMPTION
#define ENABLE_PREEMPTION
#define ENABLE_PREEMPTION_NO_CHECK
#define MP_DISABLE_PREEMPTION
#define MP_ENABLE_PREEMPTION
#define MP_ENABLE_PREEMPTION_NO_CHECK
#endif	/* MACH_RT */

#endif /* _I386_MP_H_ */

#endif /* KERNEL_PRIVATE */
