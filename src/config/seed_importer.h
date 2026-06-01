// 种子数据自动导入器 — 将解析后的种子数据写入数据库。
// 启动时若 problems 表为空则导入，否则跳过（幂等）。
#pragma once

#include "config/seeder.h"

namespace vibeoj {

// ImportResult 记录本次导入的题目和测试用例数量。
struct ImportResult {
  int problems_imported = 0;
  int test_cases_imported = 0;
};

// 将解析后的种子数据导入数据库。
// 若 problems 表已有数据则跳过（幂等），确保重复启动不会重复导入。
// 返回实际导入的题目数和测试用例数。
ImportResult import_seed_data(SeedData& data);

}  // namespace vibeoj
