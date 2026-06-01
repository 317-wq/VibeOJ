// 判题线程池 — 工作线程从提交队列取任务，完成编译和沙箱执行。
#pragma once

#include <cstdint>
#include <functional>

namespace vibeoj {
class JudgeRunner {
 public:
  static JudgeRunner& instance();
  void start(int num_workers);
  void stop();
  void enqueue(int64_t submission_id);
};

}  // namespace vibeoj
