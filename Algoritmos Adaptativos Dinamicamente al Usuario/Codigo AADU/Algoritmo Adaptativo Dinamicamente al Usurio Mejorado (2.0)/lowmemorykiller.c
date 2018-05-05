/* drivers/misc/lowmemorykiller.c
 *
 * The lowmemorykiller driver lets user-space specify a set of memory thresholds
 * where processes with a range of oom_score_adj values will get killed. Specify
 * the minimum oom_score_adj values in
 * /sys/module/lowmemorykiller/parameters/adj and the number of free pages in
 * /sys/module/lowmemorykiller/parameters/minfree. Both files take a comma
 * separated list of numbers in ascending order.
 *
 * For example, write "0,8" to /sys/module/lowmemorykiller/parameters/adj and
 * "1024,4096" to /sys/module/lowmemorykiller/parameters/minfree to kill
 * processes with a oom_score_adj value of 8 or higher when the free memory
 * drops below 4096 pages and kill processes with a oom_score_adj value of 0 or
 * higher when the free memory drops below 1024 pages.
 *
 * The driver considers memory used for caches to be free, but if a large
 * percentage of the cached memory is locked this can be very inaccurate
 * and processes may not get killed until the normal oom killer is triggered.
 *
 * Copyright (C) 2007-2008 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/oom.h>
#include <linux/sched.h>
#include <linux/rcupdate.h>
#include <linux/notifier.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/shrinker.h>

#define NUM_OF_PROCESS 100	/* That limit is never reached */
#define X_KILL_PROCESSES 3
#define ORDER_SIZE 0
#define ORDER_OOM 1
#define NO_ORDER 2
#define NO_PRINT 0
#define PRINT 1
#define TIME_INIT_ADAPT 45
#define LVL1 1024	/* 4MB */
#define LVL2 2048	/* 8MB */
#define LVL3 6144	/* 24MB */
#define LVL4 8192	/* 32MB */
#define LVL5 12288	/* 48MB */
#define LVL6 16384	/* 64MB */
#define LVL7 3072	/* 12MB */
#define LVL8 6144	/* 24MB */
#define LVL9 10240	/* 40MB */
#define LVL10 15360	/* 60MB */

#ifdef CONFIG_HIGHMEM
#define _ZONE ZONE_HIGHMEM
#else
#define _ZONE ZONE_NORMAL
#endif

/* Algorithm active if adaptive_LMK = 1. We can change the value of this
 * variable from outside the kernel.
 */
static int adaptive_LMK = 1;

/* Reclaim pages patch active if pages_patch = 1. We can change the value of
 * this variable from outside the kernel.
 */
static int pages_patch = 1;

/* 1=Extreme Ligth 2=Very Light; 3=Light; 4=Medium; 5=Aggressive;
 * 6=Very Aggressive; 7=Extreme Aggresive
 */
static int minfree_config = 4;
static int last_minfree_config = 4;

/* Generic configurations */
/* 1MB, 2MB, 3MB, 6MB, 10MB, 15MB */
static int extreme_light_minfree[6] = {LVL1 / 4, LVL2 / 4, LVL7 / 4, LVL8 / 4,
				LVL9 / 4, LVL10 / 4};
/* 2MB, 4MB, 6MB, 12MB, 20MB, 30MB */
static int very_light_minfree[6] = {LVL1 / 2, LVL2 / 2, LVL7 / 2, LVL8 / 2,
				LVL9 / 2, LVL10 / 2};
/* 4MB, 8MB, 12MB, 24MB, 40MB, 60MB */
static int light_minfree[6] = {LVL1, LVL2, LVL7, LVL8, LVL9, LVL10};
/* 4MB, 8MB, 16MB, 32MB, 48MB, 64MB */
static int medium_minfree[6] = {LVL1, LVL2, LVL3, LVL4, LVL5, LVL6};
/* 8MB, 16MB, 32MB, 64MB, 96MB, 128MB */
static int aggressive_minfree[6] = {LVL1 * 2, LVL2 * 2, LVL3 * 2, LVL4 * 2,
				LVL5 * 2, LVL6 * 2};
/* 16MB, 32MB, 64MB, 128MB, 192MB, 256MB */
static int very_aggressive_minfree[6] = {LVL1 * 4, LVL2 * 4, LVL3 * 4, LVL4 * 4,
					LVL5 * 4, LVL6 * 4};
/* 32MB, 64MB, 128MB, 256MB, 384MB, 512MB */
static int extreme_aggressive_minfree[6] = {LVL1 * 8, LVL2 * 8, LVL3 * 8,
					LVL4 * 8, LVL5 * 8, LVL6 * 8};

/* Algorithm params */
static struct timeval time_kill_X_processes;
static struct timeval time_no_kill_processes;
static int new_processes_no_kill;
static int running_processes = -1;
static long size_big_foreground_process;

/* Algorithm threshold */
static struct timeval min_time_kill_X_processes = { 1, 0 };
static struct timeval max_time_no_kill_processes = { 1200, 0 };
static struct timeval max_time_fail_measure = { 600, 0 };
static int max_new_processes_no_kill = 10;
static int max_running_processes = 27;
static long max_size_big_foreground_process = 150000;
static long max_ms_without_use_adapt_lmk = 500000;
static long min_ms_without_use_adapt_lmk = 250000;
static long ms_without_use_adapt_lmk = 300000;

/* Aux arrays */
static struct task_struct *tasks[NUM_OF_PROCESS];
static long size_of_process[NUM_OF_PROCESS];
static short oom_of_process[NUM_OF_PROCESS];
static long size_foreground_processes[NUM_OF_PROCESS];

/* Aux variables */
static int fail_measure;
static int running_processes_last_kill = -1;
static struct timeval time_last_lmk_use_1;
static struct timeval time_last_lmk_use_2 = { -1, 0 };
static struct timeval time_first_kill = { -1, 0 };
static struct timeval time_measure_no_kill = { -1, 0 };
static struct timeval time_init_adapt_lmk_1 = { -1, 0 };
static struct timeval time_init_adapt_lmk_2;
static struct timeval time_last_kill = { -1, 0 };
static struct timeval time_init_configuration = { -1, 0 };
static struct timeval time_use_adapt_lmk_1;
static struct timeval time_use_adapt_lmk_2 = { -1, 0 };

