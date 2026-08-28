# 第七阶段：最小确定性验证

## 验收结果

第七阶段已于 2026-08-16 完成验收。G/H 均为 trace v3：前 600 帧严格比较通过，1365 帧共同区间中帧号、双方输入、双方固定点位置、双方低速状态、CRT 基础 RNG 和综合状态哈希全部一致。H 仅比 G 多记录 2 帧，不存在共同区间状态分歧。正式配置已恢复 `diagnostic_forced_input=0`、`movement_only_test=0` 和 `trace=0`。

本阶段只验证当前已经实现的双人移动，不驱动远端输入。TH12 的实测注册回调顺序为 `P2 -> P1`：P2 先保存实际使用的输入并完成移动，P1 随后更新，P1 回调返回前才递增帧号并写入这一 tick 的最终双人状态。`movement_only_test=1` 时 P1 也只调用原版移动函数，用于隔离射击、中弹、复活和关卡 RNG。

- 逻辑帧号和 P1/P2 输入掩码
- P1/P2 16.8 固定点位置
- P1/P2 实际低速状态
- 主线程 CRT `rand` 的 32 位 LCG 状态及其 FNV-1a 64 位哈希
- 上述字段的逐帧状态哈希

RNG 字段目前明确命名为 `base_rng`。静态反汇编确认 TH12 使用的基础运行库随机状态位于 CRT 线程数据块 `+0x14`，更新式为 `state = state * 0x343FD + 0x269EC3`。这不是对所有 ECL/脚本随机源的承诺；后续扩大同步状态时应增加新的版本化字段，而不是复用此字段。

## 配置

```ini
[phase7]
enabled=1
trace=1
trace_directory=coop\logs
movement_only_test=1
```

`trace=0` 仍会执行采样但不写盘。文件名同时包含 PID 和本进程内的关卡序号：
`coop\logs\phase7-trace-<pid>-<run>.bin`。同一进程重新进关也不会覆盖上一份 trace。

## 人工验证

只允许启动一份游戏时，使用两次顺序启动即可，不需要同时运行两个进程。不要尝试用手工按键复现同一帧输入，第一轮验收使用固定输入。

1. 关闭游戏，在 `phase5` 中设置 `diagnostic_forced_input=136`。十进制 136 即 `0x80 | 0x08`，表示 P2 始终右移并保持低速。P1 在测试期间不要按方向键或 Shift。
2. 保持 P1 机体、难度、关卡、P2 机体和 `p2_start_x` 不变，`phase6.network_mode=off`，`phase7.trace=1`。
3. 第一次启动游戏，进入同一关卡并保持至少 15 秒，然后完整退出游戏，不只是返回标题。记下最新的 `phase7-trace-<pid>-1.bin` 为 A。
4. 第二次启动游戏，选择完全相同的内容，进入关卡并保持至少 15 秒，再完整退出。记下最新文件为 B。
5. 比较共同的前 600 帧。`--frames` 会忽略两次人工退出时刻造成的文件长度差异：

```powershell
.\coop-bin\phase7-compare.exe `
  .\coop\logs\phase7-trace-1234-1.bin `
  .\coop\logs\phase7-trace-5678-1.bin `
  --frames 600
```

成功退出码为 `0`，输出 `determinism verified: first 600 frames are identical`。退出码 `1` 表示状态、输入、长度或最低帧数不满足，比较器会报告首个分歧帧；退出码 `2` 表示参数错误、文件不完整、版本不兼容或记录校验失败。

判读顺序是先看 `p1_input`/`p2_input`，再看位置和低速，最后看 `base_rng_hash`。如果输入已经不同，不能据此判定模拟不确定；只有输入相同而位置、低速、帧号或 RNG 哈希不同，才是第七阶段失败。P2 后采样和移动隔离版本的 trace 使用格式版本 3，不能与旧版本 trace 混比。

完成固定输入验收后，把 `diagnostic_forced_input` 和 `movement_only_test` 都恢复为 `0`。动画测试单独进行：按住 A 或 D 约一秒后完全松开，P2 必须在松开后回到正面静止动画；持续按住方向直到撞边时仍保持朝向动画是正确行为，不算失败。

离线格式测试：

```powershell
ctest --test-dir .\coop\build -C Release --output-on-failure
```
