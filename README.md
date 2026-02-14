# qBittorrent Bark 通知工具（Windows）

这是一个使用 C 编写的轻量级通知工具，用于在 qBittorrent 下载开始或完成时向 Bark 发送推送。程序为 Windows GUI 子系统应用，无控制台窗口，日志输出到同目录下的日志文件。

## 功能特性

- 支持 qBittorrent 12 个占位符参数
- 结构化推送内容，自动转换容量单位
- 配置文件读取（INI），避免环境变量不可用的问题
- UTF-8 中文参数支持与 URL 编码
- 详细日志输出，便于排查问题

## 环境与依赖

- Windows 10/11
- Clang/MinGW 或 MSVC

## 构建方式

### 直接使用 Clang/MinGW

```powershell
clang bark_notify.c -o bark_notify.exe -lwinhttp -lshell32 -luser32 -lkernel32 -static -mwindows
```

### CMake 构建

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## 配置文件

程序会读取与 exe 同目录下的 `bark_notify.ini`，支持 UTF‑8 BOM 与空格。示例：

```ini
[Bark]
Key=你的BarkKey
Server=api.day.app
```

服务器地址支持自建服务端，例如：

```ini
[Bark]
Key=123
Server=127.0.0.1:8080
```

## 日志

运行后会在同目录生成 `bark_notify.log`，记录完整调用链路与错误码。

## qBittorrent 配置

在 “选项 → 下载 → 运行外部程序” 中填写：

下载完成：

```bash
D:\path\to\bark_notify.exe -E completed -N "%N" -L "%L" -G "%G" -F "%F" -R "%R" -D "%D" -C "%C" -Z "%Z" -T "%T" -I "%I" -J "%J" -K "%K"
```

下载开始：

```bash
D:\path\to\bark_notify.exe -E start -N "%N" -L "%L" -G "%G" -F "%F" -R "%R" -D "%D" -C "%C" -Z "%Z" -T "%T" -I "%I" -J "%J" -K "%K"
```

## 参数对照表

| 参数 | 含义 | 占位符 |
|---|---|---|
| -N | 种子名称 | %N |
| -L | 分类 | %L |
| -G | 标签 | %G |
| -F | 内容路径 | %F |
| -R | 根路径 | %R |
| -D | 保存路径 | %D |
| -C | 文件数量 | %C |
| -Z | 内容大小 | %Z |
| -T | Tracker | %T |
| -I | 哈希 v1 | %I |
| -J | 哈希 v2 | %J |
| -K | Torrent ID | %K |
| -E | 事件 | start / completed |

## 故障排查

- 日志显示“Key 未找到”：请检查 `bark_notify.ini` 是否存在 Key，并确认是否拼写为 `Key`。
- 日志显示“HTTP 状态码 400”：一般是 Key 错误或请求参数非法。
- 日志显示“无法连接服务器”：检查 Server 地址与端口。

## License

MIT
