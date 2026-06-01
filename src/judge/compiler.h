// 代码编译器 — 封装 g++ 编译用户提交的 C++ 源文件。
#pragma once

#include <string>

namespace vibeoj {
struct CompileResult {
  bool success;
  std::string output;  // compiler output (stdout + stderr)
};

CompileResult compile_code(const std::string& source_path, const std::string& binary_path);

}  // namespace vibeoj
