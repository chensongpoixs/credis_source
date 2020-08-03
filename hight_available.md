## 订阅模式

格式:

```
sentinelEvent();
```



在配置中配置master和salve服务器的信息  然后读取配置表的信息的 


会把其他的slave和sentinel放到master下管理   就是master的结构体sentinelRedisInstance中有的master,slave,sentie的结构


在sentinelCheckSubjectivelyDown方法中检查其他的服务是否宕机的情况然后再和其他服务协商是否宕机   规则是  n/2+1同意就真的宕机的了,这是redis中分布式中一致性原则

sentinelAskMasterStateToOtherSentinels方法中发送所有的sentinel服务查看纪元 在sentinelCheckObjectivelyDown方法中处理和记录每个sentinel服务服务的纪元是sentinel服务每个服务中保存纪元 是否同意宕机的情况


在sentinel服务中前区纪元的中进行协商的状态在sentinelStartFailover方法中修改的非常重要的一个步骤

sentinel服务发送 slaveof on one命令把一个slave服务转换master服务然后所有sentinel服务请求salve服务时就master服务进行故障转移了  这里面有点能理解的如何保证分布式一致性的问题，这里面redis使用Raft算法 先到先得原则？？？？？  是不是有点疑问，这里我也有点疑问的O(∩_∩)O哈哈~  


选举sentinel服务中leader的Raft算法

```
/* Scan all the Sentinels attached to this master to check if there
 * is a leader for the specified epoch.
 *
 * To be a leader for a given epoch, we should have the majority of
 * the Sentinels we know (ever seen since the last SENTINEL RESET) that
 * reported the same instance as leader for the same epoch. */
char *sentinelGetLeader(sentinelRedisInstance *master, uint64_t epoch) {
    dict *counters;
    dictIterator *di;
    dictEntry *de;
    unsigned int voters = 0, voters_quorum;
    char *myvote;
    char *winner = NULL;
    uint64_t leader_epoch;
    uint64_t max_votes = 0;

    serverAssert(master->flags & (SRI_O_DOWN|SRI_FAILOVER_IN_PROGRESS));
    counters = dictCreate(&leaderVotesDictType,NULL);

    voters = dictSize(master->sentinels)+1; /* All the other sentinels and me.*/
    //选举出局部leader sentinel服务
    /* Count other sentinels votes */
    di = dictGetIterator(master->sentinels);
    while((de = dictNext(di)) != NULL) {
        sentinelRedisInstance *ri = dictGetVal(de);
        // leader 前
        if (ri->leader != NULL && ri->leader_epoch == sentinel.current_epoch)
        {
            sentinelLeaderIncr(counters,ri->leader);
        }
    }
    dictReleaseIterator(di);

    /* Check what's the winner. For the winner to win, it needs two conditions:
     * 1) Absolute majority between voters (50% + 1).
     * 2) And anyway at least master->quorum votes. */
    di = dictGetIterator(counters);
    while((de = dictNext(di)) != NULL) {
        uint64_t votes = dictGetUnsignedIntegerVal(de);

        if (votes > max_votes) {
            max_votes = votes;
            winner = dictGetKey(de);
        }
    }
    dictReleaseIterator(di);

    /* Count this Sentinel vote:
     * if this Sentinel did not voted yet, either vote for the most
     * common voted sentinel, or for itself if no vote exists at all. */
    if (winner)
        myvote = sentinelVoteLeader(master,epoch,winner,&leader_epoch);
    else
        myvote = sentinelVoteLeader(master,epoch,sentinel.myid,&leader_epoch);

    if (myvote && leader_epoch == epoch) {
        uint64_t votes = sentinelLeaderIncr(counters,myvote);

        if (votes > max_votes) {
            max_votes = votes;
            winner = myvote;
        }
    }
    // 查看局部leader选举数是否大于所有sentinel服务数 v/2 +1 
    voters_quorum = voters/2+1;
    if (winner && (max_votes < voters_quorum || max_votes < master->quorum))
        winner = NULL;

    winner = winner ? sdsnew(winner) : NULL;
    sdsfree(myvote);
    dictRelease(counters);
    return winner;
}

```
