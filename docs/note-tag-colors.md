# 序号注释标签配色

文字颜色提供自动、预设色块、自定义颜色圆点。默认自动模式从序号背景主色生成同色系文字；浅色底使用深色文字，深色底使用亮色文字。标签使用柔和的不透明底色，无描边。截图中标签区域的亮度用于选择深浅版本，导出与预览共用配色计算。自定义文字颜色保持原值，必要时调整其配对底色。颜色选择取消恢复原有模式及颜色，不改变序号颜色。

参考 Ant Design filled Tag 的同色系配对方式；并非直接复制其颜色令牌。自动与预设模式按 4.5:1 检查文字与实色标签底的对比度。背景采样仅决定明暗版本，标签底色不会透出复杂截图内容。

验证：build.bat 成功；cursor_capture、annotation_render、unified_toolbar、shared_controls、capture_to_pin、capture_reselect 六项通过。测试包含黑白/灰背景、多种填充、自定义色保留、取消恢复、窄屏及高 DPI 布局。实际明暗配色图见 note-tag-colors.png，日志见 note-tag-tests.txt。
