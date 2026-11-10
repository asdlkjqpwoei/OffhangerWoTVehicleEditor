# OffHanger WoT Offline Vehicle Editor

## 编译环境要求

- Windows x64。
- Visual Studio 2026，安装 C++ 桌面开发工作负载和 v145 工具集。
- Windows SDK 10.0.26100 或更高版本。
- CMake 4.2 或更高版本；首次通过 NuGet 还原依赖需要联网。

## 编译步骤

```batch
cmake --workflow --preset release
cmake --workflow --preset debug
```

## 测试使用说明

```batch
ctest --preset release
ctest --preset debug
```

可选的真实安装验证接受游戏根目录和存档路径，保证游戏关闭的情况下:

```batch
bin\Release\OffHangerWoTOfflineVehicleEditorTests.exe "D:\Games\WoT" "D:\SaveCopies\account.dat"
```

## Build requirements

- Windows x64.
- Visual Studio 2026 with the Desktop development with C++ workload and v145 toolset.
- Windows SDK 10.0.26100 or later.
- CMake 4.2 or later; internet access is required for the initial NuGet restore.

## Build steps

```batch
cmake --workflow --preset release
cmake --workflow --preset debug
```

## Running tests

```batch
ctest --preset release
ctest --preset debug
```

Optional installation verification accepts a game root and save path. Ensure the game exited.

```batch
bin\Release\OffHangerWoTOfflineVehicleEditorTests.exe "D:\Games\WoT" "D:\SaveCopies\account.dat"
```
