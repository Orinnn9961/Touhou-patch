# 第八阶段：P2 射击与 Option

## 实现边界

P2 更新仍不调用完整的 `0x00436BA0`，避免提前引入 Bomb、受击、擦弹、复活和道具拾取。P2 回调在 `ScopedPlayerContext(Player2)` 内执行：

创建完成后按 `0x00436100` 中已经确认的原版赋值，只初始化 P2 自己的 `+0xA28` 和 `+0xC420...+0xC430` 射击状态字段。不能直接调用完整 `0x00436100`，因为其中还会访问关卡管理器和清理全局对象，而 P2 创建 Hook 执行时这些对象尚未完成装载。之后每帧执行：

1. `0x004364F0`：移动、低速和自机动画。
2. 共享 Power 数值发生变化时调用 `0x004385B0`：重建 P2 Option。
3. `0x00439A40`：读取 P2 输入位 `0x01`，更新独立射击计时并按 P2 SHT 发射。
4. `0x00439B10`：只更新 P2 `PlayerInf` 内的玩家子弹池。

V23 将共享 Power 的监视粒度收紧为整数档位 `Power / 100`。同一档位内
拾取小 P 点不再反复调用 Option 重建；跨档、复活或完整性修复时，先在
对应玩家的机体和 ANM 上下文中销毁当前有效 Option，再从空状态重建。
完整性检查覆盖全部 8 个槽：计数内缺失 active/VM，或计数外仍 active，
都会触发修复。

ANM VM ID 会被游戏回收。失活且位于当前 Option 计数之外的槽可能保留
已由原版删除的旧 ID，因此清理只对当前计数内或仍 active 的槽调用原版
VM 析构；其余槽只清零句柄。这样不会误删后来复用同一 ID 的 Bomb 或
特效，也不会让降档、死亡后的尾部 Option 留在版底继续射击。

双方共享的 Power 数值仍位于 `0x004B0C48`，这是后续共享资源阶段的预期边界。以下状态均按玩家隔离：

- `PlayerInf + 0xC420/0xC424`：射击计时和序列。
- `PlayerInf + 0x0A58` 起：玩家子弹池。
- `PlayerInf + 0x82D0` 起及 `+0xC41C`：Option 实例和数量。
- `PlayerInf + 0x0A2C`：独立可写 SHT。
- `PlayerInf + 0x10` 及各 Option 内部 VM：ANM archive 绑定和动画状态。

同角色 A/B 允许共享只读 ANM archive，但动画 VM 始终位于不同的 `PlayerInf`。不同角色使用不同 ANM archive。射击和 Option 调用期间，ANM 槽 7 会临时切换到 P2 archive，返回前恢复 P1。

## 配置

默认 P2 射击键为 `J`（Virtual-Key 74）：

```ini
[phase8]
enabled=1
p2_shoot=74
```

射击键必须与 P2 的 WASD 和 Space 不同。`diagnostic_forced_input` 现在允许射击位 `0x01`，仍拒绝 Bomb 位 `0x02`。

`phase7.movement_only_test=1` 会有意关闭 P2 射击、Option 重建和子弹更新，以保留第七阶段的纯移动确定性测试语义。正常游戏必须保持为 `0`。

## 人工测试

测试前确认：

```ini
[phase7]
trace=0
movement_only_test=0

[phase8]
enabled=1
p2_shoot=74
```

建议先使用 P1 `reimu_a`、P2 `sanae_b`，再补测同角色不同机体 P1 `reimu_a`、P2 `reimu_b`。

1. 进入第一关，确认两名玩家和各自 Option 正常显示。
2. 只按 P1 射击键，确认只有 P1 发射；松开后只按 `J`，确认只有 P2 发射。
3. 同时按住两个射击键至少 10 秒，确认两套弹型、射速、发射位置和 Option 子弹均符合各自机体。
4. 交替松开其中一人的射击键，确认另一人的射击计时和连续射击不被重置或卡住。
5. P2 按住 `J` 的同时使用 WASD 和 Space，确认子弹与 Option 跟随 P2，低速布局切换正确，P1 的 Option 不移动或切换。
6. 获得足够 Power 使 Option 数量变化，确认双方按共享 Power 同步增加 Option，但 Option 位置、动画和射击归属各自玩家。
7. 让两名玩家的子弹同时命中普通敌人，确认无崩溃、无明显残留弹和错误贴图。Boss 血量倍率与完整合作伤害规则不在本阶段验收。
8. 重开关卡、返回标题后重新进入，并连续切换至少两组 P2 机体，确认无崩溃和素材串用。

## 验收标准

- P1 与 P2 可同时持续射击，任一方开始或停止不改变另一方节奏。
- 两种 SHT 弹型、Option 数量/布局、子弹贴图和 ANM 正确，不出现互相覆盖。
- P2 普通/低速切换时 Option 行为正确，且仍保持移动边界和静止动画修复。
- `J` 不触发 Bomb；P2 仍不受击、不擦弹、不拾取道具。
- `coop/logs/patch.log` 出现一次 `phase8 P2 shoot input observed; state=1` 和一次 `phase8 P2 shot observed`。Power 改变后还应出现一次 `phase8 P2 Option rebuilt`，且不出现 `phase8 P2 combat update skipped`。
- 重开和返回标题不崩溃，重新进入后 P2 能再次正常射击。
