# iPad 风格界面间距

状态栏区域按屏幕顶部 32 px 预留；应用内容从屏幕 y=58 px 开始，和状态栏区域之间留 26 px 安全间距。应用标题、操作按钮、搜索框等顶端内容都应位于这条安全线或其下方。

共享尺寸定义在 `ios_layout.h`：`IOS_STATUS_BAR_CONTENT_BOTTOM`、`IOS_STATUS_BAR_SAFE_GAP` 和 `IOS_APP_CONTENT_SAFE_TOP`。更改这些尺寸前，应在 800×480 设备界面上确认状态栏图标与应用内容没有重叠。