static int uses_no_config;
static int limit_uses_no_config = 10;
static long test_lmk_count;
static long test_running_count;
static uint32_t lmk_count;
static uint32_t lmk_count_configuration;
static int kill;

/* If order_flag=0, processes are sorted by size. If order_flag=1,
 * processes are sorted by oom_score_adj. If order_flag=2 do
 * a new process list but not show anything.
 */
static int order_flag;

static uint32_t lowmem_debug_level = 1;

static short lowmem_adj[6] = {
	0,
	1,
	6,
	12,
};
static int lowmem_adj_size = 4;
static int lowmem_minfree[6] = {
	3 * 512,	/* 6MB */
	2 * 1024,	/* 8MB */
	4 * 1024,	/* 16MB */
	16 * 1024,	/* 64MB */
};
static int lowmem_minfree_size = 4;
static int lmk_fast_run = 1;

static unsigned long lowmem_deathpending_timeout;

#define lowmem_print(level, x...)			\
	do {						\
		if (lowmem_debug_level >= (level))	\
			pr_info(x);			\
	} while (0)

/* This function adjust the seven configurations. To do this, it take the defect
 * configuration of our device, defines this as the medium configuration,
 * and adjust the other configurations from these values.
 */
static void adapt_configurations(void)
{
	int i;

	for (i = 0; i < 6; i++) {
		medium_minfree[i] = lowmem_minfree[i];
		extreme_aggressive_minfree[i] = medium_minfree[i] * 4;
		very_aggressive_minfree[i] = medium_minfree[i] * 3;
		aggressive_minfree[i] = medium_minfree[i] * 2;
		light_minfree[i] = (medium_minfree[i] * 3) / 4;
		very_light_minfree[i] = (medium_minfree[i] * 3) / 5;
		extreme_light_minfree[i] = medium_minfree[i] / 2;
	}
	lowmem_print(1, "Configuration adapted\n");
}

/* This function allows you to switch between different minfree configurations
 * through the parameter minfree_config.
 */
static void configure_minfrees(int minfree_config)
{
	int i = 0;
	do_gettimeofday(&time_init_configuration);
	lmk_count_configuration = 0;
	lmk_count = 0;
	lowmem_print(1, "New configuration: %d\n",
			minfree_config);
	switch (minfree_config) {
	case 1:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++)
			lowmem_minfree[i] = extreme_light_minfree[i];
		break;
	case 2:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++)
			lowmem_minfree[i] = very_light_minfree[i];
		break;
	case 3:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++)
			lowmem_minfree[i] = light_minfree[i];
		break;
	case 4:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++)
			lowmem_minfree[i] = medium_minfree[i];
		break;
	case 5:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++)
			lowmem_minfree[i] = aggressive_minfree[i];
		break;
	case 6:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++)
			lowmem_minfree[i] = very_aggressive_minfree[i];
		break;
	case 7:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++)
			lowmem_minfree[i] = extreme_aggressive_minfree[i];
		break;
	default:
		break;
	}
}

/* This function clean a long array putting 0 in all its values. */
static void clean_array_long (long array[], int num_elem)
{
	int i;
	for (i = 0; i < num_elem; i++)
		array[i] = 0;
}

/* This function clean a short array putting 0 in all its values. */
static void clean_array_short(short array[], int num_elem)
{
	int i;
	for (i = 0; i < num_elem; i++)
		array[i] = 0;
}

/* This function clean a task_struct array putting NULL in all its values. */
static void clean_array_tasks(struct task_struct *array[], int num_elem)
{
	int i;
	for (i = 0; i < num_elem; i++)
		array[i] = NULL;
}

/* This function gets the size of each of the tasks of an array task_struct. */
static void get_processes_size(long array_sizes[],
		struct task_struct *array_processes[], int num_elem)
{
	int i;
	int size;

	for (i = 0; (i < num_elem) && (array_processes[i] != NULL); i++) {
		size = get_mm_rss(array_processes[i]->mm);
		array_sizes[i] = (size)*(long)(PAGE_SIZE / 1024);
	}
}

/* This function sort a task_struct array by size from largest to smallest. */
static void process_size_sort(long array_sizes[],
		struct task_struct *array_processes[], int num_elem)
{
	int i, j;
	long temp1;
	struct task_struct *temp2;

	for (i = 1; i < num_elem; i++) {
		for (j = 0; (j < num_elem - 1) && (array_processes[j] != NULL);
				j++) {
			if (array_sizes[j] < array_sizes[j + 1]) {
				temp1 = array_sizes[j];
				temp2 = array_processes[j];

				array_sizes[j] = array_sizes[j + 1];
				array_processes[j] = array_processes[j + 1];

				array_sizes[j + 1] = temp1;
				array_processes[j + 1] = temp2;
			}
		}
	}
}

/* This function gets the oom of each of the tasks of an array task_struct. */
static void get_processes_oom(short array_oom[],
		struct task_struct *array_processes[], int num_elem)
{
	int i;
	for (i = 0; (i < num_elem) && (array_processes[i] != NULL); i++)
		array_oom[i] = array_processes[i]->signal->oom_score_adj;
}

/* This function sort a task_struct array by oom from largest to smallest. */
static void process_oom_sort(short array_oom[],
		struct task_struct *array_processes[], int num_elem)
{
	int i, j;
	short temp1;
	struct task_struct *temp2;

	for (i = 1; i < num_elem; i++) {
		for (j = 0; (j < num_elem - 1) && (array_processes[j] != NULL);
				j++) {
			if (array_oom[j] < array_oom[j + 1]) {
				temp1 = array_oom[j];
				temp2 = array_processes[j];

				array_oom[j] = array_oom[j + 1];
				array_processes[j] = array_processes[j + 1];

				array_oom[j + 1] = temp1;
				array_processes[j + 1] = temp2;
			}
		}
	}
}

