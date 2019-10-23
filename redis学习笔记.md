### 一, 中的dict异步渐渐移动的操作的流程

调用流程 

server.c -> main -> initServer-> 调用 ae中aeCreateTimeEvent函数注册回调函数 serverCron

在函数serverCron->databasesCron->incrementallyRehash->dictRehashMilliseconds->dictRehash

在aeCreateTimeEvent注册事件到serverCron中了

```

long long aeCreateTimeEvent(aeEventLoop *eventLoop, long long milliseconds,
        aeTimeProc *proc, void *clientData,
        aeEventFinalizerProc *finalizerProc)
{
    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent *te;

    te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;
    te->id = id;
    aeAddMillisecondsToNow(milliseconds,&te->when_sec,&te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;
    te->prev = NULL;
    te->next = eventLoop->timeEventHead;
	if (te->next)
	{
		te->next->prev = te;
	}
    eventLoop->timeEventHead = te;
    return id;
}
```


在主函数中server.c -> main -> aeMain-> aeProcessEvents ->processTimeEvents


在processtimeEvents函数中回调 serverCro函数

```
//backcall
retval = te->timeProc(eventLoop, id, te->clientData);
```


在dictRehash函数中 dict 有两个数组 把数组1中数据移动2中把1的指针指向2，释放1的内存
为什么redis中会使用两个dict数组的数据，其实就是为异步操作如果在异步转移dict2的数据到dict1中时，新插入数据就会插入到dict2中这个是redis的dict字典的原理

```
dictEntry *dictAddRaw(dict *d, void *key, dictEntry **existing)
{
    long index;
    dictEntry *entry;
    dictht *ht;

    if (dictIsRehashing(d)) _dictRehashStep(d);

    /* Get the index of the new element, or -1 if
     * the element already exists. */
    if ((index = _dictKeyIndex(d, key, dictHashKey(d,key), existing)) == -1)
        return NULL;

    /* Allocate the memory and store the new entry.
     * Insert the element in top, with the assumption that in a database
     * system it is more likely that recently added entries are accessed
     * more frequently. */
	//这里如果redis在异步移动数据的就会使用dict2的数组
    ht = dictIsRehashing(d) ? &d->ht[1] : &d->ht[0];
    entry = zmalloc(sizeof(*entry));
    entry->next = ht->table[index];
    ht->table[index] = entry;
    ht->used++;

    /* Set the hash entry fields. */
    dictSetKey(d, entry, key);
    return entry;
}

```




ttl设置

在函数中expireSlaveKeys中master主进行ttl判断与删除操作




### 二, 冷数据的算法(LRU, FIFO, )

概论:

LRU 编辑
本词条由“科普中国”科学百科词条编写与应用工作项目 审核 。
LRU是Least Recently Used的缩写，即最近最少使用，是一种常用的页面置换算法，选择最近最久未使用的页面予以淘汰。该算法赋予每个页面一个访问字段，用来记录一个页面自上次被访问以来所经历的时间 t，当须淘汰一个页面时，选择现有页面中其 t 值最大的，即最近最少使用的页面予以淘汰。


最佳置换算法（OPT）
这是一种理想情况下的页面置换算法，但实际上是不可能实现的。该算法的基本思想是：发生缺页时，有些页面在内存中，其中有一页将很快被访问（也包含紧接着的下一条指令的那页），而其他页面则可能要到10、100或者1000条指令后才会被访问，每个页面都可以用在该页面首次被访问前所要执行的指令数进行标记。最佳页面置换算法只是简单地规定：标记最大的页应该被置换。这个算法唯一的一个问题就是它无法实现。当缺页发生时，操作系统无法知道各个页面下一次是在什么时候被访问。虽然这个算法不可能实现，但是最佳页面置换算法可以用于对可实现算法的性能进行衡量比较。
先进先出置换算法（FIFO）
最简单的页面置换算法是先入先出（FIFO）法。这种算法的实质是，总是选择在主存中停留时间最长（即最老）的一页置换，即先进入内存的页，先退出内存。理由是：最早调入内存的页，其不再被使用的可能性比刚调入内存的可能性大。建立一个FIFO队列，收容所有在内存中的页。被置换页面总是在队列头上进行。当一个页面被放入内存时，就把它插在队尾上。这种算法只是在按线性顺序访问地址空间时才是理想的，否则效率不高。因为那些常被访问的页，往往在主存中也停留得最久，结果它们因变“老”而不得不被置换出去。
FIFO的另一个缺点是，它有一种异常现象，即在增加存储块的情况下，反而使缺页中断率增加了。当然，导致这种异常现象的页面走向实际上是很少见的。
最少使用（LFU）置换算法
在采用最少使用置换算法时，应为在内存中的每个页面设置一个移位寄存器，用来记录该页面被访问的频率。该置换算法选择在之前时期使用最少的页面作为淘汰页。由于存储器具有较高的访问速度，例如100 ns，在1 ms时间内可能对某页面连续访问成千上万次，因此，通常不能直接利用计数器来记录某页被访问的次数，而是采用移位寄存器方式。每次访问某页时，便将该移位寄存器的最高位置1，再每隔一定时间(例如100 ns)右移一次。这样，在最近一段时间使用最少的页面将是∑Ri最小的页。LFU置换算法的页面访问图与LRU置换算法的访问图完全相同；或者说，利用这样一套硬件既可实现LRU算法，又可实现LFU算法。应该指出，LFU算法并不能真正反映出页面的使用情况，因为在每一时间间隔内，只是用寄存器的一位来记录页的使用情况，因此，访问一次和访问10 000次是等效的。




