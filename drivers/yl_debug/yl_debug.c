#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/list.h>
//#include <mach/subsystem_restart.h>
#include <soc/qcom/subsystem_restart.h>
//#include <mach/msm_iomap.h>
//#include <mach/gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/clk.h>
#include <linux/sched.h>
#include <linux/wakelock.h>
#include <linux/io.h>
#include <linux/proc_fs.h> //add by youjin for cpb  version 

/*add for pcb version*/
static struct pcb_version pcb_v;

struct pcb_version __yulong_pl_pcb_version__(void){
    return pcb_v;
}
/*add end*/


struct mem_test {	
	struct list_head mlist;
	char * buf;
	int count;
};

struct yl_debug_data {
	struct timer_list timer;
	struct list_head mlist;
	spinlock_t slock;
	struct mutex mlock; 
	struct completion done;
	char* memblk;
	struct kmem_cache *myslabobj;
	struct wake_lock mwake_lock;
};

struct yl_debug_data *my_data = NULL;

#define MEMBLK_LEN 64
static char crash_str[32] = "";
extern void clock_debug_print_enabled(void);
extern void	emerg_pet_watchdog(void);
extern int yldebug_subsystem_restart(const char *name);

void irq_simulate(unsigned long arg)
{
	struct yl_debug_data *data;

	data = (struct yl_debug_data *)arg;
	spin_lock(&data->slock);
	printk(KERN_ERR "before complete\n");
	complete(&data->done);
	printk(KERN_ERR "after complete\n");
	mod_timer(&data->timer, jiffies + 5 * HZ);
	spin_unlock(&data->slock);
}

void sleep_timer_fn(unsigned long arg)
{
	struct yl_debug_data *data;

	data = (struct yl_debug_data *)arg;
	printk(KERN_ERR "I want to sleep a while, but you can't see this message instead schedule warnings flooding the screen\n");
	ssleep(5);
	mod_timer(&data->timer, jiffies + 5 * HZ);
}


static int set_crash(const char *buf, struct kernel_param *kp)
{
	printk(KERN_DEBUG"%s: +-\r\n", __func__);
	if(!strncmp(buf, "point_null",strlen(buf)-1))
	{
		my_data->memblk = NULL;
		*my_data->memblk =0xEF;
		
	}else if(!strncmp(buf, "overwrite",strlen(buf)-1))
	{
		my_data->memblk = kmalloc(MEMBLK_LEN,GFP_KERNEL);
		memset(my_data->memblk,0,MEMBLK_LEN<<1);		
	}else if(!strncmp(buf, "call_panic",strlen(buf)-1))
	{
		panic("yulong panic!");
	} else if(!strncmp(buf, "schedule_in_atomic",strlen(buf)-1)) {		
		setup_timer(&my_data->timer, sleep_timer_fn, (unsigned long)my_data);
		mod_timer(&my_data->timer, jiffies + 5 * HZ);
	} else if(!strncmp(buf, "interrupt_before_initialize",strlen(buf)-1)) {
		/* As I don't know what IRQ we support, I choose timer for simulation */
		setup_timer(&my_data->timer, irq_simulate, (unsigned long)my_data);
		mod_timer(&my_data->timer, jiffies);
	
		/* do something else
		 * interrupt may happen before data is full initialized
		 * */
		msleep(10);
		init_completion(&my_data->done);	
	} else if(!strncmp(buf, "uninit-lock",strlen(buf)-1)) {
		printk(KERN_ERR "uninit-lock %s\n", buf);
		wake_lock(&my_data->mwake_lock);
	} else if (!strncmp(buf, "wdog", 4)) {
		pr_emerg("Generating a wdog bark!\n");
		raw_local_irq_disable();
		while (1)
			;
	} else if (!strncmp(buf, "dabort", 6)) {
		pr_emerg("Generating a data abort exception!\n");
		*(unsigned int *)0x0 = 0x0;
	} else if (!strncmp(buf, "pabort", 6)) {
		pr_emerg("Generating a prefetch abort exception!\n");
		((void (*)(void))0x0)();
	} else if (!strncmp(buf, "undef", 5)) {
		pr_emerg("Generating a undefined instruction exception!\n");
		BUG();
	} else if (!strncmp(buf, "bushang", 7)) {
		void __iomem *p;
		pr_emerg("Generating Bus Hang!\n");
		p = ioremap_nocache(0xFC4E0810, 32);
	//	*(unsigned int *)p = *(unsigned int *)p;
		mb();
		pr_info("*p = %x\n", *(unsigned int *)p);
		pr_emerg("Clk may be enabled.Try again if it reaches here!\n");
	} else if (!strncmp(buf, "dblfree", 7)) {
		void *p = kmalloc(sizeof(int), GFP_KERNEL);
		kfree(p);
		msleep(1000);
		kfree(p);
	} else if (!strncmp(buf, "danglingref", 11)) {
		unsigned int *p = kmalloc(sizeof(int), GFP_KERNEL);
		kfree(p);
		*p = 0x1234;
	} else if (!strncmp(buf, "lowmem", 6)) {
		int i = 0;
		pr_emerg("Allocating memory until failure!\n");
		while (kmalloc(128*1024, GFP_KERNEL))
			i++;
		pr_emerg("Allocated %d KB!\n", i*128);

	} else if (!strncmp(buf, "memcorrupt", 10)) {
		int *ptr = kmalloc(sizeof(int), GFP_KERNEL);
		*ptr++ = 4;
		*ptr = 2;
		panic("MEMORY CORRUPTION");
	} else {
		printk(KERN_ERR "unknown command %s\n", buf);
		return -EINVAL;
	}

	sprintf(crash_str, "%s", buf);
	return 0;
}

