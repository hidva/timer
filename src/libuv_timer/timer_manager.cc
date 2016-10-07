#include <sstream>

#include <exception/errno_exception.h>
#include <common/utils.h>

#include "libuv_timer/timer_manager.h"

void TimerManager::Start() {
    if (thread_num <= 0) {
        THROW(EINVAL, "invalid work_thread_num: %zu", thread_num);
    }

    std::vector<std::promise<void>> promises(thread_num);
    std::vector<std::future<void>> futures(thread_num);
    for (size_t idx = 0; idx < thread_num; ++idx) {
        futures[idx] = promises[idx].get_future();
    }

    work_threads_.reset(new std::vector<WorkThread>(thread_num));
    for (size_t idx = 0; idx < thread_num; ++idx) {
        try {
            (*work_threads_)[idx].thread = std::thread(WorkThreadMain, this, idx, &promises[idx]);
            (*work_threads_)[idx].started = true;
        } catch (...) {}
    }

    for (size_t idx = 0; idx < thread_num; ++idx) {
        if ((*work_threads_)[idx].started) {
            futures[idx].get();
        }
    }

    SetStatus(TimerManagerStatus::kStarted);
    return ;
}

void TimerManager::DoStopOrJoin(TimerManagerStatus op) {
    TimerManagerStatus expect_status = TimerManagerStatus::kStarted;
    bool cas_result = status_.compare_exchange_strong(expect_status, op,
        std::memory_order_relaxed, std::memory_order_relaxed);
    if (!cas_result) {
        std::stringstream str_stream;
        str_stream << "DoStopOrJoin ERROR! op: " << op << "; client_status: " << expect_status;
        throw std::runtime_error(str_stream.str());
    }

    for (WorkThread &work_thread : *work_threads_) {
        if (!work_thread.started)
            continue;

        work_thread.AsyncSend();
    }

    JoinAllThread();

    return ;
}

TimerManager::~TimerManager() noexcept {
    TimerManagerStatus current_status = GetStatus();
    if (current_status == TimerManagerStatus::kStarted) {
        THROW(EBUSY, "current_status: kStarted");
    }

    /* 是的, 即使 current_status 不为 kInitial, 此时析构也不是安全的.
     * 但是本来就说了, ~AsyncRedisClient() 不是线程安全的.
     */
    return ;
}

void TimerManager::WorkThread::AddRequest(std::unique_ptr<TimerRequest> &timer_req) {
    vec_mux.lock();
    ON_SCOPE_EXIT(unlock_vec_mux) {
        vec_mux.unlock();
    };

    if (!request_vec) {
        return ;
    }

    request_vec->emplace_back(std::move(timer_req));
    return ;
}


void TimerManager::StartTimer(uint64_t timeout, uint64_t repeat, const timer_callback_t &cb) {
    /* 不变量 1:
     * - 若 timer_req 为空 <---> 表明 timer_req 已经成功地交给某个 work thread 了.
     * - 若 timer_req 不为空 <---> 表明 timer_req 尚未成功地交给任何一个 work thread.
     */
    std::unique_ptr<TimerRequest> timer_req(new TimerRequest(timeout, repeat, cb));

    /* 当 DoAddTo() 抛出异常的时候, 表明 timer_req 未成功交给 work_thread, 并且 timer_req 保持不变.
     * 若 DoAddTo() 未抛出异常, 则符合不变量 1.
     */
    auto DoAddTo = [&] (WorkThread &work_thread) {
        work_thread.AddRequest(timer_req);
        if (!timer_req) {
            work_thread.AsyncSend();
        }
        return ;
    };

    auto AddTo = [&] (std::vector<WorkThread>::iterator iter) noexcept -> int {
        try {
            DoAddTo(*iter);
            return (!timer_req);
        } catch (...) {
            return 0;
        }
    };

    unsigned int sn = seq_num.fetch_add(1, std::memory_order_relaxed);
    sn %= thread_num;
    LoopbackTraverse(work_threads_->begin(), work_threads_->end(), work_threads_->begin() + sn, AddTo);

    if (timer_req) {
        throw std::runtime_error("EXECUTE ERROR");
    }

    return ;
}