/* The lowmemorykiller uses the TIF_MEMDIE flag to help ensure it doesn't
 * kill another task until the memory from the previously killed task has
 * been returned to the system.
 *
 * However the lowmemorykiller does not currently look at tasks who do not
 * have a tasks->mm, but just because a process doesn't have a tasks->mm
 * does not mean that the task's memory has been fully returned to the
 * system yet.
 *
 * In order to prevent the lowmemorykiller from unnecessarily killing
 * multiple applications in a row the lowmemorykiller has been changed to
 * ensure that previous killed tasks are no longer in the process list
 * before attempting to kill another task.
 */
static int test_task_flag(struct task_struct *p, int flag)
{
	struct task_struct *t = p;

	do {
		task_lock(t);
		if (test_tsk_thread_flag(t, flag)) {
			task_unlock(t);
			return 1;
		}
		task_unlock(t);
	} while_each_thread(p, t);

	return 0;
}

static DEFINE_MUTEX(scan_mutex);

/* Function that show active services ordered by size from largest to smallest.
 * To do this, it obtains the tasks with positive size and negative oom.
 */
static void show_services_list(void)
{
	int tasksize;
	int sop_pos = 0;
	int k = 0;
	struct task_struct *tsk;

	if (mutex_lock_interruptible(&scan_mutex) < 0)
			return;

	rcu_read_lock();
	clean_array_long(size_of_process, NUM_OF_PROCESS);
	clean_array_tasks(tasks, NUM_OF_PROCESS);

	for_each_process(tsk) {
		struct task_struct *p;
		short oom_score_adj;

		if (tsk->flags & PF_KTHREAD)
			continue;

		/* if task no longer has any memory ignore it */
		if (test_task_flag(tsk, TIF_MM_RELEASED))
			continue;

		if (time_before_eq(jiffies, lowmem_deathpending_timeout)) {
			if (test_task_flag(tsk, TIF_MEMDIE)) {
				rcu_read_unlock();
				/* give the system time to free up the memory */
				msleep_interruptible(20);
				mutex_unlock(&scan_mutex);
				return;
			}
		}

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		tasksize = get_mm_rss(p->mm);
		oom_score_adj = p->signal->oom_score_adj;

		if ((tasksize > 0) && (oom_score_adj < 0)) {
			tasks[sop_pos] = p;
			sop_pos++;
			if (sop_pos >= NUM_OF_PROCESS) {
				lowmem_print(1, "Limit of services\n");
				sop_pos = 0;
			}
		}
		if (oom_score_adj >= 0) {
			task_unlock(p);
			continue;
		}
		task_unlock(p);
		if (tasksize <= 0)
			continue;
	}

	get_processes_size(size_of_process, tasks, NUM_OF_PROCESS);
	process_size_sort(size_of_process, tasks, NUM_OF_PROCESS);

	lowmem_print(1, "LIST OF ACTIVES SERVICES\n");

	for (k = 0; (k < (NUM_OF_PROCESS)) && (tasks[k] != NULL); k++) {
		lowmem_print(1, "Service %d '%s': size(%ldkB), pid(%d), "
			"oom_score_adj(%d)\n",
			k, tasks[k]->comm, size_of_process[k], tasks[k]->pid,
			tasks[k]->signal->oom_score_adj);
	}

	rcu_read_unlock();
	mutex_unlock(&scan_mutex);
}

/* Function that show active processes. To do this, it obtains the tasks with
 * positive size and oom >= 0. Also, it has two parameters that allow us to
 * choose how sort tasks, by size or by oom, and if we want to display them
 * using printks or not.
 */
static void show_process_list(int order, int print)
{
	int aux_count_processes = 0;
	long aux_count = 0;
	int tasksize;
	int sop_pos = 0;
	int k = 0;
	struct task_struct *tsk;

	if (mutex_lock_interruptible(&scan_mutex) < 0)
			return;

	rcu_read_lock();
	clean_array_long(size_of_process, NUM_OF_PROCESS);
	clean_array_long(size_foreground_processes, NUM_OF_PROCESS);
	clean_array_short(oom_of_process, NUM_OF_PROCESS);
	clean_array_tasks(tasks, NUM_OF_PROCESS);

	for_each_process(tsk) {
		struct task_struct *p;
		short oom_score_adj;

		if (tsk->flags & PF_KTHREAD)
			continue;

		/* if task no longer has any memory ignore it */
		if (test_task_flag(tsk, TIF_MM_RELEASED))
			continue;

		if (time_before_eq(jiffies, lowmem_deathpending_timeout)) {
			if (test_task_flag(tsk, TIF_MEMDIE)) {
				rcu_read_unlock();
				/* give the system time to free up the memory */
				msleep_interruptible(20);
				mutex_unlock(&scan_mutex);
				return;
			}
		}

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		tasksize = get_mm_rss(p->mm);
		oom_score_adj = p->signal->oom_score_adj;

		if ((tasksize > 0) && (oom_score_adj >= 0)) {
			tasks[sop_pos] = p;
			aux_count_processes = sop_pos;
			sop_pos++;
			if (sop_pos >= NUM_OF_PROCESS) {
				lowmem_print(1, "Limit of processes\n");
				sop_pos = 0;
			}
		}

		if (oom_score_adj < 0) {
			task_unlock(p);
			continue;
		}
		task_unlock(p);
		if (tasksize <= 0)
			continue;
	}

	running_processes = aux_count_processes;
	test_running_count = running_processes;
	if (running_processes_last_kill == -1)
		running_processes_last_kill = running_processes;

	if (order == 0) {
		get_processes_size(size_of_process, tasks, NUM_OF_PROCESS);
		process_size_sort(size_of_process, tasks, NUM_OF_PROCESS);

		if (print == 1) {
			lowmem_print(1, "List of active processes\n");

			for (k = 0; (k < (NUM_OF_PROCESS)) &&
				(tasks[k] != NULL); k++) {
				lowmem_print(1, "Process %d '%s': size(%ldkB), "
					"pid(%d), oom_score_adj(%d)\n",
					k, tasks[k]->comm, size_of_process[k],
					tasks[k]->pid,
					tasks[k]->signal->oom_score_adj);
			}

		}
	} else if (order == 1) {
		get_processes_oom(oom_of_process, tasks, NUM_OF_PROCESS);
		process_oom_sort(oom_of_process, tasks, NUM_OF_PROCESS);

		aux_count = 0;
		for (k = 0; (k < (NUM_OF_PROCESS)) && (tasks[k] != NULL); k++) {
			if (oom_of_process[k] == 0) {
				size_foreground_processes[aux_count] =
					(get_mm_rss(tasks[k]->mm)) *
					(long)(PAGE_SIZE / 1024);
				aux_count++;
			}
		}

		if (print == 1) {
			lowmem_print(1, "List of active processes\n");

			for (k = 0; (k < (NUM_OF_PROCESS)) &&
				(tasks[k] != NULL); k++) {
				lowmem_print(1, "Process %d '%s': "
					"oom_score_adj(%d), size(%ldkB), "
					"pid(%d)\n",
					k, tasks[k]->comm, oom_of_process[k],
					(get_mm_rss(tasks[k]->mm)) *
					(long)(PAGE_SIZE / 1024),
					tasks[k]->pid);
			}
		}
	}
	rcu_read_unlock();
	mutex_unlock(&scan_mutex);
}

