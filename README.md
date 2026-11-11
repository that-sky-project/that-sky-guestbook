# That Sky Guestbook

面向 Sky: Children of the Light PC 客户端的多槽位本地账号管理器（客户端侧技术实现 / 研究项目）。

A client-side multi-slot local account manager for the Sky PC client (technical demonstration).

> **重要 / Important**
>
> 项目包含AI生成代码
> The project includes AI-generated code
> 本项目仅演示客户端侧登录请求、账号槽位与凭证保存的实现方式。  
> 正式服在服务端执行平台/账号验证；本 mod **不会也不能绕过**该验证，因此无法让正式服游客登录生效。  
> This project only demonstrates the client-side login request path, slot storage and
> credential persistence. The official server performs server-side platform/account
> verification; this mod does **not** bypass it and cannot enable guest login there.
>
> 请在你有权控制的本地研究环境中使用，并自行确认符合游戏与平台条款。  
> Use only in environments you control and in compliance with the applicable terms.

---

## 功能 / Features

- **多槽位**：最多 12 个本地/游客账号槽位，每个槽位独立保存一份 authinfo。
- **大号保留**：`Main (Steam)` 模式不改变登录类型、不注入凭证，保持平台账号登录。
- **新建小号**：`New guest` 模式用 Local 类型登录，成功后自动保存为新槽位。
- **自动保存**：选择任意槽位/大号立即写入 `slots.bin`，重启后保持。
- **中英文界面**：内置中文/English 切换，启动时尝试加载系统 CJK 字体。
- **快捷键显隐**：默认 `F3`，可在 HTML Main Menu → Settings → Key Bindings 中改为任意键。
- **账号存储可解析**：附带 `tools/parse_slots.py`，可查看槽位、UUID 与游戏数据文件对应关系。

## 技术实现 / Technical notes

测试版本：Sky.exe build **426499**（PC / Steam edition）。不同版本的偏移会变化。

客户端侧关键位置：

| 用途 | 地址（VA） |
| --- | --- |
| `AccountServerClient::AddRequest` | `0x1419d6930` |
| `AccountLoginRequest` 的 vtable | `0x141f723e0` |
| `AccountLoginRequest.type` 偏移 | `+0x68` |
| 登录请求 authinfo 字段 | `user +0x90` (16), `device +0xa0` (16), `key +0xc0` (32) |
| authinfo 反序列化 | `0x140e8bc30` |
| authinfo 目标布局 | `user +0x00` (16), `device +0x10` (16), `key +0x20` (32) |

账号类型字符串表（节选）：

| 值 | 字符串 |
| --- | --- |
| 0 | `Local` |
| 7 | `Apple` |
| 12 | `Steam` |
| 13 | `Twitch` |

实现思路：

1. Hook `AddRequest`，在登录请求入队前按当前槽位改写 `type` 并注入 authinfo；
2. Hook authinfo 反序列化，在登录响应到达后保存 64 字节的 `user/device/key`；
3. 客户端本身不会持久化 Local 账号的 authinfo，因此由本 mod 逐槽位保存并在下次登录时注入；
4. 注入字段是二进制：16 字节 user UUID、16 字节 device UUID、32 字节 key。

> 说明：在测试环境中服务端只按 `user`/`device` 识别账号，`key` 字段未参与校验；不同服务端实现可能不同。

## 构建 / Build

依赖：

- MinGW-w64 g++（GCC 12 或 15 测试通过）
- HTModLoader SDK（HTML SDK）

```bash
export HTML_SDK=/path/to/htmodloader-sdk
./build.sh
# 输出: dist/that-sky-guestbook.dll
```

SDK 根目录需要包含：

```text
includes/htmodloader/includes/htmod.h
includes/imgui-1.92.2b/imgui.h
lib/htmodloader.lib
```

## 安装 / Install

把以下文件放进：

```text
<Game>/htmodloader/mods/that-sky-guestbook/
├─ manifest.json
└─ that-sky-guestbook.dll
```

启动游戏后按 `F3` 打开窗口。

## 使用 / Usage

1. 在窗口中选择账号：
   - `Main (Steam)`：保持平台登录；
   - `Guest N`：Local 登录并注入该槽位保存的 authinfo；
   - `New guest`：Local 登录，成功后自动创建新槽位。
2. 选择后**退出登录 → 重新登录**（或重启后 Play）使选择生效。
3. `F3` 显示/隐藏窗口；窗口右上角 `X` 也可隐藏。
4. 槽位名称可以直接编辑并保存。

## 数据文件 / Data files

| 路径 | 内容 |
| --- | --- |
| `%APPDATA%\guest-login\slots.bin` | 多槽位数据（格式见 [`docs/slots-format.md`](docs/slots-format.md)） |
| `%APPDATA%\guest-login\settings.ini` | 语言设置（`lang=zh` / `lang=en`） |
| `%APPDATA%\guest-login\auth.bin` | 旧版单账号 64 字节 blob（首次启动会迁移到槽位） |
| `%APPDATA%\ThatGameCompany\com.tgc.sky.win\<user-uuid>.bin` | 每个账号对应的游戏数据文件 |

解析工具：

```bash
python tools/parse_slots.py
python tools/parse_slots.py --show-key
python tools/parse_slots.py --legacy "%APPDATA%\guest-login\auth.bin"
```

## 限制 / Limitations

- 偏移绑定在 Sky.exe build 426499；游戏更新后需要重新定位。
- 正式服有服务端平台/账号验证，本 mod 不会使其接受 Local 登录；这不是一个绕过官方认证的工具。
- 本项目不修改服务端，不提供任何服务端绕过方法。
- 使用风险自负；请遵守游戏与平台条款。

## License

GPL-3.0。见 [LICENSE](LICENSE)。
