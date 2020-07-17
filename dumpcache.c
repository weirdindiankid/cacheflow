#include <asm/current.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/pfn.h>
#include <linux/proc_fs.h>
#include <linux/rmap.h>
#include <linux/seq_file.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>

/* Global Defines */
#define CACHESETS_TO_WRITE 2048
#define L2_SIZE 2*1024*1024
#define MODNAME "dumpcache"
#define WAYS 16

/* Command to access the configuration interface */
#define DUMPCACHE_CMD_CONFIG _IOW(0, 0, unsigned long)
/* Command to initiate a cache dump */
#define DUMPCACHE_CMD_SNAPSHOT _IOW(0, 1, unsigned long)

#define FULL_ADDRESS 0

#pragma GCC push_options
#pragma GCC optimize ("O0")
// Might need this
//pragma GCC pop_options
/* Struct representing a single cache line - each cacheline struct is 68 bytes */
struct cache_line
{
	pid_t pid;
	uint64_t addr;
};

struct cache_set
{
	struct cache_line cachelines[16];
};

struct cache_sample {
	struct cache_set sets[CACHESETS_TO_WRITE];
};

/* Global variables */

/* Unfortunately this platform has two apettures for DRAM, with a
 * large hole in the middle. Here is what the address space loops like
 * when the kernel is booted with mem=2560 (2.5 GB). 
 * 
 * 0x080000000 -> 0x0fedfffff:   Normal memory (aperture 1)
 * 0x0fee00000 -> 0x0ffffffff:   Cache buffer, part 1, size = 0x1200000 (aperture 1)
 * 0x100000000 -> 0x1211fffff:   Normal memory (aperture 2)
 * 0x121200000 -> 0x17fffffff:   Cache buffer, part 2, size = 0x5ee00000 (aperture 2) 
 */

/* This vaiable is to keep track of the current buffer in use by the
 * module. It must be reset explicitly to prevent overwriting existing
 * data. */

#define CACHE_BUF_BASE1 0x0fee00000UL
#define CACHE_BUF_BASE2 0x121200000UL

#define CACHE_BUF_END1 0x0fee00000UL

//#define CACHE_BUF_END1 0x100000000UL
#define CACHE_BUF_END2 0x180000000UL

#define CACHE_BUF_SIZE1 (CACHE_BUF_END1 - CACHE_BUF_BASE1)
#define CACHE_BUF_SIZE2 (CACHE_BUF_END2 - CACHE_BUF_BASE2)

#define CACHE_BUF_COUNT1 (CACHE_BUF_SIZE1 / sizeof(struct cache_sample))
#define CACHE_BUF_COUNT2 (CACHE_BUF_SIZE2 / sizeof(struct cache_sample))

#define DUMPCACHE_CMD_VALUE_WIDTH  16 
#define DUMPCACHE_CMD_VALUE_MASK   ((1 << DUMPCACHE_CMD_VALUE_WIDTH) - 1)
#define DUMPCACHE_CMD_VALUE(cmd)		\
	(cmd & DUMPCACHE_CMD_VALUE_MASK)

/* Command to set the current buffer number */
#define DUMPCACHE_CMD_SETBUF_SHIFT           (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 1))

/* Command to retrievet the current buffer number */
#define DUMPCACHE_CMD_GETBUF_SHIFT           (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 2))

/* Command to enable/disable buffer autoincrement */
#define DUMPCACHE_CMD_AUTOINC_EN_SHIFT       (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 3))
#define DUMPCACHE_CMD_AUTOINC_DIS_SHIFT      (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 4))

/* Command to enable/disable address resolution */
#define DUMPCACHE_CMD_RESOLVE_EN_SHIFT       (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 5))
#define DUMPCACHE_CMD_RESOLVE_DIS_SHIFT      (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 6))

/* Command to enable/disable snapshot timestamping */
#define DUMPCACHE_CMD_TIMESTAMP_EN_SHIFT       (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 7))
#define DUMPCACHE_CMD_TIMESTAMP_DIS_SHIFT      (1 << (DUMPCACHE_CMD_VALUE_WIDTH + 8))

static uint32_t cur_buf = 0;
static unsigned long flags;

/* Beginning of cache buffer in aperture 1 */
static struct cache_sample * __buf_start1 = NULL;

/* Beginning of cache buffer in aperture 2 */
static struct cache_sample * __buf_start2 = NULL;

/* Pointer to buffer currently in use. */
static struct cache_sample * cur_sample = NULL;

//static struct vm_area_struct *cache_set_buf_vma;
static int dump_all_indices_done;