/* Get the size of the big process within the array of foreground processes.
 * It is important to execute the function show_processes_list(..) first to
 * update the array of foreground processes.
 */
static long get_size_big_foreground_process(void)
{
	int k;
	long final_size = 0;

	for (k = 0; (size_foreground_processes[k] != 0); k++) {
		if (size_foreground_processes[k] > final_size)
			final_size = size_foreground_processes[k];
	}

	return final_size;
}

/* Get the number of active processes. It is important to execute the function
 * show_processes_list(..) first to update the number of running processes.
 */
static int get_running_processes(void)
{
	int final_processes = 0;

	final_processes = running_processes;

	return final_processes;
}

/* Get the time that the LMK takes to kill X_processes. Until it kills
 * X_processes it returns a negative time value.
 */
static struct timeval get_time_kill_X_processes(int X_processes)
{
	struct timeval result = {-1, 0};

	int microseconds = (time_last_kill.tv_sec -
		time_first_kill.tv_sec) * 1000000 +
		((int)time_last_kill.tv_usec -
		(int)time_first_kill.tv_usec);
	int seconds = microseconds/1000000;
	microseconds = microseconds%1000000;

	if ((seconds >  min_time_kill_X_processes.tv_sec) && (lmk_count > 0))
		lmk_count = 0;

	if ((lmk_count >= X_processes) && (time_first_kill.tv_sec >= 0)) {
		result.tv_sec = seconds;
		result.tv_usec = microseconds;
	}

	return result;
}

/* Get the time without the LMK has killed any process. */
static struct timeval get_time_no_kill_processes(void)
{
	struct timeval result = {-1, 0};

	struct timeval time_now;
	do_gettimeofday(&time_now);

	if (fail_measure == 0) {
		int microseconds = (time_now.tv_sec -
			time_measure_no_kill.tv_sec) * 1000000 +
			((int)time_now.tv_usec -
			(int)time_measure_no_kill.tv_usec);
		result.tv_sec = microseconds/1000000;
		result.tv_usec = microseconds%1000000;
	}

	return result;
}

/* Get the number of new processes without the LMK has killed any process. It is
 * important to execute the function show_processes_list(..) first to update the
 * number of running processes.
 */
static int get_new_processes_no_kill(void)
{

	int diff_processes = 0;

	if ((running_processes - running_processes_last_kill) < 0) {
		/* We have killed processes manually or processes have been
		 * killed outside the LMK.
		 */
		running_processes_last_kill = running_processes;
	}
	diff_processes = running_processes - running_processes_last_kill;

	return diff_processes;
}

/* Algorithm that gets parameters with the above functions, compares these
 * parameters with the thresholds defined above and reconfigure minfrees if it
 * is necessary. It is important to execute the function show_processes_list(..)
 * first because many functions require it.
 */
static void adapt_lmk(void){

	show_process_list(ORDER_OOM, NO_PRINT);

	size_big_foreground_process = get_size_big_foreground_process();

	if (size_big_foreground_process >= max_size_big_foreground_process) {
		if (minfree_config < 5) {
			lowmem_print(1, "size_big_foreground_process: %ld KB\n",
				size_big_foreground_process);
			minfree_config = minfree_config + 2;
		} else if (minfree_config == 5) {
			lowmem_print(1, "size_big_foreground_process: %ld KB\n",
				size_big_foreground_process);
			minfree_config = minfree_config + 1;
		}
	} else {

		running_processes = get_running_processes();
		if (running_processes >= max_running_processes) {
			if (minfree_config > 3) {
				lowmem_print(1, "running_processes: %d\n",
					running_processes);
				minfree_config = minfree_config - 2;
			} else if (minfree_config == 3) {
				lowmem_print(1, "running_processes: %d\n",
					running_processes);
				minfree_config = minfree_config - 1;
			}
		}

		if ((time_first_kill.tv_sec >= 0)) {
			time_kill_X_processes =
				get_time_kill_X_processes(X_KILL_PROCESSES);

			if ((time_kill_X_processes.tv_sec >= 0) &&
				(time_kill_X_processes.tv_sec <=
					min_time_kill_X_processes.tv_sec)) {
				lowmem_print(1, "time_kill_%d_processes: %d s, "
					"%d us\n",
					X_KILL_PROCESSES,
					(int)time_kill_X_processes.tv_sec,
					(int)time_kill_X_processes.tv_usec);

				lmk_count = 0;

				if (minfree_config >= 2)
					minfree_config = minfree_config - 1;
			}
		}
	}

	time_no_kill_processes = get_time_no_kill_processes();
	if ((time_no_kill_processes.tv_sec >= 0) &&
			(time_no_kill_processes.tv_sec >
			max_time_no_kill_processes.tv_sec)) {
		lowmem_print(1, "time_no_kill_processes: %d s, %d us\n",
			(int)time_no_kill_processes.tv_sec,
			(int)time_no_kill_processes.tv_usec);

		do_gettimeofday(&time_measure_no_kill);

		if (minfree_config <= 6)
			minfree_config = minfree_config + 1;
	}

	new_processes_no_kill = get_new_processes_no_kill();
	if (new_processes_no_kill >= max_new_processes_no_kill) {
		lowmem_print(1, "new_processes_no_kill: %d\n",
			new_processes_no_kill);

		running_processes_last_kill = running_processes;

		if (minfree_config <= 6)
			minfree_config = minfree_config + 1;
	}

	/* Update the minfree configuration */
	if (minfree_config != last_minfree_config) {
		configure_minfrees(minfree_config);
		last_minfree_config = minfree_config;
		uses_no_config = 0;
		if (ms_without_use_adapt_lmk > min_ms_without_use_adapt_lmk)
			ms_without_use_adapt_lmk =
				(ms_without_use_adapt_lmk / 3) * 2;
	} else {
		uses_no_config += 1;
	}

	if ((uses_no_config >= limit_uses_no_config) &&
		(ms_without_use_adapt_lmk < max_ms_without_use_adapt_lmk)) {
		ms_without_use_adapt_lmk = (ms_without_use_adapt_lmk * 6) / 5;
		uses_no_config = 0;
	}

	return;
}

