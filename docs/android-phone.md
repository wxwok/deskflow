# Android phone screens

Deskflow has no Android client. This build can still use a phone that has USB debugging turned on, first a Galaxy Z Flip, as a screen in Monitor layout. The picture and the pointer stay in an official, unmodified scrcpy window on the computer. Nothing is drawn on the phone itself, and the Deskflow protocol is not moved into scrcpy.

The phone is not a Deskflow computer. Unplugging it makes that screen Offline. It is never written as Disabled. Disabled stays the gray switch you choose yourself. The server screen keeps its normal color.

## Register

1. Install `adb` and official scrcpy, or set both paths in Monitor layout. An empty path searches `PATH`. scrcpy is also looked for at `/opt/scrcpy-v5.0.1/scrcpy`.
2. Open Monitor layout from the tray, or from the View menu. That window is only the layout.
3. Plug the phone in, or pair wireless debugging on the LAN. The phone is listed when `adb devices` can see it. Unauthorized phones are shown and are not Online.
4. Press Register. The phone is placed below the current screens so it does not sit on the sur3–work–workwin–dream row. Drag it onto a free edge of this computer. An edge that already switches to another computer is left alone.
5. Leave "Use Android phone screens" on. Turn it off to stop discovery, hide the edge, and close scrcpy windows that Deskflow started. Phones you started yourself are left running.

USB is used when the cable is connected. If the cable is removed and wireless debugging is still up, the same registered phone stays Online. If both are gone, it is Offline until it returns.

Wireless pairing asks for the phone's LAN address, the pairing port, the code, and the connect port. The code is not saved. Addresses outside private LAN ranges are rejected, and adb is started without proxy variables.

## Use

When the phone is Online, a thin bar sits on the registered edge. Moving into that bar shows one scrcpy window for that phone, docked to the edge, with a bezel. The pointer continues at the same relative position when the desktop can warp it. Leaving the picture and the bezel, back toward the desktop, hides the window after a short pause. Pin turns that off. Hide closes the view without unregistering the phone.

The bezel has Back, Home, Recents, power, screen off, screen on, volume, mute, notifications, keyboard layout, paste, the clipboard editor, pinned apps, and hide. Dropping a file on the picture uses scrcpy. Dropping on the bezel copies the file to `/sdcard/Download` with `adb push`, because a running scrcpy has no command for a drop.

The first time the window is shown, a note says to use the phone input method for Chinese and to switch the computer input method to English. scrcpy is started with `--keyboard=uhid`. Alt+K in the phone window opens scrcpy's physical keyboard settings.

## Clipboard editor

The shortcut is Ctrl+Alt+E unless `gui/phoneClipboardShortcut` is changed. The bezel has the same editor. It is plain text and uses the computer input method. Load reads the phone clipboard with `adb shell cmd clipboard get-text` when that command exists. Send writes the phone clipboard. Send and paste writes it, focuses the scrcpy window, and sends scrcpy's Alt+V.

## Pinned apps

Pin apps saves a list for that phone's serial only. A click shows the window if it is hidden, then starts the app with `adb shell monkey`. Official `--start-app` would start another scrcpy, so it is not used on a window that is already open.

## Foldables

A foldable is one screen. Open uses the inner display and closed uses the cover. The layout rectangle changes size at the same origin. The dock stays on the same edge. Display choice uses `scrcpy --list-displays` and `wm size` when they return something useful. If they do not, scrcpy's default display is used. Some Flip cover panels have been reported as a black scrcpy image; that is a scrcpy or device limitation, not a second layout screen.

## Wayland and X11

On X11 with `xdotool`, Deskflow can unmap the prewarmed scrcpy window, move it, raise it, warp the pointer, and send scrcpy shortcuts.

On Wayland those actions are not available to a normal client. There is no prewarmed hidden window, because it could not be hidden. The window is started when the pointer enters the edge and the process is stopped when the pointer leaves. Screen off, screen on, keyboard layout, and paste then have to be done with Alt+O, Alt+Shift+O, Alt+K, and Alt+V inside the scrcpy window. A compositor may also ignore the requested window position. The hot edge is still our own window, so entering it does not depend on reading the global pointer.

Prewarm or show failures are reported as status text. They do not grab the desktop pointer.

## What this version does not do

- No RustDesk, and no proxy or Mullvad path.
- The inner and cover panels are not two places in the layout.
- scrcpy is not forked, patched, or vendored.
- No virtual pointer is drawn on the phone.
- Existing computers are not moved to make room.
- Mute has no official scrcpy shortcut. The button mutes a PulseAudio stream named scrcpy when `pactl` can see it, and otherwise starts the next window with `--no-audio`.
- Reading the phone clipboard depends on `cmd clipboard get-text`. scrcpy does not provide a read command.

## Turn it off

Uncheck "Use Android phone screens", or set `gui/androidPhones` to false. Unregister removes one phone. Registered phones are stored under `internalConfig/phones` and are not `computer_<name>/disabled` entries. Removing that group forgets the phones. Reverting this change removes the feature.

## 安卓手机屏幕

