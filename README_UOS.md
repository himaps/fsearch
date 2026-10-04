# FSearch UOS 定制版说明

本 fork 在上游 [cboxdoerfer/fsearch](https://github.com/cboxdoerfer/fsearch) 0.3.x 基础上，为统信 UOS（ARM64）桌面环境增加了两个功能：

1. **快捷键切换窗口显隐**：同一条系统快捷键在"显示 ⇄ 最小化"之间切换窗口（类似 Windows 上 Everything 的 Win+S）。
2. **快速切回上一次的文件管理器窗口**（类似 Listary）：维护最近使用的文件管理器窗口（dde-file-manager 等）队列，一键切回；配合搜索工作流：FSearch 中搜索 → Alt+G 跳回文件管理器。

"打开路径"（在文件管理器中定位文件）使用上游自带的 `org.freedesktop.FileManager1` D-Bus 集成，无需改动。

## 在 UOS ARM 上编译

```bash
sudo apt install git meson ninja-build build-essential \
                 libgtk-3-dev libglib2.0-dev libpcre2-dev libicu-dev \
                 libwnck-3-dev gettext
git clone https://github.com/himaps/fsearch.git && cd fsearch
meson setup build --prefix=/usr -Dwnck=enabled
ninja -C build
sudo ninja -C build install
```

`-Dwnck=disabled` 可禁用文件管理器窗口切换功能（不影响其他功能）。未安装 libwnck-3-dev 时默认（auto）也会自动禁用。

### GitHub 自动构建

- 每次 push / PR：`.github/workflows/build_test.yml` 自动编译 + 测试。
- 打 `v*` tag（如 `v0.3.2-uos1`）或手动触发：`.github/workflows/release.yml` 在免费 arm64 runner 上用 debian:bullseye 容器原生编译，产出 `fsearch_<版本>_arm64.deb` 与二进制 tarball（Actions 页面 artifact 下载）。安装：

  ```bash
  sudo dpkg -i fsearch_*_arm64.deb
  ```

  deb 不声明严格依赖（构建发行版库版本与 UOS 不同）；运行库（libgtk-3-0、libglib2.0-0、libpcre2-8-0、libicu、libwnck-3-0）UOS 桌面版均自带。若目标设备的 glibc 低于 2.31，请改用上面的本机编译。

## 新增配置项（`~/.config/fsearch/fsearch.conf`）

| 键 | 所属节 | 默认值 | 说明 |
|---|---|---|---|
| `toggle_window_visibility` | Interface | `true` | 二次启动 `fsearch` 时切换窗口显隐；`false` 恢复上游"总是显示"行为 |
| `hide_window_on_close` | Interface | `false` | 点关闭按钮改为隐藏窗口（进程常驻，可用快捷键唤回；Ctrl+Q 仍退出） |
| `file_manager_window_classes` | Applications | `dde-file-manager,file-manager` | 窗口切换功能匹配的 WM_CLASS 列表，逗号分隔、忽略大小写 |

用 `wmctrl -lx` 或 `xprop WM_CLASS` 查看实际窗口类名；例如 UKUI 的文件管理器可加 `,peony`。

## 快捷键使用方法

应用内（FSearch 窗口获得焦点时）：

- `Alt+G`：切回最近使用的文件管理器窗口；多个窗口时连续按键轮转。
- `Escape`：最小化窗口（进程常驻）。

全局（UOS 控制中心 → 键盘和语言 → 快捷键 → 添加自定义快捷键）：

| 快捷键建议 | 命令 | 效果 |
|---|---|---|
| Ctrl+Alt+S | `fsearch` | FSearch 窗口 显示 ⇄ 最小化 切换 |
| Ctrl+Alt+G | `fsearch --switch-file-manager`（可简写 `fsearch -g`） | 切回最近使用的文件管理器窗口 |

注意：

- 全局快捷键通过单实例机制转发给常驻的 FSearch 进程。**FSearch 未运行时 `--switch-file-manager` 无效果**，建议配合自启动常驻：把 `Exec=fsearch --minimized` 的 desktop 文件放入 `~/.config/autostart/`（启动后最小化常驻，索引同步维护）。
- 单按 Super（Win）键已被启动器占用；绑定前请在控制中心确认所选组合未被系统快捷键（截图 Ctrl+Alt+A 等）占用。

## 命令行选项（新增部分）

```
-g, --switch-file-manager   切换到最近使用的文件管理器窗口
```

## 已知限制

- 文件管理器窗口切换基于 X11（libwnck3）。Wayland 会话下该功能自动禁用（UOS V20 桌面版为 X11，可用 `echo $XDG_SESSION_TYPE` 确认）。
- 显隐切换只作用于"第一个" FSearch 窗口（上游 `--new-window` 可开多窗口；默认单窗口场景无影响）。
- "打开路径"是否复用已有文件管理器窗口（而非新开窗口）取决于 dde-file-manager 版本的 ShowItems 实现，请以实机为准。
