#pragma once

#include <stddef.h>
#include <uv.h>

#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <future>
#include <functional>

#include <concurrent/mutex.h>

struct TimerManager {
    size_t thread_num = 1;

public:
    // TimerCallbackReturnEnum 这个名字不会被使用, 所以随便起起可以
    enum TimerCallbackReturnEnum : unsigned int {
        /// 该标志设置则表明主动停止 timer.
        kStopTimer = 0x01,

        /// 若觉得 callback 执行时间可能超过了 1ms, 则应该设置该标志.
        kMoreThan1MS = 0x02
    };

    /**
     * timer callback 类型.
     *
     * timer callback 的调用形式如下:
     *
     *  param: status; 若为 0, 则表明当前定时器到期了; 若不为 0, 则表明由于出现了某些错误才会被调用, 此后
     *  当前回调不会再调用.
     *
     *  return; callback 的返回值会被当作位掩码来对待, 参见 TimerCallbackReturnEnum.
     *  unsigned int (unsigned int status);
     */
    using timer_callback_t = std::function<unsigned int(unsigned int)>;

public:

    void Start();

    ~TimerManager() noexcept;

public:
    /* 只有这里的接口才是线程安全的, 即可以在不同线程, 在同一个 TimerManager 实例上调用 Stop(), Join(), 或者
     * StartTimer(). 但是不能在线程 A 上针对同一个 TimerManager 实例(下称 T )调用 Start(), 在线程 B 上调用
     * T.StartTimer().
     */

    /**
     * 停止 TimerManager.
     *
     * 此时尚未被处理的 timer 不会被处理, 即直接以错误状态来调用 timer callback. 此时会一直等待已经处理的 timer
     * 主动停止才会返回.
     */
    void Stop() {
        DoStopOrJoin(TimerManagerStatus::kStop);
        return ;
    }

    /**
     * 停止 TimerManager.
     *
     * 此时会一直等待所有的 timer 主动停止才会返回.
     */
    void Join() {
        DoStopOrJoin(TimerManagerStatus::kJoin);
        return ;
    }

    /**
     * 启动一个定时器.
     *
     * 若 timeout 为 0, 则会立即调用 cb; 否则会在 timeout ms 之后调用 cb; 在第一次调用 cb 之后, 若 repeat
     * 为 0, 则停止当前定时器; 否则之后会每 repeat ms 之后调用一次 cb.
     */
    void StartTimer(uint64_t timeout, uint64_t repeat, const timer_callback_t &cb);

/* 本来这些都是 private 就行了.
 *
 * 但是我想重载个 operator<<(ostream &out, TimerManagerStatus); 本来是把这个重载当作是 static member, 然
 * 后编译报错. 貌似只能作为 non-member, 这样子的话, TimerManagerStatus 也就必须得是 public 了.
 */
public:

    enum class TimerManagerStatus : unsigned int {
        kInitial = 0,
        kStarted,
        kStop,
        kJoin
    };

    struct TimerRequest {
        uint64_t timeout;
        uint64_t repeat;
        timer_callback_t cb;

    public:
        TimerRequest(uint64_t timeout_arg, uint64_t repeat_arg, const timer_callback_t &cb_arg):
            timeout(timeout_arg),
            repeat(repeat_arg),
            cb(cb_arg) {
        }

        unsigned int Call(unsigned int status) noexcept {
            return cb(status);
        }
    };


    struct WorkThread {
        bool started = false;
        std::thread thread;

        // 不变量 31: 加锁顺序, 先 vec_mux 再 handle_mux.
        std::mutex vec_mux;
        /* request_vec 的内存是由 work thread 来分配.
         *
         * 不变量 4: 对于其他线程而言, 其检测到若 request_vec 为 nullptr, 则表明对应的 work thread 不再工作,
         * 此时不能往 request_vec 中加入请求. 反之, 则表明 work thread 正常工作, 此时可以压入元素.
         */
        std::unique_ptr<std::vector<std::unique_ptr<TimerRequest>>> request_vec;

        std::shared_mutex handle_mux;
        /* 不变量 3: 若 async_handle != nullptr, 则表明 async_handle 指向着的 uv_async_t 已经被初始化, 此时
         * 对其调用 uv_async_send() 不会触发 SIGSEGV.
         *
         * 其实这里可以使用读写锁, 因为 uv_async_send() 是线程安全的, 但是 uv_close(), uv_async_init() 这些
         * 并不是. 也即在执行 uv_async_send() 之前加读锁, 其他操作加写锁.
         */
        uv_async_t *async_handle = nullptr;

    public:
        void AsyncSend() noexcept {
            handle_mux.lock_shared();
            if (async_handle) {
                uv_async_send(async_handle); // 当 send() 失败了怎么办???
            }
            handle_mux.unlock_shared();
            return ;
        }

        /**
         * 将 timer_req 表示的请求追加到当前 work thread 中.
         *
         * 若抛出异常, 则表明追加失败, 此时 timer_req 引用的对象没有任何变化. 若未抛出异常, 则根据 timer_req
         * 是否为空来判断请求是否成功追加, 即当不为空时, 表明请求成功追加到当前 work thread 中.
         */
        void AddRequest(std::unique_ptr<TimerRequest> &timer_req);
    };

private:
    std::atomic<TimerManagerStatus> status_{TimerManagerStatus::kInitial}; // lock-free
    std::atomic_uint seq_num{0};
    std::unique_ptr<std::vector<WorkThread>> work_threads_;

private:
    TimerManagerStatus GetStatus() noexcept {
        return status_.load(std::memory_order_relaxed);
    }

    void SetStatus(TimerManagerStatus status) noexcept {
        status_.store(status, std::memory_order_relaxed);
        return ;
    }

    void JoinAllThread() noexcept {
        for (WorkThread &work_thread : *work_threads_) {
            if (!work_thread.started)
                continue ;

            work_thread.thread.join(); // join() 理论上不会抛出异常的.
        }
    }

    void DoStopOrJoin(TimerManagerStatus op);

private:
    static void WorkThreadMain(TimerManager *timer_manager, size_t idx, std::promise<void> *p) noexcept;

    static void OnAsyncHandle(uv_async_t* handle) noexcept;
};

inline std::ostream& operator<<(std::ostream &out, TimerManager::TimerManagerStatus status) {
    out << static_cast<unsigned int>(status);
    return out;
}
