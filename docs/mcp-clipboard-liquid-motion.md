# 剪贴板液态动效：实现与验证

日期：2026-09-22。参考：[Liquid task pill · Bencho](https://bencho.dev/finds/bryce-agent-blob)。范围是 LumaShot 的折叠侧边条拖动/吸附与面板展开/收起；没有重做列表或修改小剪贴板的输入架构。

## 已实现

- 折叠条保留 28 × 96 DIP 的逻辑尺寸。拖动时速度驱动有限幅度的伸缩、倾斜和滞后，停止后弹簧回正。静止按住鼠标也会停止动效计时。
- 四个工作区边缘都有渐进式曲线黏连。吸附仍使用现有的 24/36 DIP 进入/离开滞回；停靠位置现在抵达工作区边界，而不是留下旧的 4px 缝隙。透明绘制范围不侵入任务栏。
- 展开/收起使用可重新定向、保留速度的解析弹簧，宽高变化略有错位，圆角随过程改变。普通展开约 420ms，普通收起约 380ms；快速反向可从当前形状继续。Esc 收起及收起途中点击重新打开不必先跳到旧终点。
- 外壳和内容分离。文本、图标、列表由原有 LumaText 路径绘制后缓存，动效帧只裁剪、平移、显隐，不非等比拉伸文字。内容在外壳接近展开尺寸后显现。
- 动画外壳暂用高不透明度主题色，避免矩形原生背景漏出；展开稳定时恢复真实 DWM 亚克力。没有截取真实桌面来模拟模糊。
- 关闭系统客户端区域动画时直接落到目标。跨显示器展开、过大的过渡面以及资源失败有即时回退；DPI 变化会重建布局，显示器布局变化会取消不再可靠的拖动状态。
- 动效计时器只在运动期间使用 5/9；此前焦点探测的计时器 8 没有复用。禁用/销毁清理动效、缓存和窗口资源。

## 关键实现

- `src/clipboard/liquid_motion.h`：解析弹簧、拖动形变量、工作区约束、边缘权重和有界视口。
- `src/clipboard/liquid_surface.h/.cpp`：软件 Direct2D 矢量外壳、布尔合并的连接曲线、缓存内容和命中几何。
- `src/clipboard/panel.cpp`：生命周期、原生材质切换、动画重定向、拖动/焦点/消息协调。
- 液态模式暂时关闭原生非客户区背景，避免透明外缘出现白色矩形。原生输入区域按非零 alpha 像素生成，完整包含抗锯齿像素，而非用整数圆角替代轮廓；展开静止时移除该区域，恢复系统圆角。
- 连接处使用圆形描边连接，避免与屏幕边缘相切时产生长斜接尖刺。命中缓存只用于与当前窗口视口一致的形状，避免布局后旧几何误判。

## 验证

最终结果见以下日志；中间失败的日志不代表最终交付状态。

- `build/clipboard-liquid-build-final.log`：完整构建通过，C++20 /W4 /WX。
- `build/clipboard-liquid-tests-final.log`：相关 CTest **14/14 通过**：panel、width、header_drag、hotkey_policy、disable_memory、resource_audit（20 轮）、features、panel_pin、appearance、continuous_paste、liquid_motion、shortcut_focus、quick_input、quick_window。
- `build/clipboard-liquid-motion-final.log`：最终合成数据专项 **173/173 通过**。测试解析采样、反向连续性、精确归位、关闭动画入口、形变限幅、四边连接、负屏幕坐标、100/125/150/200% DPI、两种主题、预乘 alpha、输入区域保留全部抗锯齿像素、缓存文字复用、窗口不激活、静止计时停止和资源释放。
- `build/clipboard-liquid-shortcut-final.log`：焦点分流专项 **33/33 通过**；完整剪贴板和小剪贴板路由、非激活与事务保护仍在。
- 原有热键许可测试改用合成的小剪贴板打开状态验证放行/拦截，不再错误假定当前用户前台一定没有编辑焦点。路由正确性仍由专门的焦点测试验证。
- 真实 DWM 外观截图位于 `build/build/clipboard-*.png`：只截取自有合成背景覆盖的范围。背景中的白色纵条是用于观察透明/亚克力的测试图案，不是漏底。

### 视觉预览与复现

```powershell
.\build.bat
.\scripts\preview-clipboard-liquid.ps1
# 可选的开发辅助依赖 Pillow，不是应用运行依赖：
python .\scripts\assemble-clipboard-liquid.py
```

预览由生产渲染器绘制 150 个 720 × 780 原生像素帧，使用四条合成剪贴板记录和合成背景；不是用户桌面录像。脚本输出 `build/clipboard-liquid/liquid-frames.png`（10 列 × 15 行，30fps），可组合为 5 秒 GIF。检查了展开、收起、脱离、贴边的抽样帧，以及真实 DWM 截图。GIF 的重复静止帧可合并，但总时长保持 5 秒。

验证边界：DPI/工作区矩阵有自动化覆盖，但未声称在所有真实多显示器组合或显卡驱动上逐一手工验证。没有自动改动用户的系统动画设置；减少动画通过同一策略入口的关闭路径测试。没有把合成预览当成实际鼠标操作录像。

## 交付

- 安装包：`C:\Users\SS\Desktop\LumaShot\dist\LumaShot-Setup.exe`
- 大小：37543324 bytes
- SHA-256：`271631e6fa0c7c5d940f6593bb654405b54f043fe9e4862d0a0349a90a5c26e5`
- 构建与 payload 中的 `LumaShot.exe` SHA-256：`2a2d57fc3e24913181352dcba1a2a7f9c90259c16aa388c9bbeb9c789782b209`，逐文件校验一致。
- 安装包已检查 MZ/PE 文件头和 SHA-256；打包与 Inno Setup 构建日志分别为 `build/clipboard-liquid-package.log`、`build/clipboard-liquid-installer.log`。
- **没有运行安装器，没有替换已安装应用。**