static int get_crash(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "%s ", crash_str);
}

module_param_call(crash, set_crash, get_crash,
	NULL, 0644);

static int set_mem_alloc(const char *buf, struct kernel_param *kp)
{
	char *start=(char *)buf;
	int size;
	int count;
	int i;
	struct mem_test *pos;
	
	while (*start == ' ')
		start++;
	
	size = simple_strtoul(start, &start, 10);
		
	while (*start == ' ')
		start++;

	count=simple_strtoul(start, &start, 10);
	printk("alloc:size=%d,count=%d\n",size,count);
	
	for(i=0;i<count;i++)
	{
		pos=kmalloc(sizeof(struct mem_test),GFP_KERNEL);
		pos->buf=kmalloc(size, GFP_KERNEL);
		//zhangchangchun
		if(pos->buf == NULL)
			{
				pr_info("malloc fail count = %d,size = %d \n",count,size);
				continue;
			}
			memset(pos->buf,0x55,size);//zhangchangchun
		pos->count=size;
		
		mutex_lock(&my_data->mlock);
		list_add_tail(&pos->mlist, &my_data->mlist);		
		mutex_unlock(&my_data->mlock);
	}
	
	return 0;
}

module_param_call(mem_alloc, set_mem_alloc, NULL,
	NULL, 0200);

