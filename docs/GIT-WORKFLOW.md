# 分支与版本管理规范

> 本仓库的工作流公约，为后续**系统演化**（新增功能、修复、多版本）提供结构支持。
> 原则一句话：**`main` 永远是"可构建、可运行、`make test` 全过"的稳定线。**

---

## 1. 总体原则

1. **`main` 保持稳定**：任何时刻 checkout `main` 都应能 `make` 并启动进 Shell；
   合入 `main` 前必须通过 `make test`（或至少人工 QEMU 验证）。
2. **短生命周期分支**：分支只存活于开发期间，完成即合并删除，避免长期挂起。
3. **语义化版本**：发布用 `vX.Y.Z`，见 §6。
4. **提交信息可追溯**：用 Conventional Commits，见 §5。

---

## 2. 分支模型（两档）

### 2.1 默认档（单人 / 当前阶段）：`main` 主线 + 短期分支

```
        v1.0   v1.1   v2.0
main o──────o──────o──────o──  → 稳定可发布
       \    /   \   /  \   /
        ╳  ╳     ╳  ╳    ╳      短期功能/修复分支，验证后合回 main 并删除
       feature/a  fix/b  feature/c
```

- 直接从 `main` 切出 `feature/*` / `fix/*` / `docs/*`，完成后合回 `main`。
- 适合：单开发者、无并行大版本、改动可快速合并。

### 2.2 演化档（并行功能 / 多版本）：加 `develop` 集成线

```
   feature/x ─┐   feature/y ─┐
              ▼              ▼
        develop  o──────────o──o──  → 功能汇合、集成验证
        main     o──────────────────o──  → 稳定发布（develop 验证成熟后合入）
```

```
      fix/* ────┐
                ▼
        develop ←── feature/*，release/* 从这里长
           │
           ▼
        main  (release 验证 → merge → 打 tag vX.Y.Z)
   hotfix/* 从 main 长，合回 main + develop
```

- `develop`：**系统演化主线**，功能分支合入这里持续集成；
  稳定到可发布时再合入 `main` 并打 tag。
- 适合：多人协作、并行功能、维护多个版本。

> **本仓库默认用 2.1**（改动多为单点增强）；当需要并行开发多组功能时切换到 2.2，
> `develop` 分支已建好，直接长出 `feature/*` 即可。

---

## 3. 分支命名规范

| 前缀 | 用途 | 例子 |
|------|------|------|
| `feature/<topic>` | 新增功能 / 系统调用 | `feature/pipe`, `feature/fcntl` |
| `fix/<topic>` | 缺陷修复 | `fix/pipe-eof`, `fix/fork-stack` |
| `docs/<topic>` | 文档（教程/设计/README） | `docs/tutorial-mm` |
| `test/<topic>` | 测试/验证基建 | `test/regress-suite` |
| `refactor/<topic>` | 重构（不改行为） | `refactor/namei` |
| `chore/<topic>` | 构建/依赖/杂项 | `chore/makefile` |
| `release/<version>` | 发布准备（演化档） | `release/v1.2` |
| `hotfix/<topic>` | 从 main 的紧急修复（演化档） | `hotfix/panic-oob` |

- 全小写、`<topic>` 用连字符、一词概括用途。
- 一次性分支用完即删：`git branch -d feature/pipe`（已合并）或 `git branch -D`（未合并强制）。

---

## 4. 合并约定

| 来源 | 目标 | 方式 | 备注 |
|------|------|------|------|
| `feature/*` | `main` / `develop` | **squash merge**（`--squash`） | 保持主线历史线性、每个提交自洽 |
| `fix/*` | `main` / `develop` | squash merge | 同上 |
| `develop` | `main` | merge（保留集成历史） | 演化档的发布动作 |
| `hotfix/*` | `main` + `develop` | cherry-pick 或 merge | 同步两端 |

- 合入前：`git checkout main && git pull && make test`（本地或 CI），确认无回归。
- **不要**把构建产物（`Image`、`minix.img`、`user/*.elf`、`*.d`）提交——已在 `.gitignore`。
- 合并后删除源分支，保持整洁。

---

## 5. 提交信息规范（Conventional Commits）

格式：

```
<type>(<scope>): <subject>

<body>
```

- **type**：`feat` | `fix` | `docs` | `test` | `refactor` | `chore` | `style` | `perf`
- **scope**（可选）：涉及的模块/子系统，如 `fs`、`mm`、`sched`、`user-lib`、`docs`
- **subject**：现在时、祈使句、小写开头、**≤ 72 字符**，描述"做了什么"，不写过程

示例（与仓库现有风格一致）：

```
feat(fs): add hard links via sys_link

link bumps i_nlinks and reuses the same inode; directories cannot be
linked (POSIX).  QEMU-verified: stat nlink 1->2.
```

```
fix(mm): paging PTE flags used 0x06 which lacks the Present bit

0x06 = 0b110 drops bit0; every page became not-present and the kernel
faulted at the first fetch after CR0.PG.  Kernel pages are 0x03 (P+RW).
```

- 一个提交只做一件事；必要时在 **body** 里写"为什么"（尤其修复/踩坑）。
- 契合仓库已有的 `feat:` / `fix:` / `docs:` / `test:` / `ci:` 风格。

---

## 6. 版本号语义（SemVer 简化）

发布标签形态 `v<major>.<minor>.<patch>`：

| 位 | 何时递增 | 例子 |
|----|----------|------|
| major | 不兼容/架构级变化（如改系统调用编号语义、新的内存模型） | `v2.0` |
| minor | 新增功能（新系统调用、新命令、新子系统） | `v1.2` |
| patch | 缺陷修复、文档、小改进 | `v1.1.1` |

```
git tag -a v1.2 -m "feat: ..."
git push origin v1.2
```

- 打 tag 前先 `main` 上 `make test` 全过。
- 历史标签 `v1.0`、`v1.1-chdir` 保留原样（向后兼容旧引用）；
  新发布统一用 `vX.Y.Z` 语义化格式。

---

## 7. 与验证 / CI 的关系

- 合入 `main` / `develop` 前，**本地跑 `make test`**（8 个核心场景）或等 GitHub Actions。
- 提交信息里可标注验证方式：`QEMU-verified` / `make test passes`。
- 历史踩坑（iget/mkminix 顺序、页表标志位 0x06、schedule current 预赋值等）
  已在 `docs/NEXT-STEPS.md` 与源码注释里——读源码时留意，避免重蹈。

---

## 8. 快速速查

```bash
# 开一个功能分支（当前阶段从 main）
git checkout main && git pull
git checkout -b feature/fcntl

# ... 开发，本地验证
make test

# 合回 main（squash 保持历史干净）
git checkout main && git pull
git merge --squash feature/fcntl
git commit -m "feat: implement fcntl (F_DUPFD)"
git branch -d feature/fcntl

# 发布
git tag -a v1.2 -m "feat: fcntl"
git push origin main --tags
```

**演化档切换**（需要并行功能时）：基于 `develop` 分支长出 `feature/*`，
合入 `develop` 持续集成，成熟后再入 `main`（`develop` 分支已就绪）。
