# Claude Code MQTT 状态 Skill

这个 skill 通过 Claude Code hooks 将会话状态发送到 Hex Fight 开发板。

## 兼容性结论

| 平台 | 支持方式 | 说明 |
|---|---|---|
| Ubuntu/Linux | 直接支持 | 需要 Python 3、Mosquitto 客户端和 POSIX shell；使用 `hook.sh` + `keepalive.sh`。 |
| Windows 原生 | 支持 | 推荐使用 `hook.py`；不依赖 Bash、`nohup`、`kill`、`/tmp` 或 Unix PID 文件。 |
| Windows + WSL | 直接支持 Linux 方案 | Claude Code 和 hooks 必须运行在同一个 WSL 环境内，并且 WSL 能访问网络。 |
| macOS | 基本支持 | Linux shell 方案通常可用；需要 Python 3 和 `mosquitto_pub`。Windows 原生方案也可使用。 |

不要在 Windows 原生环境的 settings.json 中配置 `bash ~/.claude/.../hook.sh`。应配置 Python hook 的绝对路径或 Windows 用户目录路径。

## 协议

所有消息使用同一个 MQTT topic：

```text
broker: test.mosquitto.org
port: 1883
topic: hex-fight/claude-code/status/v1
```

电脑端发布的消息：

```text
online       保活
offline      会话结束
idle         Claude Code 在线但空闲
thinking     思考或生成回复
executing    执行工具
```

开发板 bridge 在本地维护在线租约，只向 desktop-app 转发：

```text
default | idle | thinking | executing
```

默认保活间隔为 5 秒，超过 20 秒未收到 `online` 自动转发 `default`。`offline` 是加速恢复路径，不能替代设备端超时机制。

## 安装前提

### Ubuntu/Linux

```sh
sudo apt-get update
sudo apt-get install -y python3 mosquitto-clients
```

### Windows

1. 安装 Python 3，并勾选 Add Python to PATH；
2. 安装 Mosquitto，确保 `mosquitto_pub.exe` 在 PATH 中，或设置 `CLAUDE_MQTT_PUB` 指向它的完整路径；
3. 将本目录复制到 Claude Code skill 目录，例如：

```text
%USERPROFILE%\\.claude\\skills\\claude-code-status
```

## 配置 hooks

将 `settings.example.json` 中的 hook 合并到 Claude Code 的 settings.json。不要覆盖已有 hooks。

Linux/macOS 示例：

```json
"command": "python3 ~/.claude/skills/claude-code-status/skill/scripts/hook.py start"
```

Windows 示例（路径按实际安装位置修改）：

```json
"command": "python C:/Users/USERNAME/.claude/skills/claude-code-status/skill/scripts/hook.py start"
```

事件映射：

```text
SessionStart      -> start  （启动保活，online + idle）
UserPromptSubmit  -> thinking
PreToolUse        -> executing
PostToolUse       -> thinking
Stop              -> idle
SessionEnd        -> end    （停止保活，offline）
```

## 依赖检查、自动安装和提示

`hook.py start` 会先检查 `mosquitto_pub`。如果找不到：

- 默认只输出适合当前操作系统的安装提示，不会在没有明确授权时修改系统；
- 设置 `CLAUDE_MQTT_AUTO_INSTALL=1` 后，才会尝试自动安装；
- 自动安装需要管理员权限、网络和可用的软件包管理器；
- 安装失败会保留清晰提示，Claude Code 本身不会因此阻塞。

支持的自动安装器：

```text
Ubuntu/Debian: apt-get + sudo
Fedora/RHEL:   dnf + sudo
Arch:          pacman + sudo
macOS:         Homebrew
Windows:       winget，回退 Chocolatey
```

推荐先手动确认：

```sh
python3 scripts/dependency.py
```

明确允许自动安装时：

```sh
CLAUDE_MQTT_AUTO_INSTALL=1 python3 scripts/dependency.py
```

Windows PowerShell：

```powershell
$env:CLAUDE_MQTT_AUTO_INSTALL = "1"
py scripts\\dependency.py
```

注意：自动安装默认关闭，避免 Claude Code hook 在后台意外触发系统包安装或管理员授权。


Linux/macOS：

```sh
python3 scripts/publish_status.py online
python3 scripts/publish_status.py idle
python3 scripts/publish_status.py thinking
python3 scripts/publish_status.py executing
python3 scripts/publish_status.py offline
```

Windows：

```powershell
py scripts\\publish_status.py online
py scripts\\publish_status.py idle
py scripts\\publish_status.py thinking
py scripts\\publish_status.py executing
py scripts\\publish_status.py offline
```

环境变量：

```text
CLAUDE_MQTT_BROKER       默认 test.mosquitto.org
CLAUDE_MQTT_PORT         默认 1883
CLAUDE_MQTT_TOPIC        默认 hex-fight/claude-code/status/v1
CLAUDE_MQTT_INTERVAL     默认 5
CLAUDE_MQTT_PUB          可选，mosquitto_pub 或 mosquitto_pub.exe 的完整路径
```

MQTT 发布失败应被视为可选显示副作用，不得阻塞 Claude Code hook。
