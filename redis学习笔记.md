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
编码格式一一对应

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





