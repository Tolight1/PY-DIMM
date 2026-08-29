# PY-DIMM Codex 开发规则

## 仓库约定

- `master` 是主分支，应保持可用并与 GitHub 上的 `origin/master` 同步。
- 开始开发前先检查工作区状态；确认没有需要保留的本地改动后，再更新 `master` 并创建新的功能分支。
- 功能分支统一使用 `codex/` 前缀，例如 `codex/fix-star-detection`。
- 当前正式源码目录是 `src/`，测试目录是 `tests/`。不要复制或创建 `src1`、`src2`、`src_new` 等完整源码副本；历史版本通过 Git 提交和分支查看。
- `build/`、`.pytest_cache/`、`__pycache__/` 等生成物不应提交；以仓库根目录的 `.gitignore` 为准。
- 开发前后都要检查 `git status`，保留用户已有的未跟踪文件。除非用户明确要求，不要删除、覆盖、批量整理或顺手提交这些文件。
- 只修改与当前任务相关的文件，避免无关格式化和大范围重写。
- GitHub 分支、提交、推送和 PR 流程见项目根目录的 `CODEX_GITHUB_WORKFLOW.md`。
- 不要把个人令牌写入项目文件、命令行脚本或聊天内容。

## 推荐开发流程

1. 检查并同步主分支：

   ```powershell
   git status --short --branch
   git switch master
   git pull --ff-only origin master
   ```

   如果工作区有用户未提交的改动，先保留并确认处理方式，不要强行切换、覆盖或清理。

2. 创建功能分支：

   ```powershell
   git switch -c codex/<功能名称>
   ```

3. 修改代码并运行与任务相关的测试，然后检查：

   ```powershell
   git status
   git diff
   ```

4. 只暂存当前任务相关文件并提交。提交信息使用清晰的类型前缀，例如 `feat:`、`fix:`、`docs:`、`test:` 或 `chore:`。是否创建分支、提交、推送或创建 PR，按用户请求和 `CODEX_GITHUB_WORKFLOW.md` 执行。

5. 验证通过后，推送功能分支并创建目标为 `master` 的 Pull Request。PR 描述应说明改动内容、验证方式和已知问题；未经用户确认不要合并 PR。

6. PR 合并后回到本地 `master` 并同步：

   ```powershell
   git switch master
   git pull --ff-only origin master
   ```

   确认不再需要后，再删除已合并的本地和远程功能分支。不要为了清理而删除用户的未跟踪文件或未合并分支。

## 验证要求

- 构建或验证前，必须先读取项目根目录的 `CODEX_BUILD_HANDOFF.md`，并按其中的 VS18 x64 Release 流程执行；如与本文件冲突，以 `AGENTS.md` 为准。
- 涉及分支创建、提交、推送、PR 或 GitHub 发布前，必须先读取项目根目录的 `CODEX_GITHUB_WORKFLOW.md`，并按其中流程操作；如与本文件冲突，以 `AGENTS.md` 为准。
- 必须根据任务风险运行适当的验证，不要把未运行的测试描述为已通过。
- Python 契约测试可使用：

  ```powershell
  python -m pytest -q
  ```

- Qt/C++ 改动应根据风险执行相应的 CMake 配置、构建或运行验证；涉及部署时还要检查 `deploy/` 生成结果。
- 如果测试或构建失败，区分本次改动引入的问题和已有基线问题，并在交付说明中如实报告。
- 提交或创建 PR 前再次确认 `src/` 没有未经任务要求的变化，并确认只提交了相关文件。
