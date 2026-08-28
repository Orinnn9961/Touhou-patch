# TH12 六种机体逆向整理

本报告针对日版 TH12 `1.00b`，只记录已经由 `th12.exe`、`th12.dat` 和提取出的玩家资源验证过的地址与字段。当前阶段没有安装任何游戏逻辑 Hook；`version_map.cpp` 只做版本签名校验，`sht-inspect.exe` 只做离线资源检查。

## 样本与范围

- `th12.exe` SHA-256：`D8D644D2E64957A3031B1A1399D0502E1DDAA5252D2C4E492770AD6717827628`
- `th12.dat` SHA-256：`DCA82DF072BA51C4B74C3D41EA1CBDCB91F709CE7CFEA23599EE52CAA34F2D72`
- PE32 x86，ImageBase `0x00400000`
- 资源副本：`coop/research/extracted/`
- 机器可读结果：`coop/research/sht-manifest.json`

地址均为进程虚拟地址（VA），不是文件偏移。换版本前必须重新做签名和资源校验。

## 六种机体总表

| 机体 | character | shotType | ANM | SHT | Option ANM | 普通/低速 | 普通斜向/低速斜向 | 命中半径 |
|---|---:|---:|---|---|---:|---:|---:|---:|
| 灵梦 A | 0 | 0 | `pl00.anm` | `pl00a.sht` | 21 | 4.5 / 2.0 | 3.1819806 / 1.4142135 | 2.0 |
| 灵梦 B | 0 | 1 | `pl00.anm` | `pl00b.sht` | 22 | 4.5 / 2.0 | 3.1819806 / 1.4142135 | 2.0 |
| 魔理沙 A | 1 | 0 | `pl01.anm` | `pl01a.sht` | 13 | 5.0 / 2.0 | 3.5355339 / 1.4142135 | 3.5 |
| 魔理沙 B | 1 | 1 | `pl01.anm` | `pl01b.sht` | 14 | 5.0 / 2.0 | 3.5355339 / 1.4142135 | 3.5 |
| 早苗 A | 2 | 0 | `pl02.anm` | `pl02a.sht` | 17 | 4.5 / 2.0 | 3.1819806 / 1.4142135 | 3.0 |
| 早苗 B | 2 | 1 | `pl02.anm` | `pl02b.sht` | 18 | 4.5 / 2.0 | 3.1819806 / 1.4142135 | 3.0 |

`optionLayout` 位于 `0x004B31D8 + (character * 2 + shotType) * 0x40`。描述表已固化在 `coop/patch/version_map.cpp`。

## 全局选择与初始化

选择状态：

- 当前角色索引：`0x004B0C90`
- 当前武器类型：`0x004B0C94`
- 组合索引：`shotType + character * 2`，范围 0..5
- 玩家单例：`0x004B4514`

生命周期：

| 用途 | 地址 |
|---|---:|
| PlayerInf 构造函数 | `0x004359A0` |
| PlayerInf 初始化 | `0x00435AE0` |
| 分配/创建 PlayerInf | `0x00436410` |
| PlayerInf 析构 | `0x00436270` |
| 删除包装函数 | `0x004364B0` |
| 删除单例 | `0x004364D0` |
| 每帧更新 | `0x00436BA0` |
| PlayerInf 大小 | `0xC59C` |

初始化函数读取选择索引，调用 ANM 加载函数 `0x0045FE60`，再调用 SHT 加载/重定位函数 `0x00437680`。原版玩家 ANM 固定放在 ANM slot `7`，所以第二个不同角色不能直接复用原路径。

## ANM 与卸载路径

资源字符串表：

- `0x004A10B4` -> `pl00.anm`
- `0x004A10A8` -> `pl01.anm`
- `0x004A109C` -> `pl02.anm`
- `0x004A1090`/`0x004A1084` -> `pl00a.sht`/`pl00b.sht`
- `0x004A1078`/`0x004A106C` -> `pl01a.sht`/`pl01b.sht`
- `0x004A1060`/`0x004A1054` -> `pl02a.sht`/`pl02b.sht`

ANM 指针表基址为 `0x004B3184`，按角色索引；SHT 指针表基址为 `0x004B3190`，按组合索引。三个角色 ANM 都包含玩家和 Option 图形，但所有玩家初始化都使用 slot `7`。

卸载顺序位于 `0x00436270`：先清理 Option/附属 ANM，随后释放 `PlayerInf + 0xA2C` 的 SHT 内存，再释放 `+0x940`、`+0x48C` 等附属资源。SHT 释放调用目标为 `0x0046C941`。ANM 管理器的 slot 卸载辅助函数是 `0x004604A0`，全局清理函数是 `0x004603D0`。

