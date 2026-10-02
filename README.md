# EU4 Menu Patch

让《欧陆风云 IV》在点击 **Back** 或 **Exit to Menu** 时直接返回主菜单，省去退出并重新启动游戏的等待。

适用于 **EU4 1.37.5.0 Inca / Windows x64**。当前为实验版。

[下载补丁](https://github.com/YozoraTempest/EU4-Menu-Patch/releases/download/v0.1.0-experimental/EU4MenuPatch-1.37.5-experimental-drop-in.zip) · [发布说明](https://github.com/YozoraTempest/EU4-Menu-Patch/releases/tag/v0.1.0-experimental) · [问题反馈](https://github.com/YozoraTempest/EU4-Menu-Patch/issues)

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
- **测试范围：** 单人、非铁人；MEIOU 与四个附属模组的测试组合。已验证选择国家界面的 Back，以及连续两局推进到下个月后返回主菜单。
- **尚未验证：** 铁人模式、多人游戏、长期战役和其他模组组合。

## 排查问题

加载日志位于 `plugins/eu4_menu_patch.log`。正常加载时包含：

```text
menu transition patch initialized; author=VulonLok
```

没有日志时，检查补丁文件位置和 `VERSION.dll` 加载器。日志出现 `REFUSED` 表示补丁未应用。

遇到崩溃、卡住或仍然重启，请[提交 Issue](https://github.com/YozoraTempest/EU4-Menu-Patch/issues/new)，附上游戏版本、模组列表、复现步骤和补丁日志。

## 构建

源码使用 C++17、MSVC 和 Windows SDK。构建、保护检查和打包命令见[构建说明](docs/build.md)。

## 许可证

[MIT](LICENSE) © 2026 VulonLok
