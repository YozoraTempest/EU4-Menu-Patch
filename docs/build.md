# 构建

## 环境

- Windows x64
- Visual Studio 2022 Build Tools，安装 C++ x64 工具链和 Windows SDK
- PowerShell

`build.ps1` 和 `test-guards.ps1` 中的 `vsRoot` 默认指向 `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`。安装位置不同时，修改这两个脚本中的路径。

在仓库根目录运行：

```powershell
.\tools\build.ps1
```

输出文件：

| 文件 | 用途 |
| --- | --- |
| `build/eu4_menu_patch.dll` | 发布用补丁 |
| `build/menu_patch_probe.dll` | 调试用补丁，额外限制游戏副本路径 |

调试版的允许路径由 `src/eu4_menu_patch.cpp` 中的 `isolated_exe` 指定。使用调试版前，需将其改为测试副本的绝对路径并重新构建。

## 保护检查

```powershell
.\tools\test-guards.ps1
```

检查补丁是否拒绝错误的进程名称、不符合调试版路径限制的程序，以及哈希不匹配的 `eu4.exe`。这些检查不替代游戏内的返回菜单和重新开局测试。

## 打包

构建完成后运行：

```powershell
.\tools\package.ps1
```

`dist` 目录中生成两个 ZIP：

- `EU4MenuPatch-1.37.5-experimental-drop-in.zip`：直接复制安装包，只包含 `plugins` 下的补丁、说明和许可证。
- `EU4MenuPatch-1.37.5-experimental.zip`：源码、DLL、构建与安装工具、文档和校验信息。

GitHub Release 提供直接复制安装包；源码可以通过 GitHub 的 Source code 下载。

## 版本校验

发布版仅支持以下 `eu4.exe`，SHA-256：

```text
9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a
```

`tools/install.ps1` 另行检查 `VERSION.dll` 的 SHA-256：

```text
1e91bb82a8ef5cf86dd20c8df45b75643b6da021aff8fabdbc78b0f4f5f9916a
```

运行时还会检查待修改的原始指令。支持其他游戏版本需要重新适配代码，不能仅修改哈希值。