mySQL里有2000w数据，redis中只存20w的数据，如何保证redis中的数据都是热点数据


相关知识：redis 内存数据集大小上升到一定大小的时候，就会施行数据淘汰策略（回收策略）。

redis 提供 6种数据淘汰策略：

1. volatile-lru：从已设置过期时间的数据集（server.db[i].expires）中挑选最近最少使用的数据淘汰
2. volatile-ttl：从已设置过期时间的数据集（server.db[i].expires）中挑选将要过期的数据淘汰
3. volatile-random：从已设置过期时间的数据集（server.db[i].expires）中任意选择数据淘汰
4. allkeys-lru：从数据集（server.db[i].dict）中挑选最近最少使用的数据淘汰
5. allkeys-random：从数据集（server.db[i].dict）中任意选择数据淘汰
6. no-enviction（驱逐）：禁止驱逐数据



淘汰机制有两种情况会触发

1. 客户端的查询或者插入的时会的触发，
2. 客户端修改内存最大内存的配置时会触发"maxmemory"


在evictionPoolPopulate方法中还是随机选择maxmemory_samples个数组的淘汰，

在k的判断idle的大小选择淘汰那个key的值这个使用不同的淘汰的机制[LRU, LFU, TTL]

```

 while (k < EVPOOL_SIZE &&
               pool[k].key &&
               pool[k].idle < idle) k++;
```



在redis中的数据都是比较正常的




### 三, redis中的数据类型-- 对象系统



```
typedef struct redisObject {
    unsigned type:4;		// 数据类型的对象
    unsigned encoding:4;	//数据编码压缩的格式
    unsigned lru:LRU_BITS; /* LRU time (relative to global lru_clock) or
                            * LFU data (least significant 8 bits frequency
                            * and most significant 16 bits access time). */
    int refcount;       // 类似于java中的引用计数 --> shared
    void *ptr;      // 保存redis中的五种数据结构的指针
} robj;
```

#### 1, redis中的数据的类型分为五种的数据类型对象

```
/* The actual Redis Object */
#define OBJ_STRING 0    /* String object. */
#define OBJ_LIST 1      /* List object. */
#define OBJ_SET 2       /* Set object. */
#define OBJ_ZSET 3      /* Sorted set object. */
#define OBJ_HASH 4      /* Hash object. */
```

#### 2, redis中的数据类型编码格式


```
/* Objects encoding. Some kind of objects like Strings and Hashes can be
 * internally represented in multiple ways. The 'encoding' field of the object
 * is set to one of this fields for this object. */
#define OBJ_ENCODING_RAW 0     /* Raw representation */
#define OBJ_ENCODING_INT 1     /* Encoded as integer */
#define OBJ_ENCODING_HT 2      /* Encoded as hash table */
#define OBJ_ENCODING_ZIPMAP 3  /* Encoded as zipmap */
#define OBJ_ENCODING_LINKEDLIST 4 /* No longer used: old list encoding. */
#define OBJ_ENCODING_ZIPLIST 5 /* Encoded as ziplist */
#define OBJ_ENCODING_INTSET 6  /* Encoded as intset */
#define OBJ_ENCODING_SKIPLIST 7  /* Encoded as skiplist */
#define OBJ_ENCODING_EMBSTR 8  /* Embedded sds string encoding */
#define OBJ_ENCODING_QUICKLIST 9 /* Encoded as linked list of ziplists */
#define OBJ_ENCODING_STREAM 10 /* Encoded as a radix tree of listpacks */


```
编码格式一一对应底层实现

