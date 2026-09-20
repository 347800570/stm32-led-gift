# 许可范围与第三方声明

根目录MIT仅适用于作者自有应用代码、测试、脚本和文档，不重新许可下列第三方文件。

| 内容 | 来源与许可 |
| --- | --- |
| STM32F0 HAL | STM32CubeF0 1.11.6，BSD-3-Clause，见ThirdParty/STM32CubeF0/Drivers/STM32F0xx_HAL_Driver/LICENSE.txt |
| CMSIS内核头文件 | ARM，Apache-2.0，见ThirdParty/STM32CubeF0/Drivers/CMSIS/LICENSE.txt |
| STM32F0 CMSIS Device | ST/ARM，依组件随附许可及STM32CubeF0 Package_license.md；保留各文件声明 |
| CubeMX生成文件及启动代码 | 保留ST/ARM版权头；按随包Package_license.md及相应组件许可，非作者单独MIT授权 |
| KiCad标准Fuse封装、Device/power符号 | KiCad库，CC-BY-SA-4.0及库例外，见LICENSES/KiCad-library-exception.txt |
| 自有PCB、原理图及作者元件设计 | CERN-OHL-P-2.0；第三方图形/元件内容保留原权利 |

硬件库仅随设计提供使用到的符号和封装，含作者Aboka库及现有Locator/easyeda转换器件资料；不宣称取得器件商标或外观的独占权。
KiCad库例外使引用这些库元素的电路设计无需整体改用CC-BY-SA，但库本身修改/分发仍应遵守相应条款。

未包含WLED或其他调研项目源码；本项目不依赖它们运行。Keil编译器、CubeMX和器件包不包含在仓库内，请按各自条款获取。
