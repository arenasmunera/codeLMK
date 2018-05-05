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

//That limit is never reached
#define NUM_OF_PROCESS 100
#define X_KILL_PROCESSES 3
#define ORDER_SIZE 0
#define ORDER_OOM 1
#define NO_ORDER 2
#define NO_PRINT 0
#define PRINT 1
#define TIME_INIT_ADAPT 45

#ifdef CONFIG_HIGHMEM
#define _ZONE ZONE_HIGHMEM
#else
#define _ZONE ZONE_NORMAL
#endif

//Muchas Líneas añadidas
static char process_to_kill[TASK_COMM_LEN]="Ninguna";
//Activo si adaptive_LMK = 1
static int adaptive_LMK = 1;

//1=Very Light; 2=Light; 3=Medium; 4=Aggressive; 5=Very Aggressive
static int minfree_configuration = 0; //3
static int last_minfree_configuration = 0; //3
// 2MB, 4MB, 5MB, 8MB, 12MB, 16MB
static int very_light_minfree[6] = {512,1024,1280,22048,3072,4096};		//ERRATA
// 4MB, 8MB, 10MB, 16MB, 24MB, 32MB
static int light_minfree[6] = {1024,2048,2560,4096,6144,8192};
// 4MB, 8MB, 16MB, 32MB, 48MB, 64MB
static int medium_minfree[6] = {1024,2048,4096,8192,12288,16384};
// 8MB, 16MB, 32MB, 64MB, 96MB, 128MB
static int aggressive_minfree[6] = {2048,4096,8192,16384,24576,32768};
// 16MB, 32MB, 64MB, 128MB, 192MB, 256MB
static int very_aggressive_minfree[6] = {4096,8192,16384,32768,49152,65536};

//static struct task_struct task_clean;
static struct task_struct *tasks[NUM_OF_PROCESS];
static long size_of_process[NUM_OF_PROCESS];
static short oom_of_process[NUM_OF_PROCESS];
static long size_foreground_processes[NUM_OF_PROCESS];

//Algorithm params
static struct timeval time_kill_X_processes;
static struct timeval time_no_kill_processes;
static int new_processes_no_kill = 0;
static int running_processes = -1;
static long size_big_foreground_process = 0;

//Aux
static int running_processes_last_kill = -1;
static struct timeval time_last_LMK_use_1;
static struct timeval time_last_LMK_use_2 = { -1, 0 };
static bool fail_measure = false;
static struct timeval time_first_kill = { -1, 0 };
static struct timeval time_measure_no_kill = { -1, 0 };
static struct timeval time_init_adapt_lmk_1 = { -1, 0 };
static struct timeval time_init_adapt_lmk_2;
static long test_lmk_count;
static long test_running_count;

//Algorithm threshold
static struct timeval min_time_kill_X_processes = { 1, 0 };
static struct timeval max_time_no_kill_processes = { 1200, 0 };
static struct timeval max_time_fail_measure = { 600, 0 };
static int max_new_processes_no_kill = 10;
static int max_running_processes = 25;
static long max_size_big_foreground_process = 125000;

static struct timeval time_last_kill = { -1, 0 };
static struct timeval time_init_configuration = { -1, 0 };
static uint32_t lmk_count = 0;
static uint32_t lmk_count_configuration = 0;
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

//If order_flag=0, processes are sorted by size. If order_flag=1 processes are sorted by oom_score_adj. If order_flag=2 do a new process list but not show anything.
static int order_flag = 0;

static unsigned long lowmem_deathpending_timeout;

#define lowmem_print(level, x...)			\
	do {						\
		if (lowmem_debug_level >= (level))	\
			pr_info(x);			\
	} while (0)

static void configure_minfrees(int minfree_configuration)
{
	int i = 0;
	do_gettimeofday (&time_init_configuration);
	lmk_count_configuration = 0;
	lmk_count = 0;
	lowmem_print(1, "New configuration: %d\n",
			minfree_configuration);
	switch (minfree_configuration) {
	case 1:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++) {
			lowmem_minfree[i]=very_light_minfree[i];
		}
	  	break;
	case 2:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++) {
			lowmem_minfree[i]=light_minfree[i];
		}
	  	break;
	case 3:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++) {
			lowmem_minfree[i]=medium_minfree[i];
		}
	  	break;
	case 4:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++) {
			lowmem_minfree[i]=aggressive_minfree[i];
		}
	  	break;
	case 5:
		for (i = 0; i < ARRAY_SIZE(lowmem_minfree); i++) {
			lowmem_minfree[i]=very_aggressive_minfree[i];
		}
	  	break;
	default:
	  	break;
	}
}