namespace {

struct WorkThreadContext {
    TimerManager *timer_manager = nullptr;
    TimerManager::WorkThread *work_thread = nullptr;
    uv_loop_t uv_loop;
};

/// 参见实现
uv_async_t* GetAsyncHandle(uv_loop_t *loop, uv_async_cb async_cb) noexcept {
    uv_async_t *handle = static_cast<uv_async_t*>(malloc(sizeof(uv_async_t)));
    if (handle == nullptr)
        return nullptr;

    int uv_rc = uv_async_init(loop, handle, async_cb);
    if (uv_rc < 0) {
        free(handle);
        return nullptr;
    }

    return handle;
}

inline void OnAsyncHandleClose(uv_handle_t* handle) noexcept {
    free(handle);
    return ;
}

inline void CloseAsyncHandle(uv_async_t *handle) noexcept {
    uv_close((uv_handle_t*)handle, OnAsyncHandleClose);
    return ;
}

inline void SetValueOn(std::promise<void> *p) noexcept {
    p->set_value();
    return ;
}

} // namespace

/* 根据 TimerManager::~TimerManager() 得知在 TimerManager 对象被销毁之前已经调用了 Stop()
 * 或者 Join(). 因此在 WorkThreadMain() 运行期间, timer_manager 指向的内存始终有效.
 *
 * 注意 p 的生命周期.
 */
void TimerManager::WorkThreadMain(TimerManager *timer_manager, size_t idx, std::promise<void> *p) noexcept {
    WorkThreadContext thread_ctx;
    thread_ctx.timer_manager = timer_manager;
    WorkThread *work_thread = &(*timer_manager->work_threads_)[idx];
    thread_ctx.work_thread = work_thread;

    ON_SCOPE_EXIT(on_thread_exit_1){
        if (p) {
            // 不变量 123: 若 p != nullptr, 则表明尚未对 p 调用过 set_xxx() 系列.
            SetValueOn(p);
            p = nullptr;
        }
    };

    if (uv_loop_init(&thread_ctx.uv_loop) < 0) {
        return ;
    }
    thread_ctx.uv_loop.data = &thread_ctx;
    ON_SCOPE_EXIT(on_thread_exit_2){
        int uv_rc = uv_loop_close(&thread_ctx.uv_loop);
        if (uv_rc < 0) {
            THROW(uv_rc, "uv_loop_close ERROR");
        }
    };

    // async_handle 指向的内存是手动管理的. 注意了.
    uv_async_t *async_handle = GetAsyncHandle(&thread_ctx.uv_loop, TimerManager::OnAsyncHandle);
    if (async_handle == nullptr) {
        return ;
    }
    async_handle->data = &thread_ctx;
    // 这里之所以没有注册一个 scope_exit 用来 close(async_handle), 是因为 async_handle 会在后面根据需要 close.

    std::unique_ptr<std::vector<std::unique_ptr<TimerRequest>>> request_vec(new(std::nothrow) std::vector<std::unique_ptr<TimerRequest>>);
    if (request_vec) {
        work_thread->vec_mux.lock();
        work_thread->request_vec.reset(request_vec.release());
        work_thread->vec_mux.unlock();

        work_thread->handle_mux.lock();
        work_thread->async_handle = async_handle;
        work_thread->handle_mux.unlock();
    } else {
        CloseAsyncHandle(async_handle);
    }

    SetValueOn(p);
    p = nullptr;

    while (uv_run(&thread_ctx.uv_loop, UV_RUN_DEFAULT)) {
        ;
    }
    return ;
}

namespace {

void OnTimerClose(uv_handle_t* handle) noexcept;

inline void CloseTimer(uv_timer_t *timer_handle) noexcept {
    uv_close((uv_handle_t*)timer_handle, OnTimerClose);
    return ;
}

void OnTimer(uv_timer_t *handle) noexcept {
    TimerManager::TimerRequest *timer_req = (TimerManager::TimerRequest *)handle->data;
    uv_loop_t *uv_loop = handle->loop;

    unsigned int cb_rc = timer_req->Call(0);

    if (timer_req->repeat == 0 || (cb_rc & TimerManager::kStopTimer)) {
        CloseTimer(handle);
    }

    if (cb_rc & TimerManager::kMoreThan1MS) {
        uv_update_time(uv_loop);
    }
    return ;
}

void OnTimerClose(uv_handle_t* handle) noexcept {
    uv_timer_t* timer_handle = (uv_timer_t*)handle;

    TimerManager::TimerRequest *timer_req = (TimerManager::TimerRequest *)timer_handle->data;
    timer_req->Call(1);
    delete timer_req;

    free(timer_handle);
    return ;
}

} // namespace

