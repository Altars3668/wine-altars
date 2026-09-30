# 功能分级（feature staging）函数对未分级功能的回答：`featurestagingprobe.exe`

Office 自带的 WinAppSDK `CoreMessagingXP.dll` 从 shcore 导入 `UnsubscribeFeatureStateChangeNotification`，WIL 的功能开关用的是
整组函数（`GetFeatureEnabledState`、`GetFeatureVariant`、订阅与退订、`RecordFeatureUsage`/`RecordFeatureError`）。Wine 原先
缺退订、`GetFeatureVariant` 与 `RecordFeatureError`，订阅是连输出句柄都不写的空桩。

探针打印各函数在 shcore、两个 API 集和 kernelbase 里能否查到，`GetFeatureEnabledState` 与 `GetFeatureVariant` 对无人分级的
功能号的回答，以及订阅交回的是什么（两次订阅是否不同）。两个 `Record*` 函数只查不调：它们会记录使用数据。

`results/featurestagingprobe.wine.txt`：Wine（altars-up 补上之后）；Windows 的待测（winref）。

构建：`x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror featurestagingprobe.c -o featurestagingprobe.exe`。
