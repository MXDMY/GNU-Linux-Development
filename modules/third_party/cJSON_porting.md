# cJSON 1.7.19 跨平台移植清单

有 C 库堆时不必改分配器，默认已对接 `malloc` / `free` / `realloc`。

## 按平台

| 平台                                      | 是否必须改库                   |
| ----------------------------------------- | ------------------------------ |
| GNU/Linux、嵌入式 Linux、RTOS（静态链接） | 否                             |
| Windows 静态库                            | 建议定义`CJSON_HIDE_SYMBOLS` |
| Windows 作为 DLL 使用                     | 定义`CJSON_IMPORT_SYMBOLS`   |

## 宏

| 移植项                                                                       | 位置                                       | 说明                                                                                                                                                                                      |
| ---------------------------------------------------------------------------- | ------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `CJSON_HIDE_SYMBOLS` / `CJSON_EXPORT_SYMBOLS` / `CJSON_IMPORT_SYMBOLS` | [`cJSON.h:31-69`](cJSON-1.7.19/cJSON.h)   | 仅 Windows。未指定时默认`CJSON_EXPORT_SYMBOLS`（`dllexport` + `__stdcall`）。静态库用 `HIDE`；DLL 使用方用 `IMPORT`。Linux / RTOS 静态链接忽略即可。                            |
| `CJSON_NESTING_LIMIT` / `CJSON_CIRCULAR_LIMIT`                           | [`cJSON.h:136-144`](cJSON-1.7.19/cJSON.h) | 默认 1000 / 10000。Linux / Windows 不必动。RTOS 任务栈常只有数 KB～十几 KB，解析/打印按嵌套深度递归：不可信输入时下调`NESTING_LIMIT`（例如 20～100）。`CIRCULAR_LIMIT` 用于打印防环。 |

## 函数

| 移植项                | 位置                                                                               | 说明                                                                                                                                                                   |
| --------------------- | ---------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `cJSON_InitHooks()` | [`cJSON.h:150`](cJSON-1.7.19/cJSON.h)、[`cJSON.c:209-238`](cJSON-1.7.19/cJSON.c) | 仅无标准堆时调用，传入自定义`malloc`/`free`。有 C 库堆时不要调用。`cJSON_Hooks` 没有 `realloc`：自定义分配器后内部 `realloc` 置空，打印改为「分配 + 拷贝」。 |

多任务共用同一份 cJSON 时，`global_hooks` / `global_error` 非线程安全：单线程使用，或自行加锁。

## 极受限平台

无 `malloc`、无 `double`/`libm`、或任务栈无法承担递归解析时：

- 无堆：必须 `cJSON_InitHooks()`。自定义 `malloc`/`free` 后无 `realloc`，大 JSON 打印会多次分配。
- 下调 `CJSON_NESTING_LIMIT` / `CJSON_CIRCULAR_LIMIT`，使其低于任务栈能承受的递归深度。
- 库硬依赖 `strtod` / `isnan` / `isinf` / `double`。无软浮点且无 libm 时无法只靠改宏完成移植。