|redis数据类型|编码格式||||
|:--:|:--:|:--:|:--:|:--:|
|OBJ_STRING|OBJ_ENCODING_INT|long类型编码格式|
|OBJ_STRING|OBJ_ENCODING_EMBSTR|字符串小于44使用该编码格式|
|OBJ_STRING|OBJ_ENCODING_RAW|字符串大于44的使用该动态申请内存(sds)|
|OBJ_LIST|OBJ_ENCODING_QUICKLIST|在内存中编码格式quick_list数据结构|
|OBJ_LIST|OBJ_ENCODING_ZIPLIST|在保存落地文件的时候是以压缩编码ziplist格式保存文件中去的，在redis启动时候要报落地文件中list结构转换quick_list编码格式|
|OBJ_HASH|OBJ_ENCODING_ZIPLIST|字符或者数字的长度小64时是要ziplist压缩编码|
|OBJ_HASH|OBJ_ENCODING_HT|字符或者数字的长度大于64时使用hashtable编码|
|OBJ_SET|OBJ_ENCODING_HT|hashtable编码|
|OBJ_SET|OBJ_ENCODING_INTSET|intset编码每个要插入字符都要检查, 字符过长就是要hashtable编码格式|
|OBJ_ZSET|OBJ_ENCODING_ZIPLIST|有序集合子字符串小于64字节时使用ziplist编码格式,在zset中年使用ziplist是两个节点为一组数据即key-value|
|OBJ_ZSET|OBJ_ENCODING_SKIPLIST|key是哈希表的插入的数字是使用跳跃表的进行排序的,跳跃表的|


在set数据结构中的intset编码格式转换为hashtable有两种情况

1. 当保存value值大于long long的最大值时会触发hashtable编码格式 
2. 当intset格式存储数据的二进制大于配置表中set-max-intset-entries的最大值时会转换为hashtable的数据结构

```
// 集合如果intset编码格式会对每一个要插入数据进行检查是否是转换longlong， 不可以就转换hashtable表的编码格式
if (isSdsRepresentableAsLongLong(value,&llval) == C_OK) {
	uint8_t success = 0;
	// 这个插入的有一个点讲究哦， 可能要转换编码格式哦 	
	// intset中的整数编码四种格式
	// 1. 一个字节
	// 2. 二个字节
	// 3. 四个字节
	// 4. 八个字节
	subject->ptr = intsetAdd(subject->ptr,llval,&success);
	if (success) {
		/* Convert to regular set when the intset contains
		 * too many entries. */
		// intset 整数编码格式的长度是否大于配置表的中的大小如果大于就要的修改成hashtable的编码格式了
		if (intsetLen(subject->ptr) > server.set_max_intset_entries)
			setTypeConvert(subject,OBJ_ENCODING_HT);
		return 1;
	}
} else {
	/* Failed to get integer from object, convert to regular set. */
	setTypeConvert(subject,OBJ_ENCODING_HT);

	/* The set *was* an intset and this value is not integer
	 * encodable, so dictAdd should always work. */
	serverAssert(dictAdd(subject->ptr,sdsdup(value),NULL) == DICT_OK);
	return 1;
}
```


redis中迭代器使用

set数据结构使用的迭代器

```
typedef struct {
    robj *subject; // redis中对象
    int encoding; // 编码格式
    int ii; /* intset iterator */
    dictIterator *di;
} setTypeIterator;

// 字典的迭代器
typedef struct dictIterator {
    dict *d;  // 哈希表
    long index;
    int table, safe; // 哈希表的数组一个有两个 0, 1
    dictEntry *entry, *nextEntry;    /* unsafe iterator fingerprint for misuse detection. */
    long long fingerprint;
} dictIterator;

typedef struct dict {
    dictType *type;
    void *privdata;
    dictht ht[2];
    long rehashidx; /* rehashing not in progress if rehashidx == -1 */
    unsigned long iterators; /* number of iterators currently running */
} dict;

```






###

redis中的数据结构体