/* In certain memory configurations there can be a large number of CMA pages
 * which are not suitable to satisfy certain memory requests. This large number
 * of unsuitable pages can cause the lowmemorykiller to not kill any tasks
 * because the lowmemorykiller counts all free pages.
 *
 * In order to ensure the lowmemorykiller properly evaluates the free memory
 * only count the free pages which are suitable for satisfying the memory
 * request.
 */
int can_use_cma_pages(gfp_t gfp_mask)
{
	int can_use = 0;
	int mtype = allocflags_to_migratetype(gfp_mask);
	int i = 0;
	int *mtype_fallbacks = get_migratetype_fallbacks(mtype);

	if (is_migrate_cma(mtype)) {
		can_use = 1;
	} else {
		for (i = 0;; i++) {
			int fallbacktype = mtype_fallbacks[i];

			if (is_migrate_cma(fallbacktype)) {
				can_use = 1;
				break;
			}

			if (fallbacktype == MIGRATE_RESERVE)
				break;
		}
	}
	return can_use;
}


/* There are cases that LMK doesn't run, even when it must run. It is due to LMK
 * shrinker not considering memory status per zone. So add LMK parameters
 * (other_free, other_file) tunnig code to consider target zone of LMK shrinker.
 */
void tune_lmk_zone_param(struct zonelist *zonelist, int classzone_idx,
					int *other_free, int *other_file,
					int use_cma_pages)
{
	struct zone *zone;
	struct zoneref *zoneref;
	int zone_idx;

	for_each_zone_zonelist(zone, zoneref, zonelist, MAX_NR_ZONES) {
		zone_idx = zonelist_zone_idx(zoneref);
		if (zone_idx == ZONE_MOVABLE) {
			if (!use_cma_pages)
				*other_free -=
				    zone_page_state(zone, NR_FREE_CMA_PAGES);
			continue;
		}

		if (zone_idx > classzone_idx) {
			if (other_free != NULL)
				*other_free -= zone_page_state(zone,
							       NR_FREE_PAGES);
			if (other_file != NULL)
				*other_file -= zone_page_state(zone,
							       NR_FILE_PAGES)
					      - zone_page_state(zone, NR_SHMEM);
		} else if (zone_idx < classzone_idx) {
			if (zone_watermark_ok(zone, 0, 0, classzone_idx, 0)) {
				if (!use_cma_pages) {
					*other_free -= min(
					  zone->lowmem_reserve[classzone_idx] +
					  zone_page_state(
					    zone, NR_FREE_CMA_PAGES),
					  zone_page_state(
					    zone, NR_FREE_PAGES));
				} else {
					*other_free -=
					  zone->lowmem_reserve[classzone_idx];
				}
			} else {
				*other_free -=
					   zone_page_state(zone, NR_FREE_PAGES);
			}
		}
	}
}

/* Currenlty most memory reclaim is done through kswapd. Since kswapd uses a gfp
 * mask of GFP_KERNEL, and because the lowmemorykiller is zone aware, the
 * lowmemorykiller will ignore highmem most of the time. This results in the
 * lowmemorykiller being overly aggressive.
 *
 * The fix to this issue is to allow the lowmemorykiller to count highmem when
 * being called by the kswapd if the lowmem watermarks are satisfied.
 */
#ifdef CONFIG_HIGHMEM
void adjust_gfp_mask(gfp_t *gfp_mask)
{
	struct zone *preferred_zone;
	struct zonelist *zonelist;
	enum zone_type high_zoneidx;

	if (current_is_kswapd()) {
		zonelist = node_zonelist(0, *gfp_mask);
		high_zoneidx = gfp_zone(*gfp_mask);
		first_zones_zonelist(zonelist, high_zoneidx, NULL,
				&preferred_zone);

		if (high_zoneidx == ZONE_NORMAL) {
			if (zone_watermark_ok_safe(preferred_zone, 0,
					high_wmark_pages(preferred_zone), 0,
					0))
				*gfp_mask |= __GFP_HIGHMEM;
		} else if (high_zoneidx == ZONE_HIGHMEM) {
			*gfp_mask |= __GFP_HIGHMEM;
		}
	}
}
#else
void adjust_gfp_mask(gfp_t *unused)
{
}
#endif

