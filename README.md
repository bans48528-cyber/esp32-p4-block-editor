# ESP32-P4 触屏积木编程器

面向 GUITION JC4880P443C_I_W（ESP32-P4 + ESP32-C6）的原生 C/LVGL 图形化编程项目。固定 800×480 横屏，用户在板载触摸屏上编辑、保存程序，通过蓝牙发送给科乐奇 / Spark_AI 执行。

## 当前功能

- 五类共 23 种积木：电机、组合电机、传感器、逻辑、声光；包含功率、持续运动及组合电机方向。
- 每份作品最多 100 个积木、8 层节点深度；最多保存 6 份作品。
- 拖放拼接、循环嵌套、插入占位投影、画布滑动、长按框选、整组移动、暂存、拖动删除、撤销与前进。
- 积木下方参数标签、跟随式参数弹窗、数字键盘、音符/拍数与 5×5 点阵绘制。
- 作品截图缩略图和作品数据保存在独立 Flash 分区，重启后可读取。
- BLE 搜索、连接、设备状态、板上 PikaPython 编译及程序上传/运行/停止请求；运行状态以设备通知为准。
- 程序区使用有内存预算的分块缓存，选框、拖动浮影和参数弹窗单独绘制。

图标来自用户提供的《编程板图标.ai》，25 个画板导出为透明 SVG，并生成 52×52 LVGL 图像。素材栏普通积木可见间隙约 50px，循环块约 51px。

## 验证与当前固件

最新已烧录版本为 2026-10-29。包含新图标、图标放大 30% 和素材栏积木间距缩短约 30%。写入校验通过；启动日志确认屏幕、GT911 触摸、固定横屏和 BLE 扫描正常，采集期间未见崩溃。

- [发布记录与应用 SHA256](firmware/current-release.md)
- [编译和烧录日志](firmware/current-release-build-flash.log)
- [启动日志](firmware/device-current-release-boot.log)
- [分块缓存实现与实机 A/B 数据](分块缓存与滑动实测.md)

电脑回归覆盖拖动、循环、多选、调参、暂存、保存、缩略图、存档迁移、100 块容量和缓存一致性。实机触摸体验、机器人外设动作和通信行为仍应按具体设备验收。现有自动滑动 A/B 实测平均约 16 FPS，尚未达到稳定 30/60 FPS。

## 开发板构建

使用 ESP-IDF 5.4.2，依赖固定为 LVGL 9.2.2、esp_lvgl_port 2.6.0；完整依赖见 [dependencies.lock](dependencies.lock)。`components/` 包含厂家 BSP、ST7701 驱动和配套 PikaPython 编译器，其余依赖由 IDF Component Manager 获取。

在已经加载 ESP-IDF 环境的终端执行：

```powershell
idf.py set-target esp32p4
idf.py build
# 确认屏幕板串口后烧录，按实际端口替换 COM10：
idf.py -p COM10 flash
```

Windows 可使用以下脚本，默认 ESP-IDF 位于 `C:/esp/esp-idf-v5.4.2`，构建目录为 `C:/esp/board-editor`。其他电脑需先修改脚本中的路径。

```powershell
.\build-board.ps1              # 同步到英文目录并编译
.\flash-latest.ps1 -BuildOnly  # 只检查编译
.\flash-latest.ps1             # 自动检测串口、编译并烧录
```

直接烧录归档固件：

```powershell
.\firmware\flash.ps1 -Port COM10
```

厂家 BSP 的 `CONFIG_BSP_LCD_TYPE_1024_600` 是沿用名称，实际面板分辨率为 480×800。横屏显示使用 PPA 硬件旋转，未使用 CPU 逐像素软件旋转。临时自动滑动诊断在发布配置中关闭。

### Flash 布局

| 分区 | 偏移 | 大小 |
| --- | --- | --- |
| nvs | 0x9000 | 0x6000 |
| phy_init | 0xF000 | 0x1000 |
| factory | 0x10000 | 4 MiB |
| thumbnails | 0x410000 | 0xC0000 |
| projects | 0x4D0000 | 0x20000 |
| works（预留） | 0x4F0000 | 0xB10000 |

当前采用六作品兼容存储，通过 `projects` NVS 分区保存作品，`works` 分区不参与作品读写。首次从厂家多 OTA 固件切换时应先完整备份并迁移分区；已有编辑器设备升级应保留作品和缩略图分区。2026-10-29 的分区迁移详见发布记录。

## Windows 电脑预览与回归

使用 Visual Studio 2022 C++ Build Tools 和 CMake。预览不模拟真实蓝牙连接、下载成功或机器人运行。

默认 `host/CMakeLists.txt` 引用本机相邻厂家资料内的 LVGL 源码。其他电脑可显式指定 LVGL 9.2.2 和 ESP-IDF cJSON 源码目录：

```powershell
cmake -S host -B host-build -G "Visual Studio 17 2022" -A x64 `
  -DLVGL_DIR="C:/deps/lvgl-9.2.2" `
  -DBOARD_CJSON_DIR="C:/esp/esp-idf-v5.4.2/components/json/cJSON"
cmake --build host-build --config Debug
.\host-build\Debug\editor_preview.exe
.\host-build\Debug\editor_host.exe
```

预览作品默认保存在进程工作目录，可通过 `BOARD_PROJECTS_FILE` 环境变量指定数据文件。更新预览前先保存当前作品并关闭旧窗口。

协议、设备状态、编译、上传、存储和缩略图还各有独立测试程序，源码位于 `host/test_*.c`。`host/check_compiler.cjs` 是与本机网页项目的源码/字节码对照工具，需要该参考项目，详情见 [下载运行阶段.md](下载运行阶段.md)。

## 图标资源

- `assets/block-icons/`：从 AI 文件导出的透明 SVG 和画板映射清单。
- `host/make_ai_icons.py`：提取画板及生成 LVGL 图像，依赖 PyMuPDF、Pillow。
- `host/make_icons.py`：从仓库内的 SVG 重新生成图标。
- `main/block_icons.c`：生成后的设备端图像，构建固件时不需要 Illustrator 或 Python 图像工具。

```powershell
python host/make_icons.py
# 从另一份 PDF 兼容的 Illustrator 文件重新导出：
python host/make_ai_icons.py --source-ai "D:/设计/编程板图标.ai"
```

仓库包含源码、图标、当前已验证固件与发布证据。设备完整 Flash 备份、历史固件、本机作品、构建缓存和临时预览不纳入版本管理。第三方组件保留原许可说明；中文字体为原型字形子集，发布产品时应核对字体授权。