//spinlock_t snap_lock = SPIN_LOCK_UNLOCK;
static DEFINE_SPINLOCK(snap_lock);

static bool rmap_one_func(struct page *page, struct vm_area_struct *vma, unsigned long addr, void *arg);
static void (*rmap_walk_func) (struct page *page, struct rmap_walk_control *rwc) = NULL;

/* Function prototypes */
static int dumpcache_open (struct inode *inode, struct file *filp);
static int dump_index(int index, struct cache_set* buf);
static int dump_all_indices(void);

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return c_start(m, pos);
}

static void c_stop(struct seq_file *m, void *v) {}

void cpu_stall (void * info)
{
	(void)info;
	spin_lock(&snap_lock);
	spin_unlock(&snap_lock);
}

static int c_show(struct seq_file *m, void *v)
{

	/* Make sure that the buffer has the right size */
	m->size = sizeof(struct cache_sample) + 32;
	m->buf = kvmalloc(sizeof(struct cache_sample) + 32, GFP_KERNEL);;
	
	/* Read buffer into sequential file interface */
	if (seq_write(m, cur_sample, sizeof(struct cache_sample)) != 0) {
		pr_info("Seq write returned non-zero value\n");
	}

	return 0;
}

/* This function returns a pointr to the ind-th sample in the
 * buffer. */
static inline struct cache_sample * sample_from_index(uint32_t ind)
{
	if (ind < CACHE_BUF_COUNT1)
		return &__buf_start1[ind];

	else if (ind < CACHE_BUF_COUNT1 + CACHE_BUF_COUNT2)
		return &__buf_start2[ind - CACHE_BUF_COUNT1];

	else
		return NULL;
}

static int acquire_snapshot(void)
{
	int processor_id;
	struct cpumask cpu_mask;
	
	/* Prepare cpu mask with all CPUs except current one */
	processor_id = get_cpu();
	cpumask_copy(&cpu_mask, cpu_online_mask);
	cpumask_clear_cpu(processor_id, &cpu_mask); //processor_id, &cpu_mask);
	
	/* Acquire lock to spin other CPUs */
	spin_lock(&snap_lock);
	preempt_disable();
	
	/* Critical section! */
	on_each_cpu_mask(&cpu_mask, cpu_stall, NULL, 0);

	/* Perform cache snapshot */
	dump_all_indices();
	
	preempt_enable();
	spin_unlock(&snap_lock);
	put_cpu();

	/* Figure out if we need to increase the buffer pointer */
	if (flags & DUMPCACHE_CMD_AUTOINC_EN_SHIFT) {
		cur_buf += 1;

		if (cur_buf >= CACHE_BUF_COUNT1 + CACHE_BUF_COUNT2) {
			cur_buf = 0;
		}

		/* Set the pointer to the next available buffer */
		cur_sample = sample_from_index(cur_buf);
	}
	
	return 0;
}

static int dumpcache_config(unsigned long cmd)
{
	/* Set the sample buffer accoridng to what passed from user
	 * space */
	if(cmd & DUMPCACHE_CMD_SETBUF_SHIFT) {
		uint32_t val = DUMPCACHE_CMD_VALUE(cmd);

		if(val >= CACHE_BUF_COUNT1 + CACHE_BUF_COUNT2)
			return -ENOMEM;
		
		cur_buf = val;
		cur_sample = sample_from_index(val);	       
	}

	if (cmd & DUMPCACHE_CMD_GETBUF_SHIFT) {
		return cur_buf;
	}

	if (cmd & DUMPCACHE_CMD_AUTOINC_EN_SHIFT) {
		flags |= DUMPCACHE_CMD_AUTOINC_EN_SHIFT;
	} else if (cmd & DUMPCACHE_CMD_AUTOINC_DIS_SHIFT) {
		flags &= ~DUMPCACHE_CMD_AUTOINC_EN_SHIFT;
	}

	if (cmd & DUMPCACHE_CMD_RESOLVE_EN_SHIFT) {
		flags |= DUMPCACHE_CMD_RESOLVE_EN_SHIFT;		
	} else if (cmd & DUMPCACHE_CMD_RESOLVE_DIS_SHIFT) {
		flags &= ~DUMPCACHE_CMD_RESOLVE_EN_SHIFT;
	}	

	return 0;
}

/* The IOCTL interface of the proc file descriptor is used to pass
 * configuration commands */
static long dumpcache_ioctl(struct file *file, unsigned int ioctl, unsigned long arg)
{
	long err;
	
	switch (ioctl) {
	case DUMPCACHE_CMD_CONFIG:
		err = dumpcache_config(arg);
		break;

	case DUMPCACHE_CMD_SNAPSHOT:
		err = acquire_snapshot();
		break;
		
	default:
		pr_err("Invalid command: 0x%08x\n", ioctl);
		err = -EINVAL;
		break;
	}
	
	return err;
}


