# 显示演示 BSP 依赖

`vendor-sifli-co5300-demo.patch` 是相对公共仓基线的完整补丁，不是相对某次未提交工作区的增量。
基线：open-vela/vendor_sifli @ af6f365eaa04a674af0467aa1a803bc4c77691ba。
提交：INKT-love/vendor_sifli @ c403c6787ccba21ab3c8505f65e13faa851bed5a。

包含当前演示实际使用的 CO5300 初始化、传输同步、显示/触摸引脚、电源和 HAL 配置改动。
保留四字节 QSPI 命令、启用正常颜色模式；屏幕实际显示及中文检查已通过。
8-bit/24-bit 模式、深睡恢复、其他板型和 HAL 异常恢复尚未全面验证。

应用方式、验证记录与公共仓 PR 见 [根 README](../../../README.md)。

公共 BSP 草稿 PR：[open-vela/vendor_sifli#36](https://github.com/open-vela/vendor_sifli/pull/36)。
