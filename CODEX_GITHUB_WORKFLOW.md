# KY-DIMM GitHub 工作流

本文只记录当前项目的分支、提交、推送和 Pull Request 流程。核心安全规则以项目根目录的 `AGENTS.md` 为准。

## 开始开发

先检查工作区：

```powershell
git status --short --branch
```

如果存在用户未提交的修改或未跟踪文件，先保留并确认处理方式，不要强行切换、覆盖或清理。

工作区干净且需要从主分支开始时：

```powershell
git switch master
git pull --ff-only origin master
git switch -c codex/<功能名称>
```

功能分支统一使用 `codex/` 前缀。

## 修改与提交

完成修改后运行相关测试，并检查：

```powershell
git status
git diff
```

只暂存当前任务相关文件：

```powershell
git add <相关文件>
git commit -m "<type>: <简短说明>"
```

提交类型可使用 `feat:`、`fix:`、`docs:`、`test:` 或 `chore:`。提交前确认 `src/` 没有未经任务要求的变化。

## 推送与 Pull Request

验证通过后，将功能分支推送到远程并创建目标为 `master` 的 PR。优先使用已连接的 GitHub `Publish Changes` 能力，必要时再使用本地 Git 命令。

PR 描述应包含：

- 改动内容
- 验证命令和结果
- 已知警告或限制

未经用户明确确认，不要合并 PR。

不要把个人令牌写入项目文件、命令行脚本或聊天内容。

## 合并后

PR 合并后回到本地 `master` 并同步：

```powershell
git switch master
git pull --ff-only origin master
```

确认分支不再需要后，再删除已合并的本地或远程分支。不要为了清理而删除用户的未跟踪文件或未合并分支。
