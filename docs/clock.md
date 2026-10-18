# 时钟 App

时钟沿用 800×480 桌面与公共状态栏，使用深色背景、左侧导航、橙色选中状态和大号数字。侧栏背景与分隔线从 y=0 延伸到底部，状态栏位于其上；内容标题遵守 58 px 安全线。

- 闹钟：最多 8 个。`+` 添加，轻点时间编辑，选择星期设置重复；不选星期表示仅一次。开关启停，`编辑` 中删除。到点自动禁用一次性闹钟，重复闹钟同一天只触发一次。
- 世界时钟：本地、伦敦、巴黎、纽约、东京、北京，显示当地日期以及相对本地的今天/昨天/明天。伦敦、巴黎及纽约使用内置的现行夏令时规则；城市清单固定，未接入完整 IANA 时区库。英国规则参考 [GOV.UK](https://www.gov.uk/when-do-the-clocks-change)，美国规则参考 [NIST](https://www.nist.gov/pml/time-and-frequency-division/popular-links/daylight-saving-time-dst)。
- 秒表：开始、停止、归零和最多 8 次计次，显示百分之一秒；切换页面和应用后继续计时。
- 计时器：小时/分钟/秒滚轮，开始、暂停、继续、取消；切换应用后继续倒数，到期弹出全屏顶层提醒。

桌面启动时通过 App 系统生命周期钩子加载闹钟，不需要先打开时钟 App。闹钟保存在 Linux 的 `$HOME/.hachimi/clock-alarms.txt` 或 Windows 的 `%USERPROFILE%\.hachimi\clock-alarms.txt`。提醒提供停止和稍后 9 分钟；同时到期的多个提醒排队显示。桌面进程退出后不再计时，也不具备关机唤醒功能。秒表、倒计时和稍后提醒属于当前进程状态。

提示音由 services/alert 提供。开发板使用 GPIO19 上的有源蜂鸣器，高电平发声、低电平关闭；可用 `HACHIMI_BUZZER_GPIO` 覆盖 GPIO 编号。每 1.5 秒循环一次：响 180 ms、停 160 ms、响 180 ms、停 980 ms。运行时按单调时钟切换电平，不阻塞 UI；停止、稍后提醒和桌面正常退出都会拉低并关闭蜂鸣器。服务在第一次提醒时设置输出，自动导出尚未导出的 GPIO，要求桌面用户具备 sysfs GPIO 写权限。Windows 使用相同节奏的声音预览。蜂鸣器不可用时，提醒弹窗会显示声音不可用。

Linux 主入口捕获 SIGTERM/SIGINT，由 UI 线程执行桌面销毁和蜂鸣器关闭；systemd 正常停止和重启也会走这条清理路径。

板端 `make buzzer-smoke` 构建专用检查（不自动执行）。`buzzer_smoke` 会实际驱动 GPIO19 约 1.6 秒，读回双滴及停顿各阶段的电平，并确认停止后为低电平。应在没有其他程序操作 GPIO19 时运行。

2026-10-15 已交叉编译并部署到 `/home/debian/hachimi-desktop/hachimi_desktop`，由 `hachimi-desktop.service` 运行。部署前完整备份位于 `/home/debian/hachimi-backup-before-clock-buzzer-20261015/`。ARM 版 `clock_smoke` 在板上通过全部六组检查；GPIO19 双滴电平读回和停止拉低检查通过。

## 回归检查

Windows 模拟器目标：

```powershell
cmake --build platform/simulator/build --target clock_smoke
# 先创建专用、可写的临时 HOME 与截图目录；从 bin 目录运行，以便加载 assets。
Push-Location platform/simulator/build/bin
.\clock_smoke.exe <临时HOME绝对路径> <截图目录绝对路径>
Pop-Location
```

Linux `make smoke` 同时构建 `clock_smoke`，运行参数相同。检查覆盖：首次打开前的已保存闹钟、添加/编辑/重复日期、持久化、秒表、倒计时暂停恢复、跨应用提醒、稍后提醒、同时到期提醒队列、桌面反复销毁的资源回收，以及侧栏贯穿状态栏的像素回归。检查采用 LVGL 内存显示与可控 tick，输出 PPM 截图，不占用 framebuffer。
