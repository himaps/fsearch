# FSearch UOS 定制版说明

**最新版本下载（arm64 deb）**：[v0.3.2-uos1 Release](https://github.com/himaps/fsearch/releases/tag/v0.3.2-uos1) ·
[直接下载 deb](https://github.com/himaps/fsearch/releases/download/v0.3.2-uos1/fsearch_0.3.2-uos1_arm64.deb)

安装：`sudo dpkg -i fsearch_*.deb`（CI 产物基于 bookworm/glibc 2.36，老 UOS V20 设备请用下面的本机编译）。

本 fork 在上游 [cboxdoerfer/fsearch](https://github.com/cboxdoerfer/fsearch) 0.3.x 基础上，为统信 UOS（ARM64）桌面环境增加了三个功能：

1. **快捷键切换窗口显隐**：同一条系统快捷键在"显示 ⇄ 最小化"之间切换窗口（类似 Windows 上 Everything 的 Win+S）。
2. **快速切回上一次的文件管理器窗口**（类似 Listary）：维护最近使用的文件管理器窗口（dde-file-manager 等）队列，一键切回；配合搜索工作流：FSearch 中搜索 → Alt+G 跳回文件管理器。
3. **批量重命名**：对选中的搜索结果按表达式批量改名（正则、`#` 计数器、忽略扩展名、预设），详见下文专章。

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
- 打 `v*` tag（如 `v0.3.2-uos1`）或手动触发：`.github/workflows/release.yml` 在免费 arm64 runner 上用 debian:bookworm 容器原生编译，**tag 触发时自动创建 GitHub Release 并附上 `fsearch_<版本>_arm64.deb` 与二进制 tarball 下载链接**（仓库右侧 Releases 页面），也可在 Actions 运行页面的 artifact 里下载。安装：

  ```bash
  sudo dpkg -i fsearch_*_arm64.deb
  ```

  deb 不声明严格依赖（构建发行版库版本与 UOS 不同）；运行库（libgtk-3-0、libglib2.0-0、libpcre2-8-0、libicu、libwnck-3-0）UOS 桌面版均自带。CI 产物基于 bookworm（glibc 2.36），**较老的 UOS V20 设备（glibc 2.28 时代）请改用上面的本机编译**。

## 新增配置项（`~/.config/fsearch/fsearch.conf`）

| 键 | 所属节 | 默认值 | 说明 |
|---|---|---|---|
| `toggle_window_visibility` | Interface | `true` | 二次启动 `fsearch` 时切换窗口显隐；`false` 恢复上游"总是显示"行为 |
| `hide_window_on_close` | Interface | `false` | 点关闭按钮改为隐藏窗口（进程常驻，可用快捷键唤回；Ctrl+Q 仍退出） |
| `file_manager_window_classes` | Applications | `dde-file-manager,file-manager` | 窗口切换功能匹配的 WM_CLASS 列表，逗号分隔、忽略大小写 |

用 `wmctrl -lx` 或 `xprop WM_CLASS` 查看实际窗口类名；例如 UKUI 的文件管理器可加 `,peony`。

## 批量重命名（新增）

选中搜索结果后按 **F2** 或右键选「重命名…」，打开批量重命名对话框（对标 Everything）：

- **原/新文件名表达式**：对文件名做查找替换。默认字面匹配（默认忽略大小写），勾选「Regular expression」后为正则（支持 `\0`~`\9` 反向引用）。
- **`#` 计数器**：新文件名表达式中的 `#` 按选中顺序自增（1、2、3…），`##` 表示字面 `#`。例：搜索 `^`，替换 `照片#_` → `照片1_`、`照片2_`…
- **Match case**：大小写敏感开关（字面与正则模式均有效）。
- **Match diacritics**：忽略变音符（`é`≈`e`），仅字面模式可用（正则模式下置灰）。
- **Ignore extension**：表达式只作用于主文件名，扩展名原样保留。
- **实时预览**：新文件名列表即时更新；红色=冲突（目标已存在）或非法名；灰色=无变化。执行时红色项自动跳过。
- **预设**：Save Preset 保存当前表达式与选项（存于 `~/.config/fsearch/fsearch.conf` 的 `[RenamePresets]` 节），下拉选择即载入，Delete Preset 删除。
- 执行后结果列表**立即刷新**（数据库增量改名，无需等待重扫）。超过 200 项时在后台线程执行并显示进度条，可取消。

已知限制：改名后不再匹配当前搜索词的结果会从列表中消失（与 Everything 一致）；对 NTFS/FAT 挂载盘建议避免在替换结果中使用 `\/:*?"<>|` 等字符（预览中会标红）。

## 快捷键使用方法（应用内）

FSearch 窗口获得焦点时：

- `F2`：对选中的搜索结果打开批量重命名对话框。
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
