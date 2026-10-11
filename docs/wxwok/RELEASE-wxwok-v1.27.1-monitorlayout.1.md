# wxwok-v1.27.1-monitorlayout.1

Pre-release for installing the per-monitor layout build. Package version **1.27.1.1** sorts above the custom `1.26.0.9999` package and above upstream `1.27.0` / `1.27.0-1`, so `dpkg` / `apt` replace those installs.

这是可安装的预发布包。包版本 **1.27.1.1** 高于已安装的自定义版本 `1.26.0.9999`，也高于上游 `1.27.0` / `1.27.0-1`，`dpkg` / `apt` 会直接升级。

## What is in this build / 包含什么

- Per-monitor virtual desktop (`section: layout`, protocol 1.9 `DMON`). The pointer can leave any monitor edge that touches another computer, including edges inside a concave bounding box.
- Wayland libei accumulator fix: a non-finite or out-of-range relative sample no longer sticks the pointer to the left edge of a client.
- Based on upstream deskflow `a7a51e00d` (1.27.0) plus those two changes. Code signing is not applied.

- 每台机器的显示器可以单独排进同一张虚拟桌面（`section: layout`，协议 1.9 的 `DMON`）。指针可以从贴着另一台电脑的任意显示器边缘切过去，包括凹进去的内边缘。
- Wayland libei 累积器修复：非有限或超出范围的相对位移不会再把指针钉在客户端左边缘。
- 基于上游 deskflow `a7a51e00d`（1.27.0），加上上述两项。没有代码签名。

## Which machine uses which file / 哪台机器装哪个包

Install this build on every machine that should share the pointer. A client older than protocol 1.9 still connects, but only as one rectangle. The libei fix runs on the Wayland **server** (work). dream is also Wayland, so it gets the same fix.

每台要共享键鼠的机器都装这个版本。低于协议 1.9 的客户端仍能连上，但只会当成一整块矩形。libei 修复在 Wayland **服务器**（work）上生效；dream 也是 Wayland，所以同样带上这个修复。

| Machine | OS | Package |
| --- | --- | --- |
| work (server) | Ubuntu 24.04 amd64, GNOME Wayland | `deskflow-1.27.1.1-ubuntu-24.04-amd64.deb` |
| dream (client) | Ubuntu 26.04 amd64, GNOME Wayland | `deskflow-1.27.1.1-ubuntu-26.04-amd64.deb` |
| sur3, x550 (clients) | MX Linux 25.3 = Debian 13 trixie amd64, X11 | `deskflow-1.27.1.1-debian-13-amd64.deb` |
| workwin (client) | Windows 11 x64 | `deskflow-1.27.1.1-win-x64.msi`, or the `.zip` if you do not want an installer |

The Ubuntu 24.04 package carries its own Qt 6.8, libei 1.4, and libportal 0.9 under `/usr/lib/deskflow`, because Ubuntu 24.04's repositories are older than this tree requires. The Debian 13 and Ubuntu 26.04 packages use each distro's own libraries. MX Linux 25.3 tracks Debian 13, so use the Debian 13 package there.

Ubuntu 24.04 的包装了自己的 Qt 6.8、libei 1.4 和 libportal 0.9（在 `/usr/lib/deskflow`），因为 24.04 仓库里的版本不够新。Debian 13 和 Ubuntu 26.04 的包使用系统自带的库。MX Linux 25.3 基于 Debian 13，用 Debian 13 的包。

## Install / 安装

Check the file first / 先核对校验：

```bash
sha256sum -c SHA256SUMS
```

Ubuntu or Debian / Ubuntu 或 Debian：

```bash
sudo apt-get install ./deskflow-1.27.1.1-ubuntu-24.04-amd64.deb
```

Use the filename that matches the machine. If apt says dependencies are missing, run `sudo apt-get install -f`.

换成这台机器对应的文件名。如果提示缺依赖，再执行 `sudo apt-get install -f`。

Windows: run the `.msi`. The `.zip` unpacks to a folder you can run without installing. Windows SmartScreen may warn because the installer is unsigned; that is expected for this fork build.

Windows：运行 `.msi`。`.zip` 解压后可以直接运行，不必安装。安装包没有签名，SmartScreen 可能会提示，这是这个 fork 构建的正常情况。

Leave "Use monitor layout for pointer switching" off until the layout is saved. An example for the current desks is `docs/examples/monitor-layout.conf` in the source tree. With no `section: layout`, the old neighbour links keep working.

先不要勾选 “Use monitor layout for pointer switching”，等布局保存好再打开。当前几台机器的例子在源码 `docs/examples/monitor-layout.conf`。没有 `section: layout` 时，原来的邻居连线仍然有效。

## Roll back / 回退

Keep the previous `.deb` or `.msi`.

留下一份旧的 `.deb` 或 `.msi`。

```bash
sudo dpkg -i deskflow_1.26.0.9999_previous.deb
```

Or remove this package / 或者卸掉这个包：

```bash
sudo apt-get remove deskflow
```

On Windows, uninstall Deskflow from Settings → Apps, then install the previous build.

在 Windows 上从「设置 → 应用」卸载 Deskflow，再装回上一版。
