# 小夭竺宝典（YaoZhuGuide）

Guild Wars 2 Nexus 插件，作者：协同学院。

【激战2的小夭竺】视频攻略大全。插件使用自有 Win32 窗口嵌入 WebView2，主页面不会调用系统默认浏览器；页面中的 `target=_blank` 和 `window.open` 请求会交给系统默认浏览器。

## 功能

- 在 Nexus Quick Access 中显示“小夭竺宝典”图标。
- 图标内嵌于 DLL，无需额外下载 PNG。
- 点击图标打开或重新加载攻略页面。
- 窗口默认尺寸为 `1000×640`，并始终置于普通窗口前面。
- 标题栏提供透明度滑块，范围为 `30%`–`100%`。
- 手动关闭窗口会结束当前 WebView2 会话，停止页面媒体；再次点击图标会创建新会话。
- WebView2 用户数据保存到 `%LOCALAPPDATA%\YaoZhuGuide\WebView2`，不会把网页缓存写入插件目录。

默认页面：

<https://v2.gw2.org.cn/bilibili-topic>

图标随 Nexus Quick Access 栏统一排列。与其他插件界面重叠时，在 Nexus 菜单（默认 `Ctrl+O`）的「选项 → 快速访问」中调整「位置」及 X/Y「补偿值」，或启用「垂直布局」；这些设置由 Nexus 保存。

## 仓库结构

```text
YaoZhuGuide.cpp          插件源码
YaoZhuGuide.sln          Visual Studio 解决方案
YaoZhuGuide.vcxproj      x64 DLL 工程
YaoZhuGuide.ini          默认配置
assets/                  Nexus Quick Access 图标
YaoZhuGuide.rc           DLL 内嵌图标资源
resource.h               图标资源 ID
fetch-webview2.nu        下载并解压 WebView2 C++ SDK
release/                 已验证的发布安装包
.github/workflows/       GitHub Release 工作流
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

可在 x64 Native Tools 命令行验证内嵌图标、默认地址、旧版 INI 迁移、Raidcore 签名和版本：

```bat
cl /nologo /std:c++20 /EHsc /utf-8 /MT /DUNICODE /D_UNICODE /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Ivendor\WebView2\build\native\include tests\single-dll.cpp /Febuild\single-dll-test.exe /Fobuild\single-dll-test.obj /link /LIBPATH:vendor\WebView2\build\native\x64 WebView2LoaderStatic.lib dbghelp.lib user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib oleaut32.lib advapi32.lib version.lib shlwapi.lib
build\single-dll-test.exe
```

## 配置与手动安装

只需将以下文件放到 Guild Wars 2 的 `addons` 目录：

```text
YaoZhuGuide.dll
```

配置文件示例：

```ini
[Browser]
Url=https://v2.gw2.org.cn/bilibili-topic
Opacity=100
```

`YaoZhuGuide.ini` 是可选文件；已有同名 INI 中的自定义地址优先于 DLL 默认地址。旧版安装包的默认攻略地址会自动转到专题首页。只接受 `http://` 和 `https://` URL；无效地址会回退到默认页面。透明度支持 `30` 到 `100`。

插件不捆绑 WebView2 Runtime。目标机器需要先安装 Microsoft Edge WebView2 Runtime；启动时会先检测运行时。若缺失，插件会显示中文提示面板，可打开微软官方下载页面或重新检测；插件不会自动下载或执行安装程序，也不会把主页面改为系统浏览器。

诊断文件写入 `%LOCALAPPDATA%\YaoZhuGuide\YaoZhuGuide.log`，按 UTF-8 保存时间、进程/线程、状态和 HRESULT；日志超过 2 MiB 会自动截断。未处理异常会尽力生成 `%LOCALAPPDATA%\YaoZhuGuide\CrashDumps\*.dmp`，并继续交给 Nexus/游戏原有的崩溃处理流程。

## 发布

正式发布前需要完成：

1. 在 Raidcore Addon Library 确认插件项目和分配的签名 `0xEA4022D7`。
2. 提升插件版本后创建对应的 Git tag 和 GitHub Release。
3. 发布只包含 DLL 的安装包，并验证 Nexus 新安装和版本更新。

当前源码使用分配的签名和 `UP_None`；仓库可以用于源码和手动安装，不依赖插件内自动更新。

发布包只包含 `YaoZhuGuide.dll`，不得包含 `vendor/`、`build/`、WebView2 用户数据目录或本地缓存。
