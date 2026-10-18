# 日历 App

界面参考 [Apple 的 iPad 日历视图说明](https://support.apple.com/zh-cn/guide/ipad/ipadf8fad203/ipados) 和 [日程编辑说明](https://support.apple.com/zh-hans/guide/ipad/ipadafeefacf/ipados)：浅灰侧栏、迷你月历、日历分类、红色操作按钮，以及顶部日/周/月/年分段切换。适配 800×480 屏幕，侧栏背景和分隔线从 y=0 贯穿状态栏；标题从 58 px 安全线开始。

- 日/周：可滚动的 24 小时时间轴，全天日程单独显示；时间重叠的日程并排显示。红线标记今天的当前时间。
- 月：六行日期网格、今天红色圆点、选中日期及分类色日程。日期旁的 `+N` 表示更多日程；再次轻点选中的日期进入日视图。
- 年：可滚动的十二个月总览，轻点月份进入月视图。
- 列表：从选中日期开始的日程，按日期和开始时间排列。
- 搜索：匹配标题和位置，保留分类筛选。
- 编辑：`+` 新建，轻点日程编辑；支持标题、位置、日期、全天、开始/结束时间及个人/工作/家庭三个分类。删除前确认。标题和位置各限 32 字，最多 64 条日程，日期范围 1900–2199 年。

本地日程保存在 Linux `$HOME/.hachimi/calendar-events.txt` 或 Windows `%USERPROFILE%\.hachimi\calendar-events.txt`。服务层不引用 LVGL，负责公历/闰年计算、字段校验和持久化；文本字段使用十六进制编码，支持 UTF-8 和分隔符。完整写入临时文件后原子替换，保存失败回滚内存改动，无法解析的原文件保留并禁止覆盖。

当前提供设备本地日程，不包含 iCloud/CalDAV 同步、邀请、农历、重复日程或日程提醒。屏幕键盘为 LVGL 英文键盘，已有中文日程可以显示，尚无拼音输入法。闹钟及其蜂鸣器提醒仍由时钟 App 运行时负责。

## 检查

`calendar_smoke` 使用独立 HOME 和 LVGL 内存显示，覆盖公历与闰年、UTF-8 保存重载、写入失败回滚、日程添加/编辑/删除、输入校验、分类筛选、五种视图、搜索、全天日程、应用切换、重启持久化、侧栏顶端像素及定时器回收，输出 PPM 截图。

Windows：`cmake --build platform/simulator/build --target calendar_smoke`。Linux：`make smoke`。从资源所在目录运行：

```sh
calendar_smoke <预先创建的专用临时HOME绝对路径> <预先创建的截图目录绝对路径>
```

2026-10-15 已部署到板端 `/home/debian/hachimi-desktop/hachimi_desktop`，服务 `hachimi-desktop.service` 运行正常。部署前完整备份：`/home/debian/hachimi-backup-before-calendar-20261015/`。ARM 版 `calendar_smoke`、`clock_smoke` 和原有 `runtime_smoke` 全部通过；检查使用临时 HOME 和内存显示，测试日程未写入正式用户数据。
