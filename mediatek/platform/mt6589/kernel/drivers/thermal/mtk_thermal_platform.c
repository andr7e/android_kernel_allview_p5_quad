#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/thermal.h>
#include <linux/platform_device.h>
#include <linux/aee.h>
#include <linux/xlog.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>
#include <linux/err.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/bug.h>
#include <linux/workqueue.h>

#include <mach/mtk_thermal_platform.h>
#include <mach/mt_typedefs.h>
#include <mach/mt_storage_logger.h>
#include <mach/mtk_mdm_monitor.h>

#include <mach/mt_thermal.h>

//************************************
// Definition
//************************************

/* Number of CPU CORE */
#if defined(CONFIG_ARCH_MT6589) || defined(CONFIG_ARCH_MT6582)  
#define NUMBER_OF_CORE (4)
#else
#if defined(CONFIG_ARCH_MT6577)
#define NUMBER_OF_CORE (2)
#else
/* CONFIG_ARCH_MT6575 */
#define NUMBER_OF_CORE (1)
#endif 
#endif 

#if defined(CONFIG_MTK_SMART_BATTERY)
// global variable from battery driver...
extern kal_bool gFG_Is_Charging;
#endif

#if defined(CONFIG_ARCH_MT6589)
// get MT6589 GPU loading...
extern unsigned int mt_gpufreq_cur_load(void); 
extern unsigned int mt_gpufreq_cur_freq(void);
#endif

//************************************
// Global Variable
//************************************
static bool enable_ThermalMonitorXlog = false;

static DEFINE_MUTEX(MTM_SYSINFO_LOCK);