void TimerManager::OnAsyncHandle(uv_async_t* handle) noexcept {
    WorkThreadContext *thread_ctx = (WorkThreadContext*)handle->data;
    WorkThread *work_thread = thread_ctx->work_thread;
    std::unique_ptr<std::vector<std::unique_ptr<TimerRequest>>> request_vec;

    auto HandleRequest = [&] (std::unique_ptr<TimerRequest> &request) noexcept {
        uv_timer_t *timer_handle = (uv_timer_t*)malloc(sizeof(uv_timer_t));
        if (!timer_handle) {
            request->Call(1);
            return ;
        }
        // 注意, 释放 free(timer_handle);

        int uv_rc = uv_timer_init(&thread_ctx->uv_loop, timer_handle);
        if (uv_rc < 0) {
            free(timer_handle);
            request->Call(1);
            return ;
        }
        TimerRequest *timer_req = request.release();
        timer_handle->data = timer_req;
        // 此后, TimerRequest 的内存由 timer_handle 引用; 而 timer_handle 由 uv_loop 引用.

        uv_rc = uv_timer_start(timer_handle, OnTimer, timer_req->timeout, timer_req->repeat);
        if (uv_rc < 0) {
            // 当 timer_handle 被 close 时, TimerRequest->cb 会被调用, 所以这里不再调用.
            CloseTimer(timer_handle);
        }

        return ;
    };

    auto HandleRequests = [&] (std::vector<std::unique_ptr<TimerRequest>> &requests) noexcept {
        for (auto &request : requests) {
            HandleRequest(request);
        }
        return ;
    };

    auto OnRequest = [&] () noexcept {
//        uv_update_time(&thread_ctx->uv_loop);

        auto *tmp = new(std::nothrow) std::vector<std::unique_ptr<TimerRequest>>;

        work_thread->vec_mux.lock();
        request_vec.reset(work_thread->request_vec.release()); // noexcept
        work_thread->request_vec.reset(tmp); // noexcept
        work_thread->vec_mux.unlock();

        if (request_vec) {
            HandleRequests(*request_vec);
        }

        return ;
    };

    auto OnJoin = [&] () noexcept {
//        uv_update_time(&thread_ctx->uv_loop);

        work_thread->vec_mux.lock();
        request_vec.reset(work_thread->request_vec.release()); // noexcept
        work_thread->vec_mux.unlock();

        work_thread->handle_mux.lock();
        work_thread->async_handle = nullptr;
        work_thread->handle_mux.unlock();


        if (request_vec) {
            HandleRequests(*request_vec);
        }

        CloseAsyncHandle(handle);
        return ;
    };

    auto OnStop = [&] () noexcept {
        work_thread->vec_mux.lock();
        request_vec.reset(work_thread->request_vec.release()); // noexcept
        work_thread->vec_mux.unlock();

        work_thread->handle_mux.lock();
        work_thread->async_handle = nullptr;
        work_thread->handle_mux.unlock();

        if (request_vec) {
            for (auto &request : *request_vec) {
                request->Call(1);
            }
        }

        CloseAsyncHandle(handle);
        return ;
    };

    switch (thread_ctx->timer_manager->GetStatus(/* std::memory_order_relaxed */)) {
    case TimerManagerStatus::kStarted:
        OnRequest();
        break;
    case TimerManagerStatus::kStop:
        OnStop();
        break;
    case TimerManagerStatus::kJoin:
        OnJoin();
        break;
    default: // unreachable
        throw std::runtime_error("富强, 民主, 文明, 和谐, 自由, 平等, 公正, 法治, 爱国, 敬业, 诚信, 友善");
    }

    return ;
}
