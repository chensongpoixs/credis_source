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