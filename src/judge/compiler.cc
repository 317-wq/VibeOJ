// g++ 编译实现 — 通过 popen 调用 g++ 编译 C++ 源文件，捕获编译输出。
#include "judge/compiler.h"

#include <array>
#include <cstdio>
#include <cstdlib>

namespace vibeoj {

CompileResult compile_code(const std::string& source_path,
                           const std::string& binary_path) {
  // 构建编译命令：-O2 优化，-Wall 开启常用警告，-fmax-errors=20 限制错误数量
  std::string cmd = "g++ -std=c++17 -O2 -Wall -fmax-errors=20 -o "
                    + binary_path + " " + source_path + " 2>&1";

  std::array<char, 4096> buffer{};
  std::string output;
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    return {false, "failed to execute g++"};
  }
  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    output += buffer.data();
  }
  int ret = pclose(pipe);
  // pclose 返回子进程退出状态：0 = 编译成功，非 0 = 编译失败
  bool success = (ret == 0);
  return {success, output};
}

}  // namespace vibeoj
