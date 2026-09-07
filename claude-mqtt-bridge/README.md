# Claude Code MQTT 状态桥接

本目录提供 Hex Fight 开发板上的 Claude Code 状态桥接服务，以及电脑端 Claude Code 状态发布 Skill。

整体链路如下：

```text
Claude Code hooks
    -> 电脑端状态脚本
    -> MQTT broker
    -> 开发板 mosquitto_sub
    -> claude-mqtt-bridge.sh
    -> ubus desktop.set_claude_status
    -> desktop-app 屏幕显示
```

## 功能概览

开发板可以显示 Claude Code 的三种工作状态：

```text
idle       -> claude-idle.webp      -> 休息中
thinking   -> claude-thinking.webp  -> 正在思考中
executing  -> claude-executing.webp -> 正在工作中
```

Claude Code 不在线时，开发板恢复原有普通数字表情轮播：

```text
default -> 普通数字轮播
```

`default` 只在开发板 bridge 和 desktop-app 之间使用，不是电脑端 MQTT 发布消息。

豆包语音和音乐的显示优先级高于 Claude Code 状态：

```text
豆包语音/音乐 > Claude Code 状态 > 普通数字轮播
```

## MQTT 协议

默认配置：

```text
Broker: test.mosquitto.org
Port:   1883
Topic:  vela-go/claude-code/status/v1
```

所有消息使用同一个 topic，payload 为纯文本：

```text
online
offline
idle
thinking
executing
```

消息处理规则：

| MQTT 消息 | bridge 行为 | 发送给 desktop-app |
|---|---|---|
| `online` | 刷新在线租约；首次在线时进入 CC 空闲 | `idle` |
| `offline` | 立即清除在线状态 | `default` |
| `idle` | 仅在线租约有效时转发 | `idle` |
| `thinking` | 仅在线租约有效时转发 | `thinking` |
| `executing` | 仅在线租约有效时转发 | `executing` |
| 20 秒未收到 `online` | 自动判定离线 | `default` |

默认参数：

```text
电脑端保活间隔：5 秒
开发板在线超时：20 秒
```

电脑异常断电、网络断开、Claude Code 崩溃或 `offline` 消息丢失时，开发板仍会在超时后自动恢复普通轮播。

## 开发板部署

Claude MQTT bridge 使用独立部署脚本，与 `desktop-app/deploy.sh` 完全解耦。该脚本使用仓库其他模块一致的 SSH/SCP 方式：

```sh
DEVICE_IP=192.168.1.100 bash claude-mqtt-bridge/deploy.sh
```

也可以通过仓库根目录选择性部署 bridge：

```sh
DEVICE_IP=192.168.1.100 bash deploy.sh
```

仅部署 bridge：

```sh
DEVICE_IP=192.168.1.100 bash deploy.sh claude-mqtt
```

部署脚本会负责：

1. 检查 SSH 设备连接；
2. 检查开发板上的 `mosquitto_sub`；
3. 缺少时通过设备端 `apt-get` 安装 `mosquitto-clients`；
4. 推送并启动 Claude MQTT bridge；
5. 启用并重启 bridge；
6. 检查 bridge 是否 active。

根目录 `deploy.sh` 默认模块列表中**不包含** `claude-mqtt`，因此执行不带模块参数的部署不会安装 MQTT bridge：

```sh
DEVICE_IP=***.***.*.*** bash deploy.sh
```

上面的命令只部署根脚本默认模块；需要 MQTT bridge 时，必须显式指定 `claude-mqtt`，或直接执行 bridge 自己的部署脚本。

独立 bridge 部署脚本会负责：

1. 检查 `DEVICE_IP` 和 SSH 连接；
2. 检查开发板上的 `mosquitto_sub`；
3. 缺少时通过设备端 `apt-get` 安装 `mosquitto-clients`；
4. 通过 SCP 推送 bridge 脚本和 systemd 服务文件；
5. 执行 `systemctl daemon-reload`；
6. 启用并重启 bridge 服务；
7. 检查 bridge 是否 active，失败时输出最近日志。

设备端文件：

```text
/usr/local/bin/claude-mqtt-bridge.sh
/etc/systemd/system/claude-mqtt-bridge.service
```

### desktop-app 部署中的字体