static int set_mem_free(const char *buf, struct kernel_param *kp)
{
	char *start=(char *)buf;
	char *buf0; //zhangchangchun
	int ret;//zhangchangchun
	int size;
	int count;	
	struct mem_test *pos;	
	struct mem_test *npos;
	
	while (*start == ' ')
		start++;
	
	size = simple_strtoul(start, &start, 10);
		
	while (*start == ' ')
		start++;
		
		buf0 = kmalloc(size,GFP_KERNEL);//zhangchangchun
		memset(buf0,0x55,size);//zhangchangchun

	count=simple_strtoul(start, &start, 10);
	
	printk("free:size=%d,count=%d\n",size,count);
	if(count<=0)
	{
		printk("count=%d,do nothing\n",count);		
		return 0;
	}
	
	mutex_lock(&my_data->mlock);
	list_for_each_entry_safe(pos,npos, &my_data->mlist, mlist)
	if(pos && (pos->count == size))	
	{
		list_del(&pos->mlist);	
		 ret =memcmp(pos->buf,buf0,size);//zhangchangchun
		 if(ret != 0){
		 	  pr_info("#### zcc mem_alloc buf already changed by other application \n");
		 	  panic("mem_alloc buf changed \n");
		 	}	
		kfree(pos->buf);
		pos->buf = NULL;
		kfree(pos);		
		
		count--;
		if(count==0)
			break;
	}	
	mutex_unlock(&my_data->mlock);
	
	printk("out:free:size=%d,count=%d\n",size,count);
	
	kfree(buf0);  //zhangchangchun
	buf0 = NULL;  //zhangchangchun
	
	return 0;
}

module_param_call(mem_free, set_mem_free, NULL,
	NULL, 0200);

static int set_slab(const char *buf, struct kernel_param *kp)
{
	if(!my_data->myslabobj)
		my_data->myslabobj=kmem_cache_create("my_slab_obj",MEMBLK_LEN,0,SLAB_HWCACHE_ALIGN,NULL);
	
	if(!my_data->memblk)
		my_data->memblk=kmem_cache_alloc(my_data->myslabobj,GFP_KERNEL);

	memset(my_data->memblk,0xFE,MEMBLK_LEN);
		
	if(!strncmp(buf, "leak",strlen(buf)-1))
	{
		printk("Do test case:%s! @%d\n",buf,__LINE__);
		kmem_cache_destroy(my_data->myslabobj);
	}
	else if(!strncmp(buf, "refree",strlen(buf)-1))
	{
		printk("Do test case:%s! @%d\n",buf,__LINE__);
		kfree(my_data->memblk);
		kfree(my_data->memblk);
		kmem_cache_destroy(my_data->myslabobj);
	}
	else if(!strncmp(buf, "abort",strlen(buf)-1))
	{	
		printk("Do test case:%s! @%d\n",buf,__LINE__);
		kfree(my_data->memblk);
		memset(my_data->memblk,0x12,MEMBLK_LEN);
		kmem_cache_destroy(my_data->myslabobj);
	}
	else if(!strncmp(buf, "overwrite",strlen(buf)-1))
	{
		printk("Do test case:%s! @%d\n",buf,__LINE__);	
		memset(my_data->memblk,0,MEMBLK_LEN<<1);		
		kfree(my_data->memblk);
		kmem_cache_destroy(my_data->myslabobj);
	}
	else printk("Unknown test case:%s!\n",buf);
	my_data->myslabobj=NULL;
	my_data->memblk=NULL;
	return 0;
}
module_param_call(slab, set_slab, NULL,
	NULL, 0200);

static int set_subsystem(const char *buf, struct kernel_param *kp)
{
	pr_info("%s[%d] with arg %s by %s[%d]\n",
			__func__,__LINE__, buf, current->comm, current->pid);
	if(!strncmp(buf, "modem",strlen(buf)-1))
	{
		yldebug_subsystem_restart("modem");
	}
	else if(!strncmp(buf, "lpass",strlen(buf)-1))
	{
		yldebug_subsystem_restart("lpass");
	}
	else if(!strncmp(buf, "dsps",strlen(buf)-1))
	{
		yldebug_subsystem_restart("dsps");
	}
	else if(!strncmp(buf, "wcnss",strlen(buf)-1))
	{
		yldebug_subsystem_restart("wcnss");
	}
	else if(!strncmp(buf, "adsp",strlen(buf)-1))
	{
		yldebug_subsystem_restart("adsp");
	}
	else if(!strncmp(buf, "venus",strlen(buf)-1))
	{
		yldebug_subsystem_restart("venus");
	}
	else if(!strncmp(buf, "modem_debug",strlen(buf)-1))
	{
		subsystem_restart("modem");
	}
	else if(!strncmp(buf, "lpass_debug",strlen(buf)-1))
	{
		subsystem_restart("lpass");
	}
	else if(!strncmp(buf, "dsps_debug",strlen(buf)-1))
	{
		subsystem_restart("dsps_debug");
	}
	else if(!strncmp(buf, "wcnss_debug",strlen(buf)-1))
	{
		subsystem_restart("wcnss");
	}
	else if(!strncmp(buf, "adsp_debug",strlen(buf)-1))
	{
		subsystem_restart("adsp");
	}
	else if(!strncmp(buf, "venus_debug",strlen(buf)-1))
	{
		subsystem_restart("venus");
	}
	else printk("Unknown subsystem!Please check!");
	return 0;
}
module_param_call(subsystem, set_subsystem, NULL,
	NULL, 0200);

