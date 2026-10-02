# EU4 Menu Patch

署名：**VulonLok**。

DLL 文件属性中的公司、文件说明和备注均包含该署名，初始化日志也会记录 `author=VulonLok`。发布包的 manifest.json 使用 `author` 字段记录署名。

EU4 1.37.5.0 Windows x64 的原生返回菜单补丁。使用 DLL 修改当前进程的菜单切换路径，调用引擎自己的世界重置、历史效果绑定与界面切换函数。eu4.exe 文件保持原样。

## 下载与使用

[下载直接复制版](https://github.com/YozoraTempest/EU4-Menu-Patch/raw/refs/heads/main/downloads/EU4MenuPatch-1.37.5-experimental-drop-in.zip)。保存战役并退出游戏，将压缩包解压到 eu4.exe 所在目录，合并 plugins 文件夹，再正常启动游戏即可。

首次安装会新增 `plugins/eu4_menu_patch.dll`，更新时覆盖同名文件。直接复制版还包含说明和 MIT 许可证，目录结构已经摆好：

```text
Europa Universalis IV/
  eu4.exe
  VERSION.dll
  plugins/
    eu4_menu_patch.dll
    eu4_menu_patch.README.txt
    eu4_menu_patch.LICENSE.txt
```

游戏需要已有支持加载 plugins 目录 DLL 的 VERSION.dll 加载器。补丁 DLL 启动时会自行校验 eu4.exe 哈希。确认生效可查看 `plugins/eu4_menu_patch.log` 中的 `menu transition patch initialized; author=VulonLok`。

[源码与脚本完整包](https://github.com/YozoraTempest/EU4-Menu-Patch/raw/refs/heads/main/downloads/EU4MenuPatch-1.37.5-experimental.zip)另外提供带安装前校验和旧版备份的脚本安装方式。解压后，从解压目录运行：

```powershell
.\tools\install.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\Europa Universalis IV'
```

将参数替换为自己的游戏目录。安装脚本要求下文列出的 eu4.exe 哈希及已验证的 VERSION.dll 插件加载器；成功安装后，下次启动游戏时加载补丁。可在 DLL 属性的“详细信息”中查看 VulonLok 署名，在 plugins/eu4_menu_patch.log 中查看加载结果。卸载时退出游戏，再运行：

```powershell
.\tools\uninstall.ps1 -GameDirectory 'D:\SteamLibrary\steamapps\common\Europa Universalis IV'
```

仓库与补丁包仅包含本补丁源码、工具和 DLL。游戏文件及已有插件加载器需要自行准备。

## 当前状态

已完成本机单人非铁人回归。原版选国家界面及战役返回菜单在初稿中通过；MEIOU 初稿的历史效果空指针已定位并修复。最终修订在 MEIOU 及四个附属模组组合下，选国家界面返回、两次开局、两次推进到下个月和两次战役退出均通过，始终使用同一进程。

当前仍为实验性补丁。验证范围是本机五个模组的组合、单人、非铁人和两轮月度推进；长时间存档运行、多人与铁人模式尚未验证。其他游戏版本会被文件哈希保护拒绝。

## 支持的程序

- EU4 1.37.5.0 Inca，Windows x64。
- eu4.exe SHA-256：`9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a`。
- 当前机器已有 Matanki EU4dll 的 VERSION.dll 加载器，会自动加载 plugins 目录中的 DLL。补丁不包含或覆盖该加载器、汉化 DLL 和游戏文件。

启动时校验程序名称、文件哈希和目标内存中的原始指令。任何不符均拒绝安装钩子。研究版另行限制隔离副本的绝对路径。

## 构建与检查

需要 Visual Studio 2022 Build Tools 的 x64 C++ 工具链和 Windows SDK。当前构建脚本使用本机 Build Tools 安装路径。

```powershell
.\tools\build.ps1
.\tools\test-guards.ps1
```

输出 `build/eu4_menu_patch.dll` 与 `build/menu_patch_probe.dll`。研究版的隔离路径由 src/eu4_menu_patch.cpp 中的 `isolated_exe` 指定，目前对应首次验证使用的本机路径；在其他位置开展研究时需修改该路径并重新构建。二进制保护测试使用小型宿主程序，验证错误进程、研究目录限制与程序哈希不符时的拒绝行为。

## 研究环境

Python 工具依赖记录在 requirements.txt。analyze.py 使用 PE 展开信息、反汇编和交叉引用索引；inspect_process.py 仅读取隔离游戏进程中的状态；trace.py 在隔离进程中观察原生函数调用。

private 目录中的游戏副本、模组描述文件、存档、转储及分析索引属于本地研究数据，不应打包发布。交付物只应包括补丁、构建源码、说明和必要的校验信息。

## 技术记录

详见 [调查记录](docs/investigation.md)。关键修复是补全 ResetGame 在 FrontEndIdler 下遗漏的 History PostValidate，使重新读取的历史效果绑定到宣战理由等定义对象。Back 请求延迟到下一次界面更新处理；战役退出使用引擎读档路径中的 GUI 重置包装函数。

日志写在补丁 DLL 同目录的 eu4_menu_patch.log。卸载应先退出使用该 DLL 的游戏进程，再移除仅属于本补丁的 eu4_menu_patch.dll；现有加载器、汉化组件、模组和存档无需删除。

## 安装与移除

可通过以下命令将交付 DLL 加入当前机器的 plugins 目录，下次启动游戏时加载：

```powershell
.\tools\install.ps1
```

脚本核对 eu4.exe 和本机已验证的 VERSION.dll 哈希，仅复制 eu4_menu_patch.dll。已有补丁若需替换，会先备份到 private/patch-backups；同版本重复安装不会写入文件。正在运行的旧游戏不会因此被注入或重启。

移除命令会将补丁 DLL 移到备份目录：

```powershell
.\tools\uninstall.ps1
```

## 许可证

本补丁源码与工具采用 [MIT 许可证](LICENSE)，Copyright (c) 2026 VulonLok。
