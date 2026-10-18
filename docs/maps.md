# 北京地图

默认地图覆盖北京中心城区，中心为 WGS84 `39.9042, 116.4074`，范围为北纬 39.86°–39.95°、东经 116.34°–116.47°。这是一份公开示例区域，不代表用户实时位置，也没有接入 GPS。

![北京离线地图与地点搜索](images/beijing-map.png)

包含 z13–z16 四档缩放、729 张 256×256 RGB565 瓦片和 512 个地点。地图底图、道路及索引重新从北京的 OpenStreetMap 数据生成，没有沿用其他城市的瓦片。

支持中文、拼音和首字母搜索、全部/景点/公园筛选、拖动、缩放及选中地点。箭头和 N 回到默认地图中心；距离为地点到地图中心的直线距离，不是导航距离。左侧房子按钮和底部/右侧手势可返回桌面。

数据目录为 `custom/assets/maps/beijing`，运行目录对应 `assets/maps/beijing`。使用 `HACHIMI_MAPS_DIR` 可以覆盖默认路径。原始公开数据、区域边界、地点索引和来源声明随地图一起提供。

```sh
python3 -m pip install -r tools/offline_maps/requirements.txt
python3 tools/offline_maps/build_beijing.py --font custom/assets/ios/PingFang.ttf
# 需要更新公开矢量数据时加 --download。
```

此工具只在显式 --download 时访问网络，不抓取公共栅格瓦片服务器。字体仅用于制作地图标注。设备运行地图不需要 Python 或 API Key。

后台线程按需加载本地瓦片；32 槽缓存最多使用 4 MiB 像素缓冲，界面只重绘受影响区域。退出页面暂停 UI 更新；销毁时回收线程与缓存，后台不调用 LVGL。

数据：© OpenStreetMap contributors，采用 [ODbL 1.0](https://opendatacommons.org/licenses/odbl/1-0/)，见 [版权与署名说明](https://www.openstreetmap.org/copyright)。`metadata.json`、`source.osm.json` 和 `ATTRIBUTION.txt` 一并保留。