void tune_lmk_param(int *other_free, int *other_file, struct shrink_control *sc)
{
	gfp_t gfp_mask;
	struct zone *preferred_zone;
	struct zonelist *zonelist;
	enum zone_type high_zoneidx, classzone_idx;
	unsigned long balance_gap;
	int use_cma_pages;

	gfp_mask = sc->gfp_mask;
	adjust_gfp_mask(&gfp_mask);

	zonelist = node_zonelist(0, gfp_mask);
	high_zoneidx = gfp_zone(gfp_mask);
	first_zones_zonelist(zonelist, high_zoneidx, NULL, &preferred_zone);
	classzone_idx = zone_idx(preferred_zone);
	use_cma_pages = can_use_cma_pages(gfp_mask);

	balance_gap = min(low_wmark_pages(preferred_zone),
			  (preferred_zone->present_pages +
			   KSWAPD_ZONE_BALANCE_GAP_RATIO-1) /
			   KSWAPD_ZONE_BALANCE_GAP_RATIO);

	if (likely(current_is_kswapd() && zone_watermark_ok(preferred_zone, 0,
			  high_wmark_pages(preferred_zone) + SWAP_CLUSTER_MAX +
			  balance_gap, 0, 0))) {
		if (lmk_fast_run)
			tune_lmk_zone_param(zonelist, classzone_idx, other_free,
				       other_file, use_cma_pages);
		else
			tune_lmk_zone_param(zonelist, classzone_idx, other_free,
				       NULL, use_cma_pages);

		if (zone_watermark_ok(preferred_zone, 0, 0, _ZONE, 0)) {
			if (!use_cma_pages) {
				*other_free -= min(
				  preferred_zone->lowmem_reserve[_ZONE]
				  + zone_page_state(
				    preferred_zone, NR_FREE_CMA_PAGES),
				  zone_page_state(
				    preferred_zone, NR_FREE_PAGES));
			} else {
				*other_free -=
				  preferred_zone->lowmem_reserve[_ZONE];
			}
		} else {
			*other_free -= zone_page_state(preferred_zone,
						      NR_FREE_PAGES);
		}

		lowmem_print(4, "lowmem_shrink of kswapd tunning for highmem "
			     "ofree %d, %d\n", *other_free, *other_file);
	} else {
		tune_lmk_zone_param(zonelist, classzone_idx, other_free,
			       other_file, use_cma_pages);

		if (!use_cma_pages) {
			*other_free -=
			  zone_page_state(preferred_zone, NR_FREE_CMA_PAGES);
		}

		lowmem_print(4, "lowmem_shrink tunning for others ofree %d, "
			     "%d\n", *other_free, *other_file);
	}
}

/*'sc' is passed shrink_control which includes a count 'nr_to_scan' and
 * a 'gfpmask'. It should look through the least-recently-used 'nr_to_scan'
 * entries and attempt to free them up.  It should return the number of objects
 * which remain in the cache.  If it returns -1, it means it cannot do any
 * scanning at this time (eg. there is a risk of deadlock).
 *
 * The 'gfpmask' refers to the allocation we are currently trying to fulfil.
 *
 * Note that 'shrink' will be passed nr_to_scan == 0 when the VM is querying
 * the cache size, so a fastpath for that case is appropriate.
 */
