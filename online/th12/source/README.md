# TH12 双人补丁（第一至第九阶段）

## 第九阶段状态

当前运行时已加入 P2 子弹/激光伤害、P1/P2 独立中弹与擦弹分派、P2
死亡复活状态机，以及具有固定同帧规则的道具接触归属。共享资源结算仍留待
后续阶段；地址、归属规则和已知边界见
`research/phase9-collision-item-ownership.md`。

```ini
[phase9]
enabled=1
collision_log=0
```

当前包含启动器、32 位 `dinput8.dll` 代理、补丁 DLL、六机体逆向资料、玩家上下文层、双机体资源银行、P2 移动/射击/判定，以及帧输入和局域网协议骨架。进入一局后会创建第二名玩家；Bomb 隔离和共享资源冲突结算尚未实现。网络输入本阶段只交换和缓存，不会驱动游戏模拟。

## 直接使用

正式产物已经部署在 `th12.exe` 同目录：

- `coop-launcher.exe`：校验版本并启动游戏。
- `dinput8.dll`：把原版 `DirectInput8Create` 转发到 Windows 系统 DLL，再加载补丁。
- `th12_coop.dll`：补丁入口；校验地址映射，捕获 P1 上下文，并安装第四阶段资源生命周期 Hook。

双击 `coop-launcher.exe` 即可。它只接受已确认的日文 1.00b：

`D8D644D2E64957A3031B1A1399D0502E1DDAA5252D2C4E492770AD6717827628`

仅执行校验而不启动游戏：

```powershell
.\coop-launcher.exe --verify
```

自动化静默校验：

```powershell
.\coop-launcher.exe --verify --silent
```

## 配置和日志

`coop\config.ini` 是后续阶段共用的配置入口。把 `patch.enabled` 改为 `0` 会保留 DirectInput 转发，但跳过加载 `th12_coop.dll`。第四阶段可单独配置：

```ini
[phase4]
enabled=1
player2_airframe=sanae_b
```

P2 机体可设为 `reimu_a`、`reimu_b`、`marisa_a`、`marisa_b`、`sanae_a`、`sanae_b`。

日志位置：

- `coop\logs\launcher.log`
- `coop\logs\bootstrap.log`
- `coop\logs\patch.log`

## 重新构建

需要 Visual Studio 2022 Build Tools 的 x86 C++ 工具链：

```powershell
powershell -ExecutionPolicy Bypass -File .\coop\build.ps1
```

产物生成在 `coop-bin`。其中 `proxy-smoke.exe` 只用于开发测试，不需要随游戏分发。

运行自动测试：

```powershell
ctest --test-dir .\coop\build -C Release --output-on-failure
```

## 第二阶段逆向资料

完整地址、六机体资源、移动参数、Option、射击、Bomb 和卸载路径见：

- `research\phase2-airframe-map.md`
- `research\sht-manifest.json`
- `patch\version_map.h` / `patch\version_map.cpp`

重新验证六套 SHT 并生成机器可读清单：

```powershell
.\coop-bin\sht-inspect.exe --check --json .\coop\research\sht-manifest.json .\coop\research\extracted\pl00a.sht .\coop\research\extracted\pl00b.sht .\coop\research\extracted\pl01a.sht .\coop\research\extracted\pl01b.sht .\coop\research\extracted\pl02a.sht .\coop\research\extracted\pl02b.sht
```

## 第三阶段玩家上下文

设计、切换协议和单人兼容边界见 `research\phase3-player-context.md`。独立运行上下文测试：

```powershell
.\coop-bin\player-context-tests.exe
```

当前只初始化并捕获 P1，不创建 P2。后续 Hook 必须通过 `ScopedPlayerContext` 在调用原版玩家函数前后成对切换，避免角色、机体、输入和 `PlayerInf*` 泄漏到另一个玩家。

## 第四阶段双资源驻留

设计、地址证据、slot 7 临时加载方案和释放顺序见 `research\phase4-dual-resources.md`。独立运行资源银行测试：

```powershell
.\coop-bin\resource-bank-tests.exe
```

同角色 A/B 共享只读 ANM archive，但始终使用不同的可写 SHT；不同角色分别持有 ANM archive。P2 archive 不占用固定全局槽，避免与关卡和 Boss 动态 ANM 槽冲突。

## 第五阶段 P2 移动

P2 生命周期、移动入口、输入位、边界和人工测试清单见 `research\phase5-player2-movement.md`。

默认控制：

- P1：方向键移动，Shift 低速。
- P2：WASD 移动，Space 低速。

第五阶段配置位于 `coop\config.ini` 的 `[phase5]`。运行离线输入测试：

```powershell
.\coop-bin\player2-input-tests.exe
```

进入一局后应出现两个玩家。本节记录的是第五阶段移动基线；当前构建已由第八、九阶段扩展为射击、受击、复活和道具接触归属，仍不会释放 Bomb。

## 第六阶段帧输入与局域网骨架

帧输入结构和早期网络骨架见 `research\phase6-frame-input-network.md`；实际应用输入的 v7 92 字节输入束协议、手动固定延迟、暂停、断线和状态校验见 `research\phase13-lan-lockstep.md`。正式配置保持：

```ini
[phase6]
enabled=1
network_mode=off
input_delay=2
adaptive_delay=0
input_redundancy=8
```

离线测试程序：

```powershell
.\coop-bin\frame-input-tests.exe
.\coop-bin\network-protocol-tests.exe
.\coop-bin\lan-session-tests.exe
```

`host`/`client` 模式现已按角色实际应用输入，日志记录 `network_apply=1`。主机固定控制 P1，客户端固定控制 P2；双方房间参数和机体组合必须一致。
