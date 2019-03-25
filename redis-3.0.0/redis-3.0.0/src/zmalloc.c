/* zmalloc - total amount of allocated memory aware version of malloc()
 *
 * Copyright (c) 2009-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>

/* This function provide us access to the original libc free(). This is useful
 * for instance to free results obtained by backtrace_symbols(). We need
 * to define this function before including zmalloc.h that may shadow the
 * free implementation if we use jemalloc or another non standard allocator. */
void zlibc_free(void *ptr) {
    free(ptr);
}

#include <string.h>
#include <pthread.h>
#include "config.h"
#include "zmalloc.h"

#ifdef HAVE_MALLOC_SIZE
#define PREFIX_SIZE (0)
#else
#if defined(__sun) || defined(__sparc) || defined(__sparc__)
#define PREFIX_SIZE (sizeof(long long))
#else
#define PREFIX_SIZE (sizeof(size_t))
#endif
#endif

/* Explicitly override malloc/free etc when using tcmalloc. */
#if defined(USE_TCMALLOC)
#define malloc(size) tc_malloc(size)
#define calloc(count,size) tc_calloc(count,size)
#define realloc(ptr,size) tc_realloc(ptr,size)
#define free(ptr) tc_free(ptr)
#elif defined(USE_JEMALLOC)
#define malloc(size) je_malloc(size)
#define calloc(count,size) je_calloc(count,size)
#define realloc(ptr,size) je_realloc(ptr,size)
#define free(ptr) je_free(ptr)
#endif

#if defined(__ATOMIC_RELAXED)
#define update_zmalloc_stat_add(__n) __atomic_add_fetch(&used_memory, (__n), __ATOMIC_RELAXED)
#define update_zmalloc_stat_sub(__n) __atomic_sub_fetch(&used_memory, (__n), __ATOMIC_RELAXED)
#elif defined(HAVE_ATOMIC)
#define update_zmalloc_stat_add(__n) __sync_add_and_fetch(&used_memory, (__n))
#define update_zmalloc_stat_sub(__n) __sync_sub_and_fetch(&used_memory, (__n))
#else
#define update_zmalloc_stat_add(__n) do { \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory += (__n); \
    pthread_mutex_unlock(&used_memory_mutex); \
} while(0)

#define update_zmalloc_stat_sub(__n) do { \
    pthread_mutex_lock(&used_memory_mutex); \
    used_memory -= (__n); \
    pthread_mutex_unlock(&used_memory_mutex); \
} while(0)

#endif

#define update_zmalloc_stat_alloc(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_add(_n); \
    } else { \
        used_memory += _n; \
    } \
} while(0)

#define update_zmalloc_stat_free(__n) do { \
    size_t _n = (__n); \
    if (_n&(sizeof(long)-1)) _n += sizeof(long)-(_n&(sizeof(long)-1)); \
    if (zmalloc_thread_safe) { \
        update_zmalloc_stat_sub(_n); \
    } else { \
        used_memory -= _n; \
    } \
} while(0)

static size_t used_memory = 0;
//  zmalloc_thread_safe是一个全局静态变量（static int）。它是操作是否是线程安全的标识。1 表示线程安全，0 表示非线程安全。
static int zmalloc_thread_safe = 0; 
pthread_mutex_t used_memory_mutex = PTHREAD_MUTEX_INITIALIZER;

static void zmalloc_default_oom(size_t size) {
    fprintf(stderr, "zmalloc: Out of memory trying to allocate %zu bytes\n",
        size);
    fflush(stderr);
    abort();
}

static void (*zmalloc_oom_handler)(size_t) = zmalloc_default_oom;

void *zmalloc(size_t size) {
    void *ptr = malloc(size+PREFIX_SIZE);

    if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;
#else
    *((size_t*)ptr) = size;
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    return (char*)ptr+PREFIX_SIZE;
#endif
}

void *zcalloc(size_t size) {
    void *ptr = calloc(1, size+PREFIX_SIZE);

    if (!ptr) zmalloc_oom_handler(size);
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_alloc(zmalloc_size(ptr));
    return ptr;
#else
    *((size_t*)ptr) = size;
    update_zmalloc_stat_alloc(size+PREFIX_SIZE);
    return (char*)ptr+PREFIX_SIZE;
#endif
}

void *zrealloc(void *ptr, size_t size) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
#endif
    size_t oldsize;
    void *newptr;

    if (ptr == NULL) return zmalloc(size);
#ifdef HAVE_MALLOC_SIZE
    oldsize = zmalloc_size(ptr);
    newptr = realloc(ptr,size);
    if (!newptr) zmalloc_oom_handler(size);

    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_alloc(zmalloc_size(newptr));
    return newptr;
#else
    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);
    newptr = realloc(realptr,size+PREFIX_SIZE);
    if (!newptr) zmalloc_oom_handler(size);

    *((size_t*)newptr) = size;
    update_zmalloc_stat_free(oldsize);
    update_zmalloc_stat_alloc(size);
    return (char*)newptr+PREFIX_SIZE;
#endif
}

