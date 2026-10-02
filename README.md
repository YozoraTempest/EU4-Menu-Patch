# EU4 Menu Patch

让《欧陆风云 IV》在点击 **Back** 或 **Exit to Menu** 时直接返回主菜单，省去退出并重新启动游戏的等待。

适用于 **EU4 1.37.5.0 Inca / Windows x64**。当前版本：**v0.1.1-experimental**。

[下载补丁](https://github.com/YozoraTempest/EU4-Menu-Patch/releases/download/v0.1.1-experimental/EU4MenuPatch-1.37.5-v0.1.1-experimental-drop-in.zip) · [发布说明](https://github.com/YozoraTempest/EU4-Menu-Patch/releases/tag/v0.1.1-experimental) · [问题反馈](https://github.com/YozoraTempest/EU4-Menu-Patch/issues)

## v0.1.1 更新

- 返回主菜单后，等待菜单切换完成、校验和就绪及 Steam 房间状态清空，再恢复多人入口。
- 退出时清理 Steam 房间、房主状态、加入请求和房间搜索回调，重新注册房间事件处理。
- 释放上一局的迷你地图控制器及其窗口、按钮回调，处理房间列表右下角残留战役界面的问题。

本版仍为实验版。开发候选版已测试原版 Steam 房间列表和连续建房；双客户端联机和战役同步尚未验证。

## 安装

需要游戏目录中已有 [Matanki EU4dll](https://github.com/matanki-saito/EU4dll) 的 `VERSION.dll` 加载器，用于加载 `plugins` 中的 DLL。补丁包不附带加载器。

1. 保存战役并完全退出游戏。
2. 下载补丁 ZIP，将里面的 `plugins` 文件夹复制到 `eu4.exe` 所在目录，合并同名文件夹。
3. 正常启动游戏。

安装后的目录：

```text
Europa Universalis IV/
├── eu4.exe
├── VERSION.dll
└── plugins/
    ├── eu4_menu_patch.dll
    ├── eu4_menu_patch.README.txt
    └── eu4_menu_patch.LICENSE.txt
```

更新时，退出游戏后覆盖 `plugins/eu4_menu_patch.dll`。安装不需要运行脚本；`eu4.exe` 文件保持原样，补丁在游戏启动后修改内存中的菜单切换逻辑。

## 卸载

退出游戏，删除 `plugins/eu4_menu_patch.dll`。

## 兼容性

- **游戏版本：** 1.37.5.0 Inca，Windows x64。补丁会检查 `eu4.exe` 的 SHA-256 和目标指令，校验失败时拒绝应用。校验值见[构建说明](docs/build.md#版本校验)。
- **本次测试：** 原版、非铁人、Steam 联机。开发候选版已验证单人选国界面 Back 后进入多人房间列表，以及连续两次建房、进入选国地图、返回主菜单。迷你地图修订的日志已记录两次战役退出时释放控制器，界面残留是否消失及重新开局后的按钮响应尚待人工确认。
- **发布构建：** 已通过进程名称、调试副本路径和可执行文件哈希的保护检查。最终打包的 DLL 尚未完成完整游戏内回归。
- **尚未验证：** 双客户端加入、退出及重新加入房间，联机战役同步、铁人模式和长期战役。
- **模组：** 本版未复测 MEIOU 或其他模组。v0.1.0 的 MEIOU 与四个附属模组测试记录仅适用于旧版。

## 排查问题

加载日志位于 `plugins/eu4_menu_patch.log`。正常加载时包含：

```text
menu transition patch initialized; author=VulonLok
```

没有日志时，检查补丁文件位置和 `VERSION.dll` 加载器。日志出现 `REFUSED` 表示补丁未应用。

遇到崩溃、卡住或仍然重启，请[提交 Issue](https://github.com/YozoraTempest/EU4-Menu-Patch/issues/new)，附上游戏版本、模组列表、复现步骤和补丁日志。

## 交流

[加入 QQ 交流群：欧陆风云 · 永夜的星月回廊](https://qm.qq.com/q/Csnqqd8rUO)

## 构建

源码使用 C++17、MSVC 和 Windows SDK。构建、保护检查和打包命令见[构建说明](docs/build.md)。

## 许可证

[MIT](LICENSE) © 2026 VulonLok
