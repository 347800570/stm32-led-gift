# 开发、测试与维护

## 环境

- Windows、Keil MDK，ARM Compiler 5.06 update 6；此版本未验证ARMClang6或GCC迁移。
- Keil器件包Keil.STM32F0xx_DFP 3.1.1、ARM.CMSIS 6.1.0。
- 可选STM32CubeMX及STM32CubeF0 1.11.6，编辑外设配置时使用。
- KiCad 9用于硬件编辑和导出。
- Python 3、unicorn、pyelftools用于软件逻辑测试。

## 编译

用Keil打开 `Software/Official Version/MPLT_V03/MDK-ARM/MPLT.uvprojx` 或 `Software/Official Version/SRKL_V01/MDK-ARM/MPLT.uvprojx`，选择MPLT目标并编译。
驱动源码随仓库放在ThirdParty/STM32CubeF0，工程使用相对路径；Keil工具链和器件包需要自行合法安装。
输出分别在工程内 `MDK-ARM/MPLT/MPLT.hex` 和 `MDK-ARM/MPLT/SRKL.hex`。不要混用两种布局固件。
预生成固件在仓库firmware目录，仅在重新构建、测试后更新，并运行 `python scripts/check_release.py` 核验。

修改CubeMX配置后注意重新添加自定义源文件分组，保留USER CODE调用；CubeMX可能恢复驱动的本机路径，需要重新改为ThirdParty相对路径。

## 仿真测试

在仓库根目录执行：

    python -m pip install -r requirements-test.txt
    python scripts/test_all.py

必须先编译两套正式工程，测试读取新生成的AXF，不读取firmware里的HEX。
测试覆盖遥控、关灯、DMA编码与恢复、温度、所有灯珠坐标、火焰传播、音量柱滞回及鼓点检测。
仿真只执行ARM函数并模拟HAL边界，不代表真实中断延迟、供电和USB电气兼容性已验证。

## 修改灯珠排布

1. 编辑对应工程的灯带位置.txt。每个制表符单元表示一列，行代表高度；编号连续且不重复。
2. 同步修改Core/Inc/led.h中的LED_COUNT、led_layout.h中的宽高，以及Core/Src/led_layout.c的坐标；数组下标为编号减一，底部y=0。
3. 调整火焰总冷却和测试中的布局预期。只改LED_COUNT不够，火焰和音量柱都依赖空间映射。
4. 编译核算RAM；6KB包含静态数据和栈。默认堆为0，不要未经评估加入动态内存。
5. 上板先用低亮度检查末端灯珠，再测试6/8号方向和9号声控。

## 后续维护

GitHub仓库及其本地检出目录是发布版本的维护入口。原个人实验目录不会自动同步；在其上做的新实验应有选择地移入本仓库，再提交测试结果。
自定义代码优先放在app、effects、led、audio、beat等模块；CubeMX生成区域不要直接手写业务逻辑。
每次发布更新CHANGELOG、编译两套固件、运行测试、刷新firmware和SHA256SUMS，并记录实物验证范围。
CI只检查发行清单与哈希，不声称远端拥有Keil编译环境。
