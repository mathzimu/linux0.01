# 构建依赖

## 必需工具

### 1. 汇编器 + 编译器 + 链接器

| 工具 | 用途 | 验证 |
|------|------|------|
| `as` (GAS) | x86汇编 → ELF对象 | `as --version` |
| `gcc` / `clang` | C → ELF对象（i386, freestanding） | `gcc --version` |
| `ld` | ELF链接（i386, `-m elf_i386`） | `ld --version` |
| `objcopy` | ELF → 原始二进制（flat binary） | `objcopy --version` |

**平台说明：**
- **Linux**: `apt install build-essential gcc-multilib`
- **macOS**: `brew install i386-elf-gcc i386-elf-binutils` 或使用 Docker
- **Docker**: `docker build -t linux-0.01-builder .` (项目已提供 Dockerfile)

### 2. 构建工具

| 工具 | 用途 | 验证 |
|------|------|------|
| `make` | 构建自动化 | `make --version` |

**安装**：所有平台标配。

### 3. 运行时

| 工具 | 用途 | 验证 |
|------|------|------|
| `qemu-system-i386` | 仿真运行（推荐） | `qemu-system-i386 --version` |
| `bochs` | 仿真运行（备选） | `bochs --version` |

**安装：**
- **Linux**: `apt install qemu-system-x86`
- **macOS**: `brew install qemu`
- **Docker**: 已包含在 Dockerfile 中

---

## 可选工具（ISO 打包）

| 工具 | 用途 | Makefile 目标 |
|------|------|---------------|
| `xorriso` | 创建 El Torito 启动 ISO | `make iso`（首选） |
| `genisoimage` | 创建 El Torito 启动 ISO（备选） | `make iso` |
| `mkisofs` | 创建 El Torito 启动 ISO（备选） | `make iso` |

**验证**：`xorriso --version` 或 `genisoimage --version`

**安装：**
- **Linux**: `apt install xorriso`
- **macOS**: `brew install xorriso`
- **Docker**: 已包含在 Dockerfile 中

---

## Docker 方式（推荐，平台无关）

```bash
# 构建 Docker 镜像（自动安装全部依赖）
docker build -t linux-0.01-builder .

# 编译内核 + 生成 ISO
docker run --rm -v $(pwd):/kernel -w /kernel linux-0.01-builder make clean all iso

# 或使用项目脚本
./scripts/build.sh docker
```

Dockerfile 基于 `ubuntu:22.04`，已安装：
- `build-essential` `gcc-multilib` — 编译工具
- `xorriso` — ISO 生成
- `qemu-system-x86` — 运行时仿真

---

## 完整安装命令汇总

```bash
# === Linux (Ubuntu/Debian) ===
sudo apt update
sudo apt install -y build-essential gcc-multilib qemu-system-x86 xorriso

# === Linux (Arch) ===
sudo pacman -S --needed base-devel qemu xorriso

# === macOS (Homebrew) ===
brew install qemu xorriso
# 交叉编译：brew install i386-elf-gcc i386-elf-binutils
# 或使用 Docker（推荐）

# === Docker (平台无关) ===
docker build -t linux-0.01-builder .
```