Deskflow 没有安卓客户端。这个版本可以把打开了 USB 调试的手机（首先是 Galaxy Z Flip）放进显示器布局。画面和指针都在电脑上的官方 scrcpy 窗口里，不改 scrcpy，也不在手机屏幕上画指针。

手机不是一台 Deskflow 电脑。拔线或断开后是 Offline，不会写成 Disabled。Disabled 仍是你自己关掉的灰色状态。服务器那块屏幕保持原来的颜色。

### 注册

1. 安装 `adb` 和官方 scrcpy，或在显示器布局里填写路径。路径留空则搜索 `PATH`，scrcpy 还会查找 `/opt/scrcpy-v5.0.1/scrcpy`。
2. 从托盘或“视图”菜单打开显示器布局。这个窗口只有布局。
3. 插上手机，或在局域网里配对无线调试。`adb devices` 能看到时才会列出。未授权的手机会显示，但不是 Online。
4. 点注册。手机会放在现有屏幕下方，不会挤进 sur3–work–workwin–dream 那一排。把它拖到这台电脑的空闲边缘。已经连到别的电脑的边缘不会被占用。
5. 保持“使用安卓手机屏幕”打开。关掉它会停止发现、藏起热边，并关闭 Deskflow 自己启动的 scrcpy。你自己打开的 scrcpy 会留着。

插着 USB 时走 USB。拔掉 USB 但无线调试还在，同一台已注册手机仍是 Online。两条都断了就是 Offline，重新连上再恢复。

无线配对需要手机的局域网地址、配对端口、配对码和连接端口。配对码不会保存。不是局域网私有地址会被拒绝，adb 启动时不带代理变量。

### 使用

手机 Online 时，注册边缘有一条细边。指针进入这条边，就显示这一台手机的一个 scrcpy 窗口，贴在边缘，旁边是边框。桌面能移动指针时，指针按相对位置接上。离开画面和边框、回到桌面方向，停一下就会隐藏。钉住之后不自动隐藏。隐藏不会取消注册。

边框上有返回、主页、最近任务、电源、关掉或打开手机屏幕、音量、静音、通知栏、键盘布局、粘贴、剪贴板编辑、常用应用和隐藏。把文件拖到画面上走 scrcpy 自己的拖放。拖到边框上会用 `adb push` 拷到 `/sdcard/Download`，因为正在运行的 scrcpy 没有接收拖放的命令。

窗口第一次出现时会提示：中文用手机输入法，电脑输入法切到英文。scrcpy 使用 `--keyboard=uhid`。在手机窗口里按 Alt+K 打开 scrcpy 的物理键盘设置。

### 剪贴板编辑

默认快捷键是 Ctrl+Alt+E，可以用 `gui/phoneClipboardShortcut` 改。边框上也有同一个编辑框。只接受纯文本，用电脑输入法，包括中文。读取依赖 `adb shell cmd clipboard get-text`。发送只写入手机剪贴板。发送并粘贴还会聚焦 scrcpy 并按下它的 Alt+V。

### 常用应用

钉住的应用按这台手机的序列号分开保存。点击时如果窗口是隐藏的会先显示，再用 `adb shell monkey` 启动。官方 `--start-app` 会再开一个 scrcpy，所以已经打开的窗口不用它。

### 折叠屏

折叠屏是一块屏幕。打开用内屏，合上用外屏，布局里的位置不变，只改宽高，窗口仍贴在原来的边。显示信息来自 `scrcpy --list-displays` 和 `wm size`。读不到就用 scrcpy 的默认显示。有的 Flip 外屏投出来是黑的，那是 scrcpy 或设备的限制，不会变成第二块可放置的屏幕。

### Wayland 和 X11

X11 上装了 `xdotool` 时，可以预热并藏起 scrcpy，再移动、置前、移动指针、发送 scrcpy 快捷键。

Wayland 做不到这些。不能预热一个藏起来的窗口，否则它会一直露在外面。指针进入边缘才启动，离开就结束进程。关屏、开屏、键盘布局和粘贴需要在 scrcpy 窗口里按 Alt+O、Alt+Shift+O、Alt+K 和 Alt+V。合成器也可能不理会窗口位置。热边是 Deskflow 自己的窗口，所以不依赖读取全局指针。

预热或显示失败只会提示，不会抓住桌面指针。

### 这一版没有的东西

- 没有 RustDesk，也不走代理或 Mullvad。
- 内屏和外屏不是布局里的两块屏幕。
- 不修改、不 fork、不内置一份改过的 scrcpy。
- 不在手机屏幕上画虚拟指针。
- 不为了腾位置去移动现有电脑。
- scrcpy 没有静音快捷键。按钮会在 `pactl` 看得到名为 scrcpy 的 PulseAudio 流时把它静音，否则下一次打开窗口时加上 `--no-audio`。
- 读取手机剪贴板依赖 `cmd clipboard get-text`。scrcpy 没有读取命令。

### 关掉

取消“使用安卓手机屏幕”，或把 `gui/androidPhones` 设为 false。取消注册会去掉一台手机。登记内容在 `internalConfig/phones`，不是 `computer_<name>/disabled`。删掉这一组就会忘掉手机。回退这次改动会去掉整个功能。
