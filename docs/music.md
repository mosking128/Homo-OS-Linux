# 音乐

支持 Linux ALSA PCM S16_LE WAV 播放及同名 UTF-8 LRC 歌词；支持暂停、定位、切歌、音量、循环、封面与后台播放。将自己的音乐放到运行目录 music/，或使用 HACHIMI_MUSIC_DIR 指定目录。仓库不提供歌曲、歌词库或专辑封面。

曲库在桌面启动时扫描，最多 256 首，递归深度最多 5 层，不跟随符号链接。默认音频设备为 default，可用 HACHIMI_AUDIO_DEVICE 覆盖。设备需安装 libasound.so.2 并正确配置声卡和输出路由。

支持 RIFF/WAVE PCM S16_LE，单/双声道、8–192 kHz；MP3、AAC 和 FLAC 需要另加解码器。音乐文件旁的同名 PNG 用作封面，缺少时显示中性占位图。tools/music/extract_covers.py 可以从用户自己的 WAV ID3 封面生成 PNG。

LRC 支持 BOM、多个时间标签、毫秒/百分秒、排序、offset 与增强时间标签。返回桌面后继续播放；销毁桌面时回收音频线程。页面隐藏时暂停视图刷新。

完整 music_smoke 测试要求一套外部 12 曲 WAV/LRC 测试夹具，默认不加入 CTest；可设置 HACHIMI_TEST_MUSIC_DIR 启用。其他架构测试在空曲库上运行，不需要或上传个人音频。
