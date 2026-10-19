# ZzPureTools 升级与 ZzTermWidget 移除设计

- 日期：2026-10-19
- 仓库：ZzClawTerm（master）
- 前置：Z3 已收官并发布 `v0.3.0`（08e191d）；ZzTermCore pin 在
  `272db3f`（m20）；ZzPureTools pin 在 `d6b90f1d`；ZzTermWidget 已不参与
  构建但子模块元数据仍存在。

## 1. 目的与范围

完成两项维护收口：

1. 将 `third_party/ZzPureTools` 升级到修复后的最新 master，并适配其
   superbuild、中央工作区模式和第三方依赖变化。
2. 彻底移除已废弃的 `third_party/ZzTermWidget` 子模块及其活跃引用。

范围内：

- 修复 ZzPureTools 上游 superbuild include 路径问题；
- 主仓升级 ZzPureTools pin，并显式采用 `ZzWorkspaceCenterMode::Stacked`；
- 处理 Stacked 模式下旧布局恢复失败的一次性清理；
- 适配或重写仍残留旧组件接口的禁用测试；
- 删除 ZzTermWidget 的 `.gitmodules` 条目、gitlink、README 条目和误导性注释；
- 用全新构建目录完成 Debug/Release 验证，并以三平台 CI 为最终门禁。

范围外：

- 不启用或改造 ZzPureTools 新增的无关控件能力；
- 不回改历史 specs/plans/acceptance 文档；
- 不处理 Segoe Fluent Icons 字体的授权补齐（用户已明确本次先忽略，
  后续另行处理）；
- 不改变 ZzTermCore m20 已发布的引擎语义。

## 2. 决策记录（brainstorming 定案）

| # | 议题 | 定案 |
| --- | --- | --- |
| 1 | ZzPureTools 升级方式 | 先修上游，再完整升级到修复后的最新 master；不做主仓长期绕行补丁 |
| 2 | 中央工作区模式 | 显式采用新 `Stacked`，不保留外层 `Tabbed` 兼容结构 |
| 3 | 旧工作区布局 | 允许一次性重置；恢复失败后清空旧配置，随后按新格式保存恢复 |
| 4 | Segoe 字体授权 | 本次接受风险并继续使用；授权补齐延后，不作为升级门禁 |
| 5 | ZzTermWidget 删除策略 | 先清理/适配测试残留，再删除子模块元数据；历史文档保留 |
| 6 | 验证方式 | 不复用含旧 `qmsetup_DIR` 的构建缓存；使用全新构建目录验证 |

## 3. 现状证据

### 3.1 ZzPureTools

当前 pin `d6b90f1d266b6aa97b0ffb3a75deaa4bc2314514` 是上游
`origin/master = e46bee18dde152edd15d1bfe687d2dc979b96f70` 的祖先。
升级区间包含 234 个提交，主仓真实适配面集中在三点：

1. `ZzPureTools/CMakeLists.txt` 新增
   `target_include_directories(... "${CMAKE_SOURCE_DIR}/ZzFluentUI/widgets/src")`；
   在 ZzClawTerm 以 `add_subdirectory` 消费时，`CMAKE_SOURCE_DIR` 指向主仓
   根目录，导致路径不存在，必须先修上游。
2. `ZzWorkspaceShell::create` 默认中央模式从 `Tabbed` 变为 `Stacked`；
   旧版保存的 Tabbed 布局在新模式下会被拒绝。
3. QWindowKit 从 `2813c1f8` 升至 `5626b3ae`，qmsetup/stdcorelib 构建链
   同步更新；Windows/macOS 平台窗口层需要 CI 和实机重点观察。

主仓当前消费面为 `ZzPureTools`、`Zz::FluentUI`、`Zz::WindowKit`、
`ZzLog::ZzLog` 等静态目标，未发现对已删除标题栏旧 API 的活跃引用。

### 3.2 ZzTermWidget

当前 gitlink 为 `b0d4a774906465e7b94062447a8d7896fe74ddad`，但：

- 根 CMake 不再 `add_subdirectory(third_party/ZzTermWidget)`；
- `src/CMakeLists.txt` 不再链接 `ZzQTermWidget::qtermwidget`；
- CI、打包脚本和安装规则没有旧组件路径；
- `.gitmodules` 仍保留子模块条目；
- README 仍保留“供配色/键位资源对照”的过时说明；
- `src/log/ZzLogLine.h` 仍把 attributes 格式所有权描述为旧组件；
- `tst_ZzSplitContainer`、`tst_ZzConnectFlow`、`tst_ZzSettingsPage`、
  `tst_PerfScrollback` 仍处禁用状态，其中 `tst_ZzConnectFlow` 和
  `tst_PerfScrollback` 仍包含旧接口残留。

## 4. 设计

### 4.1 ZzPureTools 上游修复

在 ZzPureTools 仓库修复 superbuild include 路径，把依赖顶层源码根的路径
改为当前项目根路径，例如 `PROJECT_SOURCE_DIR` 或等价的
`CMAKE_CURRENT_FUNCTION_LIST_DIR` 推导路径。修复后在上游仓库完成构建和
相关测试，再推上游 master。

主仓不长期保留针对该问题的兼容 include 补丁；若验证阶段发现仍有其他
superbuild 假设，优先继续修上游。

