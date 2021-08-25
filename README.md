# MQTT Demo (C++)

mqtt客户端示例代码，基于libpaho-c库开发。对libpaho-c进行了二次封装，无需关注底层细节，如重连、缓存、回调、内存管理等底层操作。

### 使用
- 应用层只需订阅主题设置回调即可
- 应用层直接调用mqttPublish()发布消息
- 应用层需周期性调用spinOnce()是的mqtt消息处理得以执行
- 无需另外运行线程执行spinOnce()，在主线程周期调用即可

### 示例
- 发布/localization/pose主题,使用json格式
```
{
    "xyz" : [0.2, 0.9, 3.56]
}
```
- 发布/quit主题，示例程序将终止
```
任意内容
```
- 订阅/alive/string主题，会周期性收到如下字符串
```
i am alive!
```
- 订阅/alive/json主题，会周期性收到如下json数据
```
{
    "cmd" : "alive"
}
```