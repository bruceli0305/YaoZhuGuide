# 小夭竺宝典（YaoZhuGuide）

Guild Wars 2 Nexus 插件，作者：协同学院。

【激战2的小夭竺】视频攻略大全。插件使用自有 Win32 窗口嵌入 WebView2，主页面不会调用系统默认浏览器；页面中的 `target=_blank` 和 `window.open` 请求会交给系统默认浏览器。

## 功能

- 在 Nexus Quick Access 中显示“小夭竺宝典”图标。
- 点击图标打开或重新加载攻略页面。
- 窗口默认尺寸为 `1000×640`，并始终置于普通窗口前面。
- 标题栏提供透明度滑块，范围为 `30%`–`100%`。
- 手动关闭窗口会结束当前 WebView2 会话，停止页面媒体；再次点击图标会创建新会话。
- WebView2 用户数据保存到 `%LOCALAPPDATA%\YaoZhuGuide\WebView2`，不会把网页缓存写入插件目录。

默认页面：

<https://v2.gw2.org.cn/guides/bilibili-1846648930-c-8136102>

## 仓库结构

```text
YaoZhuGuide.cpp          插件源码
YaoZhuGuide.sln          Visual Studio 解决方案
YaoZhuGuide.vcxproj      x64 DLL 工程
YaoZhuGuide.ini          默认配置
assets/                  Nexus Quick Access 图标
fetch-webview2.nu        下载并解压 WebView2 C++ SDK
```

`vendor/`、`build/` 和 `.vs/` 都是本地构建产物，不提交到仓库。

## 构建

要求：

- Windows
- Visual Studio C++ x64 工具链
- Nushell
- Microsoft Edge WebView2 Runtime（运行插件的机器需要）

在仓库根目录执行：

```text
nu fetch-webview2.nu
msbuild YaoZhuGuide.sln /m /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

输出文件：

```text
build\x64\Release\YaoZhuGuide.dll
```

## 配置与手动安装

将以下文件放到 Guild Wars 2 的 `addons` 目录：

```text
YaoZhuGuide.dll
YaoZhuGuide.ini
YaoZhuGuideIcon.png
```

配置文件示例：

```ini
[Browser]
Url=https://v2.gw2.org.cn/guides/bilibili-1846648930-c-8136102
Opacity=100
```

只接受 `http://` 和 `https://` URL；无效地址会回退到默认页面。透明度支持 `30` 到 `100`。

插件不捆绑 WebView2 Runtime。目标机器需要先安装 Microsoft Edge WebView2 Runtime；运行时缺失时插件会显示错误状态，不会把主页面改为系统浏览器。

## 发布

正式发布前需要完成：

1. 在 Raidcore Addon Library 创建插件项目并通过审核。
2. 将 `GetAddonDef` 中的临时负签名替换为 Raidcore 分配的正式签名。
3. 将插件版本提升后创建对应的 Git tag 和 GitHub Release。
4. 发布 DLL、INI 和图标，并验证 Nexus 新安装和版本更新。

当前源码仍使用临时签名和 `UP_None`，因此仓库可以用于源码和手动安装，但尚未宣称已接入 Nexus 官方列表或自动更新。

发布包不得包含 `vendor/`、`build/`、WebView2 用户数据目录或本地缓存。正式接入 Nexus 前，还要确认插件库安装/更新时会同时带上旁边的图标和配置文件；否则应把图标改为 DLL 内置资源，避免新安装后回退到 Nexus 默认图标。
