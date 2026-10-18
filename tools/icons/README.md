# iOS SVG 图标缓存

统一源素材是用户提供的 `custom/assets/ios/ios-wifi.svg` 、`ios-battery-full.svg`、`ios-musical-notes.svg`、`ios-sunny.svg` 和 `yitaiwang.svg`，文件原样保存。状态栏和控制中心共用从这些 SVG 生成的透明度缓存，不加载旧的同名 PNG，也不在开发板上解析 SVG。

缓存有 Wi-Fi 26/44 px、普通及供电电池 36 px、音量音符 40 px、亮度太阳 40 px、以太网 44 px 七份图像。供电电池来自 `custom/assets/ios/ios-battery-charging.svg`。每个尺寸直接从 SVG 高分辨率渲染后抗锯齿缩小，不放大小位图。生成结果为 `framework/ui/ios_icon_assets.c`，使用可着色的 LVGL A8 图像，共 10392 字节像素数据；各图记录源文件 SHA-256。颜色和透明度由 UI 设置。

仅在修改 SVG 后需要生成工具，正常的 Windows/ARM 编译不需要 Python 或 SVG 依赖：

```powershell
python -m pip install --target tmp/svg-build-deps -r tools/icons/requirements.txt
python tools/icons/build_ios_icons.py --deps tmp/svg-build-deps
python tools/icons/build_ios_icons.py --deps tmp/svg-build-deps --check
```

生成后的 C 源码随工程保存。CMake 使用共享源文件列表；Linux Makefile 自动纳入 framework 下的源码。