static int lowmem_shrink(struct shrinker *s, struct shrink_control *sc)
{
	int aux_count_processes = 0;
	struct task_struct *tsk;
	struct task_struct *selected = NULL;
	int rem = 0;
	int tasksize;
	int i, k;
	int sop_pos = 0;
	short min_score_adj = OOM_SCORE_ADJ_MAX + 1;
	int minfree = 0;
	int selected_tasksize = 0;
	short selected_oom_score_adj;
	int array_size = ARRAY_SIZE(lowmem_adj);
	int other_free = global_page_state(NR_FREE_PAGES) - totalreserve_pages;
	int other_file;
	int us;
	int us2;
	struct reclaim_state *reclaim_state = current->reclaim_state;

	/* How many slab objects shrinker() should scan and try to reclaim */
	unsigned long nr_to_scan = sc->nr_to_scan;

	if (time_init_configuration.tv_sec == -1) {
		adapt_configurations();
		do_gettimeofday(&time_init_configuration);
		do_gettimeofday(&time_init_adapt_lmk_1);
		do_gettimeofday(&time_last_kill);
		do_gettimeofday(&time_measure_no_kill);
		do_gettimeofday(&time_last_lmk_use_2);
	}

	do_gettimeofday(&time_last_lmk_use_1);

	if ((time_last_lmk_use_1.tv_sec - time_last_lmk_use_2.tv_sec) >=
		max_time_fail_measure.tv_sec) {
		fail_measure = 1;
		do_gettimeofday(&time_measure_no_kill);
		lowmem_print(1, "Fail measure\n");
	} else {
		fail_measure = 0;
	}

	do_gettimeofday(&time_last_lmk_use_2);
	do_gettimeofday(&time_init_adapt_lmk_2);

	/* It starts to call the algorithm TIME_INIT_ADAPT seconds after
	 * the device is started. In addition, it limit the number of executions
	 * of the algorithm, no more than 1 in max_ms_without_use_adapt_lmk.
	 */
	if ((((time_init_adapt_lmk_2.tv_sec) - (time_init_adapt_lmk_1.tv_sec)) >
		TIME_INIT_ADAPT) && (adaptive_LMK == 1)) {

		do_gettimeofday(&time_use_adapt_lmk_1);

		if (time_use_adapt_lmk_2.tv_sec == -1) {
			adapt_lmk();
			do_gettimeofday(&time_use_adapt_lmk_2);
		}

		us2 = (time_use_adapt_lmk_1.tv_sec -
			time_use_adapt_lmk_2.tv_sec) * 1000000 +
			((int)time_use_adapt_lmk_1.tv_usec -
				(int)time_use_adapt_lmk_2.tv_usec);


		/* Exception: execute the adapt algorithm if we have killed one
		 * process in the last execution.
		 */
		if ((us2 >= ms_without_use_adapt_lmk) || (kill == 1)) {
			kill = 0;
			adapt_lmk();
			do_gettimeofday(&time_use_adapt_lmk_2);
		}

	}

	if (minfree_config != last_minfree_config) {
		configure_minfrees(minfree_config);
		last_minfree_config = minfree_config;
	}

	if (nr_to_scan > 0) {
		if (mutex_lock_interruptible(&scan_mutex) < 0)
			return 0;
	}

	if (global_page_state(NR_SHMEM) + total_swapcache_pages() <
		global_page_state(NR_FILE_PAGES)) {
		other_file = global_page_state(NR_FILE_PAGES) -
						global_page_state(NR_SHMEM) -
						total_swapcache_pages();
	} else {
		other_file = 0;
	}

	tune_lmk_param(&other_free, &other_file, sc);

	if (lowmem_adj_size < array_size)
		array_size = lowmem_adj_size;
	if (lowmem_minfree_size < array_size)
		array_size = lowmem_minfree_size;
	for (i = 0; i < array_size; i++) {
		minfree = lowmem_minfree[i];
		if (other_free < minfree && other_file < minfree) {
			min_score_adj = lowmem_adj[i];
			break;
		}
	}

	if (nr_to_scan > 0)
		lowmem_print(3, "lowmem_shrink %lu, %x, ofree %d %d, ma %hd\n",
				nr_to_scan, sc->gfp_mask, other_free,
				other_file, min_score_adj);
	rem = global_page_state(NR_ACTIVE_ANON) +
		global_page_state(NR_ACTIVE_FILE) +
		global_page_state(NR_INACTIVE_ANON) +
		global_page_state(NR_INACTIVE_FILE);
	if (nr_to_scan <= 0 || min_score_adj == OOM_SCORE_ADJ_MAX + 1) {
		lowmem_print(5, "lowmem_shrink init %lu, %x, return %d\n",
			     nr_to_scan, sc->gfp_mask, rem);

		if (nr_to_scan > 0)
			mutex_unlock(&scan_mutex);

		return rem;
	}
	selected_oom_score_adj = min_score_adj;

	rcu_read_lock();
	clean_array_long(size_of_process, NUM_OF_PROCESS);
	clean_array_short(oom_of_process, NUM_OF_PROCESS);
	clean_array_tasks(tasks, NUM_OF_PROCESS);
	for_each_process(tsk) {
		struct task_struct *p;
		short oom_score_adj;

		if (tsk->flags & PF_KTHREAD)
			continue;

		/* if task no longer has any memory ignore it */
		if (test_task_flag(tsk, TIF_MM_RELEASED))
			continue;

		if (time_before_eq(jiffies, lowmem_deathpending_timeout)) {
			if (test_task_flag(tsk, TIF_MEMDIE)) {
				rcu_read_unlock();
				/* give the system time to free up the memory */
				msleep_interruptible(20);
				mutex_unlock(&scan_mutex);
				return 0;
			}
		}

		p = find_lock_task_mm(tsk);
		if (!p)
			continue;

		tasksize = get_mm_rss(p->mm);
		oom_score_adj = p->signal->oom_score_adj;
		if ((tasksize > 0) && (oom_score_adj >= 0)) {
			tasks[sop_pos] = p;
			aux_count_processes = sop_pos;
			sop_pos++;
			if (sop_pos >= NUM_OF_PROCESS) {
				lowmem_print(1, "Limit of processes\n");
				sop_pos = 0;
			}
		}

		if (oom_score_adj < min_score_adj) {
			task_unlock(p);
			continue;
		}

		task_unlock(p);
		if (tasksize <= 0)
			continue;

		if (selected) {
			if (oom_score_adj < selected_oom_score_adj)
				continue;
			if (oom_score_adj == selected_oom_score_adj &&
			    tasksize <= selected_tasksize)
				continue;
		}
		selected = p;
		selected_tasksize = tasksize;
		selected_oom_score_adj = oom_score_adj;
		lowmem_print(2, "select '%s' (%d), adj %hd, size %d, to kill\n",
			     p->comm, p->pid, oom_score_adj, tasksize);
	}
	running_processes = aux_count_processes;

	if (selected) {

		if (lmk_count == 0)
			do_gettimeofday(&time_first_kill);

		do_gettimeofday(&time_last_kill);
		do_gettimeofday(&time_measure_no_kill);
		us = (time_last_kill.tv_sec - time_first_kill.tv_sec) *
			1000000 + ((int)time_last_kill.tv_usec -
				(int)time_first_kill.tv_usec);

		lowmem_print(1, "Killing '%s' (%d), adj %hd, "
				"to free %ldkB on behalf of '%s' (%d) because "
				"cache %ldkB is below limit %ldkB for "
				"oom_score_adj %hd. Free memory is %ldkB above "
				"reserved. Number of kill processes with the "
				"actual minfree config: %d in %ld second. "
				"Since kill first process: %d in %d s %d us\n",
				selected->comm, selected->pid,
				selected_oom_score_adj,
				selected_tasksize * (long)(PAGE_SIZE / 1024),
				current->comm, current->pid,
				other_file * (long)(PAGE_SIZE / 1024),
				minfree * (long)(PAGE_SIZE / 1024),
				min_score_adj,
				other_free * (long)(PAGE_SIZE / 1024),
				lmk_count_configuration + 1,
				time_last_kill.tv_sec -
					time_init_configuration.tv_sec,
				lmk_count + 1,
				us/1000000,
				us%1000000);

		running_processes_last_kill = running_processes;

		if (order_flag == 0) {
			get_processes_size(size_of_process, tasks,
				NUM_OF_PROCESS);
			process_size_sort(size_of_process, tasks,
				NUM_OF_PROCESS);

			lowmem_print(1, "List of active processes\n");

			for (k = 0; (k < (NUM_OF_PROCESS)) &&
					(tasks[k] != NULL); k++) {
				lowmem_print(1, "Process %d '%s': size(%ldkB), "
					"pid(%d), oom_score_adj(%d)\n",
					k, tasks[k]->comm, size_of_process[k],
					tasks[k]->pid,
					tasks[k]->signal->oom_score_adj);
			}

		} else if (order_flag == 1) {
			get_processes_oom(oom_of_process, tasks,
				NUM_OF_PROCESS);
			process_oom_sort(oom_of_process, tasks, NUM_OF_PROCESS);

			lowmem_print(1, "List of active processes\n");

			for (k = 0; (k < (NUM_OF_PROCESS)) &&
					(tasks[k] != NULL); k++) {
				lowmem_print(1, "Process %d '%s': "
					"oom_score_adj(%d), size(%ldkB), "
					"pid(%d)\n",
					k, tasks[k]->comm, oom_of_process[k],
					(get_mm_rss(tasks[k]->mm)) *
						(long)(PAGE_SIZE / 1024),
					tasks[k]->pid);
			}
		}

		lowmem_deathpending_timeout = jiffies + HZ;
		send_sig(SIGKILL, selected, 0);
		set_tsk_thread_flag(selected, TIF_MEMDIE);
		rem -= selected_tasksize;
		rcu_read_unlock();
		lmk_count++;
		lmk_count_configuration++;
		test_lmk_count++;
		kill = 1;
		/* give the system time to free up the memory */
		msleep_interruptible(20);

		if (reclaim_state && (pages_patch == 1))
			reclaim_state->reclaimed_slab += selected_tasksize;

	} else {
		rcu_read_unlock();
	}

	lowmem_print(4, "lowmem_shrink exit %lu, %x, return %d\n",
		     nr_to_scan, sc->gfp_mask, rem);
	mutex_unlock(&scan_mutex);
	return rem;
}

