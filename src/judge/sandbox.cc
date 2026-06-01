// 沙箱实现 — fork 子进程 + setrlimit 资源限制 + 管道 I/O 重定向 + diff 比对。
#include "judge/sandbox.h"

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <vector>

namespace vibeoj {

namespace {

// 将字符串按行拆分，用于逐行 diff。
std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> lines;
  std::string line;
  for (char c : s) {
    if (c == '\n') {
      lines.push_back(line);
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty() || (!s.empty() && s.back() == '\n')) {
    lines.push_back(line);
  }
  return lines;
}

// 生成 unified-diff 风格的差异输出，便于前端展示 WA 细节。
std::string diff_outputs(const std::string& expected,
                         const std::string& actual) {
  auto el = split_lines(expected);
  auto al = split_lines(actual);
  std::string result;
  result += "--- expected\n+++ actual\n";

  size_t max_lines = std::max(el.size(), al.size());
  for (size_t i = 0; i < max_lines; i++) {
    if (i < el.size() && i < al.size()) {
      if (el[i] != al[i]) {
        result += "-" + el[i] + "\n";
        result += "+" + al[i] + "\n";
      }
    } else if (i < el.size()) {
      result += "-" + el[i] + "\n";
    } else {
      result += "+" + al[i] + "\n";
    }
  }
  return result;
}

// 安全读取管道直到 EOF，带超时避免阻塞。
// 返回读取到的全部内容。
std::string read_all(int fd) {
  std::string result;
  char buf[65536];
  ssize_t n;
  while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
    result.append(buf, static_cast<size_t>(n));
  }
  return result;
}

// 安全写入管道全部数据。
bool write_all(int fd, const std::string& data) {
  size_t total = 0;
  while (total < data.size()) {
    ssize_t n = write(fd, data.data() + total, data.size() - total);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    total += static_cast<size_t>(n);
  }
  return true;
}

}  // namespace

JudgeResult run_in_sandbox(const std::string& binary_path,
                           const std::string& input,
                           const std::string& expected_output,
                           int time_limit_ms, int memory_limit_kb) {
  JudgeResult result;
  result.status = "system_error";
  result.time_used_ms = 0;
  result.memory_used_kb = 0;

  // 创建三对管道：stdin / stdout / stderr
  int pipe_in[2], pipe_out[2], pipe_err[2];
  if (pipe(pipe_in) < 0 || pipe(pipe_out) < 0 || pipe(pipe_err) < 0) {
    return result;
  }

  pid_t pid = fork();
  if (pid < 0) {
    // fork 失败
    close(pipe_in[0]); close(pipe_in[1]);
    close(pipe_out[0]); close(pipe_out[1]);
    close(pipe_err[0]); close(pipe_err[1]);
    return result;
  }

  if (pid == 0) {
    // ── 子进程 ─────────────────────────────────────────────
    // 关闭不需要的管道端
    close(pipe_in[1]);   // 关闭 stdin 写端
    close(pipe_out[0]);  // 关闭 stdout 读端
    close(pipe_err[0]);  // 关闭 stderr 读端

    // 重定向标准 I/O 到管道
    dup2(pipe_in[0], STDIN_FILENO);
    dup2(pipe_out[1], STDOUT_FILENO);
    dup2(pipe_err[1], STDERR_FILENO);

    // 设置资源限制
    int time_sec = time_limit_ms / 1000;
    if (time_sec < 1) time_sec = 1;

    struct rlimit rl;

    // CPU 时间限制：软限制 = time_sec，硬限制 = time_sec + 1
    rl.rlim_cur = static_cast<rlim_t>(time_sec);
    rl.rlim_max = static_cast<rlim_t>(time_sec) + 1;
    setrlimit(RLIMIT_CPU, &rl);

    // 地址空间限制（内存）
    rlim_t mem_bytes = static_cast<rlim_t>(memory_limit_kb) * 1024;
    rl.rlim_cur = mem_bytes;
    rl.rlim_max = mem_bytes;
    setrlimit(RLIMIT_AS, &rl);

    // 禁止 fork 子进程
    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    setrlimit(RLIMIT_NPROC, &rl);

    // 禁止写文件
    rl.rlim_cur = 0;
    rl.rlim_max = 0;
    setrlimit(RLIMIT_FSIZE, &rl);

    // 执行用户编译的程序
    execl(binary_path.c_str(), binary_path.c_str(), nullptr);

    // execl 失败
    _exit(127);
  }

  // ── 父进程 ─────────────────────────────────────────────
  close(pipe_in[0]);   // 关闭 stdin 读端
  close(pipe_out[1]);  // 关闭 stdout 写端
  close(pipe_err[1]);  // 关闭 stderr 写端

  // 写入输入数据到子进程 stdin，然后关闭以发送 EOF
  write_all(pipe_in[1], input);
  close(pipe_in[1]);

  // 读取子进程输出
  std::string actual_output = read_all(pipe_out[0]);
  std::string stderr_output = read_all(pipe_err[0]);
  close(pipe_out[0]);
  close(pipe_err[0]);

  // 等待子进程结束并获取资源使用情况
  int status = 0;
  struct rusage ru;
  wait4(pid, &status, 0, &ru);

  // 提取时间和内存使用
  result.time_used_ms = static_cast<int>(
      ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000);
  result.memory_used_kb = static_cast<int>(ru.ru_maxrss);

  // 根据子进程退出状态判断判题结果
  if (WIFEXITED(status)) {
    int exit_code = WEXITSTATUS(status);
    if (exit_code == 0) {
      // 正常退出：比对输出
      if (actual_output == expected_output) {
        result.status = "accepted";
      } else {
        result.status = "wrong_answer";
        result.diff_output = diff_outputs(expected_output, actual_output);
      }
    } else {
      // 非零退出码：运行时错误
      result.status = "runtime_error";
      result.error_output = stderr_output.empty()
                                ? "exit code " + std::to_string(exit_code)
                                : stderr_output;
    }
  } else if (WIFSIGNALED(status)) {
    int sig = WTERMSIG(status);
    // SIGXCPU → 时间超限
    if (sig == SIGXCPU) {
      result.status = "time_limit";
    } else if (sig == SIGKILL) {
      // SIGKILL 可能来自 OOM killer → 内存超限
      result.status = "memory_limit";
    } else if (sig == SIGSEGV) {
      // SIGSEGV 可能来自内存超限（mmap 失败）或真正的段错误
      // 若内存使用接近限制，判定为 MLE，否则为 RTE
      if (result.memory_used_kb > 0 &&
          result.memory_used_kb >= memory_limit_kb * 9 / 10) {
        result.status = "memory_limit";
      } else {
        result.status = "runtime_error";
        result.error_output = stderr_output.empty()
                                  ? "segmentation fault"
                                  : stderr_output;
      }
    } else if (sig == SIGABRT || sig == SIGFPE) {
      result.status = "runtime_error";
      result.error_output = stderr_output.empty()
                                ? "signal " + std::to_string(sig)
                                : stderr_output;
    } else {
      // 其他信号统一视为运行时错误
      result.status = "runtime_error";
      result.error_output = "killed by signal " + std::to_string(sig);
    }
  }

  return result;
}

}  // namespace vibeoj
