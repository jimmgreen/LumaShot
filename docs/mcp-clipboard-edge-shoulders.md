# 剪贴板贴边肩部轮廓修正

日期：2026-09-22。

## 目标与原因

用户指出贴边形态应像边缘自身向内长出的凸起，上下肩部以凹圆角转入边界，而不是从胶囊侧壁中段拉出连接颈。
旧实现的连接高度为 `(half-r)*strength`，首个贝塞尔控制点还将高度乘以 `0.75`，导致连接向中段收缩；胶囊靠边一侧的凸圆角仍明显可见。

## 改动

- `src/clipboard/liquid_surface.cpp`：完全吸附时连接点达到上下肩部，连接高度为 `half`；使用四分之一圆弧的三次贝塞尔切向长度，让肩部水平切线与边界竖直切线平顺过渡。上下/左右四个方向共享这一逻辑。
- 接近边缘时继续使用原有吸附权重，保留脱离过程、布尔合并、抗锯齿、命中区域及静止无持续渲染的行为。
- 不改变 28 × 96 DIP 内容布局、主题、计数、图标、吸附位置规则或展开面板架构。本轮修正的是连接轮廓，不是把整个控件改成参考图中的尺寸和黑色配色。
- `tests/clipboard_liquid_motion_test.cpp`：新增肩部填充、两端凹圆角延伸、圆角外部透明/不命中检查，覆盖四边、两主题及 100/125/150/200% DPI。

## 验证

- 修改生产代码前：365 项检查中 128 项失败，均为新加的肩部连接断言，见 `build/clipboard-edge-before.log`。
- 修改后：365/365 通过，见 `build/clipboard-edge-after.log`。
- 完整构建成功，见 `build/clipboard-edge-build-after.log`。
- 相关 CTest 4/4 通过：clipboard_panel、clipboard_width、clipboard_header_drag、clipboard_appearance，见 `build/clipboard-edge-regression.log`。
- 两个修改的 C++ 文件当前编辑器诊断无 warning/error；诊断不是构建或视觉验收的替代。
- 生产渲染器重新生成 150 帧合成预览，见 `build/clipboard-liquid/frames/` 和 `build/clipboard-edge-preview.log`。静止右侧贴边帧 `build/clipboard-liquid/frames/frame-1000.png` 经放大检查，已呈现上下肩部接入边界的凹圆角，不再露出靠边侧凸圆角。
- 预览使用合成内容，不读取个人剪贴板或抓取个人桌面；未声称已在全部真实多显示器环境手工验证。

## 安装包

打包日志：`build/clipboard-edge-package.log`；安装器构建日志：`build/clipboard-edge-installer.log`。
- 安装包：`dist/LumaShot-Setup.exe`，37536249 bytes；Inno Setup 构建成功。
- SHA-256：`3fefb894ca001bae646ed86231fb8b537c602228cfddf11f191c13e6f31266f9`。
- 已核对 MZ 文件头；payload 主程序与 build 主程序逐字节相同，主程序 SHA-256 为 `9b125add470aa1ce9c10c51e4be2bb2efd6d8c5fcf77b943a15e001218f94c76`。
- 没有运行安装器，没有替换已安装版本。
