# 桌面截图贴纸外观

本页安装包校验信息对应贴纸功能首次交付；后续设置共存和首屏优化及新版安装包见 `docs/mcp-settings-capture-and-startup.md`。

## 行为

设置新增「截图贴纸」：无、边框阴影（默认）、圆角卡片、拍立得、卷角纸张。

- 保存设置后，现有贴图和后续新建贴图均使用所选样式。
- 切换外观保留截图内容在屏幕上的位置、缩放比例、锁定状态和 OCR 数据。
- 效果仅属于桌面悬浮窗口，不写入复制、PNG 保存或标注源图。
- 圆角只作用于外框，截图四角不被裁剪；拍立得增加下方留白。
- 「无」不分配阴影位图；其他样式在外观、尺寸或 DPI 变化时生成阴影，移动和普通重绘复用缓存。
- 恢复默认选择边框阴影；取消设置不应用草稿。

## 实现

- `src/model/pin_style.h`：稳定枚举值 0–4 和默认/非法值处理。
- `src/app/preferences.h`、`src/app/preferences.cpp`：`General/PinStyle` 持久化。旧配置缺少该键时使用简单边框阴影。
- `src/app/settings_process.cpp`：独立设置进程 IPC 增加样式字段，协议版本升级至 4，并校验范围。
- `src/app/settings_dialog.cpp`、`src/app/resources.rc`：五种可键盘操作的外观选项及缩略示意；延续浅色、深色和按可用屏幕尺寸缩放的布局。
- `src/pin/paper.h`、`src/pin/paper.cpp`：DPI 感知外框、命中区域和透明阴影。
- `src/pin/pin.h`、`src/pin/pin.cpp`：新贴图应用样式，现有贴图刷新时保持图像位置、缩放和锁定；缓存校验包括完整布局。
- `src/app/application.cpp`：读取当前偏好，保存后刷新所有贴图。

## 验证

新增 `tests/pin_style_test.cpp`，使用合成图片覆盖五种效果的实际分层窗口绘制、源像素完整性、透明阴影、位置/缩放/锁定保持、缓存、新旧贴图同步以及 96/144/192 DPI 和极窄图片。

扩展 `tests/settings_dialog_test.cpp`，覆盖五种选项、默认/恢复默认/取消、INI 往返、旧配置、非法枚举及设置进程编解码。

## 验证结果（2026-09-17）

- `build.bat` 完成，105 个构建步骤成功，退出码 0。
- CTest 7/7 通过：`selection_tools`、`paper_appearance`、`pin_styles`、`pin_interaction`、`pin_image`、`pin_annotation`、`settings_dialog`。总耗时 42.20 秒。
- IDE 诊断：0 个错误、0 个警告。
- 检查了实际渲染产物：`build/pin-styles-preview.png`、`build/settings-light.png`、`build/settings-dark-150.png`。五种外观可辨识，浅色/深色选项与底部保存按钮均完整显示。
- 测试只使用合成图像和独立测试设置文件；未读取个人偏好或将个人剪贴板数据用作测试样本。现有交互回归会将合成内容写入剪贴板。
- 日志：`build-sticker.log`、`sticker-tests.log`、`sticker-package.log`、`sticker-installer.log`、`sticker-delivery-check.log`。

## 安装包

`dist/LumaShot-Setup.exe`

- 大小：77,163,908 字节。
- SHA-256：`4091515E3911DB4EA43FA4BAC9E9D63314CC6C71419D2A2C120FD04D8471E083`。
- `scripts/package.ps1 -PackageName LumaShot-setup-payload` 与 `scripts/build-installer.ps1` 成功完成。
- 核对打包目录与构建目录中的主程序、三个 worker、`onnxruntime.dll`、`lumatext.dll`、`ffmpeg.exe`：SHA-256 全部一致。
- 未运行安装程序、未替换已安装应用。本次验证不声称完成实际安装或升级运行验证。
