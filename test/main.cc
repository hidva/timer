
#include <glog/logging.h>
#include <gflags/gflags.h>

#include "libuv_timer/timer_manager.h"

DEFINE_int32(work_thread_num, 4, "work thread num");

struct TimerCB {
    int timer_id = 0;
    int times = 0;
    int total_times = 5;

public:
    TimerCB(int id, int total_times_arg) noexcept :
        timer_id(id),
        total_times(total_times_arg) {
        LOG(INFO) << "id: " << timer_id << "; times: " << times << "; total_times: " << total_times;
    }

    unsigned int operator()(unsigned int status) noexcept {
        LOG(INFO) << "id: " << timer_id << "; times: " << times << "; total_times: " << total_times
                  << "; status: " << status;

        if (++times >= total_times)
            return TimerManager::kStopTimer;
        return 0;
    }
};

int main(int argc, char **argv) {
    google::SetUsageMessage("TimerManager Test");
    google::SetVersionString("1.0.0");
    google::ParseCommandLineFlags(&argc, &argv, false);

    google::InitGoogleLogging(argv[0]);
    google::InstallFailureSignalHandler();

    TimerManager timer_manager;
    timer_manager.thread_num = FLAGS_work_thread_num;
    timer_manager.Start();

    timer_manager.StartTimer(0, 0, TimerCB(1, 3));
    timer_manager.StartTimer(3000, 0, TimerCB(2, 3));
    timer_manager.StartTimer(0, 2000, TimerCB(3, 3));
    timer_manager.StartTimer(1000, 2000, TimerCB(4, 3));

    timer_manager.Join();
    return 0;
}