## SHT、射击和移动

`0x00437680` 不只是文件读取：它在内存中重定位 SHT 的十个 shot-set 指针，并把每条 `0x34` 字节射击记录的四个回调索引（`+0x24`、`+0x28`、`+0x2C`、`+0x30`）替换为函数指针。因此每个 PlayerInf 必须有独立的、可写的 SHT 副本。

SHT 头字段（由 `sht-inspect.exe` 检查）：

- `+0x00` 格式版本：3
- `+0x02` shot-set 数量：10
- `+0x04` 文件命中半径原始值：灵梦 2.0、魔理沙 2.7、早苗 2.4
- `+0x10` 普通速度，`+0x14` 低速速度
- `+0x18` 普通斜向速度，`+0x1C` 低速斜向速度
- `+0x20` 最大火力级数：4
- `+0x24` 火力步长：40
- shot-set 表：`+0x268`，每项 8 字节
- 射击记录：每条 `0x34` 字节，以首字节为负值的记录结束

射击入口：

- 生成单发/子弹：`0x00439630`
- 按火力选择 shot-set：`0x004399D0`
- 主射击更新：`0x00439A40`
- 主更新根据当前火力除以 `powerStep`，从十个 shot-set 中选择记录。

六套 SHT 的记录数和回调索引已写入 `sht-manifest.json`。例如灵梦 A 的每级记录数为 `2,4,6,8,10`，魔理沙 B 为 `2,4,6,12,14`；这些差异不能用一个共享的全局射击表代替。

初始化还会从以下三张角色表覆盖碰撞相关字段：

- `0x004B31A8`：有效命中半径 `2.0, 3.5, 3.0`
- `0x004B31B4`：原始值 `60, 70, 60`
- `0x004B31C0`：原始值 `100, 118, 100`
- `0x004B31CC`：原始值 `5, 7, 6`

后三张表目前只标记为碰撞相关原始表，尚未赋予更具体的语义。

## Option

Option 更新/重建入口为 `0x004385B0`。它读取 `optionLayout` 表项，并使用以下 ANM 脚本：灵梦 A/B `21/22`，魔理沙 A/B `13/14`，早苗 A/B `17/18`。Option 不能只复制一个角色的布局，因为六个组合索引各自占据独立的 `0x40` 字节表项。

## Bomb

Bomb 输入检测位于玩家更新流程 `0x00436D0F` 附近，消耗库存调用 `0x00422F20`，然后按同一个组合索引调用 Bomb 分发器 `0x00406BF0`。Bomb 状态更新/结束分发器是 `0x00406CE0`。

| 机体 | Bomb 开始 | Bomb 更新/结束 |
|---|---:|---:|
| 灵梦 A | `0x0046A5F0` | `0x0046A970` |
| 灵梦 B | `0x00408120` | `0x004082E0` |
| 魔理沙 A | `0x00407010` | `0x004072D0` |
| 魔理沙 B | `0x00407780` | `0x00407950` |
| 早苗 A | `0x004089C0` | `0x00408B90` |
| 早苗 B | `0x00408CB0` | `0x00408F00` |

## 对双人实现的直接结论

1. 不能只把全局角色索引改成两个值：原版单例、ANM slot 7、SHT 指针和 Option 状态都会互相覆盖。
2. 每个玩家需要独立的 PlayerInf、SHT 可写副本、ANM slot/实例和 Option 布局状态；输入、位置、火力、Bomb 库存也必须分开。
3. 子弹和 Bomb 的敌我归属需要在生成入口处携带 player id；否则两个机体的 callback 会落到同一全局上下文。
4. 本阶段不改 Boss 血量、不改网络、不写 replay；下一阶段应先做双 PlayerInf 的本地生命周期和资源隔离，再接入 Wi-Fi 状态同步。

## 验证命令

```powershell
powershell -ExecutionPolicy Bypass -File .\coop\build.ps1
ctest --test-dir .\coop\build -C Release --output-on-failure
.\coop-bin\sht-inspect.exe --check --json .\coop\research\sht-manifest.json .\coop\research\extracted\pl00a.sht .\coop\research\extracted\pl00b.sht .\coop\research\extracted\pl01a.sht .\coop\research\extracted\pl01b.sht .\coop\research\extracted\pl02a.sht .\coop\research\extracted\pl02b.sht
.\coop-launcher.exe --verify --silent
```
