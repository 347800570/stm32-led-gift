# 基于STM32的灯带礼物制作项目

用木板、可寻址灯带和自己设计的控制板，制作一块能遥控、感知温度、跟随音乐闪动的灯牌。
采用 STM32F042G6U6TR + WS2812B，提供 KiCad 硬件、CubeMX/Keil 固件和基础实验。固件独立运行，不需要 Wi-Fi、手机应用或联网服务，也不是 WLED 固件移植版。

## 能做什么

| 遥控键 | 灯光效果 |
| --- | --- |
| 1 | 整条红橙黄绿蓝靛紫循环渐变 |
| 2 | 暖橙呼吸 |
| 3 | 沿灯珠编号流动的彩虹 |
| 4 | 蓝白流星 |
| 5 | 随机星光 |
| 6 | 根据实际位置，从木板底部向上燃烧的火焰 |
| 7 | 环境冷暖：18°C蓝、25°C绿、32°C红 |
| 8 | 根据声音大小，自下而上点亮的音量柱 |
| 9 | 检测鼓点能量突增，触发整条短暂闪光 |

POWER开关；+/-调整亮度；LEFT/RIGHT在1～7号调速度，在8/9号调声音灵敏度。
关灯时其他键不唤醒，也不修改设置。上电默认3号、15%亮度、速度和灵敏度均为第3档；调整只保存在RAM，断电恢复默认。

## 选择版本

| 固件 | 灯珠数 | 说明 |
| --- | --- | --- |
| [MPLT-67.hex](firmware/MPLT-67.hex) | 67 | 原始布局，27×8空间网格 |
| [SRKL-74.hex](firmware/SRKL-74.hex) | 74 | 新布局，33×12空间网格 |

74颗版本的9种模式已由作者上板验证正常。两套固件均在Windows上用ARMCC5重新编译并进行ARM指令仿真回归；仿真不验证实际电气时序。
首次发行 v0.1.0 为原型参考版本。硬件DRC仍有29项违规，不能把本仓库当作已完成生产审查的设计。

## 快速开始

1. 准备对应控制板、5V WS2812B灯带、NEC红外遥控器、5V电源及USB数据线或SWD下载器。
2. 将灯带DIN接到PB0对应的LED1_DATA，灯带和控制板共地，核对5V极性。
3. 用STM32CubeProgrammer下载对应HEX。SWD连接SWDIO、SWCLK、GND，必要时加NRST；USB DFU操作见[接线与烧录](docs/quick-start.md)。
4. 正常启动时BOOT0为低，复位后应出现彩虹。串口115200、8N1能看到READY日志。
5. 按遥控器1～9逐项验证。空间灯效要求实物排布匹配坐标文件；其他布局须重新编译。

电源按5V设计。Type-C接口并不意味着可以直接使用充电器标称的全部功率；供电能力取决于5V档、线缆、接口及布线。软件最高30%亮度不是闭环电流限制。

## 从哪里开始看

- [接线、下载和遥控使用](docs/quick-start.md)
- [编译、测试与移植布局](docs/development.md)
- [硬件状态与已知问题](docs/hardware.md)
- [基础实验导航](Software/Test/README.md)
- [67颗工程说明](Software/Official%20Version/MPLT_V03/README.md)
- [74颗工程说明](Software/Official%20Version/SRKL_V01/README.md)
- [原理图PDF](Hardware/LED_V01/Schematic.pdf)、[BOM](Hardware/LED_V01/BOM.csv)、[KiCad工程](Hardware/LED_V01/SCH-PCB/LED_V01.kicad_pro)
- [版本记录](CHANGELOG.md)、[贡献说明](CONTRIBUTING.md)、[第三方许可](THIRD_PARTY_NOTICES.md)

## 已知限制

- 使用同一套硬件引脚和指定NEC键码；其他遥控器需要重新学习键码。
- 声控是低频能量突增检测，不是音乐语义识别；拍桌也可能触发，部分音乐会漏检。
- 74颗版RAM静态分配5976/6144 B（含1024 B预留栈），继续增加灯珠或功能前必须重新核算。
- 温度仅用于显示环境冷暖，不承担过温保护。
- PCB为原型资料，DRC未通过；未发布可直接下单的Gerber附件。

## 许可

自有应用代码、测试和文档采用[MIT](LICENSE)；自有硬件设计采用[CERN-OHL-P-2.0](LICENSES/CERN-OHL-P-2.0.txt)。
STM32、CMSIS以及第三方元件库保留各自许可，以上授权不替代第三方许可，范围见[声明](THIRD_PARTY_NOTICES.md)。
