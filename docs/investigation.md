# EU4 1.37.5.0 返回菜单原生补丁调查

署名：**VulonLok**。

## 目标与范围

在当前进程中清理战役、切换回主菜单，并允许重新开始战役。目标程序为 Windows x64 EU4 1.37.5.0 Inca，文件 SHA-256：

`9ad3efe1af169f40ee577f9dae5debbc87af6fb8b5450fb345ebf110dc4d771a`

补丁通过现有 VERSION.dll 的 plugins 加载机制运行。只修改进程内存，不修改 eu4.exe 文件。所有研究均在 private/runtime 副本及独立用户目录内进行。

## 社区背景

官方开发日志说明，重启用于完整重置无法可靠清理的引擎状态。原有行为应视为引擎的已知限制；取消重启标记本身不能完成清理。

- 官方开发日志：[Steam EU4 社区公告](https://store.steampowered.com/news/posts/?appids=236850&enddate=1591099569&feed=steam_community_announcements)
- 社区讨论：[2025 年关于返回主菜单重启的讨论](https://steamcommunity.com/app/236850/discussions/0/596263353496689224/)
- 当前加载器实现：[Matanki/EU4dll](https://github.com/matanki-saito/EU4dll)

## 已确认的原生路径

以下地址均为本版本 eu4.exe 的 RVA，不是运行时绝对地址。

| 地址 | 用途与证据 |
| --- | --- |
| 0x242BF50 | 应用实例全局指针 |
| 0x233FE78 | 世界实例全局指针 |
| 0x233FEA0 | 历史管理器全局指针 |
| 0x242BA90 | 宣战理由类型数据库全局指针 |
| 0x14C2500 | 设置退出及重启标记 |
| 0x14C3A10 | 启动替代进程，调用 CreateProcessA |
| 0x111DFD7 | 选国家界面 Back 回调中内联的退出及重启标记写入 |
| 0x815CAE | 战役 Idle 中处理返回菜单请求的分支 |
| 0x210000 | 重置世界与重新载入历史 |
| 0x826C20 | 战役读档使用的 GUI 清理、世界重置及 GUI 引用重建包装函数 |
| 0x10D23C0 | 构造主菜单及选国家界面所属的 FrontEndIdler |
| 0x14C2530 | 延迟切换 Idler，使旧界面在合适的更新边界销毁 |
| 0x7E9B70 | History PostValidate，重新绑定历史效果 |

选国家界面 Back 的回调返回后，下一次 FrontEndIdler 更新执行重置及菜单构造，避免在按钮回调栈中销毁当前界面。

## MEIOU 崩溃定位（2026-10-02）

首个 MEIOU 测试加载了 MEIOU、Pop Display、ugc_3503711503、ugc_3323053948、redux-subject。日志中发生一次选国家界面返回菜单，之后开始 1356.12.31 战役，在执行 Late History 时崩溃。日志未记录战役返回菜单事件，所以不能将该次故障描述为第二个已进入战役的存档崩溃。

异常为访问冲突，指令 RVA 0x362F39：`mov r11d, [r8+0x10]`。异常上下文中 R8 为 0。

实际调用者是添加宣战理由的脚本效果，调用地址 RVA 0x6246CD。效果对象 +0x60 保存的类型指针为 0，+0x58 为 480，目标标签为 KOR。对应的模组历史条目在 `history/countries/CSE - Shen.txt` 第 36 行：

```text
add_casus_belli = { type = cb_restore_personal_union months = 480 target = KOR }
```

该类型在模组 `common/cb_types/00_cb_types.txt` 第 1 行存在。效果的原生校验函数 RVA 0x6241A0 会通过类型数据库查找名称并写入 +0x60。应修复绑定流程，不能简单忽略空指针或删除历史效果。

ResetGame 在 RVA 0x2104CA 调用当前 Idler 的虚函数 +0x140；只有结果为真才在 RVA 0x2104D7 调用 History PostValidate。FrontEndIdler 的该虚函数为 RVA 0x8F450，直接返回 false，导致新历史效果未绑定。战役 Idler 的函数 RVA 0x802540 读取对象 +0x1158。战役 GUI 重置包装函数 0x826C20 在 0x826F6A 临时将该字段设为 1，再调用 ResetGame，并在 0x826FA8 恢复为 0；因此不能从正常战役运行时字段为 0 推断它漏掉了绑定。

修订补丁在选国家界面 ResetGame 返回后显式调用同一原生 History PostValidate。随后两次战役的开局和返回已由进程状态及日志确认，用户最终确认两局均正常推进到下个月。

战役退出阶段的动态跟踪进一步证实了绑定机制：同一个 `cb_restore_personal_union` 历史效果在原生校验前 +0x60 为 0，校验后为有效类型对象地址。原生调用栈为 ResetGame → GUI 重置包装函数 → 补丁回调，符合所选清理路径。

## 验证记录

| 场景 | 结果 |
| --- | --- |
| 原版 Single Player → Back | 用户确认回主菜单；同一 PID 31260，退出及重启标记均为 0 |
| 原版非铁人 1444 战役 → Exit to Menu | 用户确认回主菜单；同一 PID 31260 |
| MEIOU 初稿返回后开局 | 失败：上述宣战理由空指针 |
| MEIOU 未加载补丁的全新启动对照 | PID 19112；用户确认能进入并推进。Back 仍重启，符合未加载补丁的对照行为 |
| MEIOU 增加历史绑定后的修订 | PID 21024，选国家界面 Back 直接返回，日志完成 History PostValidate；用户确认随后开局及推进正常，国家由 message.log 确认为 BYZ |
| MEIOU 战役退出及第二次开局 | PID 21024；两次战役退出均切换到 FrontEndIdler，退出、重启标记均为 0；两局均成功执行 Late History 并建立战役界面。用户最终确认两局都正常推进到下个月 |

原版回归使用补充历史绑定前的主体实现；最终修订的完整回归在本机五模组组合下完成。两轮月度推进不能据此证明长期存档、多人与铁人模式的稳定性。

回归通过后，首次交付 DLL 已安装到正式游戏 `plugins/eu4_menu_patch.dll`，SHA-256 为 `264af910241d737a446e02ad13a806fd90cb3dba2e0fa941ccfe959d0e04127d`。eu4.exe 哈希仍与最初一致，现有 VERSION.dll、汉化组件、模组和原有战役存档未改动。正式进程 PID 37592 的启动时间仍为 21:21:06，未被注入、终止或重启。补丁在下次启动游戏时生效。测试副本与跟踪进程已退出。

补丁的二进制保护已通过实际加载测试：非 eu4.exe 宿主返回 -1；研究 DLL 拒绝隔离目录之外的宿主，返回 -1；名称为 eu4.exe 但哈希不符的宿主返回 -2。三种情况均未安装钩子。钩子安装前先准备全部跳转桩并取得所有目标的写权限，失败时保持原指令和虚函数指针。

同一 MEIOU 测试进程记录到三次菜单切换：选国家界面返回约 5.7 秒，第一局战役返回约 9.6 秒，第二局战役返回约 20.8 秒。全程没有新增崩溃目录，异常观察日志没有新增访问冲突；动态跟踪没有记录重启请求、退出或替代进程启动。这些是本轮观测值，不代表所有存档的耗时或长期稳定性。

## 署名更新

2026-10-02 按用户要求增加 VulonLok 署名：说明、调查记录、源码头部、DLL 版本资源、初始化日志及发布包 manifest.json。DLL 文件属性的 CompanyName 为 VulonLok，Comments 为 Author: VulonLok。

署名版 SHA-256 为 `42a27bf1d1a843f2ce9c8271f66e6f62a9b31e5dc73faea8ce10b8fc23a09e77`。此版重新构建并通过三项已有二进制保护检查；上述游戏回归对应首次交付版，署名更新未修改菜单切换逻辑，因此未重复进行游戏回归。
