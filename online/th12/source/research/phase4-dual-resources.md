# 第四阶段：双机体资源同时驻留

本阶段为 TH12 日版 `1.00b` 建立两套玩家资源银行。目标是让任意两种机体的 ANM 与 SHT 同时留在内存中，并为下一阶段创建 P2 时提供明确的绑定入口。当前版本仍不创建第二个 `PlayerInf`，也不执行 P2 更新、射击或 Bomb。

## 为什么不能给 P2 固定分配另一个 ANM 槽

ANM 管理器根指针位于 `0x004CE8CC`，32 项槽表位于管理器对象的 `+0x004B50C0`。原版玩家固定使用 slot 7；slot 8 之后会被关卡、Boss 和其他动态资源占用，28 至 31 也存在动态加载路径，因此没有经过所有关卡证明的永久空闲槽。

加载函数 `0x0045FE60` 使用以下寄存器参数：

| 参数 | 含义 |
|---|---|
| `ECX` | ANM slot |
| `EBX` | 文件名 |
| 返回 `EAX` | archive 指针 |

内部加载路径 `0x0045FC90` 会分配 `0x138` 字节的 archive 对象，并在加载完成前把它登记到指定槽。因此不同角色的 P2 采用临时借用 slot 7 的方式：

1. 保存 P1 的 slot 7 archive。
2. 临时把 slot 7 清空。
3. 仍通过原版 loader 把 P2 ANM 加载到 slot 7。
4. 捕获新 archive 指针。
5. 恢复 P1 的 slot 7。
6. P2 archive 脱离全局槽表，由补丁单独持有和销毁。

`ScopedAnmSlotRestore` 保证普通成功和失败返回都会恢复 P1 槽。补丁不会占用关卡的其他 ANM slot。

## ANM、子弹贴图和特效隔离

同一角色的 A/B 机体共用一个 `pl00.anm`、`pl01.anm` 或 `pl02.anm`。该 archive 本身同时包含两种机体所需的玩家、Option、子弹和特效脚本，所以同角色组合共享 archive，不会产生脚本覆盖。

不同角色必须持有不同 archive。已确认的使用路径都从当前 `PlayerInf + 0x10` 取 archive，而不是再次查 slot 7：

- Option 重建 `0x004385B0` 在创建动画前读取 `PlayerInf + 0x10`。
- 射击生成 `0x00439630` 在 `0x00439899` 和 `0x00439947` 读取 `PlayerInf + 0x10`，再传给 `0x004615A0`。
- 玩家本体初始化同样把 archive 保存到 `PlayerInf + 0x10`。

因此下一阶段只要在 P2 初始化后调用 `BindPlayer2Resources(P2)`，玩家、Option、子弹贴图和这些路径产生的特效就会解析 P2 自己的 archive。两个 archive 可以使用相同脚本编号，不会互相覆盖。

Bomb 的机体分发和玩家归属仍属于后续生命周期/上下文 Hook。本阶段只保证所需 ANM 素材可同时驻留，不声称已经完成双人 Bomb 行为。

## SHT 必须始终独立

SHT loader `0x00437680` 使用 `EAX=文件名`、`ESI=PlayerInf-like buffer`，并只把结果写到 `ESI + 0xA2C`。loader 会原地把射击记录中的回调索引重定位为函数指针，因此 SHT 是可写运行时数据，不能由两个玩家共享。

补丁使用一个零初始化的 `0xA30` 字节 scratch buffer 调用原版 loader，然后取出 `scratch + 0xA2C`。无论 P1/P2 是否同角色、同机体，P2 都有独立 SHT 分配。

## 生命周期与失败回滚

补丁改写以下原版 `CALL`，并在写入前验证 opcode 和原目标地址：

- Player 初始化调用点：`0x00436465 -> 0x00435AE0`
- Player 销毁调用点：`0x0040EC97`、`0x00422436`、`0x00436473`、`0x004364B1`、`0x004364DC` -> `0x00436270`

P1 初始化成功后才预加载 P2。P1 初始化失败、地址签名不符、slot 7 状态不符、ANM/SHT 加载失败或资源银行检测到碰撞时，补丁会释放已经取得的 P2 分配、清空银行并保留原版单人流程。

销毁前先释放 P2：

- 独立 SHT：游戏释放函数 `0x0046CA4F`
- 不同角色的脱离式 ANM：archive 内容销毁 `0x004604E0`，随后 `0x0046CA4F`
- 同角色共享 ANM：不释放 P1 archive，只释放 P2 SHT

随后继续调用原版 Player destroy，由原版释放 slot 7 的 P1 ANM 和 P1 SHT，避免双重释放。

## 资源银行约束

`DualResourceBanks` 对所有 36 种 P1/P2 组合建立显式规划，并维持以下不变量：

- 两个玩家的 SHT 指针永不相同。
- 不同角色的 ANM archive 指针永不相同。
- 同角色允许共享 ANM archive。
- 无效或碰撞布局在校验失败时不提交，原有银行状态不变。

配置项位于 `coop/config.ini`：

```ini
[phase4]
enabled=1
player2_airframe=sanae_b
```

`player2_airframe` 可取 `reimu_a`、`reimu_b`、`marisa_a`、`marisa_b`、`sanae_a`、`sanae_b`。

## 当前边界

- 已完成任意机体组合的驻留规划、加载、绑定接口和对称释放。
- 已完成 36 种组合的离线资源银行测试和六套 SHT 检查。
- 已在真实 TH12 自动演示中验证不同角色分支：`marisa_a + sanae_b` 显示 `anm=isolated; sht=isolated`，驻留后持续运行正常。
- 已在真实 TH12 自动演示中验证同角色分支：`marisa_a + marisa_b` 显示 `anm=shared-character; sht=isolated`。
- 两条分支都通过标准窗口关闭触发 Player destroy，记录 P2 资源银行释放后由游戏自行退出。
- 尚未分配或更新 P2，因此真实画面中还不会出现第二名玩家。
- 36 种组合共享相同的三份 ANM loader 路径与六份已检查 SHT，但没有逐一做 36 次真实游戏启动；离线组合测试不等于完整运行时矩阵。
- 后续创建 P2 后，必须先销毁所有引用 P2 archive/SHT 的动画与子弹对象，再释放资源银行。