static const struct seq_operations dumpcache_seq_ops = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show
};
	
/* ProcFS entry setup and definitions  */
static const struct file_operations dumpcache_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = dumpcache_ioctl,
	.compat_ioctl = dumpcache_ioctl,
	.open    = dumpcache_open,
	.read    = seq_read,
	.llseek	 = seq_lseek,
	.release = seq_release
};

static inline void asm_flush_cache(void) {
    asm volatile(
        "MCR p15, 0, r0, c7, c5, 0\t\n"
        "MCR p15, 0, Rd, c7, c6, 0"
    );
}

// Ramindex operation
// 4.3.64 in ARM Cortex-A57 MPCore Processor Technical Reference Manual
static inline void asm_ramindex_mcr(u32 ramindex)
{
	asm volatile(
	    "sys #0, c15, c4, #0, %0\t\n"
	    "dsb sy\t\n"
	    "isb" :: "r" (ramindex));
}


// reading from DL1DATA0_EL1
// 4.3.63 in ARM Cortex-A57 MPCore Processor Technical Reference Manual
static inline void asm_ramindex_mrc(u32 *dl1data, u8 sel)
{
	if (sel & 0x01) asm volatile("mrs %0,S3_0_c15_c1_0" : "=r"(dl1data[0]));
	if (sel & 0x02) asm volatile("mrs %0,S3_0_c15_c1_1" : "=r"(dl1data[1]));
	if (sel & 0x04) asm volatile("mrs %0,S3_0_c15_c1_2" : "=r"(dl1data[2]));
	if (sel & 0x08) asm volatile("mrs %0,S3_0_c15_c1_3" : "=r"(dl1data[3]));
}


// Get Tag of L2 cache entry at (index,way)
// Tag bank select ignored, 2MB L2 cache assumed
static inline void get_tag(u32 index, u32 way, u32 *dl1data)
{
	u32 ramid    = 0x10;
	u32 ramindex = (ramid << 24) + (way << 18) + (index << 6);
	asm_ramindex_mcr(ramindex);
	asm_ramindex_mrc(dl1data, 0x01);

	// Check if MOESI state is invalid, and if so, zero out the address
	if (((*dl1data) & 0x03UL) == 0) {
		*dl1data = 0;
		return;
	}
	// Isolate the tag
	*dl1data &= ~(0x03UL);
	*dl1data <<= 12;
	*dl1data |= (index << 5);
}

bool rmap_one_func(struct page *page, struct vm_area_struct *vma, unsigned long addr, void *arg)
{
        
	struct mm_struct* mm;
	struct task_struct* ts;
	struct process_data
	{
		pid_t pid;
		uint64_t addr;
	};

	((struct process_data*) arg)->addr = 0;

	// Check if mm struct is null
	mm = vma->vm_mm;
	if (!mm) {
		((struct process_data*) arg)->pid = (pid_t)99999;
		return true;
	}

	// Check if task struct is null
	ts = mm->owner;
	if (!ts) {
		((struct process_data*) arg)->pid = (pid_t)99999;
		return true;
	}

	// If pid is 1, continue searching pages
	if ((ts->pid) == 1) {
		((struct process_data*) arg)->pid = (ts->pid);
		return true;
	}

	// *Probably* the correct pid
	((struct process_data*) arg)->pid = (ts->pid);
	((struct process_data*) arg)->addr = addr;
	return false;
}

int done_func(struct page *page)
{
	return 1;
} 


bool invalid_func(struct vm_area_struct *vma, void *arg)
{
	struct process_data
	{
		pid_t pid;
		uint64_t addr;
	};

	((struct process_data*) arg)->pid = (pid_t)99999;
	return false;
} 