static int do_dump(const char *buf, struct kernel_param *kp)
{
	if(!strncmp(buf, "regulators",strlen(buf)-1))
	{
		//regulatorstatusprint_yl(0);
	}
	else if(!strncmp(buf, "gpios",strlen(buf)-1))
	{
		//gpiolib_show_yl(0);
	}
	else if(!strncmp(buf, "clocks",strlen(buf)-1))
	{
		//clock_debug_print_enabled();
	}
	else printk("Unknown subsystem!Please check!");
	return 0;
}

static int get_threads_stack(char *buffer, struct kernel_param *kp)
{
	struct task_struct *g;
	struct task_struct *p;
	unsigned task_state;
	static const char stat_nam[] = "RSDTtZX";

	//emerg_pet_watchdog();
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if((p->state != TASK_RUNNING) && (p->state != TASK_UNINTERRUPTIBLE))
			continue;
		task_state = p->state ? __ffs(p->state) + 1 : 0;
		printk(KERN_INFO "\nName:%-13.13s %c PID: %5d parent-PID: %5d task-prio: %4d\n",
			     p->comm, task_state >= sizeof(stat_nam) ? '?' : stat_nam[task_state], p->pid, p->parent->pid, p->prio);

		show_stack(p, NULL);
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);

	return 0;
}

module_param_call(dump, do_dump, get_threads_stack,
	NULL, 0644);

static ssize_t pcb_ver_read(struct file *file, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	u16 data_len = 0;
	s32 ret;
	u8 buf[32] = {0};
	
	data_len = scnprintf(buf, sizeof(buf), "%d -- %s\n",
			pcb_v.pcb_id, pcb_v.proj_id);
			
	pr_info("%s[%d] ===> %s\n", __func__, __LINE__, buf);
	ret = simple_read_from_buffer(user_buf, count, ppos,
		buf, data_len);
		
		
	return ret;

}

static const struct file_operations pcb_ver_proc_fops = {
	//.write = pcb_ver_write,
	.read = pcb_ver_read,
	//.open = simple_open,
	.owner = THIS_MODULE,
};

static struct proc_dir_entry *pcb_ver_proc_entry;
static int __init get_yulong_pcb_version(void){
    int rc;
	unsigned int pcb_id;
	const char *proj_id;
	struct device_node *of_yulong_node;

	of_yulong_node = of_find_compatible_node(NULL, NULL, "qcom,msm8952-mtp");
	if(!of_yulong_node){
	    pr_err("%s: Failed to find qcom, msm8952-mtp\n", __func__);
		return -ENODEV;
	}

	rc = of_property_read_u32(of_yulong_node, "yulong,pcb-id", &pcb_id);
	if(rc){
	    pr_err("%s: Failed to find pcb-id property in qcom, msm8952-mtp node %d\n", __func__, rc);
	    return rc;
	}

	rc = of_property_read_string(of_yulong_node, "yulong,project-id",&proj_id);
	if(rc){
	    pr_err("%s: Failed to find project-id property in qcom, msm8952-mtp node %d\n", __func__, rc);
	}
	pcb_v.pcb_id = pcb_id;
	pcb_v.proj_id = proj_id;
	pr_err("yulong pcb_version:%d\n",pcb_v.pcb_id);
	pr_err("yulong project_version:%s\n",pcb_v.proj_id);
	//create_proc_read_entry("pcb_version",0,NULL,read_proc_data,(void *)&pcb_v);
	pcb_ver_proc_entry = proc_create("pcb_ver",
			S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP,
			pcb_ver_proc_entry,
			&pcb_ver_proc_fops);
	if (pcb_ver_proc_entry == NULL) {
		pr_err("Couldn't create pcb_ver proc entry!");
	}	
	printk("test pcb_id:%d; project id:%s\n", YL_CURT_PCB_VER, YL_CURT_PROJ_ID);

	return 0;
}
/* add end */