/* Provide zmalloc_size() for systems where this function is not provided by
 * malloc itself, given that in that case we store a header with this
 * information as the first bytes of every allocation. */
#ifndef HAVE_MALLOC_SIZE
size_t zmalloc_size(void *ptr) {
    void *realptr = (char*)ptr-PREFIX_SIZE;
    size_t size = *((size_t*)realptr);
    /* Assume at least that all the allocations are padded at sizeof(long) by
     * the underlying allocator. */
	// 其中if用于判断本次内存的变化量是否是long类型大小的整数倍，
	//如果不是，则进行一个对齐处理，而后通过update_zmalloc_stat_add
	//安全的增加used_memory的值。
	/************************************************************************/
	/* 这段代码和我在上一篇博文中介绍的zfree()函数中的内容颇为相似。大家可以去阅读那一篇博文。
	这里再概括一下，
	zmalloc(size)在分配内存的时候会多申请sizeof(size_t)个字节大小的内存
	【64位系统中是8字节】，即调用malloc(size+8)，
	所以一共申请分配size+8个字节，zmalloc(size)会在已分配内存的首地址开始的8字节
	中存储size的值，实际上因为内存对齐，malloc(size+8)分配的内存可能会比size+8要多一些，
	目的是凑成8的倍数，所以实际分配的内存大小是size+8+X【(size+8+X)%8==0 (0<=X<=7)】。
	然后内存指针会向右偏移8个字节的长度。zfree()就是zmalloc()的一个逆操作，
	而zmalloc_size()的目的就是计算出size+8+X的总大小
	/************************************************************************/
	if (size&(sizeof(long) - 1))
	{
		size += sizeof(long) - (size&(sizeof(long) - 1));
	}
    return size+PREFIX_SIZE;
}
#endif

void zfree(void *ptr) {
#ifndef HAVE_MALLOC_SIZE
    void *realptr;
    size_t oldsize;
#endif

    if (ptr == NULL) return;
#ifdef HAVE_MALLOC_SIZE
    update_zmalloc_stat_free(zmalloc_size(ptr));
    free(ptr);
#else
    realptr = (char*)ptr-PREFIX_SIZE;
    oldsize = *((size_t*)realptr);
    update_zmalloc_stat_free(oldsize+PREFIX_SIZE);
    free(realptr);
#endif
}

char *zstrdup(const char *s) {
    size_t l = strlen(s)+1;
    char *p = zmalloc(l);

    memcpy(p,s,l);
    return p;
}

size_t zmalloc_used_memory(void) {
    size_t um;

    if (zmalloc_thread_safe) {
#if defined(__ATOMIC_RELAXED) || defined(HAVE_ATOMIC)
        um = update_zmalloc_stat_add(0);
#else
        pthread_mutex_lock(&used_memory_mutex);
        um = used_memory;
        pthread_mutex_unlock(&used_memory_mutex);
#endif
    }
    else {
        um = used_memory;
    }

    return um;
}

void zmalloc_enable_thread_safeness(void) {
    zmalloc_thread_safe = 1;
}

void zmalloc_set_oom_handler(void (*oom_handler)(size_t)) {
    zmalloc_oom_handler = oom_handler;
}

/* Get the RSS information in an OS-specific way.
 *
 * WARNING: the function zmalloc_get_rss() is not designed to be fast
 * and may not be called in the busy loops where Redis tries to release
 * memory expiring or swapping out objects.
 *
 * For this kind of "fast RSS reporting" usages use instead the
 * function RedisEstimateRSS() that is a much faster (and less precise)
 * version of the function. */

#if defined(HAVE_PROC_STAT)
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
/************************************************************************/
/*  获取RSS的大小，这个RSS可不是我们在网络上常常看到的RSS，而是指的Resident Set Size，表示当前进程实际所驻留在内存中的空间大小，即不包括被交换（swap）出去的空间。
        了解一点操作系统的知识，就会知道我们所申请的内存空间不会全部常驻内存，系统会把其中一部分暂时不用的部分从内存中置换到swap区（装Linux系统的时候我们都知道有一个交换空间）。
        该函数大致的操作就是在当前进程的 /proc/<pid>/stat 【<pid>表示当前进程id】文件中进行检索。该文件的第24个字段是RSS的信息，它的单位是pages（内存页的数目）                                                                    */
/************************************************************************/
size_t zmalloc_get_rss(void) {
	//通过调用库函数sysconf()【大家可以man sysconf查看详细内容】来查询内存页的大小。
    int page = sysconf(_SC_PAGESIZE);
    size_t rss;
    char buf[4096];
    char filename[256];
    int fd, count;
    char *p, *x;

    snprintf(filename,256,"/proc/%d/stat",getpid());
    if ((fd = open(filename,O_RDONLY)) == -1) return 0;
    if (read(fd,buf,4096) <= 0) {
        close(fd);
        return 0;
    }
    close(fd);

    p = buf;
    count = 23; /* RSS is the 24th field in /proc/<pid>/stat */
    while(p && count--) {
        p = strchr(p,' ');
        if (p) p++;
    }
    if (!p) return 0;
    x = strchr(p,' ');
    if (!x) return 0;
    *x = '\0';  // 因为循环结束也可能是p变成了空指针，所以判断一下p是不是空指针。接下来的的几部操作很好理解，就是将第24个字段之后的空格设置为'\0'，这样p就指向一个一般的C风格字符串了。

    rss = strtoll(p,NULL,10);
    rss *= page;
    return rss;
}
#elif defined(HAVE_TASKINFO)
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/task.h>
#include <mach/mach_init.h>

