# 固定横屏 LVGL 积木编辑器

ESP32-P4 原生 C/LVGL 工程，固定 800×480 横屏，启动 270°。屏幕负责编辑，科乐奇通信尚未接入。

已实现五类 17 种积木、多层循环、连接曲线和参考图标、参数键盘、点阵绘制、多选/框选、整组暂存、拖动删除、双向画布、撤销前进和作品本地保存。每作品最多 32 个节点、8 层节点深度，旧存档保留并迁移到版本 2。

完整操作、容量和测试说明见 [完整编辑器.md](完整编辑器.md)。

## 验证状态

2026-09-10 已通过主机真实 LVGL 触摸事件、模型边界/随机移动、存档迁移和 800×480 渲染测试。新版已烧录到 COM10，启动程序、应用和分区表校验通过；15 秒启动采集未见崩溃。日志确认显示与触摸驱动初始化，`full editor home started; 800x480 fixed rotation=270`。

实体触摸手感、满容量设备性能及断电保存仍需实机操作验收。firmware/ 是当前固件，firmware/stage1/ 保留上一阶段文件，device-backup/ 保留原始固件及本次烧录前 NVS 备份。

## 固件构建

在已安装 ESP-IDF 5.4.x 的终端中：

```powershell
idf.py set-target esp32p4
idf.py build
# 确认屏幕板的端口后再执行，替换 COM_PORT：
idf.py -p COM_PORT flash monitor
```

build-board.ps1 会自动同步源码到 C:/esp/board-editor、加载开发环境并编译。components/ 中包含厂家屏幕 BSP 和 ST7701 驱动，其余依赖由 IDF Component Manager 获取。首次构建需要网络；当前依赖解析与设备端链接已通过。

厂家 BSP 的 `CONFIG_BSP_LCD_TYPE_1024_600` 名称沿用旧板，但实际分辨率在厂家头文件中已改为 480×800。此工程使用该项启动面板，在 main.c 中仅执行一次 270° 旋转得到固定横屏（按用户反馈，将首版横屏调转 180°）。已把本工程复制的 BSP 初始 swap_xy 设为 false，避免与一次性旋转叠加。触摸坐标随 LVGL 显示方向转换，仍需真机四角验证；不添加用户切换功能。

使用部分缓冲和软件旋转，关闭避免撕裂模式；这两项在厂家 BSP 中互斥。首次上板检查显示、四角触摸、拖动响应、内存和撕裂情况。原厂家资料未修改。

## 主机验证

使用 Visual Studio 2022 C++ Build Tools 和 CMake：

```powershell
cmake -S host -B host-build -G "Visual Studio 17 2022" -A x64
cmake --build host-build --config Debug
.\host-build\Debug\editor_host.exe
.\host-build\Debug\editor_preview.exe
```

host/CMakeLists.txt 默认引用相邻厂家资料中的 LVGL 9.2.2，可通过 `-DLVGL_DIR=路径` 指定其他 9.2.2 源码。中文字体为本机字体生成的原型字形子集，生成脚本 host/make_font.py；产品发布时需使用有相应授权的字体资源。


当前源码已烧录，版本与校验记录见 firmware/current-release.md。本目录已建立独立本地 Git 仓库，编译缓存、临时预览、设备备份和历史固件归档不纳入提交。
