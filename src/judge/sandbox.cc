// 沙箱实现（fork/execve/setrlimit/pipe）— Phase 2。
#include "judge/sandbox.h"

namespace vibeoj {

JudgeResult run_in_sandbox(const std::string&, const std::string&,
                           const std::string&, int, int) {
  return {"system_error", 0, 0, "", ""};  // Phase 2
}

}  // namespace vibeoj
