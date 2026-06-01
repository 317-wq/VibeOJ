// 判题线程池实现 — 工作线程循环取任务，完成 编译 → 沙箱执行 → 数据库更新。
#include "judge/runner.h"

#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "common/log.h"
#include "db/connection_pool.h"
#include "db/dao.h"
#include "judge/compiler.h"
#include "judge/sandbox.h"
#include "model/problem.h"
#include "model/submission.h"

namespace vibeoj {

struct JudgeRunner::Impl {
  std::vector<std::thread> workers;
  std::queue<int64_t> pending;
  std::mutex mtx;
  std::condition_variable cv;
  bool running = false;

  // 单次判题流程：编译 → 逐个运行测试用例 → 更新数据库。
  void process(int64_t submission_id) {
    // 获取数据库连接
    auto guard = ConnectionPool::instance().acquire();
    if (!guard) {
      LOG_ERROR("JudgeRunner: failed to acquire DB connection for sub %ld",
                submission_id);
      return;
    }

    // 查询提交记录
    auto sub = SubmissionDAO::FindById(*guard, submission_id);
    if (!sub.has_value()) {
      LOG_ERROR("JudgeRunner: submission %ld not found", submission_id);
      return;
    }

    // 查询题目信息（获取时间/内存限制）
    auto problem = ProblemDAO::FindById(*guard, sub->problem_id);
    if (!problem.has_value()) {
      LOG_ERROR("JudgeRunner: problem %ld not found for sub %ld",
                sub->problem_id, submission_id);
      SubmissionDAO::UpdateStatus(*guard, submission_id,
                                  SubmissionStatus::system_error);
      return;
    }

    // 查询测试用例
    auto test_cases = TestCaseDAO::FindByProblemId(*guard, sub->problem_id);
    if (test_cases.empty()) {
      LOG_ERROR("JudgeRunner: no test cases for problem %ld (sub %ld)",
                sub->problem_id, submission_id);
      SubmissionDAO::UpdateStatus(*guard, submission_id,
                                  SubmissionStatus::system_error);
      return;
    }

    int total_cases = static_cast<int>(test_cases.size());

    // 确保临时目录存在
    std::filesystem::path tmp_dir = "/tmp/judge";
    std::error_code ec;
    std::filesystem::create_directories(tmp_dir, ec);
    if (ec) {
      LOG_ERROR("JudgeRunner: cannot create /tmp/judge: %s", ec.message().c_str());
      SubmissionDAO::UpdateStatus(*guard, submission_id,
                                  SubmissionStatus::system_error);
      return;
    }

    std::string source_path = (tmp_dir / (std::to_string(submission_id) + ".cpp")).string();
    std::string binary_path = (tmp_dir / (std::to_string(submission_id) + ".out")).string();

    // 将源代码写入临时文件
    {
      std::ofstream src(source_path);
      src << sub->code;
      if (!src.good()) {
        LOG_ERROR("JudgeRunner: failed to write source for sub %ld", submission_id);
        SubmissionDAO::UpdateStatus(*guard, submission_id,
                                    SubmissionStatus::system_error);
        return;
      }
    }

    // ── 编译阶段 ──────────────────────────────────────────
    SubmissionDAO::UpdateStatus(*guard, submission_id, SubmissionStatus::compiling);
    auto compile_result = compile_code(source_path, binary_path);

    if (!compile_result.success) {
      SubmissionDAO::UpdateStatus(*guard, submission_id,
                                  SubmissionStatus::compile_error,
                                  0, total_cases, 0, 0,
                                  compile_result.output);
      std::filesystem::remove(source_path);
      LOG_INFO("JudgeRunner: sub %ld compile_error", submission_id);
      return;
    }

    // ── 运行阶段 ──────────────────────────────────────────
    SubmissionDAO::UpdateStatus(*guard, submission_id, SubmissionStatus::running);

    int passed = 0;
    int max_time = 0;
    int max_memory = 0;
    std::string final_diff;
    SubmissionStatus final_status = SubmissionStatus::accepted;

    for (const auto& tc : test_cases) {
      auto result = run_in_sandbox(binary_path, tc.input, tc.expected_output,
                                   problem->time_limit_ms,
                                   problem->memory_limit_kb);

      max_time = std::max(max_time, result.time_used_ms);
      max_memory = std::max(max_memory, result.memory_used_kb);

      if (result.status == "accepted") {
        passed++;
      } else {
        final_status = submission_status_from_string(result.status);
        if (result.status == "wrong_answer") {
          final_diff = result.diff_output;
        }
        break;  // 遇到第一个失败即停止
      }
    }

    // ── 更新数据库 ────────────────────────────────────────
    SubmissionDAO::UpdateStatus(*guard, submission_id, final_status,
                                passed, total_cases, max_time, max_memory,
                                "", final_diff);

    LOG_INFO("JudgeRunner: sub %ld judged: %s (%d/%d) time=%dms mem=%dkb",
             submission_id, submission_status_to_string(final_status).c_str(),
             passed, total_cases, max_time, max_memory);

    // 清理临时文件
    std::filesystem::remove(source_path);
    std::filesystem::remove(binary_path);
  }

  // 工作线程主循环
  void worker_loop() {
    while (true) {
      int64_t submission_id;
      {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [this] { return !running || !pending.empty(); });
        if (!running && pending.empty()) return;
        if (pending.empty()) continue;
        submission_id = pending.front();
        pending.pop();
      }
      process(submission_id);
    }
  }
};

JudgeRunner& JudgeRunner::instance() {
  static JudgeRunner runner;
  return runner;
}

void JudgeRunner::start(int num_workers) {
  if (impl_) return;  // already started
  impl_ = std::make_unique<Impl>();
  impl_->running = true;

  if (num_workers <= 0) {
    num_workers = static_cast<int>(std::thread::hardware_concurrency());
    if (num_workers <= 0) num_workers = 4;
  }

  for (int i = 0; i < num_workers; i++) {
    impl_->workers.emplace_back(&Impl::worker_loop, impl_.get());
  }

  LOG_INFO("JudgeRunner started with %d workers", num_workers);
}

void JudgeRunner::stop() {
  if (!impl_) return;
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->running = false;
  }
  impl_->cv.notify_all();
  for (auto& t : impl_->workers) {
    if (t.joinable()) t.join();
  }
  impl_.reset();
  LOG_INFO("JudgeRunner stopped");
}

void JudgeRunner::enqueue(int64_t submission_id) {
  if (!impl_) return;
  {
    std::lock_guard<std::mutex> lock(impl_->mtx);
    impl_->pending.push(submission_id);
  }
  impl_->cv.notify_one();
}

}  // namespace vibeoj
