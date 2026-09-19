# 显示演示验证记录

日期：2026-09-19。

## 实机验证（本次提交整理前已完成）

- SF32LB52-DevKit-LCD，CO5300 390×450，FT6146，CH343 UART。
- 应用固件以 sftool 0.2.5、compat 模式写入 0x12010000，并通过写入校验。
- 用户确认修复后颜色正常；按后续截图要求仅去掉呼吸页待机的重复数字，保留原布局。
- 启动日志：ReadID=0x331100；power=0x9c；format=0xd5；LCD 390×450；touch ready；font check, 0 missing glyphs。
- 最新版本串口状态：page=2、frames=45、heartbeat=212。此前相同显示配置运行超过 159 秒，未观察到重启。
- 页面切换与刷新有串口观测；不将有限观察表述为长时间可靠性或完整触摸验收。

实机已烧录文件大小：1,084,688 字节。
SHA-256：ef0c2a4c221524f8e01915e0e15a8b10af735c3781b3381fb511ee06d9402df4。

## 提交前重建

在独立 Git worktree 中，使用已记录的 NuttX、apps、openvela LVGL、SiFli 基线和完整 BSP 补丁重新构建；不依赖原 vendor/sifli 中未提交的 Makefile、NSH 配置、ILI8688E 驱动或 Kconfig 修改。

应用代码、ui_demo 配置和用于显示的 BSP 源码与实机已测版本一致。整理后的重建固件未再次烧录，编译路径会影响断言字符串和二进制大小，不能据此宣称二进制逐字节相同。

最终构建：成功。链接器仍报告现有平台的 RWX LOAD segment 与 build-id discarded 警告。
SRAM 静态占用 74,064 字节；未分配 PSRAM。
完整依赖版本见 workspace-revisions.json。工具链 arm-none-eabi GCC 13.4.0，CMake 3.28.3。

## 主机检查

相同 UI 源码，openvela LVGL 9.1；预览程序使用 -Wall -Wextra -Werror。
检查页面导航、心情弹窗、呼吸阶段、自动轮播、400 次页面切换和缺字检查。
overview.png 是主机渲染图片，不是实物照片。

## 未覆盖范围

真实传感器和生理准确性、模型推理、BLE/云服务、持久化、深睡唤醒和功耗、长期运行与故障恢复、其他屏幕或板型均未验收。BSP 依赖包含工作区已有的电源/HAL 调整，公共 PR 需要维护者进一步审阅。

提交隔离重建产物：1084720 字节；SHA-256：fe57a56f588e72d317fc329aeda5d792f0584965d32ce0b7a7fb3fb0b7884d27。
