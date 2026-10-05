# 构建与发布

## 环境

- Windows x64
- Visual Studio 2022，安装 MSVC C++ x64 工具链和 Windows SDK
- PowerShell 7（`pwsh`）
- Git for Windows（`git`、`sh`、`awk`、`sha256sum`）
- 发布时需要 GitHub CLI（`gh`）

脚本通过 Visual Studio Installer 的 `vswhere` 查找工具链，支持 Build Tools、Community、Professional 和 Enterprise。构建和自动检查无需安装游戏或 Python 调查依赖。

## 本机构建

在仓库根目录运行：

```sh
sh ./tools/ci.sh
```

打包要求源码已提交、工作区干净。完整流程依次编译、运行加载保护检查、生成成品包、检查 ZIP，再验证错误产物会被拒绝。

单独执行构建或保护检查时，仍可使用已有入口：

```sh
pwsh -NoProfile -File ./tools/build.ps1
pwsh -NoProfile -File ./tools/test-guards.ps1
pwsh -NoProfile -File ./tools/package.ps1
```

| 文件 | 用途 |
| --- | --- |
| `build/eu4_menu_patch.dll` | 发布补丁 |
| `build/menu_patch_probe.dll` | 研究副本补丁，额外限制加载路径 |
| `build/build-info.json` | 提交、版本、构建输入和 DLL 哈希 |
| `build/automated-validation.json` | 加载保护检查结果及被检查的 DLL 哈希 |
| `build/package-info.json` | 成品名称、提交和 ZIP 哈希 |
| `build/*.log`、`*.pdb`、`*.map` | 构建日志与调试信息 |

研究副本的允许路径由 `src/eu4_menu_patch.cpp` 中的 `isolated_exe` 指定。CI 只检查路径不符时拒绝加载，不需要该目录存在。

## 成品

版本唯一来源为根目录 `VERSION`，DLL 日志、Windows 文件属性和玩家说明在构建、打包时自动生成。

`dist/` 输出：

```text
EU4MenuPatch-1.37.5-v<版本>-experimental.zip
SHA256SUMS.txt
```

成品 ZIP 只有 `plugins` 中的 DLL、安装说明和许可证。源码通过 GitHub 的 Source code 下载；日志和符号放在 Actions 构建产物中。仓库 `downloads/` 的历史文件保留，后续成品不再提交到 Git。

## GitHub Actions

三个工作流都使用 `windows-2022` 和 `sh -e {0}`，调用相同的本机脚本。

| 工作流 | 触发 | 行为 |
| --- | --- | --- |
| CI | 向 `main`、`develop` 推送或提交 PR | 构建、检查和打包；产物保留 14 天 |
| Nightly | Actions 页面手动运行 | 构建 `develop` 的指定提交并发布预发布；产物保留 30 天 |
| Release | 同仓库的 `develop → main` PR 合并 | 构建准确的合并提交并发布实验版；产物保留 30 天 |

Nightly 的 `commit` 留空时使用当前 `develop`；填写时只接受属于 `develop` 历史的 7–40 位提交 SHA。Nightly 标签包含北京时间日期和提交短哈希，例如 `v0.1.2-nightly-20261005-a1b2c3d`。

版本发布标签为 `v<VERSION>-experimental`。准备发布时，在 `develop` 更新 `VERSION` 和 `CHANGELOG.md`，检查通过后通过 PR 合并到 `main`。当前两个发布渠道都标记为 Pre-release，不设置为 Latest。

发布先检查标签、版本和提交，再创建草稿、上传 ZIP 与校验文件，核对 GitHub 资产摘要后公开。重跑完整发布会跳过；未完成的草稿可以继续上传，已公开资产不覆盖。发布工作流使用内置 `GITHUB_TOKEN` 的 `contents: write` 权限；普通 CI 只有读取权限。

本机测试 Nightly 打包和发布预检查：

```sh
sh ./tools/ci.sh --channel Nightly --date 20261005
sh ./tools/publish-release.sh --channel Nightly --date 20261005 --check-only
```

去掉 `--check-only` 会上传并公开发布，需要已登录 `gh` 且有仓库写入权限。

## 检查范围

自动检查验证错误进程名、研究副本路径、游戏 EXE 哈希是否被拒绝，以及构建与测试记录、ZIP 文件清单、内容和 SHA-256 是否一致。回归检查还会主动改动 DLL、测试记录、ZIP 清单和校验文件，确认校验器拒绝错误产物并恢复原文件。

`game_runtime_verified` 固定记录为 `false`。CI 无法验证返回菜单、Steam 会话退出、迷你地图界面和战役同步；当前构建不继承旧候选版的游戏测试结论，也不验证 MEIOU 模组组合。

## 版本校验

补丁仅支持以下 `eu4.exe`，SHA-256：

```text
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

`tools/install.ps1` 另行检查本机验证过的 `VERSION.dll`，SHA-256：

```text
1e91bb82a8ef5cf86dd20c8df45b75643b6da021aff8fabdbc78b0f4f5f9916a
```

运行时还检查目标原始指令。适配其他游戏版本需要重新调查引擎，不能仅修改哈希值。