static struct shrinker lowmem_shrinker = {
	.shrink = lowmem_shrink,
	.seeks = DEFAULT_SEEKS * 16
};

static int __init lowmem_init(void)
{
	register_shrinker(&lowmem_shrinker);
	return 0;
}

static void __exit lowmem_exit(void)
{
	unregister_shrinker(&lowmem_shrinker);
}

#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER_AUTODETECT_OOM_ADJ_VALUES
static short lowmem_oom_adj_to_oom_score_adj(short oom_adj)
{
	if (oom_adj == OOM_ADJUST_MAX)
		return OOM_SCORE_ADJ_MAX;
	else
		return (oom_adj * OOM_SCORE_ADJ_MAX) / -OOM_DISABLE;
}

static void lowmem_autodetect_oom_adj_values(void)
{
	int i;
	short oom_adj;
	short oom_score_adj;
	int array_size = ARRAY_SIZE(lowmem_adj);

	if (lowmem_adj_size < array_size)
		array_size = lowmem_adj_size;

	if (array_size <= 0)
		return;

	oom_adj = lowmem_adj[array_size - 1];
	if (oom_adj > OOM_ADJUST_MAX)
		return;

	oom_score_adj = lowmem_oom_adj_to_oom_score_adj(oom_adj);
	if (oom_score_adj <= OOM_ADJUST_MAX)
		return;

	lowmem_print(1, "lowmem_shrink: convert oom_adj to oom_score_adj:\n");

	for (i = 0; i < array_size; i++) {
		oom_adj = lowmem_adj[i];
		oom_score_adj = lowmem_oom_adj_to_oom_score_adj(oom_adj);
		lowmem_adj[i] = oom_score_adj;
		lowmem_print(1, "oom_adj %d => oom_score_adj %d\n",
			     oom_adj, oom_score_adj);
	}
}

static int lowmem_adj_array_set(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_array_ops.set(val, kp);

	/* HACK: Autodetect oom_adj values in lowmem_adj array */
	lowmem_autodetect_oom_adj_values();

	return ret;
}

static int lowmem_adj_array_get(char *buffer, const struct kernel_param *kp)
{
	return param_array_ops.get(buffer, kp);
}

static void lowmem_adj_array_free(void *arg)
{
	param_array_ops.free(arg);
}

static struct kernel_param_ops lowmem_adj_array_ops = {
	.set = lowmem_adj_array_set,
	.get = lowmem_adj_array_get,
	.free = lowmem_adj_array_free,
};

static const struct kparam_array __param_arr_adj = {
	.max = ARRAY_SIZE(lowmem_adj),
	.num = &lowmem_adj_size,
	.ops = &param_ops_short,
	.elemsize = sizeof(lowmem_adj[0]),
	.elem = lowmem_adj,
};
#endif

static int lowmem_get_processes(char *buffer, const struct kernel_param *kp)
{
	show_process_list(order_flag, PRINT);

	return 0;
}

/* cat -> get -> show_processes_list(..) */
static struct kernel_param_ops lowmem_ops_processes = {
	.get = lowmem_get_processes,
};

static int lowmem_get_services(char *buffer, const struct kernel_param *kp)
{
	show_services_list();

	return 0;
}

/* cat -> get -> show_services_list(..) */
static struct kernel_param_ops lowmem_ops_services = {
	.get = lowmem_get_services,
};

module_param_named(cost, lowmem_shrinker.seeks, int, S_IRUGO | S_IWUSR);
#ifdef CONFIG_ANDROID_LOW_MEMORY_KILLER_AUTODETECT_OOM_ADJ_VALUES
__module_param_call(MODULE_PARAM_PREFIX, adj,
		    &lowmem_adj_array_ops,
		    .arr = &__param_arr_adj,
		    S_IRUGO | S_IWUSR, -1);
__MODULE_PARM_TYPE(adj, "array of short");
#else
module_param_array_named(adj, lowmem_adj, short, &lowmem_adj_size,
			S_IRUGO | S_IWUSR);
#endif
module_param_array_named(minfree, lowmem_minfree, uint, &lowmem_minfree_size,
			S_IRUGO | S_IWUSR);
module_param_named(debug_level, lowmem_debug_level, uint, S_IRUGO | S_IWUSR);
module_param_named(lmk_fast_run, lmk_fast_run, int, S_IRUGO | S_IWUSR);

module_param_named(order_flag, order_flag, int, S_IRUGO | S_IWUSR);
module_param_named(minfree_config, minfree_config, int,
			S_IRUGO | S_IWUSR);
module_param_named(adaptive_LMK, adaptive_LMK, int, S_IRUGO | S_IWUSR);
module_param_named(pages_patch, pages_patch, int, S_IRUGO | S_IWUSR);
module_param_named(test_lmk_count, test_lmk_count, long, S_IRUGO);
module_param_named(test_running_count, test_running_count, long, S_IRUGO);
module_param_cb(show_services_list, &lowmem_ops_services, NULL, 0644);
module_param_cb(show_processes_list, &lowmem_ops_processes, NULL, 0644);

module_init(lowmem_init);
module_exit(lowmem_exit);

MODULE_LICENSE("GPL");