```
typedef struct redisDb {
    dict *dict;                 /* The keyspace for this DB */
    dict *expires;              /* Timeout of keys with a timeout set */
	// 订阅模式
    dict *blocking_keys;        /* Keys with clients waiting for data (BLPOP)*/
    dict *ready_keys;           /* Blocked keys that received a PUSH */
    dict *watched_keys;         /* WATCHED keys for MULTI/EXEC CAS */
    int id;                     /* Database ID */
    long long avg_ttl;          /* Average TTL, just for stats */
    list *defrag_later;         /* List of key names to attempt to defrag one by one, gradually. */
} redisDb;
```




long 类型在内存放置

1. -128~127之间的数据使用两个字节

第一个字节为 0XC0
第二个字节为 整数的值得

2. -32768~32767使用三个字节

第一个字节为 0XC1
第二个字节和第三个为 整数的值得


3. -‭2147483648‬ ~ 2147483647使用四个字节


第一个字节为 0XC2
第二个字节和第三个和第四个为 整数的值得

字符串类型

1. 长度在0到64之间的使用一个字节

高两位为00后6位为字符串的长度

```
buf[0] = (len&0xFF)|(RDB_6BITLEN<<6);
```

2. 长度在64到16384之间使用二个字节

高两位为01 后面14位为长度

```
buf[0] = ((len>>8)&0xFF)|(RDB_14BITLEN<<6);
buf[1] = len&0xFF;
```

3. 长度在uint32max值之间的使用5个字节

高八位为0X80 后面4个字节存放长度

```
  /* Save a 32 bit len */
        buf[0] = RDB_32BITLEN;
        if (rdbWriteRaw(rdb,buf,1) == -1) return -1;
        uint32_t len32 = htonl(len);
        if (rdbWriteRaw(rdb,&len32,4) == -1) return -1;
```
4. 长度大于uintmax的值使用9个字节


高八位为0X81 后面8个字节存放长度

```
 buf[0] = RDB_64BITLEN;
        if (rdbWriteRaw(rdb,buf,1) == -1) return -1;
        len = htonu64(len);
        if (rdbWriteRaw(rdb,&len,8) == -1) return -1;
```





在redis中的异步保存数据的流程

1. 管道用于业务进程的新的操作通知异步保存数据的进程
2. 创建子进程保存数据,并接受业务进程数据的新的操作保存到server.aof_child_diff在子保存redis中的数据结束时再保存这些数据

```
/**
* 在业务进程中的创建管道，负责通知子进程
*/
int aofCreatePipes(void) {
    int fds[6] = {-1, -1, -1, -1, -1, -1};
    int j;
	// 创建管道
    if (pipe(fds) == -1) goto error; /* parent -> children data. */
    if (pipe(fds+2) == -1) goto error; /* children -> parent ack. */
    if (pipe(fds+4) == -1) goto error; /* parent -> children ack. */
    /* Parent -> children data is non blocking. */
    if (anetNonBlock(NULL,fds[0]) != ANET_OK) goto error;
    if (anetNonBlock(NULL,fds[1]) != ANET_OK) goto error;
	// 把孩子节点放到事件驱动中的
    if (aeCreateFileEvent(server.el, fds[2], AE_READABLE, aofChildPipeReadable, NULL) == AE_ERR) goto error;

    server.aof_pipe_write_data_to_child = fds[1];
    server.aof_pipe_read_data_from_parent = fds[0];
    server.aof_pipe_write_ack_to_parent = fds[3];
    server.aof_pipe_read_ack_from_child = fds[2];
    server.aof_pipe_write_ack_to_child = fds[5];
    server.aof_pipe_read_ack_from_parent = fds[4];
    server.aof_stop_sending_diff = 0;
    return C_OK;

error:
    serverLog(LL_WARNING,"Error opening /setting AOF rewrite IPC pipes: %s",
        strerror(errno));
    for (j = 0; j < 6; j++) if(fds[j] != -1) close(fds[j]);
    return C_ERR;
}
```

异步保存进程

```
/**
* 保存业务进程的数据到server.aof_child_diff中
*/
ssize_t aofReadDiffFromParent(void) {
    char buf[65536]; /* Default pipe buffer size on most Linux systems. */
    ssize_t nread, total = 0;

    while ((nread =
            read(server.aof_pipe_read_data_from_parent,buf,sizeof(buf))) > 0) {
        server.aof_child_diff = sdscatlen(server.aof_child_diff,buf,nread);
        total += nread;
    }
    return total;
}
```






特殊的操作

1. signalKeyAsReady


业务进程通知方法

1. propagateExpire
2. propagate



#### 一, 在过期的字典中的调用

