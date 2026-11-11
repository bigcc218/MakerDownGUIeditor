# 参与贡献

感谢你愿意帮助 MDGE Studio。下面是这个仓库的最小工作流。

## 报告问题

提交 Issue 前请先搜索已有 Issue。报告时尽量包含：

- Windows 版本与屏幕缩放比例（例如 Windows 11、150%）。
- MinGW-w64 g++ 版本（`g++ --version`）。
- 复现步骤、期望结果与实际结果。
- 如果涉及 `.mdge` 文件，请附上一个最小的复现项目。

## 提交代码

1. Fork 本仓库并从 `main` 创建分支，分支名建议使用 `feat/…`、`fix/…` 或 `docs/…`。
2. 在仓库根目录运行 `build.bat`，确认程序、`core_tests` 与 `association_tests` 都能编译通过且测试全绿。
3. 行为变更请同时补充相应测试：核心逻辑（模型、`.mdge` 读写、Markdown 解析与导出、路径解析）写入 `tests/core_tests.cpp`；文件关联状态机、注册值和恢复策略写入 `tests/association_tests.cpp`。核心逻辑应当保持可测试且不依赖窗口。
4. 如果改动了 `.mdge` 文件格式，必须同步更新 [docs/MDGE_FORMAT.md](docs/MDGE_FORMAT.md)，并且遵守其中的兼容性规则：不改变已有 tag 的含义、读取端跳过未知 tag、`END` 保持为最后一条记录。
5. 提交 PR 前请先 `git rebase` 或合并最新的 `main`，并在 PR 描述中说明动机与验证方式。

## 代码风格

- C++20，4 空格缩进，单行宽度约 90 列。
- 文件与目录命名保持现状：类/函数使用 `PascalCase`，局部变量使用 `camelCase`。
- 保持“只依赖系统 DLL”的约束：不要引入第三方运行时依赖。
- 界面图标一律用几何图形和文字自绘，不使用 emoji。
- 用户可见的文案使用中文；注释保持简洁、说明“为什么”而不是“做了什么”。

## 许可证

提交的代码默认以 MIT 许可证授权，见 [LICENSE](LICENSE)。