新板固件没有预装中文字体。执行 `desktop-app/deploy.sh` 时，会通过 ADB 推送 desktop-app 运行时需要的字体：

```text
desktop-app/res/fonts/NotoSansCJK-Regular.ttc
    -> /usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc
```

同时会推送图标字体：

```text
desktop-app/res/fonts/FontAwesome.otf
    -> /usr/share/font-awesome/fonts/FontAwesome.otf
```

如果 `NotoSansCJK-Regular.ttc` 不存在，`desktop-app/deploy.sh` 会直接报错退出，不会继续完成 desktop-app 部署。

### 部署顺序和版本要求

本功能同时修改了设备端 `desktop-app` 和 MQTT bridge，因此首次部署或更新时需要分别部署两部分：

1. 先编译并部署最新版 desktop-app；
2. 再单独部署 MQTT bridge。

部署 desktop-app：

```sh
cd desktop-app
bash build.sh
bash deploy.sh
```

部署 MQTT bridge：

```sh
cd ..
DEVICE_IP=***.***.*.*** bash claude-mqtt-bridge/deploy.sh
```

只有最新版 desktop-app 包含 Claude Code 的 `set_claude_status` ubus 接口、状态处理逻辑、中文状态标签和 `claude-*.webp` 资源。只部署 MQTT bridge 而不更新 desktop-app 时，bridge 可能处于运行状态，但开发板无法正确显示 Claude Code 状态。

systemd 服务特性：

```ini
After=network-online.target desktop.service
Restart=always
RestartSec=3
```

手动检查：

```sh
systemctl status claude-mqtt-bridge.service
journalctl -u claude-mqtt-bridge.service -f
```

## desktop-app 接口

bridge 最终只调用一个 ubus 方法：

```sh
ubus call desktop set_claude_status '{"state":"default"}'
ubus call desktop set_claude_status '{"state":"idle"}'
ubus call desktop set_claude_status '{"state":"thinking"}'
ubus call desktop set_claude_status '{"state":"executing"}'
```

接口允许的状态：

```text
default
idle
thinking
executing
```

## 电脑端 Skill

跨平台 Skill 位于：

```text
claude-mqtt-bridge/skill/
```

内容：

```text
skill/SKILL.md
skill/settings.example.json
skill/scripts/dependency.py
skill/scripts/hook.py
skill/scripts/publish_status.py
```

### Ubuntu/Linux

安装依赖：

```sh
sudo apt-get update
sudo apt-get install -y python3 mosquitto-clients
```

将 `skill` 目录复制到 Claude Code skill 目录，例如：

```sh
mkdir -p ~/.claude/skills/claude-code-status
cp -r skill/* ~/.claude/skills/claude-code-status/
```

### Windows 原生

安装：

1. Python 3；
2. Eclipse Mosquitto；
3. 确保 `mosquitto_pub.exe` 在 PATH 中。

也可以配置完整路径：

```powershell
$env:CLAUDE_MQTT_PUB = "C:\Program Files\mosquitto\mosquitto_pub.exe"
```

复制到：

```text
%USERPROFILE%\.claude\skills\claude-code-status
```

Windows 不应使用：

```text
hook.sh
keepalive.sh
```

应使用跨平台 Python 入口：

```text
scripts/hook.py
```

Windows hook 示例：

```json
{
  "type": "command",
  "command": "python C:/Users/USERNAME/.claude/skills/claude-code-status/scripts/hook.py start",
  "timeout": 5
}
```

### Windows + WSL

如果 Claude Code 运行在 WSL 内，可以直接使用 Linux 方式，但 Python、Mosquitto 和 hooks 必须位于同一个 WSL 环境中。

### macOS

安装依赖：

```sh
brew install python mosquitto
```

然后使用 `hook.py`，或使用兼容的 shell 入口。

## 依赖检查和自动安装

运行依赖检查：

```sh
python3 skill/scripts/dependency.py
```

Windows：

```powershell
py skill\scripts\dependency.py
```

找不到 `mosquitto_pub` 时，脚本会给出当前操作系统的安装提示。

默认不会自动修改系统。明确允许自动安装时：

Linux/macOS：

```sh
CLAUDE_MQTT_AUTO_INSTALL=1 python3 skill/scripts/dependency.py
```

Windows PowerShell：

