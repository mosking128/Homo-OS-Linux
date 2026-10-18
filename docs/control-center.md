# 状态栏下拉控制中心

控制中心提供全屏模糊底层和右侧集中排列的控制组件，适配 800×480 横屏。

顶部 Wi-Fi、电池和 Wi-Fi 卡片与状态栏共用用户提供的 `custom/assets/ios/ios-wifi.svg`、`ios-battery-full.svg`。按实际尺寸从 SVG 生成 A8 缓存后编入程序，避免放大小 PNG 或在开发板上实时解析 SVG；生成方法见 `tools/icons/README.md`。

- 顶部显示本地日期、星期，以及右侧留有间距的 Wi-Fi 和电池图标，不显示电量文字。Windows 使用系统电源状态；Linux 从 `/sys/class/power_supply` 读取。外接供电或充电时，状态栏和控制中心统一使用用户提供的 `ios-battery-charging.svg`；其他情况使用普通电池图标。
- Wi-Fi 卡片显示 SSID 和连接状态，点击切换无线开关；以太网卡片显示网线状态/IP，点击切换接口开关。优先使用板卡的 `eth2`，没有时选第一个 `ethN`，只改变链路启停，不改地址、路由或开机配置。
- 亮度和音量滑条仅显示图标，不显示文字或百分比。亮度滑条释放时调用背光服务，并保留 20% 下限；拖动期间不反复写持久化文件。音量滑条共用音乐服务的软件播放音量，音乐 App 中调整后也会同步回来。
- 播放器共用音乐 App 后台服务，播放时显示当前曲名、歌手、进度，支持上一首、播放/暂停、下一首。未播放或暂停时显示“暂无播放”，清空歌手与进度；没有曲目时禁用播放按钮。
- Windows 模拟器中，网络开关和亮度仅改变预览状态，不修改宿主机网络或屏幕。电量仍读取宿主系统。Windows 音频后端暂不播放，点击播放会给出已有服务的反馈；实际音频由 Linux ALSA 后端负责。

- 首页和应用内均可从顶部 40 px 状态栏向下拖动。
- 下拉超过 60 px 可展开，短拖松手会回弹；展开后上滑超过 60 px 可收起。
- 展开和收起使用 200 ms 动画；停留时点击背景不会退出。
- 部分展开和动画期间也会阻止触摸穿透到应用。调用桌面的 Home 接口会先收起控制中心。
- 应用开关动画进行中不接受新的下拉，避免截到过渡画面。

## 绘制与内存

每次从关闭状态下拉时，截取当前 800×480 RGB565 画面，按 5×5 区域平均缩小到 160×96。低分辨率图像经过三轮可分离的二项式高斯近似核 `[1,4,6,4,1]/16`，再用双线性插值恢复全屏并加入均匀的冷色暗调。不使用渐变模糊。

截图缓冲复用为最终模糊图，保留约 750 KiB；处理时额外需要约 90 KiB 临时空间，完成后释放。恢复 RGB565 时加入固定有序抖动，减轻色带。拖动和吸附动画只改变显示范围，不重新计算模糊、不实时缩放图像。分配或截图失败时使用纯色背景，仍可收起。桌面销毁会清理覆盖层、动画和缓存。

主体实现位于 `framework/ui/control_center.c`，由桌面统一管理，不需要各 App 单独接入。Linux Makefile 自动纳入 framework 源文件，模拟器通过共享 CMake 源文件列表链接。

控件和可暂停的刷新定时器位于 `framework/ui/control_center_controls.c`。播放器每 500 ms 刷新，日期/电量每 5 秒刷新，网络每 10 秒异步读取，关闭后停止刷新；尚未结束的网络操作只继续轮询至完成。后续上板时应同时更新 `/usr/local/libexec/hachimi-network`，新版帮助程序增加以太网操作和接口管理状态字段，服务解析仍兼容旧状态格式。

## Windows 验证

```powershell
cmake --build platform/simulator/build-display --target simulator control_center_smoke network_control_smoke calendar_smoke transition_smoke --parallel 4
cd platform/simulator/build-display/bin
./control_center_smoke.exe
./network_control_smoke.exe
```

控制中心测试使用内存显示及 LVGL 指针输入，不操作 Windows 窗口。覆盖短拖回弹、全屏模糊的像素对比、遮罩阻止点击穿透、上滑收起、原画面恢复、重新截取背景、应用内打开、Home 的关闭顺序，以及打开或动画中销毁后的资源清理；还验证亮度下限、音量后端同步、网络预览开关，以及存在歌曲时播放器切歌和反馈。可传入两个 PPM 输出路径，分别导出计算器和首页的展开效果，第三个可选参数是包含两首 WAV 的临时测试音乐目录。

`network_control_smoke` 使用测试传输层验证以太网状态解析和操作白名单。`python tests/network_control_helper_smoke.py` 使用模拟系统命令验证帮助程序的接口选择与参数限制，不改变真实网卡状态。

当前已在 Windows 验证，开发板仍待通电后验证耗时与触摸手感。