static void clean_array_long (long array[], int num_elem)
{
	int i;
	for (i = 0; i < num_elem ; i++) {
            	array[i] = 0;
         	}
}

static void clean_array_short (short array[], int num_elem)
{
	int i;
	for (i = 0; i < num_elem ; i++) {
            	array[i] = 0;
         	}
}


static void clean_array_tasks (struct task_struct *array[], int num_elem)
{
	int i;
	for (i = 0; i < num_elem ; i++) {
            	array[i] = NULL;
         	}
}

static void get_processes_size(long array_sizes[], struct task_struct *array_processes[], int num_elem)
{
	int i;
	int size;

	for (i = 0; (i < num_elem) && (array_processes[i] != NULL); i++) {
		size = get_mm_rss(array_processes[i]->mm);
		array_sizes[i] = (size)*(long)(PAGE_SIZE / 1024);
	}	

}

static void process_size_sort(long array_sizes[], struct task_struct *array_processes[], int num_elem)
{
	int i,j;
	long temp1;
	struct task_struct *temp2;

	for (i = 1; i < num_elem; i++) {  
		for (j = 0; (j < num_elem - 1) && (array_processes[j]!= NULL); j++) {
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


static void get_processes_oom(short array_oom[], struct task_struct *array_processes[], int num_elem)
{
	int i;

	for (i = 0; (i < num_elem) && (array_processes[i] != NULL); i++) {
		array_oom[i] = array_processes[i]->signal->oom_score_adj;
	}	

}

static void process_oom_sort(short array_oom[], struct task_struct *array_processes[], int num_elem)
{
	int i,j;
	short temp1;
	struct task_struct *temp2;

	for (i = 1; i < num_elem; i++) {
		for (j = 0; (j < num_elem - 1) && (array_processes[j]!= NULL); j++) {
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

/*The lowmemorykiller uses the TIF_MEMDIE flag to help ensure it doesn't
kill another task until the memory from the previously killed task has
been returned to the system.

However the lowmemorykiller does not currently look at tasks who do not
have a tasks->mm, but just because a process doesn't have a tasks->mm
does not mean that the task's memory has been fully returned to the
system yet.

In order to prevent the lowmemorykiller from unnecessarily killing
multiple applications in a row the lowmemorykiller has been changed to
ensure that previous killed tasks are no longer in the process list
before attempting to kill another task.*/
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

/* Function that show the active processes
*/
static void show_process_list (int order, int print)
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
		
		if((tasksize > 0) && (oom_score_adj >= 0)){
			tasks[sop_pos] = p;
			aux_count_processes=sop_pos; 	//running_processes tomara el valor del numero de procesos en la ultima vez que se entre
			sop_pos++;
			if(sop_pos >= NUM_OF_PROCESS){
				lowmem_print(1, "Limit of processes\n");
				sop_pos = 0;
			}
		}
		
		if (oom_score_adj < 0) {
			task_unlock(p);
			continue;
		}
		task_unlock(p);
		if (tasksize <= 0){
			continue;
		}
	}

	running_processes = aux_count_processes;
	test_running_count = running_processes;
	if (running_processes_last_kill == -1){
		running_processes_last_kill = running_processes;
	}

	if (order == 0){
		get_processes_size(size_of_process, tasks, NUM_OF_PROCESS);
		process_size_sort(size_of_process, tasks, NUM_OF_PROCESS);

		if (print == 1){
			lowmem_print(1,"List of active processes\n");

			for (k = 0; (k < (NUM_OF_PROCESS)) && (tasks[k] != NULL); k++) {
				lowmem_print(1,"Process %d '%s': size(%ld kB), pid(%d), oom_score_adj(%d)\n",
					k, tasks[k]->comm, size_of_process[k], tasks[k]->pid, tasks[k]->signal->oom_score_adj);
			}
		}
	} else if(order == 1){
		get_processes_oom(oom_of_process, tasks, NUM_OF_PROCESS);
		process_oom_sort(oom_of_process, tasks, NUM_OF_PROCESS);

		aux_count = 0;
		for (k = 0; (k < (NUM_OF_PROCESS)) && (tasks[k] != NULL); k++) {
			if (oom_of_process[k] == 0){
				size_foreground_processes[aux_count] = (get_mm_rss(tasks[k]->mm))*(long)(PAGE_SIZE / 1024);
				aux_count++;
			}
		}

		if (print == 1){
			lowmem_print(1,"List of active processes\n");

			for (k = 0; (k < (NUM_OF_PROCESS)) && (tasks[k] != NULL); k++) {
				lowmem_print(1,"Process %d '%s': oom_score_adj(%d), size(%ld kB), pid(%d)\n",
					k, tasks[k]->comm, oom_of_process[k], (get_mm_rss(tasks[k]->mm))*(long)(PAGE_SIZE / 1024), tasks[k]->pid);
			}
		}
	}

	rcu_read_unlock();	
	mutex_unlock(&scan_mutex);	
}

/*
Obtengo el tiempo que tarda en matar X procesos, hasta que no mate ese numero estare devolviendo un valor de tiempo negativo. Cuando alcance ese numero
restare el tiempo actual menos el tiempo en que se empezo a contar los procesos matados. Posteriormente este valor se comparara con su respectivo umbral.
Si pasa el tiempo minimo en matar 10 procesos sin haber llegado a esa cuenta, volvemos a iniciar la cuenta y el medidor de tiempo
*/

static struct timeval get_time_kill_X_processes(int X_processes)
{
	struct timeval result = {-1,0};

	//Abrir cerrojo
	if((lmk_count >= X_processes) && (time_first_kill.tv_sec >= 0)){
		//do_gettimeofday(&time_last_kill);   Me interesa el tiempo cuando se mato la ultima vez, no el actual. Para ser realista la medida de cuanto ha tardado en matar X procesos
		int microseconds = (time_last_kill.tv_sec - time_first_kill.tv_sec) * 1000000 + ((int)time_last_kill.tv_usec - (int)time_first_kill.tv_usec);
		result.tv_sec = microseconds/1000000;
		result.tv_usec = microseconds%1000000;
	}
	//Cerrar cerrojo

	return result;
}

/*
Obtengo el tiempo que ha pasado sin matar procesos pero entrando a trabajar el LMK
*/
static struct timeval get_time_no_kill_processes(void)
{
	struct timeval result = {-1,0};

	struct timeval time_now;
	do_gettimeofday (&time_now);

	//Abrir cerrojo
	if (fail_measure == false){
		int microseconds = (time_now.tv_sec - time_measure_no_kill.tv_sec) * 1000000 + ((int)time_now.tv_usec - (int)time_measure_no_kill.tv_usec);
		result.tv_sec = microseconds/1000000;
		result.tv_usec = microseconds%1000000;
	}
	//Cerrar cerrojo

	return result;
}

static int get_new_processes_no_kill(void)
{

	//show_process_list();

	int diff_processes = 0;

	//Abrir cerrojo
	if ((running_processes - running_processes_last_kill) < 0){
		running_processes_last_kill = running_processes; // Los he matado manualmente o se han matado desde fuera del LMK
	}
	diff_processes = running_processes - running_processes_last_kill;
	//Cerrar cerrojo

	return diff_processes;
}

/*
Obtengo el peso de los X procesos mas pesados. La lista de procesos seria la ultima que se hubiera realizado, quizas convenga primero renovar la lista con el show_process_list
*/
static long get_size_big_foreground_process(void)
{	

	//show_process_list();
	int k;
	long final_size = 0;

	//Abrir cerrojo
	for (k=0; (size_foreground_processes[k] != 0); k++){
		if(size_foreground_processes[k] > final_size){
			final_size = size_foreground_processes[k];
		}
	}
	//Cerrar cerrojo

	return final_size;	
}

/*
Obtengo el numero de procesos actual con valor de oom > 0 y tamaño > 0
*/
static int get_running_processes(void)
{

	//show_process_list();

	int final_processes = 0;

	//Abrir cerrojo
	final_processes = running_processes;
	//Cerrar cerrojo

	return final_processes;

}

/*
Algoritmo que en funcion de los parametros obtenidos compara con los umbrales y modifica la configuracion si es necesario.
*/
static void adapt_lmk(void){

	show_process_list(ORDER_OOM, NO_PRINT);

	size_big_foreground_process = get_size_big_foreground_process();

	if (size_big_foreground_process >= max_size_big_foreground_process){
		if(minfree_configuration != 4){
			lowmem_print(1,"size_big_foreground_process: %ld KB\n",
				size_big_foreground_process);
			minfree_configuration = 4;
		}
		return;
	}

	running_processes = get_running_processes();
	if (running_processes >= max_running_processes){
		if (minfree_configuration != 2){
			lowmem_print(1,"running_processes: %d\n",
				running_processes);
			minfree_configuration = 2;
		}
		return;
	}

	//Abrir cerrojo (Quizas quitar cerrojo de get_time_kill_X_processes)
	if ((time_first_kill.tv_sec >= 0)){
		time_kill_X_processes = get_time_kill_X_processes(X_KILL_PROCESSES);
		if(time_kill_X_processes.tv_sec >  min_time_kill_X_processes.tv_sec){
			lmk_count = 0;
		}else{
			if ((time_kill_X_processes.tv_sec >= 0) & (time_kill_X_processes.tv_sec <= min_time_kill_X_processes.tv_sec)){
				lowmem_print(1,"time_kill_X_processes: %d s, %d us\n",
					(int)time_kill_X_processes.tv_sec, (int)time_kill_X_processes.tv_usec);
				lmk_count = 0;			//Solo se ejecuta una vez y vuelve a iniciarse la cuenta
				//Siempre que se produzca la condicion, reiniciamos la cuenta, se pueda o no cambiar de configuracion
				if (minfree_configuration >= 2){
					minfree_configuration = minfree_configuration - 1;
				}
				return;
			}	
		}
	}
	//Cerrar cerrojo

	time_no_kill_processes = get_time_no_kill_processes();
	if ((time_no_kill_processes.tv_sec >= 0) & (time_no_kill_processes.tv_sec > max_time_no_kill_processes.tv_sec)){
	
		lowmem_print(1,"time_no_kill_processes: %d s, %d us\n",
			(int)time_no_kill_processes.tv_sec, (int)time_no_kill_processes.tv_usec);
		//Abrir cerrojo
		do_gettimeofday (&time_measure_no_kill);  //Para que solo se ejecute una vez y vuelva a empezarse la cuenta
		//Siempre que se produzca la condicion, reiniciamos la cuenta, se pueda o no cambiar de configuracion
		if (minfree_configuration <= 4){
					minfree_configuration = minfree_configuration + 1;
		}
		//Cerrar cerrojo
		return;
	}

	new_processes_no_kill = get_new_processes_no_kill();
	if (new_processes_no_kill >= max_new_processes_no_kill){

		lowmem_print(1,"running_processes: %d. running_processes_last_kill: %d \n",
			running_processes, running_processes_last_kill);
		
		lowmem_print(1,"new_processes_no_kill: %d \n",
			new_processes_no_kill);

		//Abrir cerrojo
		running_processes_last_kill = running_processes;	//Solo se ejecuta una vez y vuelve a iniciarse la cuenta
		//Siempre que se produzca la condicion, reiniciamos la cuenta, se pueda o no cambiar de configuracion
		if (minfree_configuration <= 4){
					minfree_configuration = minfree_configuration + 1;
		}
		//Cerrar cerrojo
		return;
	}
}


//static DEFINE_MUTEX(scan_mutex);

/*In certain memory configurations there can be a large number of
CMA pages which are not suitable to satisfy certain memory
requests.
This large number of unsuitable pages can cause the
lowmemorykiller to not kill any tasks because the
lowmemorykiller counts all free pages.

In order to ensure the lowmemorykiller properly evaluates the
free memory only count the free pages which are suitable for
satisfying the memory request.*/
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


/*There are cases that LMK doesn't run, even when it must run.
It is due to LMK shrinker not considering memory status per zone.
So add LMK parameters(other_free, other_file) tunnig code to
consider target zone of LMK shrinker.*/

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

/*Currenlty most memory reclaim is done through kswapd.
Since kswapd uses a gfp mask of GFP_KERNEL, and because
the lowmemorykiller is zone aware, the lowmemorykiller will
ignore highmem most of the time.
This results in the lowmemorykiller being overly aggressive.

The fix to this issue is to allow the lowmemorykiller to
count highmem when being called by the kswapd if the lowmem
watermarks are satisfied.*/
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

/*'sc' is passed shrink_control which includes a count 'nr_to_scan'
and a 'gfpmask'.  It should look through the least-recently-used
'nr_to_scan' entries and attempt to free them up.  It should return
the number of objects which remain in the cache.  If it returns -1, it means
it cannot do any scanning at this time (eg. there is a risk of deadlock).

The 'gfpmask' refers to the allocation we are currently trying to
fulfil.
 
Note that 'shrink' will be passed nr_to_scan == 0 when the VM is
querying the cache size, so a fastpath for that case is appropriate.
*/

static int lowmem_shrink(struct shrinker *s, struct shrink_control *sc)
{
	int aux_count_processes = 0;
	struct task_struct *tsk;
	struct task_struct *selected = NULL;
	int rem = 0;
	int tasksize;
	int i;
	int sop_pos=0;
	int k;
	short min_score_adj = OOM_SCORE_ADJ_MAX + 1;
	int minfree = 0;
	int selected_tasksize = 0;
	short selected_oom_score_adj;
	int array_size = ARRAY_SIZE(lowmem_adj);
	int other_free;
	int other_file;
	int us;
	//Array of process names
	//char process_names[NUM_OF_PROCESS][TASK_COMM_LEN];
	//Array of process pid
	//int process_pids[NUM_OF_PROCESS];
	//Array de punteros
	//struct task_struct *tasks[NUM_OF_PROCESS];
	//long size_of_process[NUM_OF_PROCESS];


	/* How many slab objects shrinker() should scan and try to reclaim */
	unsigned long nr_to_scan = sc->nr_to_scan;

	if (time_init_configuration.tv_sec == -1){
		do_gettimeofday (&time_init_configuration);
		lowmem_print(1, "Initial time %ld\n",
			time_init_configuration.tv_sec);
	}

	if (time_init_adapt_lmk_1.tv_sec == -1){
		do_gettimeofday (&time_init_adapt_lmk_1);
	}

	if (time_last_kill.tv_sec == -1){
		do_gettimeofday (&time_last_kill);
	}

	if (time_measure_no_kill.tv_sec == -1){
		do_gettimeofday (&time_measure_no_kill);
	}

	do_gettimeofday (&time_last_LMK_use_1);

	if (time_last_LMK_use_2.tv_sec == -1){
		do_gettimeofday (&time_last_LMK_use_2);
	}

	if ((time_last_LMK_use_1.tv_sec - time_last_LMK_use_2.tv_sec) >= max_time_fail_measure.tv_sec){
		fail_measure = true;
		do_gettimeofday (&time_measure_no_kill);
		lowmem_print(1,"Fail measure\n");
	}else{
		fail_measure = false;
	}

	do_gettimeofday (&time_last_LMK_use_2);
	do_gettimeofday (&time_init_adapt_lmk_2);


	//llamada al algoritmo
	if ((((time_init_adapt_lmk_2.tv_sec) - (time_init_adapt_lmk_1.tv_sec)) > TIME_INIT_ADAPT) && (adaptive_LMK == 1)){
		adapt_lmk();
	}

	//lowmem_print(1, "Aviso -1 \n");

	if (minfree_configuration != last_minfree_configuration){
		configure_minfrees(minfree_configuration);
		last_minfree_configuration = minfree_configuration;
	}

	//lowmem_print(1, "Aviso 0\n");

	if (nr_to_scan > 0) {
		if (mutex_lock_interruptible(&scan_mutex) < 0)
			return 0;
	}

	//lowmem_print(1, "Aviso 1 \n");

	other_free = global_page_state(NR_FREE_PAGES);

	if (global_page_state(NR_SHMEM) + total_swapcache_pages() <
		global_page_state(NR_FILE_PAGES))
		other_file = global_page_state(NR_FILE_PAGES) -
						global_page_state(NR_SHMEM) -
						total_swapcache_pages();
	else
		other_file = 0;

	//lowmem_print(1, "Aviso 2\n");

	tune_lmk_param(&other_free, &other_file, sc);

	//lowmem_print(1, "Aviso 3\n");

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

	//lowmem_print(1, "Aviso 4\n");

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

		//lowmem_print(1, "Aviso 7\n");

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

		
		//Añadido para guardar los punteros de los procesos con tamaño mayor que 0 y oom_score_adj mayor que 0
		tasksize = get_mm_rss(p->mm);
		oom_score_adj = p->signal->oom_score_adj;
		if((tasksize > 0) && (oom_score_adj >= 0)){
			tasks[sop_pos] = p;
			aux_count_processes=sop_pos; //running_processes tomara el valor del numero de procesos en la ultima vez que se entre
			sop_pos++;
			if(sop_pos >= NUM_OF_PROCESS){
				lowmem_print(1, "Limit of processes\n");
				sop_pos = 0;
			}
		}

		//Añadido para que este sea el primer proceso matado si está abierto
		if (strncmp((p->comm),process_to_kill,15)==0){
			lowmem_print(1,"Arenas '%s'\n",
				process_to_kill);
			selected = p;
			selected_oom_score_adj = p->signal->oom_score_adj;
			selected_tasksize = get_mm_rss(p->mm);
			task_unlock(p);
			break;
		}

		//oom_score_adj = p->signal->oom_score_adj;
		if (oom_score_adj < min_score_adj) {
			task_unlock(p);
			continue;
		}
		//tasksize = get_mm_rss(p->mm);
		task_unlock(p);
		if (tasksize <= 0){
			continue;
		}

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

		if (lmk_count==0){
			do_gettimeofday(&time_first_kill);
		}
		do_gettimeofday(&time_last_kill);
		do_gettimeofday(&time_measure_no_kill);
		us = (time_last_kill.tv_sec - time_first_kill.tv_sec) * 1000000 + ((int)time_last_kill.tv_usec - (int)time_first_kill.tv_usec);

		lowmem_print(1, "Killing '%s' (%d), adj %hd," \
				" to free %ldkB on behalf of '%s' (%d) because" \
				" cache %ldkB is below limit %ldkB for oom_score_adj %hd." \
				" Free memory is %ldkB above reserved." \
				" Number of kill processes with the" \
				" actual minfree configuration: %d in %ld second." \
				" Time since kill the first process: %d in %d s %d us \n",
			     selected->comm, selected->pid,
			     selected_oom_score_adj,
			     selected_tasksize * (long)(PAGE_SIZE / 1024),
			     current->comm, current->pid,
			     other_file * (long)(PAGE_SIZE / 1024),
			     minfree * (long)(PAGE_SIZE / 1024),
			     min_score_adj,
			     other_free * (long)(PAGE_SIZE / 1024),
			     lmk_count_configuration + 1,
			     time_last_kill.tv_sec - time_init_configuration.tv_sec,
			     lmk_count + 1,
			     us/1000000,
			     us%1000000);


		running_processes_last_kill = running_processes;

		if (order_flag == 0){
			get_processes_size(size_of_process, tasks, NUM_OF_PROCESS);
			process_size_sort(size_of_process, tasks, NUM_OF_PROCESS);

			lowmem_print(1,"List of active processes\n");

			for (k = 0; (k < (NUM_OF_PROCESS)) && (tasks[k] != NULL); k++) {
				lowmem_print(1,"Process %d '%s': size(%ld kB), pid(%d), oom_score_adj(%d)\n",
					k, tasks[k]->comm, size_of_process[k], tasks[k]->pid, tasks[k]->signal->oom_score_adj);
			}
		}else if(order_flag == 1){
			get_processes_oom(oom_of_process, tasks, NUM_OF_PROCESS);
			process_oom_sort(oom_of_process, tasks, NUM_OF_PROCESS);

			lowmem_print(1,"List of active processes\n");

			for (k = 0; (k < (NUM_OF_PROCESS)) && (tasks[k] != NULL); k++) {
				lowmem_print(1,"Process %d '%s': oom_score_adj(%d), size(%ld kB), pid(%d)\n",
					k, tasks[k]->comm, oom_of_process[k], (get_mm_rss(tasks[k]->mm))*(long)(PAGE_SIZE / 1024), tasks[k]->pid);
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
		/* give the system time to free up the memory */
		msleep_interruptible(20);
	} else
		rcu_read_unlock();

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
module_param_string(process_to_kill, process_to_kill, TASK_COMM_LEN, S_IRUGO | S_IWUSR);
module_param_named(minfree_configuration, minfree_configuration, int, S_IRUGO | S_IWUSR);
module_param_named(adaptive_LMK, adaptive_LMK, int, S_IRUGO | S_IWUSR);
module_param_named(test_lmk_count, test_lmk_count, long, S_IRUGO);
module_param_named(test_running_count, test_running_count, long, S_IRUGO);
module_param_cb(show_services_list, &lowmem_ops_services, NULL, 0644);
module_param_cb(show_processes_list, &lowmem_ops_processes, NULL, 0644);

module_init(lowmem_init);
module_exit(lowmem_exit);

MODULE_LICENSE("GPL");

