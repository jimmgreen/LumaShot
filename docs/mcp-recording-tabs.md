# GIF／录屏模式 Tab 动效

## 范围与实现

- 就绪面板“截图／GIF／录屏”三段式 Tab 共用蓝色底板；GIF 与录屏切换采用先拉伸115ms、再收拢250ms并轻微回弹的两阶段动画，复用截图工具栏的纯时间采样模型。
- 悬停140ms渐变，按下70ms、释放180ms颜色反馈；文字和命中区域不缩放。选中底板覆盖标签中心时使用白字，其余保持正常文字色。
- 坐标相对固定的246-DIP Tab组，不相对456/544-DIP宽的整个面板。切换后保留当前窗口水平中心和顶边；预留较高的就绪布局空间。手动移动后也保留位置，必要时按所在显示器工作区避让。小工作区下，两种模式使用一致的适配缩放；缩放/DPI改变时归位。
- 面板原有实际尺寸和参数切换语义保留，不对内容、窗口尺寸或文字进行缩放插值。点击“截图”仍立即转交主程序，不等动画结束。
- 专用16ms计时器5仅在运动期间运行；静止、隐藏、离开就绪状态、失焦、取消及销毁时停止或清理反馈。遵循系统客户端动画设置，计时器创建失败则直接归位。
- 指针离开和取消时释放Tab拥有的鼠标捕获；保留系统对预览拖动的默认取消行为。动画不启动录制会话、不调用抓屏、编码或导出。

## 文件与复现

- `src/recording/tab_motion.h`：模式ID映射及固定局部槽位，复用 `ui::ToolbarMotion`，未改动共享运动模型。
- `src/recording/panel.*`：渲染快照与底板、反馈、文字颜色。
- `src/recording/worker.cpp`：输入、按需计时、系统设置、生命周期及模式切换位置保持。
- `tests/recording_tab_motion_test.h`：由现有录制UI测试程序包含，支持 `--tab-motion`、`--tab-preview`。
- `scripts/preview-recording-tabs.ps1`：120帧、30fps合成轨迹，使用实际生产渲染器；输出 `build/recording-tabs/frames` 和 `tab-frames.png`。

```powershell
.\build.bat
ctest --test-dir build --output-on-failure -R "^(recording_tab_motion|recording_ui|toolbar_motion|shared_controls)$"
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/preview-recording-tabs.ps1
```

## 验证

最终结果和交付校验见下方。构建使用 `CMAKE_BUILD_PARALLEL_LEVEL=2` 限制并发；首次全目标构建及最终增量构建均保留日志。

专项覆盖：两个方向、快速反向切换、精确停靠、关闭动画、深浅主题、1/1.25/1.5/2 DPI生产渲染、动画前后相同命中矩形、静止状态与原静态渲染逐像素一致、透明圆角、真实窗口消息／Command路径、参数默认值、位置保持、计时器和鼠标捕获清理。

增加位置检查时，初版测试夹具未像实际 `RunUi` 一样设置 `screen`，导致初次放置使用全屏回退、切换时使用扣除任务栏后的工作区，出现一条位置断言失败。修正为与实际启动路径一致的 `MonitorFromWindow` 初始化，未放宽断言。失败记录保留在 `build/recording-tabs-fixture-failure.log`。

预览为合成时间轴，不是真实鼠标录像；已检查拉伸、收拢和停靠关键帧。未声称完成所有真实混合DPI多显示器组合的人工验收。本次只运行相关回归，上一轮文字颜色／对比度和外部菜单焦点两项扩展失败未在本次修改或重测。

## 交付

- 最终专项：**94项检查通过，0失败**。
- 最终相关CTest：`recording_ui`、`shared_controls`、`toolbar_motion`、`recording_tab_motion`，**4/4通过**。
- 保留此前截图工具栏及剪贴板动效，主程序未被本次录制UI修改。
- 安装包：`dist/LumaShot-Setup.exe`，37557672 bytes。
- 安装包SHA-256：`0e9334f99ab38fa09539c3c796ead37f6e578ca14c422def3efe2d95857641fd`。
- 主程序SHA-256：`9df5b75f8983c85e5f436d2f3f3d8af1702f8f382df272630dd6960fc8bdcde8`。
- 录制工作进程SHA-256：`e491c2b79912005ea9f1d1306b553a05f9232a9e55f4308b8080d5a02e346a64`。
- 主程序、录制工作进程均与payload逐字节一致；安装器编译成功，已检查文件大小、MZ头和SHA-256。未声称完成安装流程验收或所有依赖文件的逐项哈希核对。
- 日志：`build/recording-tabs-build-delivery.log`、`recording-tabs-tests-final.log`、`recording-tabs-preview-final.log`、`recording-tabs-package-final.log`、`recording-tabs-installer.log`、`recording-tabs-delivery.log`，后五项同在 `build/`。
- **没有运行安装器，没有替换已安装应用。**
