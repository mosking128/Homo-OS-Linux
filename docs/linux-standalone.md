# 独立 Linux 版本

本版本保留现有 800×480 桌面和 App，源码包不包含 Windows/SDL 模拟器、GUI Guider 生成页面、Git 或本地构建目录。默认地图与天气城市均为北京。

## 生成源码包

在完整工程根目录执行：

```sh
python3 tools/package_linux.py output/homo-linux-source.tar.gz
```

解压到独立目录后执行下面的构建命令即可，无需原工作区的模拟器目录。包内 `platform/lvgl/` 和 `custom/assets/` 为运行依赖；第三方许可与资源归属文件一并保留。

## i.MX6ULL：Make

```sh
JOBS=4 sh platform/linux/build-armhf.sh
```

可设置 `CC` 或 `CROSS_COMPILE` 指定兼容目标板 glibc 的 ARM hard-float 工具链。输出为 `platform/linux/build-armhf/bin/hachimi_desktop` 和 `assets/`。开发板默认启用 NEON/PXP；不会要求构建机安装 CMake。

## i.MX6ULL：CMake

```sh
cmake -S platform/linux -B build/linux-armhf \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/platform/linux/armhf-toolchain.cmake" \
  -DCMAKE_C_COMPILER="$HOME/.arm-linux-gnueabihf-current/bin/arm-linux-gnueabihf-gcc" \
  -DCMAKE_BUILD_TYPE=Release -DHACHIMI_USE_NEON=ON -DHACHIMI_USE_PXP=ON
cmake --build build/linux-armhf --parallel 4
```

输出位于 `build/linux-armhf/bin/`，包括图标、字体和地图资源。构建测试需要 Python 3；只编译正式程序时可设置 `-DBUILD_TESTING=OFF`。

## 通用 Linux：关闭板级加速

```sh
cmake -S platform/linux -B build/linux-native \
  -DCMAKE_BUILD_TYPE=Release -DHACHIMI_USE_NEON=OFF -DHACHIMI_USE_PXP=OFF
cmake --build build/linux-native --parallel 4
```

仍使用 Linux framebuffer/evdev，不是 X11/Wayland 窗口程序。当前界面要求 800×480；Goodix 修正仍保留在 Linux 入口，完整板级目录迁移留待后续整理。

## 验证

Make 可构建板端内存显示测试，不占用真实 framebuffer：

```sh
make -C platform/linux CC="$HOME/.arm-linux-gnueabihf-current/bin/arm-linux-gnueabihf-gcc" \
  -j4 smoke architecture-smoke led-service-smoke
```

将测试程序和资源一起复制到目标板，从资源所在目录运行：

```sh
python3 tests/run_smoke.py /absolute/path/architecture_smoke
python3 tests/run_smoke.py /absolute/path/clock_smoke
python3 tests/run_smoke.py /absolute/path/runtime_smoke
./led_service_smoke
```

`run_smoke.py` 为每次执行创建临时 HOME，天气测试不读取真实密钥，非音乐测试不扫描真实曲库。`runtime_smoke` 包含实体 LED 操作，成功后恢复进入该页面时读取的通道值。`clock_smoke` 会触发提示音。

CMake 的本机验证使用 `platform/preview` 后端，不调用网络/时间 helper 或写入 LED；正式桌面始终使用 Linux 后端：

```sh
cmake --build build/linux-native --target smoke --parallel 4
ctest --test-dir build/linux-native --output-on-failure
```

跨平台 CMake 构建生成的是 ARM 测试程序，需要复制到板端运行。音乐完整测试要求现有的 12 首 WAV/LRC 测试曲库，只有设置 `HACHIMI_TEST_MUSIC_DIR` 后才加入 CTest。

## 部署与回滚

先备份目标目录中的 `hachimi_desktop` 和 `assets/`，再停止 `hachimi-desktop.service`、替换程序及资源并重新启动。保留原有 `music/`、`books/`、用户状态、helper 和系统服务配置。首次部署的 helper 与 systemd 配置方法见 `platform/linux/README.md`。

部署完成后确认服务为 active，并比较磁盘程序和 `/proc/<MainPID>/exe` 的 SHA-256。实际 framebuffer/evdev 验证可使用 `tests/board_evdev_smoke.py`；脚本会临时停止桌面，结束后恢复服务。RGB 验证脚本 `tests/board_led_control_smoke.py` 会恢复此前的灯光值。

如启动或验证失败，停止服务，将备份中的程序和资源恢复到原位置，再启动服务。
