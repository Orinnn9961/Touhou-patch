# 第三阶段：玩家上下文封装

本阶段把 TH12 原本依赖全局变量的玩家选择和运行状态封装为可保存、可恢复的 `PlayerContext`。目标是让后续 P2 初始化、更新、射击和 Bomb Hook 能在调用原版函数前切换上下文，同时保持现有单人流程不变。

## 上下文字段

| 字段 | 原版地址 | 类型 | 含义 |
|---|---:|---|---|
| `character` | `0x004B0C90` | `int32_t` | 角色：0 灵梦、1 魔理沙、2 早苗 |
| `shotType` | `0x004B0C94` | `int32_t` | 机体：0 A、1 B |
| `inputMask` | `0x004D49D0` | `uint32_t` | 当前玩家输入位掩码 |
| `playerInf` | `0x004B4514` | `PlayerInf*` | 原版玩家单例指针 |

核心实现位于：

- `patch/player_context.h` / `patch/player_context.cpp`：与游戏地址无关的保存、切换和作用域恢复逻辑。
- `patch/runtime_player_context.h` / `patch/runtime_player_context.cpp`：把上下文绑定到 TH12 1.00b 的四个全局地址。
- `tests/player_context_tests.cpp`：使用内存中的假全局验证行为，不依赖启动游戏。

## 切换协议

`PlayerContextManager::Activate(target)` 严格执行以下顺序：

1. 检查目标上下文已经配置；无效目标不写任何全局。
2. 从四个实时游戏全局保存当前玩家上下文。
3. 把目标玩家的四个字段写入对应游戏全局。
4. 最后更新 `activeSlot`。

后续调用原版函数时应优先使用作用域封装：

```cpp
{
    ScopedPlayerContext playerScope(contexts, PlayerSlot::kPlayer2);
    if (!playerScope.IsActive()) {
        return;
    }
    CallOriginalPlayerUpdate();
}  // 自动保存 P2 的最新状态并恢复进入作用域前的玩家
```

作用域可以嵌套。析构时使用同一个切换协议恢复先前玩家，因此 P2 在原版函数中对输入或 PlayerInf 指针产生的变化不会丢失。

## 单人无回归边界

DLL 初始化流程只有在 TH12 1.00b 地址签名全部通过后才绑定上下文。`InitializePlayer1` 只读取四个全局并标记 P1 为当前上下文，不写内存、不修改代码、不安装 Hook。

当前版本没有创建 P2，也没有任何自动 `Activate` 调用。因此正常单人模式继续由游戏独占写入角色、机体、输入和 PlayerInf 单例；补丁不会逐帧覆盖这些值。地址校验失败时，上下文层不会初始化。

## 自动测试覆盖

- 初始化为只读操作。
- 游戏在启动后修改 P1 状态，首次切换仍捕获最新值。
- P1/P2 往返完整保存角色、机体、输入和 PlayerInf 指针。
- `ScopedPlayerContext` 退出时恢复 P1，并保存 P2 在作用域中的更新。
- 未配置 P2 或尝试覆盖活动上下文时，不写游戏全局。
- 只使用 P1 时，游戏对四个全局的写入原样通过。

## 本阶段不包含

- 不分配第二个 `PlayerInf`。
- 不加载第二套 ANM/SHT。
- 不 Hook `PlayerUpdate`、射击、Option 或 Bomb。
- 不拆分库存、火力、残机等共享资源。
- 不接入网络输入。

下一阶段应在已确认的 `0x00436410` 创建路径外建立 P2 生命周期，并在调用原版初始化/更新函数的最小范围内使用 `ScopedPlayerContext`。在 ANM slot 和 SHT 内存完成隔离前，不应实际创建不同角色的 P2。