### 4.2 主仓升级与 Stacked 适配

1. 将 `third_party/ZzPureTools` pin 升到包含上游修复的最新 master。
2. `ZzAppShell` 创建 WorkspaceShell 时显式传入
   `ZzPureTools::ZzWorkspaceCenterMode::Stacked`，不依赖上游默认值。
3. 布局恢复失败时清空主仓保存的 workspace layout 配置，让旧 Tabbed 数据
   只失败一次；随后保存的新数据必须能正常恢复。
4. 不手工解析或迁移上游私有布局字节格式。
5. 上游已修复应用标题覆盖问题后，再评估删除主仓旧标题重放 workaround；
   只有真实窗口测试证明无回归时才清理。

### 4.3 Segoe 字体资源

本次接受上游新增的 Segoe Fluent Icons 资源随静态构建进入产物，不将其作为
升级门禁。该决定仅记录为已知合规风险；不新增临时剔除逻辑，以免偏离上游
并增加后续同步成本。

### 4.4 ZzTermWidget 测试残留收口

删除子模块前，先处理活跃测试源码，避免仓库留下指向已删除组件的半截状态：

- `tst_ZzSettingsPage`：优先恢复注册，若失败按当前设置页接口最小适配；
- `tst_ZzSplitContainer`：更新过时注释并恢复注册，验证焦点代理落到新终端
  前端；
- `tst_ZzConnectFlow`：删除旧 `qtermwidget.h` include，按新
  `sendData(QByteArray)` 信号口径适配后恢复注册；
- `tst_PerfScrollback`：改用 `ZzCoreTerminalWidget + ZzScrollbackBridge`
  真实链路重写；性能阈值按新链路 Release 实测后重新标定，旧
  `tests/perf/records` 记录保留作历史对照。

恢复或重写测试的目的不是扩大功能范围，而是保证删除旧组件后测试基座与当前
架构一致。

### 4.5 ZzTermWidget 删除

测试残留收口后，在同一删除提交中处理：

- 从 `.gitmodules` 删除 `third_party/ZzTermWidget` section；
- 从 index 删除 `third_party/ZzTermWidget` gitlink；
- 删除根 CMake 中“目录保留”的过时注释；
- 删除 README 子模块表中的 ZzTermWidget 行；
- 更新 `src/log/ZzLogLine.h` 的 attributes 所有权注释，改为日志引擎不解析
  的终端层不透明负载；
- 清理开发者本机的 submodule config/cache，但这类本机元数据不进入提交。

历史 specs、plans、acceptance 和旧 perf records 不回改。

## 5. 提交拆分

1. ZzPureTools 上游：修复 superbuild include 路径并推送；
2. 主仓：升级 ZzPureTools pin，接入 Stacked 与旧布局清理；
3. 主仓：适配/恢复 ZzTermWidget 残留测试并更新活跃注释；
4. 主仓：删除 ZzTermWidget 子模块元数据。

每个提交都保持可 bisect；不把上游修复、主仓适配和删除混成一个提交。

## 6. 验证

### 6.1 上游 ZzPureTools

- 独立构建通过；
- 受影响组件测试通过；
- superbuild 消费路径不再引用不存在的顶层目录。

### 6.2 ZzClawTerm 本机

使用全新构建目录，不复用旧 qmsetup 缓存：

- Debug 全量 `ctest --output-on-failure`；
- Release 全量测试；
- 重点测试：`tst_ZzAppShell`、`tst_ZzShellContextMenu`、
  `tst_ZzSessionConfigWindow`、`tst_ZzSplitContainer`、
  `tst_ZzConnectFlow`、`tst_ZzSettingsPage`、`tst_PerfScrollback`；
- Linux 打包脚本 smoke；
- 手工确认旧布局只重置一次，Stacked 新布局能保存恢复。

### 6.3 CI 与三平台

- 主仓 push 后以现有三平台 CI 为最终门禁；
- Windows 重点观察标题栏按钮、高 DPI、accent/边框；
- macOS 重点观察 Cocoa 系统按钮、全屏与窗口重建；
- Linux 重点观察 X11/Wayland 窗口行为和真实终端链路。

## 7. 风险与回滚

| 风险 | 处理 |
| --- | --- |
| QWindowKit/qmsetup 构建链重写导致平台失败 | 使用干净构建目录定位；必要时继续修上游或暂缓 pin |
| Stacked 改变用户界面结构 | 已确认采用新结构；旧布局只重置一次 |
| Segoe 字体授权不完整 | 用户明确接受本次先使用；后续单独补齐授权或剔除资源 |
| 性能测试新链路阈值失真 | Release 实测后重新标定，旧 records 仅作历史对照 |
| 升级后窗口行为回归 | 按提交拆分回滚主仓 pin 或上游修复，不回滚 ZzTermCore m20 |

## 8. 完成标准

- ZzPureTools pin 位于包含 superbuild 修复的最新 master；
- 主仓显式使用 Stacked，旧布局恢复逻辑符合一次性重置口径；
- ZzTermWidget 不再出现在 `.gitmodules`、gitlink、活跃源码、构建、README
  或测试依赖中；
- 本机 Debug/Release 验证通过；
- 三平台 CI 通过；
- Segoe 授权风险以明确决策记录留档，但不阻塞本次收口。