static int __dump_index_resolve(int index, struct cache_set* buf)
{
	int way;
	u32 physical_address;
	struct page* derived_page;
	struct rmap_walk_control rwc;
	struct rmap_walk_control * rwc_p;

	/* This will be used to invoke address resolution */
	struct cache_line process_data_struct;
		
	// Instantiate rmap walk control struct
	rwc.arg = &process_data_struct;
	rwc.rmap_one = rmap_one_func;
	rwc.done = NULL; //done_func;
	rwc.anon_lock = NULL;
	rwc.invalid_vma = invalid_func;
	rwc_p = &rwc;

	for (way = 0; way < WAYS; way++) {
		get_tag(index, way, &physical_address);
		if (!physical_address)
			continue;

		derived_page = phys_to_page(((u64)physical_address << 1));

		// Initalize struct
		(buf->cachelines[way]).pid = 0; //process_data_struct->pid;// = 0;
		(buf->cachelines[way]).addr = ((u64)physical_address << 1); //process_data_struct->addr;// = 0;

		/* Reset address */
		process_data_struct.addr = 0;
		
	        // This call populates the struct in rwc struct
		rmap_walk_func(derived_page, rwc_p);

		// Fill cacheline struct with values obtained from rmap_walk_func
		(buf->cachelines[way]).pid = process_data_struct.pid;
		if(process_data_struct.addr != 0) {
#if FULL_ADDRESS == 0
			(buf->cachelines[way]).addr = process_data_struct.addr;
#else
			(buf->cachelines[way]).addr = process_data_struct.addr | (((u64)physical_address << 1) & 0xfff);
#endif			
		}
	}
       
	return 0;
}

static int __dump_index_noresolve(int index, struct cache_set* buf)
{
	int way;
	u32 physical_address;

	for (way = 0; way < WAYS; way++) {
		get_tag(index, way, &physical_address);
		if (!physical_address)
			continue;
		
		// Initalize struct
		(buf->cachelines[way]).pid = 0; //process_data_struct->pid;// = 0;
		(buf->cachelines[way]).addr = ((u64)physical_address); //process_data_struct->addr;// = 0;
		
	}
       
	return 0;
}

/* Invoke a smaller-footprint function in case address resolution has
 * not been requested */
static int dump_index(int index, struct cache_set* buf)
{
	if (flags & DUMPCACHE_CMD_RESOLVE_EN_SHIFT)
		return __dump_index_resolve(index, buf);
	else
		return __dump_index_noresolve(index, buf);
}

static int dump_all_indices(void) {
	int i = 0;
	for (i = 0; i < CACHESETS_TO_WRITE; i++) {
		if (dump_index(i, &cur_sample->sets[i]) == 1){
			//printk(KERN_INFO "Error dumping index: %d", i);
			return 1;
		}
	}
	return 0;
}

/* ProcFS interface definition */
static int dumpcache_open(struct inode *inode, struct file *filp)
{
	int ret;

	if (!cur_sample) {
		pr_err("Something went horribly wrong. Invalid buffer.\n");
		return -EBADFD;
	}
	
	ret = seq_open(filp, &dumpcache_seq_ops);
	return ret;
}

int init_module(void)
{
	//printk(KERN_INFO "dumpcache module is loaded\n");
	dump_all_indices_done = 0;

	pr_info("Initializing SHUTTER. Entries: Aperture1 = %ld, Aperture2 = %ld\n",
	       CACHE_BUF_COUNT1, CACHE_BUF_COUNT2);

	/* Resolve the rmap_walk_func required to resolve physical
	 * address to virtual addresses */
	if (!rmap_walk_func) {
		/* Attempt to find symbol */
		preempt_disable();
		mutex_lock(&module_mutex);
		rmap_walk_func = (void*) kallsyms_lookup_name("rmap_walk_locked");
		mutex_unlock(&module_mutex);
		preempt_enable();

		/* Have we found a valid symbol? */
		if (!rmap_walk_func) {
			pr_err("Unable to find rmap_walk symbol. Aborting.\n");
			return -ENOSYS;
		}
	}
	
	/* Map buffer apertures to be accessible from kernel mode */
	__buf_start1 = (struct cache_sample *) ioremap_nocache(CACHE_BUF_BASE1, CACHE_BUF_SIZE1);
	__buf_start2 = (struct cache_sample *) ioremap_nocache(CACHE_BUF_BASE2, CACHE_BUF_SIZE2);

	/* Check that we are all good! */
	if(/*!__buf_start1 ||*/ !__buf_start2) {
		pr_err("Unable to io-remap buffer space.\n");
		return -ENOMEM;
	}
	
	/* Set default flags, counter, and current sample buffer */
	flags = 0;
	cur_buf = 0;
	cur_sample = sample_from_index(0);
	
	/* Setup proc interface */
	proc_create(MODNAME, 0644, NULL, &dumpcache_fops);
	return 0;
}

void cleanup_module(void)
{
	//printk(KERN_INFO "dumpcache module is unloaded\n");
	if(__buf_start1) {
		iounmap(__buf_start1);
		__buf_start1 = NULL;
	}

	if(__buf_start2) {
		iounmap(__buf_start2);
		__buf_start2 = NULL;		
	}	
		
	remove_proc_entry(MODNAME, NULL);
}

#pragma GCC pop_options
MODULE_LICENSE("GPL");
