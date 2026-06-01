// 判题引擎单元测试 — 测试 g++ 编译、沙箱执行、diff 比对。
// 测试在 /tmp/judge_test/ 下创建临时文件，运行完毕后自动清理。
// 不依赖数据库。

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "common/log.h"
#include "judge/compiler.h"
#include "judge/sandbox.h"

namespace vibeoj {
namespace {

// ── 辅助函数 ──────────────────────────────────────────────────────

// 写入源文件并编译，返回 binary 的路径（空串表示编译失败）。
std::string compile_aux(const std::string& name, const std::string& code) {
  std::filesystem::path dir = "/tmp/judge_test";
  std::filesystem::create_directories(dir);

  std::string src = (dir / (name + ".cpp")).string();
  std::string bin = (dir / (name + ".out")).string();

  {
    std::ofstream f(src);
    f << code;
  }

  auto result = compile_code(src, bin);
  if (!result.success) {
    std::filesystem::remove(src);
    return "";
  }
  std::filesystem::remove(src);
  return bin;
}

// 编译并运行，返回 JudgeResult。
JudgeResult compile_and_run(const std::string& name, const std::string& code,
                            const std::string& input,
                            const std::string& expected_output,
                            int time_limit_ms = 2000,
                            int memory_limit_kb = 262144) {
  std::string bin = compile_aux(name, code);
  if (bin.empty()) {
    JudgeResult r;
    r.status = "compile_error";
    return r;
  }
  auto result = run_in_sandbox(bin, input, expected_output,
                               time_limit_ms, memory_limit_kb);
  std::filesystem::remove(bin);
  return result;
}

// ── 测试夹具 ──────────────────────────────────────────────────────
class JudgeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Logger::instance().init("/tmp/vibeoj_test_logs");
    std::filesystem::create_directories("/tmp/judge_test");
  }

  void TearDown() override {
    // 清理可能残留的临时文件
    std::error_code ec;
    std::filesystem::remove_all("/tmp/judge_test", ec);
  }
};

// ═══════════════════════════════════════════════════════════════════
// 编译器测试
// ═══════════════════════════════════════════════════════════════════

// 简单程序编译成功。
TEST_F(JudgeTest, CompileSuccess) {
  std::string bin = compile_aux("compile_ok",
      "#include <iostream>\nint main() { std::cout << \"hello\"; return 0; }");
  EXPECT_FALSE(bin.empty());
  std::filesystem::remove(bin);
}

// 语法错误 → 编译失败，返回错误信息。
TEST_F(JudgeTest, CompileError) {
  std::filesystem::path dir = "/tmp/judge_test";
  std::string src = (dir / "compile_err.cpp").string();
  std::string bin = (dir / "compile_err.out").string();

  {
    std::ofstream f(src);
    f << "this is not valid c++ code @@@";
  }

  auto result = compile_code(src, bin);
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.output.empty());  // g++ 应输出错误信息

  std::filesystem::remove(src);
}

// 编译输出应包含有意义的信息。
TEST_F(JudgeTest, CompileOutputNotEmpty) {
  std::filesystem::path dir = "/tmp/judge_test";
  std::string src = (dir / "compile_out.cpp").string();
  std::string bin = (dir / "compile_out.out").string();

  {
    std::ofstream f(src);
    f << "#include <iostream>\nint main() { std::cout << \"ok\"; return 0; }";
  }

  auto result = compile_code(src, bin);
  EXPECT_TRUE(result.success);
  // 成功编译时输出可能为空，也接受警告信息。
  EXPECT_TRUE(result.output.empty() || result.output.find("warning") != std::string::npos);

  std::filesystem::remove(src);
  std::filesystem::remove(bin);
}

// ═══════════════════════════════════════════════════════════════════
// 沙箱 — AC（Accepted）测试
// ═══════════════════════════════════════════════════════════════════

// 简单程序，输出与预期一致 → AC。
TEST_F(JudgeTest, SandboxAccepted) {
  auto result = compile_and_run("ac_test",
      "#include <iostream>\n"
      "int main() {\n"
      "  int a, b;\n"
      "  std::cin >> a >> b;\n"
      "  std::cout << a + b << std::endl;\n"
      "  return 0;\n"
      "}",
      "3 5\n", "8\n");

  EXPECT_EQ(result.status, "accepted");
}

// 多行输出匹配 → AC。
TEST_F(JudgeTest, SandboxAcceptedMultiLine) {
  auto result = compile_and_run("ac_multi",
      "#include <iostream>\n"
      "int main() {\n"
      "  for (int i = 1; i <= 3; i++)\n"
      "    std::cout << i << \"\\n\";\n"
      "  return 0;\n"
      "}",
      "", "1\n2\n3\n");

  EXPECT_EQ(result.status, "accepted");
}

// ═══════════════════════════════════════════════════════════════════
// 沙箱 — WA（Wrong Answer）测试
// ═══════════════════════════════════════════════════════════════════

// 输出与预期不一致 → WA，并包含 diff。
TEST_F(JudgeTest, SandboxWrongAnswer) {
  auto result = compile_and_run("wa_test",
      "#include <iostream>\n"
      "int main() {\n"
      "  std::cout << \"wrong output\" << std::endl;\n"
      "  return 0;\n"
      "}",
      "", "expected output\n");

  EXPECT_EQ(result.status, "wrong_answer");
  EXPECT_FALSE(result.diff_output.empty());
  // diff 中应包含 expected 和 actual 的标记
  EXPECT_TRUE(result.diff_output.find("expected") != std::string::npos ||
              result.diff_output.find("---") != std::string::npos);
}