size_t zmalloc_get_rss(void) {
    task_t task = MACH_PORT_NULL;
    struct task_basic_info t_info;
    mach_msg_type_number_t t_info_count = TASK_BASIC_INFO_COUNT;

    if (task_for_pid(current_task(), getpid(), &task) != KERN_SUCCESS)
        return 0;
    task_info(task, TASK_BASIC_INFO, (task_info_t)&t_info, &t_info_count);

    return t_info.resident_size;
}
#else
size_t zmalloc_get_rss(void) {
    /* If we can't get the RSS in an OS-specific way for this system just
     * return the memory usage we estimated in zmalloc()..
     *
     * Fragmentation will appear to be always 1 (no fragmentation)
     * of course... */
    return zmalloc_used_memory();
}
#endif


/************************************************************************/
/*         这个函数是查询内存碎片率（fragmentation ratio），即RSS和所分配总内存空间的比值。需要用zmalloc_get_rss()获得RSS的值，再以RSS的值作为参数传递进来。
--------------------------------------------------------------------------------------------------------------------------------------------------------------
内存碎片分为：内部碎片和外部碎片
内部碎片：是已经被分配出去（能明确指出属于哪个进程）却不能被利用的内存空间，直到进程释放掉，才能被系统利用；
外部碎片：是还没有被分配出去（不属于任何进程），但由于太小了无法分配给申请内存空间的新进程的内存空闲区域。                                                                    */
/************************************************************************/
/* Fragmentation = RSS / allocated-bytes */
float zmalloc_get_fragmentation_ratio(size_t rss) {
    return (float)rss/zmalloc_used_memory();
}

/* Get the sum of the specified field (converted form kb to bytes) in
 * /proc/self/smaps. The field must be specified with trailing ":" as it
 * apperas in the smaps output.
 *
 * Example: zmalloc_get_smap_bytes_by_field("Rss:");
 */
#if defined(HAVE_PROC_SMAPS)
size_t zmalloc_get_smap_bytes_by_field(char *field) {
    char line[1024];
    size_t bytes = 0; 
	// 该文件反映了该进程的相应线性区域的大小
    FILE *fp = fopen("/proc/self/smaps","r");
    int flen = strlen(field);

    if (!fp) return 0;
    while(fgets(line,sizeof(line),fp) != NULL) {
        if (strncmp(line,field,flen) == 0) {
            char *p = strchr(line,'k');
            if (p) {
                *p = '\0';
                bytes += strtol(line+flen,NULL,10) * 1024;
            }
        }
    }
    fclose(fp);
    return bytes;
}
#else
size_t zmalloc_get_smap_bytes_by_field(char *field) {
    ((void) field);
    return 0;
}
#endif

/************************************************************************/
/*     源代码很简单，该函数的本质就是在调用 zmalloc_get_smap_bytes_by_field("Private_Dirty:");其完成的操作就是扫描 /proc/self/smaps文件，统计其中所有 Private_Dirty字段的和。那么这个Private_Dirty是个什么意思呢？
        大家继续观察一下，我在上面贴出的 /proc/self/smaps文件的结构，它有很多结构相同的部分组成。其中有几个字段有如下的关系：

Rss=Shared_Clean+Shared_Dirty+Private_Clean+Private_Dirty
        其中：
Shared_Clean:多进程共享的内存，且其内容未被任意进程修改 
Shared_Dirty:多进程共享的内存，但其内容被某个进程修改 
Private_Clean:某个进程独享的内存，且其内容没有修改 
Private_Dirty:某个进程独享的内存，但其内容被该进程修改
    其实所谓的共享的内存，一般指的就是Unix系统中的共享库（.so文件）的使用，共享库又叫动态库（含义同Windows下的.dll文件），它只有在程序运行时才被装入内存。这时共享库中的代码和数据可能会被多个进程所调用，于是就会产生共享（Shared）与私有（Private）、干净（Clean）与脏（Dirty）的区别了。此外该处所说的共享的内存除了包括共享库以外，还包括System V的IPC机制之一的共享内存段（shared memory）
--------------------------------------------------------------------------------------------------------------------------------------------------------------
关于smaps文件中Shared_Clean、Shared_Dirty、Private_Clean、Private_Dirty这几个字段含义的详细讨论，有位网友进行了深入地探究，并形成了博文，推荐阅读：                                                                   */
/************************************************************************/
size_t zmalloc_get_private_dirty(void) {
    return zmalloc_get_smap_bytes_by_field("Private_Dirty:");
}