/* added by wangyadong for get device info begin */
static char buff[512] = { 0 };
struct mutex main_device_mutex;
int all_info_len = 0;
int get_device_info(char* buf)
{
  //printk("enter %s\n", __FUNCTION__);
  int len = 0;
  if (buf == NULL)
  {
    printk("error buf is NULL\n");
    return -1;
  }
  len = strlen(buf);

  if (len <= 0 || len > 32)
    {
      printk("get_device_info error with len %d\n", len);
      return -1;
    }

  all_info_len += len;
  mutex_lock(&main_device_mutex);
  if(all_info_len < 512)
  {
      strcat(buff, buf);
  }
  else
  {
      printk("get_device_info : all_info_len is too long!! \n");
  }
  //printk("buf string %s\n", buff);
  mutex_unlock(&main_device_mutex);
  return 0;
}

static ssize_t main_devices_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
  printk("enter %s\n", __FUNCTION__);
  return sprintf(buf, buff);
}

static struct kobj_attribute all_main_devices_attr =
{
  .attr =
  {
     .name = "devs",
     .mode = S_IRUGO,
  },
  .show = main_devices_show,
};
static struct attribute *main_devices_attributes[] =
  { &all_main_devices_attr.attr, NULL };

static struct attribute_group main_devices_group =
{
  .attrs = main_devices_attributes
};
static void __init devices_sysfs_init(void)
{
  struct kobject *properties_kobj;
  int ret = 0;
  properties_kobj = kobject_create_and_add("main_devices", NULL);
  if (properties_kobj)
  ret = sysfs_create_group(properties_kobj, &main_devices_group);
  if (ret < 0)
  printk("Create virtual key properties failed!\n");
  mutex_init(&main_device_mutex);
}
/* added by wangyadong for get device info end */

static int yl_debug_init(void)
{
	my_data = kmalloc(sizeof(*my_data), GFP_KERNEL);
	if (!my_data)
		return -ENOMEM;
	
	get_yulong_pcb_version();//add for pcb version
	INIT_LIST_HEAD(&my_data->mlist);
	spin_lock_init(&my_data->slock);
	mutex_init(&my_data->mlock);
	my_data->myslabobj=NULL;
	my_data->memblk=NULL;

	devices_sysfs_init();/* added by wangyadong for get device info */	
	printk(KERN_ALERT "%s\n", __func__);
	return 0;
}

static void yl_debug_exit(void)
{
	struct mem_test *pos;	
	struct mem_test *npos;

	printk(KERN_ALERT "%s\n", __func__);

	if (!my_data)
		return;

	mutex_lock(&my_data->mlock);
	list_for_each_entry_safe(pos,npos, &my_data->mlist, mlist)
	if(pos)	
	{
		list_del(&pos->mlist);		
		kfree(pos->buf);
		pos->buf = NULL;
		kfree(pos);	
	}
	mutex_unlock(&my_data->mlock);

	del_timer(&my_data->timer);
	mutex_destroy(&my_data->mlock);
	kfree(my_data);
}

module_init(yl_debug_init);
module_exit(yl_debug_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("yulong debug driver");
MODULE_AUTHOR("YULONG");