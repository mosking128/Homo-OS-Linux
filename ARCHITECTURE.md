# 桌面架构

```text
framework/          桌面、App 管理器、注册表接口、导航和动画
framework/ui/       字体、状态栏、控制中心及布局
product/            默认 App、图标、Home/Dock 排列及独立后台模块清单
apps/               App 视图与交互
services/           业务模型、服务接口与任务状态
platform/linux/     framebuffer/evdev、Linux 服务后端与 i.MX6ULL 适配
platform/preview/   无硬件预览/测试后端
platform/lvgl/      LVGL 9.4 第三方源码
config/             独立公共 LVGL 配置
cmake/              Make/CMake 共用源码清单及运行时库
custom/assets/      UI 资源与北京地图
tests/              服务、生命周期、UI 与实体板验证
tools/              地图制作、图标缓存、隐私检查与打包
```

`hachimi_desktop_create(catalog)` 接收产品提供的 `hachimi_app_catalog_t`。框架不包含具体 App 头文件。清单数组与模块指针在桌面运行期间必须有效；运行中拒绝重新配置，销毁后可换产品。`ios_desktop_create()` 是默认产品的兼容入口。

App 使用 create/on_open/on_close 管理视图，系统钩子管理跨应用后台任务。后台清单不依赖图标是否可见，拒绝重复模块，按启动的逆序停止。音乐和闹钟跨应用运行；时钟隐藏时暂停页面刷新，继续后台计时。当前采用单进程、静态注册、首次打开后缓存视图，尚无动态插件或缓存淘汰。

服务使用普通 C 数据结构，不依赖 LVGL。控件只在 UI 线程访问。LED App 通过 services/led 调用 Linux 后端；服务处理直接访问失败后的 helper 降级、40 ms 写入节流、读取与用户修改冲突和取消。网络/时间 helper 只允许固定操作。

状态栏控件随创建注册、删除注销，最后一个控件删除后释放定时器。地图通过产品描述的 reserve_left_edge 声明左边缘归 App 导航使用，桌面不判断地图 App ID。

Linux 与预览构建共享运行时源码清单；平台后端及链接依赖绑定到 CMake target。公共 LVGL 配置不依赖模拟器或 GUI Guider。Linux Make 默认启用 i.MX6ULL NEON/PXP；通用 Linux CMake 默认关闭，两种构建均复制 UI 和地图资源。

新增 App：实现 apps/<name>/ 模块，在 framework/app_registry.h 追加 App ID，在 cmake/hachimi_runtime_sources.list 加入源码，并在 product/default_apps.c 配置图标、排列、手势策略与后台模块。完整裁剪需要同时移除产品引用及源码条目；只移除图标不会裁掉代码。现有限制为 18 个 Home 图标、24 个 Home/Dock 入口和 16 个缓存 App 视图。

测试使用内存显示和独立临时 HOME。本机 CTest 使用 preview 后端，实体板 Make 测试使用 Linux 后端。测试网络数据为合成示例，不是项目所有者的网络配置。
