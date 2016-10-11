## Versioning

This project follows the [semantic versioning](http://semver.org/) scheme. The API change and backwards compatibility rules are those indicated by SemVer.

## 是什么

C++ 定时器, 基于 C++11, libuv 实现.

`TimerManager` 在 `Start()` 时会创建 `thread_num` 个工作线程, 每一个工作线程中都运行着一个 uv loop.

当通过 `TimerManager::StartTimer()` 提交一个定时器时, 会通过 round-robin 算法选择一个工作线程(下称 P), 然
后将定时器请求交给 P. P 在收到定时器请求之后会根据请求细节创建一个 `uv_timer_t` 对象来实现定时机制.

## 怎么用

test/main.cc 中有一个很简短的例子, 可以参考. 关于接口具体的语义可以参考每一个接口的注释.

### 依赖

1.  C++11
2.  [libuv, v1.9.1](https://github.com/libuv/libuv/tree/v1.9.1)
3.  [common, v1.1.0](https://github.com/pp-qq/common/tree/v1.1.0)