//************************************
//  Macro
//************************************
#define THRML_LOG(fmt, args...) \
    do { \
        if (enable_ThermalMonitorXlog) { \
            xlog_printk(ANDROID_LOG_INFO, "THERMAL/MONITOR", fmt, ##args); \
        } \
    } while(0)


#define THRML_ERROR_LOG(fmt, args...) \
    do { \
        xlog_printk(ANDROID_LOG_INFO, "THERMAL/MONITOR", fmt, ##args); \
    } while(0)


//************************************
//  Define
//************************************

//*********************************************
// System Information Monitor
//*********************************************
static mm_segment_t oldfs;

/*
 *  Read Battery Information.
 *
 *  "cat /sys/devices/platform/mt6575-battery/FG_Battery_CurrentConsumption"
 *  "cat /sys/class/power_supply/battery/batt_vol"
 *  "cat /sys/class/power_supply/battery/batt_temp"
 */
static int get_sys_battery_info(char* dev)
{
    int fd;
    int nRet;
    int nReadSize;
    char *pvalue = NULL;
    char buf[64];

    oldfs = get_fs();
    set_fs(KERNEL_DS);
    fd = sys_open(dev, O_RDONLY, 0);
    if (fd < 0)
    {
        THRML_LOG("[get_sys_battery_info] open fail dev:%s fd:%d \n", dev, fd);
        set_fs(oldfs);
        return fd;
    }

    nReadSize = sys_read(fd, buf, sizeof(buf) - 1);
    THRML_LOG("[get_sys_battery_info] nReadSize:%d\n", nReadSize);
    nRet = simple_strtol(buf, &pvalue, 10);

    set_fs(oldfs);
    sys_close(fd);

    return nRet;
}

//*********************************************
// Get Wifi Tx throughput
//*********************************************
static int get_sys_wifi_throughput(char* dev, int nRetryNr)
{
    int fd;
    int nRet;
    int nReadSize;
    int nRetryCnt=0;
    char *pvalue = NULL;
    char buf[64];

    oldfs = get_fs();
    set_fs(KERNEL_DS);

    /* If sys_open fail, it will retry "nRetryNr" times. */
    do {
        fd = sys_open(dev, O_RDONLY, 0);
        if(nRetryCnt > nRetryNr) {
            THRML_LOG("[get_sys_wifi_throughput] open fail dev:%s fd:%d \n", dev, fd);
            set_fs(oldfs);
            return fd;
        }
        nRetryCnt++;
    } while(fd < 0);

    if(nRetryCnt > 1) {
       THRML_LOG("[get_sys_wifi_throughput] open fail nRetryCnt:%d \n", nRetryCnt);
    }

    nReadSize = sys_read(fd, buf, sizeof(buf) - 1);
    THRML_LOG("[get_sys_wifi_throughput] nReadSize:%d\n", nReadSize);
    nRet = simple_strtol(buf, &pvalue, 10);

    set_fs(oldfs);
    sys_close(fd);

    return nRet;
}

//*********************************************
// For get_sys_cpu_usage_info_ex()
//*********************************************

#define CPU_USAGE_CURRENT_FIELD (0)
#define CPU_USAGE_SAVE_FIELD    (1)
#define CPU_USAGE_FRAME_FIELD   (2)

struct cpu_index_st
{
    unsigned long  u[3];
    unsigned long  s[3];
    unsigned long  n[3];
    unsigned long  i[3];
    unsigned long  w[3];
    unsigned long  q[3];
    unsigned long  sq[3]; 
    unsigned long  tot_frme;
    unsigned long  tz; 
    int  usage;
    int  freq;
};

/*
struct gpu_index_st
{
    int  usage;
    int  freq;
};
*/

static struct cpu_index_st cpu_index_list[4];   ///< 4-Core is maximum 
//static struct gpu_index_st gpu_index;

#if defined(CONFIG_ARCH_MT6577)
#define NO_CPU_CORES (2)
static int cpufreqs[NO_CPU_CORES];
static int cpuloadings[NO_CPU_CORES];
#elif defined(CONFIG_ARCH_MT6589) || defined(CONFIG_ARCH_MT6582)
#define NO_CPU_CORES (4)
static int cpufreqs[NO_CPU_CORES];
static int cpuloadings[NO_CPU_CORES];
#endif


#define SEEK_BUFF(x, c)  while(*x != c)x++; \
                            x++;

#define TRIMz_ex(tz, x)   ((tz = (unsigned long long)(x)) < 0 ? 0 : tz)

//*********************************************
// CPU Index
//*********************************************
#if 0
static int get_sys_cpu_usage_info_ex_procstat(void)
{
    int fd;
    int nReadSize;
    char szTempBuf[256];
    char buf[256];
    char *pbuf;
    int nCoreIndex = 0, i;


    oldfs = get_fs();
    set_fs(KERNEL_DS);
    fd = sys_open("/proc/stat", O_RDONLY, 0);

    if (fd < 0)
    {
        THRML_LOG("[get_sys_cpu_usage_info] open fail fd:%d \n", fd);
        set_fs(oldfs); 
        return -1;
    }

    nReadSize = sys_read(fd, buf, sizeof(buf) - 1);
    buf[254] = '\n';
    buf[255] = 0x0;
    set_fs(oldfs);
    sys_close(fd);

    pbuf = buf;
    SEEK_BUFF(pbuf, '\n');
    
    THRML_LOG("[Read Buff]:%s \n", buf);

	for (nCoreIndex = 0; nCoreIndex < NUMBER_OF_CORE; nCoreIndex++)
	{
        int ret = 0;
	
		sprintf(szTempBuf, "cpu%01d %%lu %%lu %%lu %%lu %%lu %%lu %%lu", nCoreIndex);

		/* Get CPU Info */
		if (strncmp(pbuf, "cpu", 3) == 0)
		{
    		ret = sscanf(pbuf, szTempBuf, &cpu_index_list[nCoreIndex].u[CPU_USAGE_CURRENT_FIELD], &cpu_index_list[nCoreIndex].n[CPU_USAGE_CURRENT_FIELD], 
    							          &cpu_index_list[nCoreIndex].s[CPU_USAGE_CURRENT_FIELD], &cpu_index_list[nCoreIndex].i[CPU_USAGE_CURRENT_FIELD], 
    							          &cpu_index_list[nCoreIndex].w[CPU_USAGE_CURRENT_FIELD], &cpu_index_list[nCoreIndex].q[CPU_USAGE_CURRENT_FIELD], 
    							          &cpu_index_list[nCoreIndex].sq[CPU_USAGE_CURRENT_FIELD]);

            SEEK_BUFF(pbuf, '\n');

            THRML_LOG("sscanf = %d, buf = %0x, pbuf = %0x\n", ret, (int) &buf[0], (int) pbuf);
        }
        
		/* Frame */
	    cpu_index_list[nCoreIndex].u[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].u[CPU_USAGE_CURRENT_FIELD] -  
                                                                    cpu_index_list[nCoreIndex].u[CPU_USAGE_SAVE_FIELD];
		cpu_index_list[nCoreIndex].n[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].n[CPU_USAGE_CURRENT_FIELD] -  
                                                                    cpu_index_list[nCoreIndex].n[CPU_USAGE_SAVE_FIELD];
		cpu_index_list[nCoreIndex].s[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].s[CPU_USAGE_CURRENT_FIELD] -  
                                                                    cpu_index_list[nCoreIndex].s[CPU_USAGE_SAVE_FIELD];
		cpu_index_list[nCoreIndex].i[CPU_USAGE_FRAME_FIELD] = TRIMz_ex(cpu_index_list[nCoreIndex].tz, 
                                                                    (cpu_index_list[nCoreIndex].i[CPU_USAGE_CURRENT_FIELD] - 
                                                                     cpu_index_list[nCoreIndex].i[CPU_USAGE_SAVE_FIELD])) ;
		cpu_index_list[nCoreIndex].w[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].w[CPU_USAGE_CURRENT_FIELD] -  
                                                                    cpu_index_list[nCoreIndex].w[CPU_USAGE_SAVE_FIELD];
		cpu_index_list[nCoreIndex].q[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].q[CPU_USAGE_CURRENT_FIELD] -  
                                                                    cpu_index_list[nCoreIndex].q[CPU_USAGE_SAVE_FIELD] ;
		cpu_index_list[nCoreIndex].sq[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].sq[CPU_USAGE_CURRENT_FIELD] -  
                                                                    cpu_index_list[nCoreIndex].sq[CPU_USAGE_SAVE_FIELD];

		/* Total Frame */
		cpu_index_list[nCoreIndex].tot_frme = cpu_index_list[nCoreIndex].u[CPU_USAGE_FRAME_FIELD] +
											 cpu_index_list[nCoreIndex].n[CPU_USAGE_FRAME_FIELD] +
											 cpu_index_list[nCoreIndex].s[CPU_USAGE_FRAME_FIELD] +
											 cpu_index_list[nCoreIndex].i[CPU_USAGE_FRAME_FIELD] +
											 cpu_index_list[nCoreIndex].w[CPU_USAGE_FRAME_FIELD] +
											 cpu_index_list[nCoreIndex].q[CPU_USAGE_FRAME_FIELD] +
											 cpu_index_list[nCoreIndex].sq[CPU_USAGE_FRAME_FIELD];
		
		/* CPU Usage */
		if(cpu_index_list[nCoreIndex].tot_frme > 0)
		{
			cpuloadings[nCoreIndex] = (100-(((int)cpu_index_list[nCoreIndex].i[CPU_USAGE_FRAME_FIELD]*100)/(int)cpu_index_list[nCoreIndex].tot_frme));
		}else
		{
			/* CPU unplug case */
			cpuloadings[nCoreIndex] = 0;
		}

		cpu_index_list[nCoreIndex].u[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].u[CPU_USAGE_CURRENT_FIELD];
	    cpu_index_list[nCoreIndex].n[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].n[CPU_USAGE_CURRENT_FIELD];
	    cpu_index_list[nCoreIndex].s[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].s[CPU_USAGE_CURRENT_FIELD];
		cpu_index_list[nCoreIndex].i[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].i[CPU_USAGE_CURRENT_FIELD];
		cpu_index_list[nCoreIndex].w[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].w[CPU_USAGE_CURRENT_FIELD];
		cpu_index_list[nCoreIndex].q[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].q[CPU_USAGE_CURRENT_FIELD];
		cpu_index_list[nCoreIndex].sq[CPU_USAGE_SAVE_FIELD] = cpu_index_list[nCoreIndex].sq[CPU_USAGE_CURRENT_FIELD];
                    
		THRML_LOG("CPU%d Frame:%d USAGE:%d  \n", nCoreIndex, cpu_index_list[nCoreIndex].tot_frme, cpuloadings[nCoreIndex]);

        for(i=0 ; i<3 ; i++)
        {
            THRML_LOG("Index [u:%d] [n:%d] [s:%d] [i:%d] [w:%d] [q:%d] [sq:%d] \n", i, cpu_index_list[nCoreIndex].u[i],
                                                                  cpu_index_list[nCoreIndex].n[i],
                                                                  cpu_index_list[nCoreIndex].s[i],
                                                                  cpu_index_list[nCoreIndex].i[i],
                                                                  cpu_index_list[nCoreIndex].w[i],
                                                                  cpu_index_list[nCoreIndex].q[i],
                                                                  cpu_index_list[nCoreIndex].sq[i]);

        }
	}//for

    return 0;

}
#endif

#include <linux/kernel_stat.h>
#include <linux/cpumask.h>
#include <asm/cputime.h>
#include <linux/sched.h>
#include <linux/tick.h>
#include <linux/time.h>

#ifdef arch_idle_time

static cputime64_t get_idle_time(int cpu)
{
	cputime64_t idle;

	idle = kcpustat_cpu(cpu).cpustat[CPUTIME_IDLE];
	if (cpu_online(cpu) && !nr_iowait_cpu(cpu))
		idle += arch_idle_time(cpu);
	return idle;
}

static cputime64_t get_iowait_time(int cpu)
{
	cputime64_t iowait;

	iowait = kcpustat_cpu(cpu).cpustat[CPUTIME_IOWAIT];
	if (cpu_online(cpu) && nr_iowait_cpu(cpu))
		iowait += arch_idle_time(cpu);
	return iowait;
}

#else

static u64 get_idle_time(int cpu)
{
	u64 idle, idle_time = -1ULL;

	if (cpu_online(cpu))
		idle_time = get_cpu_idle_time_us(cpu, NULL);

	if (idle_time == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.idle */
		idle = kcpustat_cpu(cpu).cpustat[CPUTIME_IDLE];
	else
		idle = usecs_to_cputime64(idle_time);

	return idle;
}

static u64 get_iowait_time(int cpu)
{
	u64 iowait, iowait_time = -1ULL;

	if (cpu_online(cpu))
		iowait_time = get_cpu_iowait_time_us(cpu, NULL);

	if (iowait_time == -1ULL)
		/* !NO_HZ or cpu offline so we can rely on cpustat.iowait */
		iowait = kcpustat_cpu(cpu).cpustat[CPUTIME_IOWAIT];
	else
		iowait = usecs_to_cputime64(iowait_time);

	return iowait;
}

#endif

static int get_sys_cpu_usage_info_ex(void)
{
    int nCoreIndex = 0, i;

    for (i = 0; i < NO_CPU_CORES; i ++)
        cpuloadings[i] = 0;

	for_each_online_cpu(nCoreIndex) {

		/* Get CPU Info */
		cpu_index_list[nCoreIndex].u[CPU_USAGE_CURRENT_FIELD] = kcpustat_cpu(nCoreIndex).cpustat[CPUTIME_USER];
        cpu_index_list[nCoreIndex].n[CPU_USAGE_CURRENT_FIELD] = kcpustat_cpu(nCoreIndex).cpustat[CPUTIME_NICE];
        cpu_index_list[nCoreIndex].s[CPU_USAGE_CURRENT_FIELD] = kcpustat_cpu(nCoreIndex).cpustat[CPUTIME_SYSTEM];
        cpu_index_list[nCoreIndex].i[CPU_USAGE_CURRENT_FIELD] = get_idle_time(nCoreIndex);
        cpu_index_list[nCoreIndex].w[CPU_USAGE_CURRENT_FIELD] = get_iowait_time(nCoreIndex);
        cpu_index_list[nCoreIndex].q[CPU_USAGE_CURRENT_FIELD] = kcpustat_cpu(nCoreIndex).cpustat[CPUTIME_IRQ];
        cpu_index_list[nCoreIndex].sq[CPU_USAGE_CURRENT_FIELD] = kcpustat_cpu(nCoreIndex).cpustat[CPUTIME_SOFTIRQ];

		/* Frame */
	    cpu_index_list[nCoreIndex].u[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].u[CPU_USAGE_CURRENT_FIELD] -
                                                                cpu_index_list[nCoreIndex].u[CPU_USAGE_SAVE_FIELD];
		cpu_index_list[nCoreIndex].n[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].n[CPU_USAGE_CURRENT_FIELD] -
                                                                cpu_index_list[nCoreIndex].n[CPU_USAGE_SAVE_FIELD];
		cpu_index_list[nCoreIndex].s[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].s[CPU_USAGE_CURRENT_FIELD] -
                                                                cpu_index_list[nCoreIndex].s[CPU_USAGE_SAVE_FIELD];
		cpu_index_list[nCoreIndex].i[CPU_USAGE_FRAME_FIELD] = TRIMz_ex(cpu_index_list[nCoreIndex].tz,
                                                                (cpu_index_list[nCoreIndex].i[CPU_USAGE_CURRENT_FIELD] -
                                                                cpu_index_list[nCoreIndex].i[CPU_USAGE_SAVE_FIELD])) ;
		cpu_index_list[nCoreIndex].w[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].w[CPU_USAGE_CURRENT_FIELD] -
                                                                cpu_index_list[nCoreIndex].w[CPU_USAGE_SAVE_FIELD];
		cpu_index_list[nCoreIndex].q[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].q[CPU_USAGE_CURRENT_FIELD] -
                                                                cpu_index_list[nCoreIndex].q[CPU_USAGE_SAVE_FIELD] ;
		cpu_index_list[nCoreIndex].sq[CPU_USAGE_FRAME_FIELD] = cpu_index_list[nCoreIndex].sq[CPU_USAGE_CURRENT_FIELD] -
                                                                cpu_index_list[nCoreIndex].sq[CPU_USAGE_SAVE_FIELD];

		/* Total Frame */
		cpu_index_list[nCoreIndex].tot_frme = cpu_index_list[nCoreIndex].u[CPU_USAGE_FRAME_FIELD] +
											  cpu_index_list[nCoreIndex].n[CPU_USAGE_FRAME_FIELD] +
											  cpu_index_list[nCoreIndex].s[CPU_USAGE_FRAME_FIELD] +
											  cpu_index_list[nCoreIndex].i[CPU_USAGE_FRAME_FIELD] +
											  cpu_index_list[nCoreIndex].w[CPU_USAGE_FRAME_FIELD] +
											  cpu_index_list[nCoreIndex].q[CPU_USAGE_FRAME_FIELD] +
											  cpu_index_list[nCoreIndex].sq[CPU_USAGE_FRAME_FIELD];

		/* CPU Usage */
		if (cpu_index_list[nCoreIndex].tot_frme > 0) {
			cpuloadings[nCoreIndex] = (100-(((int)cpu_index_list[nCoreIndex].i[CPU_USAGE_FRAME_FIELD]*100)/(int)cpu_index_list[nCoreIndex].tot_frme));
		} else {
			/* CPU unplug case */
			cpuloadings[nCoreIndex] = 0;
		}

		cpu_index_list[nCoreIndex].u[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].u[CPU_USAGE_CURRENT_FIELD];
	    cpu_index_list[nCoreIndex].n[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].n[CPU_USAGE_CURRENT_FIELD];
	    cpu_index_list[nCoreIndex].s[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].s[CPU_USAGE_CURRENT_FIELD];
		cpu_index_list[nCoreIndex].i[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].i[CPU_USAGE_CURRENT_FIELD];
		cpu_index_list[nCoreIndex].w[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].w[CPU_USAGE_CURRENT_FIELD];
		cpu_index_list[nCoreIndex].q[CPU_USAGE_SAVE_FIELD]  = cpu_index_list[nCoreIndex].q[CPU_USAGE_CURRENT_FIELD];
		cpu_index_list[nCoreIndex].sq[CPU_USAGE_SAVE_FIELD] = cpu_index_list[nCoreIndex].sq[CPU_USAGE_CURRENT_FIELD];

		THRML_LOG("CPU%d Frame:%d USAGE:%d\n", nCoreIndex, cpu_index_list[nCoreIndex].tot_frme, cpuloadings[nCoreIndex]);

        for (i=0 ; i<3 ; i++) {
            THRML_LOG("Index %d [u:%d] [n:%d] [s:%d] [i:%d] [w:%d] [q:%d] [sq:%d] \n", 
                      i, 
                      cpu_index_list[nCoreIndex].u[i], 
                      cpu_index_list[nCoreIndex].n[i],
                      cpu_index_list[nCoreIndex].s[i],
                      cpu_index_list[nCoreIndex].i[i],
                      cpu_index_list[nCoreIndex].w[i],
                      cpu_index_list[nCoreIndex].q[i],
                      cpu_index_list[nCoreIndex].sq[i]);

        }
	}

    return 0;

}

#if 0
static int get_sys_cpu_freq_info(char* dev, int nRetryNr)
{
    int fd;
    int nRet=0;
    int nReadSize;
    int nRetryCnt=0;
    char *pvalue = NULL;
    char buf[64];

    oldfs = get_fs();
    set_fs(KERNEL_DS);


    /* If sys_open fail, it will retry three times. */
    do
    {
        fd = sys_open(dev, O_RDONLY, 0);
        if(nRetryCnt > nRetryNr)
        {
            THRML_LOG("[get_sys_cpu_freq_info] open fail dev:%s fd:%d \n", dev, fd);
            set_fs(oldfs);
            return fd;
        }
        nRetryCnt++;
    }while(fd < 0);

    if(nRetryCnt > 1)
    {
       THRML_LOG("[get_sys_cpu_freq_info] open fail nRetryCnt:%d \n", nRetryCnt);
    }

    nReadSize = sys_read(fd, buf, sizeof(buf) - 1);
    //THRML_LOG("[get_sys_cpu_freq_info] nReadSize:%d\n", nReadSize);
    nRet = simple_strtol(buf, &pvalue, 10);

    set_fs(oldfs);
    sys_close(fd);

    return nRet;
}

static int get_sys_all_cpu_freq_info(void)
{
    int nCPU_freq_temp, i;
    char szTempBuf[512];

    for(i=0 ; i<NUMBER_OF_CORE ; i++)
    {
        sprintf(szTempBuf, "/sys/devices/system/cpu/cpu%01d/cpufreq/cpuinfo_cur_freq", i);
        nCPU_freq_temp = get_sys_cpu_freq_info(szTempBuf, 3);
        if(nCPU_freq_temp > 0)
        {
            cpufreqs[i] = nCPU_freq_temp/1000;
        }else
        {
            /* CPU is unplug now */
            cpufreqs[i] = nCPU_freq_temp*10;
        }
    }      

    return 0;
}
#endif

#include <linux/cpufreq.h>
static int get_sys_all_cpu_freq_info(void)
{
    int i;

    for(i=0 ; i<NO_CPU_CORES ; i++)
    {
        cpufreqs[i] = cpufreq_quick_get(i)/1000; // MHz
    }      

    return 0;
}

/* Init */
static int __init mtk_thermal_platform_init(void)
{
    int err = 0;
    
    return err;
}

/* Exit */
static void __exit mtk_thermal_platform_exit(void)
{
   
}

int mtk_thermal_get_cpu_info(
    int *nocores, 
    int **cpufreq, 
    int **cpuloading)
{
#if defined(CONFIG_ARCH_MT6577) || defined(CONFIG_ARCH_MT6589) || defined(CONFIG_ARCH_MT6582)
    //******************
    // CPU Usage
    //******************
    mutex_lock(&MTM_SYSINFO_LOCK);

    /* Read CPU Usage Information */
    get_sys_cpu_usage_info_ex();

    get_sys_all_cpu_freq_info();

    mutex_unlock(&MTM_SYSINFO_LOCK);

    if (nocores)
        *nocores = NO_CPU_CORES;

    if (cpufreq)
        *cpufreq = cpufreqs;

    if (cpuloading)
        *cpuloading = cpuloadings;

    return 0;
#else
    return -1;
#endif
}

#if defined(CONFIG_ARCH_MT6589)
#define NO_GPU_CORES (1)
static int gpufreqs[NO_GPU_CORES];
static int gpuloadings[NO_GPU_CORES];
#endif

int mtk_thermal_get_gpu_info(
    int *nocores,
    int **gpufreq,
    int **gpuloading)
{
    //******************
    // GPU Index
    //******************
#if defined(CONFIG_ARCH_MT6589)
    if (nocores)
        *nocores = NO_GPU_CORES;

    if (gpufreq)
    {
        gpufreqs[0] = mt_gpufreq_cur_freq()/1000; // the return value is KHz
        *gpufreq = gpufreqs;
    }

    if (gpuloading)
    {
        gpuloadings[0] = mt_gpufreq_cur_load();
        *gpuloading = gpuloadings;
    }

    return 0;

#else
    return -1;
#endif
}

int mtk_thermal_get_batt_info(
    int *batt_voltage,
    int *batt_current,
    int *batt_temp)
{
    //******************
    // Battery
    //******************

    /* Read Battery Information */
#if defined(CONFIG_ARCH_MT6575) || defined(CONFIG_ARCH_MT6577)
    if (batt_current)
    {
        *batt_current = get_sys_battery_info("/sys/devices/platform/mt6329-battery/FG_Battery_CurrentConsumption");
        // the return value is 0.1mA
        if (*batt_current%10 <5)
            *batt_current /= 10;
        else
            *batt_current = 1+(*batt_current/10);
    
#if defined(CONFIG_MTK_SMART_BATTERY)
        if (KAL_TRUE == gFG_Is_Charging)
        {
            *batt_current *= -1;
        }
#endif
    }

#elif defined(CONFIG_ARCH_MT6589)
    if (batt_current)
    {
        // MT6589 PMIC is MT6320
        *batt_current = get_sys_battery_info("/sys/devices/platform/battery/FG_Battery_CurrentConsumption");
        // the return value is 0.1mA
        if (*batt_current%10 <5)
            *batt_current /= 10;
        else
            *batt_current = 1+(*batt_current/10);


#if defined(CONFIG_MTK_SMART_BATTERY)
        if (KAL_TRUE == gFG_Is_Charging)
        {
            *batt_current *= -1;
        }
#endif
    }
    
#else
    if (batt_current)
        *batt_current = 0;
#endif

    if (batt_voltage)
        *batt_voltage = get_sys_battery_info("/sys/class/power_supply/battery/batt_vol");

    if (batt_temp)
        *batt_temp = get_sys_battery_info("/sys/class/power_supply/battery/batt_temp");

    return 0;
}

#if defined (CONFIG_ARCH_MT6589)
#define NO_EXTRA_THERMAL_ATTR (7)
static char* extra_attr_names[NO_EXTRA_THERMAL_ATTR] = {0};
static int extra_attr_values[NO_EXTRA_THERMAL_ATTR] = {0};
static char* extra_attr_units[NO_EXTRA_THERMAL_ATTR] = {0};
#endif

int mtk_thermal_get_extra_info(
    int *no_extra_attr,
    char ***attr_names, 
    int **attr_values, 
    char ***attr_units)
{
#if defined(CONFIG_ARCH_MT6589)
    int size, i=0;
    
    if (no_extra_attr)
        *no_extra_attr = NO_EXTRA_THERMAL_ATTR;

    //******************
    // Modem Index
    //******************
    {
        struct md_info *p_info;
        mtk_mdm_get_md_info(&p_info, &size);
        if (size <= NO_EXTRA_THERMAL_ATTR-1)
        {
            for (i=0; i<size; i++) 
            {
                extra_attr_names[i] = p_info[i].attribute;
                extra_attr_values[i] = p_info[i].value;
                extra_attr_units[i] = p_info[i].unit;
            }
        }
    }

    //******************
    // Wifi Index
    //******************
    /* Get Wi-Fi Tx throughput */
    extra_attr_names[i] = "WiFi_TP";
    extra_attr_values[i] = get_sys_wifi_throughput("/proc/wmt_tm/tx_thro", 3);
    extra_attr_units[i] = "Kbps";

    if (attr_names)
        *attr_names = extra_attr_names;

    if (attr_values)
        *attr_values = extra_attr_values;

    if (attr_units)
        *attr_units = extra_attr_units;

    return 0;

#else
    return -1;
#endif 
    
}

#if defined(CONFIG_ARCH_MT6589)
#if defined(MTK_FAN5405_SUPPORT) || defined(MTK_NCP1851_SUPPORT) || defined(MTK_BQ24196_SUPPORT) // supported charger IC
extern int force_get_tbat(void);
#endif
#endif

int mtk_thermal_force_get_batt_temp(
    void)
{
    int ret = 0;

#if defined(CONFIG_ARCH_MT6589)
#if defined(MTK_FAN5405_SUPPORT) || defined(MTK_NCP1851_SUPPORT) || defined(MTK_BQ24196_SUPPORT) // supported charger IC
    ret = force_get_tbat();
#endif
#endif

    return ret;
}

static unsigned int _thermal_scen = 0;

unsigned int mtk_thermal_set_user_scenarios(
    unsigned int mask)
{
    if ((mask & MTK_THERMAL_SCEN_CALL)) // only one scen is handled now...
    {
        set_taklking_flag(true); // make mtk_ts_cpu.c aware of call scenario
        _thermal_scen |= (unsigned int) MTK_THERMAL_SCEN_CALL;
    }
    return _thermal_scen;
}

unsigned int mtk_thermal_clear_user_scenarios(
    unsigned int mask)
{
    if ((mask & MTK_THERMAL_SCEN_CALL)) // only one scen is handled now...
    {
        set_taklking_flag(false); // make mtk_ts_cpu.c aware of call scenario
        _thermal_scen &= ~((unsigned int) MTK_THERMAL_SCEN_CALL);
    }
    return _thermal_scen;
}

//*********************************************
// Export Interface
//*********************************************

EXPORT_SYMBOL(mtk_thermal_get_cpu_info);
EXPORT_SYMBOL(mtk_thermal_get_gpu_info);
EXPORT_SYMBOL(mtk_thermal_get_batt_info);
EXPORT_SYMBOL(mtk_thermal_get_extra_info);
EXPORT_SYMBOL(mtk_thermal_force_get_batt_temp);
EXPORT_SYMBOL(mtk_thermal_set_user_scenarios);
EXPORT_SYMBOL(mtk_thermal_clear_user_scenarios);
module_init(mtk_thermal_platform_init);
module_exit(mtk_thermal_platform_exit);


