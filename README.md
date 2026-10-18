# Homo OS Linux

基于 **LVGL 9.4** 的模块化嵌入式 Linux 桌面，面向 **i.MX6ULL、800×480 RGB565 framebuffer 与 evdev 触摸**。本仓库为独立 Linux 版本，不包含 Windows 模拟器、GUI Guider 生成工程或原开发仓库历史。

默认天气城市与地图示例均为 **北京**。地图包含北京中心城区真实 OpenStreetMap 数据、729 张本地瓦片和 512 个地点，无需地图 API Key，也不读取 GPS。

![北京地图界面](docs/images/beijing-map.png)

## 功能

- 桌面、Dock、应用展开/收起动画、边缘返回手势与控制中心。
- 设置、文件管理、TXT 阅读、计算器、时钟/闹钟/倒计时、日历、天气、地图、音乐及 RGB 灯控制。
- 信息、信箱和浏览器为界面示例；没有连接个人账号或外部消息服务。
- App 产品清单与后台模块清单独立，服务不依赖 LVGL；Linux/预览后端分别接入。
- i.MX6ULL 可启用 NEON/PXP；缺少 PXP 时保留软件渲染路径。

## Linux 本机构建

需要 C 编译器、CMake 3.16+ 和 Python 3（仅构建测试时需要）。正式桌面仍要求 framebuffer/evdev；无显示设备的电脑可以运行内存显示测试。

```sh
cmake -S platform/linux -B build/native -DCMAKE_BUILD_TYPE=Release
cmake --build build/native --parallel 4
cmake --build build/native --target smoke --parallel 4
ctest --test-dir build/native --output-on-failure
```

输出为 `build/native/bin/hachimi_desktop` 与 `assets/`。本机构建默认关闭 NEON/PXP。

## i.MX6ULL 交叉编译

使用与目标板 glibc 兼容的 ARM hard-float 工具链：

```sh
CROSS_COMPILE=arm-linux-gnueabihf- JOBS=4 sh platform/linux/build-armhf.sh
```

输出为 `platform/linux/build-armhf/bin/`。此入口使用 Make，不要求构建机安装 CMake。也可以使用 [CMake 交叉编译方式](docs/linux-standalone.md)。

## 部署

备份板端程序后，将可执行文件与 `assets/` 一起复制到运行目录。已有安装应先停止桌面服务，替换后再启动。用户的书籍、音乐及系统网络配置不随源码发布。

```sh
cd /home/debian/hachimi-desktop
./hachimi_desktop
```

设备默认 `/dev/fb0`、`/dev/input/event2`，可设置 `HACHIMI_FBDEV`、`HACHIMI_EVDEV`。systemd、设备权限与固定操作 helper 安装见 [Linux 集成说明](platform/linux/README.md)。

## 隐私与配置

- **没有内置用户 Wi-Fi 名称、密码或天气 API 密钥。** Wi-Fi 状态与连接由目标机的系统网络服务管理。
- 天气密钥由用户在目标机设置 `HACHIMI_WEATHER_KEY`，或存入本地私有 `weather.key`；未配置时仍可打开天气界面。
- 默认城市为北京，地图为北京公开示例区域。示例联系人和邮件正文均为虚构内容。
- 测试网络数据为合成示例；示例 IPv4 使用文档地址段。
- 本仓库从清理后的文件建立全新历史，未带入原仓库提交、截图、日志、用户状态或网络配置。
- 可运行 `python3 tools/audit_public.py` 检查常见凭据、个人路径与不应发布的配置文件。

## 扩展 App

在 `apps/<name>/` 实现 `hachimi_app_module_t`；将源文件加入 `cmake/hachimi_runtime_sources.list`，并在 `product/default_apps.c` 绑定图标、排列和可选后台钩子。隐藏图标与完整裁剪不同：完整裁剪还需要移除源码条目和后台引用。

目前 Home 最多 18 个图标，Home/Dock 合计最多 24 个入口，最多缓存 16 个不同 App 视图。新增 App 不需要修改桌面动画或平台入口。详见 [架构说明](ARCHITECTURE.md)。

## 文档

[独立构建与验证](docs/linux-standalone.md) · [北京地图](docs/maps.md) · [天气](docs/weather.md) · [时钟](docs/clock.md) · [日历](docs/calendar.md) · [音乐](docs/music.md) · [控制中心](docs/control-center.md)

## 许可与来源

项目自有代码采用 [MIT](LICENSE)。第三方代码、字体、地图数据及图形资源保留各自权利与来源，不自动改为根目录 MIT，详见 [THIRD_PARTY.md](licenses/THIRD_PARTY.md)。

PingFangSC 字体采用所列来源仓库固定版本，并附其 MIT 文本；现有图标与壁纸由项目所有者确认具有公开分发授权。仓库不包含歌曲、歌词库或用户相册。