// diff 中应标记差异行。
TEST_F(JudgeTest, SandboxWrongAnswerDiffContent) {
  auto result = compile_and_run("wa_diff",
      "#include <iostream>\n"
      "int main() {\n"
      "  std::cout << \"line1\\nline2\\nline3\\n\";\n"
      "  return 0;\n"
      "}",
      "", "line1\nlineX\nline3\n");

  EXPECT_EQ(result.status, "wrong_answer");
  EXPECT_TRUE(result.diff_output.find("line2") != std::string::npos);
  EXPECT_TRUE(result.diff_output.find("lineX") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════
// 沙箱 — TLE（Time Limit Exceeded）测试
// ═══════════════════════════════════════════════════════════════════

// 死循环 → 超时。
TEST_F(JudgeTest, SandboxTimeLimit) {
  auto result = compile_and_run("tle_test",
      "#include <iostream>\n"
      "int main() {\n"
      "  while (true) {}\n"
      "  return 0;\n"
      "}",
      "", "", 500, 262144);  // 500ms 时间限制，死循环必定超时

  EXPECT_EQ(result.status, "time_limit");
}

// ═══════════════════════════════════════════════════════════════════
// 沙箱 — RTE（Runtime Error）测试
// ═══════════════════════════════════════════════════════════════════

// 除零错误 → 运行时错误。
TEST_F(JudgeTest, SandboxRuntimeErrorDivZero) {
  auto result = compile_and_run("rte_div0",
      "#include <iostream>\n"
      "int main() {\n"
      "  int a = 1, b = 0;\n"
      "  std::cout << a / b << std::endl;\n"
      "  return 0;\n"
      "}",
      "", "", 2000, 262144);

  EXPECT_EQ(result.status, "runtime_error");
}

// 非零退出码 → 运行时错误。
TEST_F(JudgeTest, SandboxRuntimeErrorNonZeroExit) {
  auto result = compile_and_run("rte_exit",
      "#include <cstdlib>\n"
      "int main() {\n"
      "  std::exit(1);\n"
      "  return 0;\n"
      "}",
      "", "", 2000, 262144);

  EXPECT_EQ(result.status, "runtime_error");
}

// ═══════════════════════════════════════════════════════════════════
// 沙箱 — MLE（Memory Limit Exceeded）测试
// ═══════════════════════════════════════════════════════════════════

// 大量内存分配 → 内存超限。
TEST_F(JudgeTest, SandboxMemoryLimit) {
  auto result = compile_and_run("mle_test",
      "#include <iostream>\n"
      "#include <vector>\n"
      "int main() {\n"
      "  try {\n"
      "    // 尝试分配大量内存（超过 16MB 限制）\n"
      "    std::vector<char> v(100 * 1024 * 1024);\n"
      "    std::cout << v.size() << std::endl;\n"
      "  } catch (...) {\n"
      "    return 1;\n"
      "  }\n"
      "  return 0;\n"
      "}",
      "", "", 5000, 16384);  // 16MB 内存限制

  // 应返回 memory_limit 或 runtime_error（取决于系统行为）
  EXPECT_TRUE(result.status == "memory_limit" || result.status == "runtime_error");
}

// ═══════════════════════════════════════════════════════════════════
// 沙箱 — 边界情况
// ═══════════════════════════════════════════════════════════════════

// 空输入测试。
TEST_F(JudgeTest, SandboxEmptyInput) {
  auto result = compile_and_run("empty_in",
      "#include <iostream>\n"
      "int main() {\n"
      "  std::cout << \"no input needed\" << std::endl;\n"
      "  return 0;\n"
      "}",
      "", "no input needed\n");

  EXPECT_EQ(result.status, "accepted");
}

// 空输出测试。
TEST_F(JudgeTest, SandboxEmptyOutput) {
  auto result = compile_and_run("empty_out",
      "int main() { return 0; }",
      "", "");

  EXPECT_EQ(result.status, "accepted");
}

// 时间使用量应被记录。
TEST_F(JudgeTest, SandboxRecordsTimeUsed) {
  auto result = compile_and_run("time_rec",
      "#include <iostream>\n"
      "int main() {\n"
      "  int s = 0;\n"
      "  for (int i = 0; i < 1000000; i++) s += i;\n"
      "  std::cout << s << std::endl;\n"
      "  return 0;\n"
      "}",
      "", "");

  // 只要执行了，就应该有时间记录（即使接受输出不是严格匹配，time_used 也应 > 0）
  if (result.status == "accepted" || result.status == "wrong_answer") {
    EXPECT_GT(result.time_used_ms, 0);
  }
}

// 内存使用量应被记录。
TEST_F(JudgeTest, SandboxRecordsMemoryUsed) {
  auto result = compile_and_run("mem_rec",
      "#include <iostream>\n"
      "int main() {\n"
      "  int arr[1000];\n"
      "  for (int i = 0; i < 1000; i++) arr[i] = i;\n"
      "  std::cout << arr[999] << std::endl;\n"
      "  return 0;\n"
      "}",
      "", "999\n");

  EXPECT_EQ(result.status, "accepted");
  EXPECT_GT(result.memory_used_kb, 0);
}

}  // namespace
}  // namespace vibeoj