```powershell
$env:CLAUDE_MQTT_AUTO_INSTALL = "1"
py skill\scripts\dependency.py
```

支持的安装器：

```text
Ubuntu/Debian -> apt-get + sudo
Fedora/RHEL   -> dnf + sudo
Arch          -> pacman + sudo
macOS         -> Homebrew
Windows       -> winget，回退 Chocolatey
```

自动安装可能需要：

- 管理员权限；
- 网络连接；
- 可用的软件包源；
- 用户确认或系统授权。

安装失败不会阻塞 Claude Code，会保留安装提示。

## Claude Code hooks 映射

推荐配置：

```text
SessionStart      -> hook.py start
UserPromptSubmit  -> hook.py thinking
PreToolUse        -> hook.py executing
PostToolUse       -> hook.py thinking
Stop              -> hook.py idle
SessionEnd        -> hook.py end
```

`SessionStart` 的动作：

```text
启动 keepalive
发布 online
发布 idle
```

keepalive 每隔 5 秒发布：

```text
online
```

`SessionEnd` 的动作：

```text
停止 keepalive
发布 offline
```

如果 Claude Code、电脑或网络异常，开发板依靠 20 秒租约超时恢复默认轮播。

## 手动测试

监听 MQTT：

```sh
mosquitto_sub -h test.mosquitto.org -p 1883 \
  -t 'vela-go/claude-code/status/v1' -v
```

发布状态：

```sh
mosquitto_pub -h test.mosquitto.org -p 1883 \
  -t 'vela-go/claude-code/status/v1' -m online

mosquitto_pub -h test.mosquitto.org -p 1883 \
  -t 'vela-go/claude-code/status/v1' -m thinking

mosquitto_pub -h test.mosquitto.org -p 1883 \
  -t 'vela-go/claude-code/status/v1' -m executing

mosquitto_pub -h test.mosquitto.org -p 1883 \
  -t 'vela-go/claude-code/status/v1' -m idle

mosquitto_pub -h test.mosquitto.org -p 1883 \
  -t 'vela-go/claude-code/status/v1' -m offline
```

使用电脑端 Skill 测试：

```sh
python3 skill/scripts/publish_status.py online
python3 skill/scripts/publish_status.py thinking
python3 skill/scripts/publish_status.py idle
python3 skill/scripts/publish_status.py offline
```

Windows：

```powershell
py skill\scripts\publish_status.py online
py skill\scripts\publish_status.py thinking
py skill\scripts\publish_status.py idle
py skill\scripts\publish_status.py offline
```

## 故障排查

### MQTT 能看到消息，但板子不切换

检查设备 bridge：

```sh
adb shell "systemctl is-active claude-mqtt-bridge.service"
adb shell "journalctl -u claude-mqtt-bridge.service -n 50 --no-pager"
```

检查 desktop ubus：

```sh
adb shell "ubus call desktop set_claude_status '{\"state\":\"idle\"}'"
adb shell "ubus call desktop set_claude_status '{\"state\":\"thinking\"}'"
adb shell "ubus call desktop set_claude_status '{\"state\":\"executing\"}'"
```

### 板子持续显示 thinking 或 executing

MQTT 状态按最后收到的有效消息生效。检查 MQTT 监听输出，确认 `idle` 后是否又收到了：

```text
thinking
executing
```

也检查 Claude Code hooks 是否重复触发：

```sh
ps -ef | grep claude-code-status
```

### 没有 `online`

检查电脑端 keepalive：

Linux/macOS：

```sh
ps -ef | grep keepalive
cat /tmp/claude-code-status-keepalive.pid
```

Windows：

```powershell
Get-Content "$HOME\.claude-code-status-keepalive.pid"
Get-Process python
```

### 缺少 `mosquitto_pub`

执行：

```sh
python3 skill/scripts/dependency.py
```

或设置：

```text
CLAUDE_MQTT_PUB=<mosquitto_pub 的完整路径>
```

## 约束

- 当前方案不考虑多个 Claude Code 终端同时运行；
- 使用公共匿名 MQTT broker，不提供身份区分和消息隔离；
- MQTT 发布失败不会阻塞 Claude Code；
- `offline` 不是唯一的离线判断依据，开发板的 20 秒超时才是最终兜底。
