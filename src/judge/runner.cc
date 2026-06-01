// 线程池实现（std::queue + mutex + condition_variable）— Phase 2。
#include "judge/runner.h"

namespace vibeoj {

JudgeRunner& JudgeRunner::instance() {
  static JudgeRunner runner;
  return runner;
}

void JudgeRunner::start(int) {}     // Phase 2
void JudgeRunner::stop() {}         // Phase 2
void JudgeRunner::enqueue(int64_t) {} // Phase 2

}  // namespace vibeoj