1. expireIfNeeded(惰性删除)
2. freeMemoryIfNeeded(设置内存的大小检测)
2. activeExpireCycleTryExpire(定时删除)


#### 二, 新增加的操作记录

客户端使用阻塞获取信息使用
1. handleClientsBlockedOnKeys
2. execCommandPropagateMulti
3. serveClientBlockedOnList
4. streamPropagateXCLAIM
5. streamPropagateGroupID

客户端请求

1. call中有两个



1. 当客户端使用阻塞式请求数据时, redis先是查询没有就把数据保存到db->blocking_keys的字典中,在下次有客户端插入数据时在通知该客户端和通知异步进程
  


这里我没有明白



```

// 这里我没有看懂 是什么原因使用这种方式难道上面的还不够吗？？？？ 还需这种发生操作
// 1. spop弹出队利中的数据的个数
// 2. 如果大于的现有的队利的个数就不需要纪录了
// 3. 小于现有的队礼的个数就要纪录数据的了弹出的数据具体的那些数据key-value
// 思考: 为什么大于时就不会纪录了，小于就要纪录具体弹出的数据的key-value呢
// 这个有可能弹出数据错误数据的， 不知道弹出具体那个key-value， 而设计这个模式为弹出数据的正确性

if (server.also_propagate.numops) {
	int j;
	redisOp *rop;

	if (flags & CMD_CALL_PROPAGATE) {
		for (j = 0; j < server.also_propagate.numops; j++) {
			rop = &server.also_propagate.ops[j];
			int target = rop->target;
			/* Whatever the command wish is, we honor the call() flags. */
			if (!(flags&CMD_CALL_PROPAGATE_AOF)) target &= ~PROPAGATE_AOF;
			if (!(flags&CMD_CALL_PROPAGATE_REPL)) target &= ~PROPAGATE_REPL;
			// aof异步保存数据的进程开启了
			if (target)
				propagate(rop->cmd, rop->dbid, rop->argv, rop->argc, target);
		}
	}
	redisOpArrayFree(&server.also_propagate);
}
```




```
/**
* 这个设计还是挺巧妙的哦
* blpop 等待
* 这个操作是不会在main loop 记录下来的是因为 全局的 server.dirty没有加一的操作，正好于压入的操作一起通知异步写入数据的进程handleClientsBlockedOnKeys
*/
void blockForKeys(client *c, int btype, robj **keys, int numkeys, mstime_t timeout, robj *target, streamID *ids) {
    dictEntry *de;
    list *l;
    int j;

    c->bpop.timeout = timeout;
    c->bpop.target = target;

    if (target != NULL) incrRefCount(target);

    for (j = 0; j < numkeys; j++) {
        /* The value associated with the key name in the bpop.keys dictionary
         * is NULL for lists and sorted sets, or the stream ID for streams. */
        void *key_data = NULL;
        if (btype == BLOCKED_STREAM) {
            key_data = zmalloc(sizeof(streamID));
            memcpy(key_data,ids+j,sizeof(streamID));
        }

        /* If the key already exists in the dictionary ignore it. */
        if (dictAdd(c->bpop.keys,keys[j],key_data) != DICT_OK) {
            zfree(key_data);
            continue;
        }
        incrRefCount(keys[j]);

        /* And in the other "side", to map keys -> clients */
        de = dictFind(c->db->blocking_keys,keys[j]);
        if (de == NULL) {
            int retval;

            /* For every key we take a list of clients blocked for it */
            l = listCreate();
            retval = dictAdd(c->db->blocking_keys,keys[j],l);
            incrRefCount(keys[j]);
            serverAssertWithInfo(c,keys[j],retval == DICT_OK);
        } else {
            l = dictGetVal(de);
        }
        listAddNodeTail(l,c);
    }
    blockClient(c,btype);
}
```




net

1. prepareClientToWrite->unprotectClient->clientInstallWriteHandler



master -> slave 


slaveof 命令中执行函数中的数字master的地址和ip的删除当前主机的名称



slave -> master 连接的过程


connectWithMaster

状态由REPL_STATE_CONNECT变成REPL_STATE_CONNECTING

同步的socket连接syncWithMaster

1. 发送ping 状态由REPL_STATE_CONNECTING变成REPL_STATE_RECEIVE_PONG
2. 接受pong 状态变成REPL_STATE_SEND_AUTH

在状态REPL_STATE_SEND_PSYNC时校验的偏移量







