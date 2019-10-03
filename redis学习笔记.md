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
