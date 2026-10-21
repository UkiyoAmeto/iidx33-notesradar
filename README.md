# IIDX 33 NOTES RADAR Cap Unlock

> ## ⚠️ 重要警告（在线服务器）
>
> **严禁在连接在线服务器时关闭 `EnablePlayerCap`。**
> 关闭后玩家雷达（PNR）可能突破 200.00 并被服务器记录为异常数值，**可能导致封号**。
> 在线游玩时请务必保持 `EnablePlayerCap=1`，只在完全离线的环境里才考虑改动它。

**简体中文** | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md)

beatmania IIDX 33 Sparkle Shower (LDJ) 的 NOTES RADAR 上限解锁插件。

游戏把谱面雷达的六个轴硬性夹在 **200.00**。本插件拆掉这条上限，让选曲画面和副屏
显示真实值（可以超过 200），同时保证**玩家雷达（PNR）在结算与上报时仍然按 200.00 封顶**。

---

## 功能

| 功能 | 说明 | 配置项 |
|---|---|---|
| 谱面雷达上限解锁 | 选曲画面 / 副屏显示真实值（>200.00） | `EnableCap` |
| 玩家雷达封顶 | PNR 结算与上报仍按 200.00 | `EnablePlayerCap` |
| 手动重算 | `Ctrl+Alt+R` 调用游戏自己的全曲雷达重算 | `EnableRecalcKey` |

## 原理

谱面雷达由 6 个原始指标经过一个 **4 段折线归一化函数**得到，输出 `[0, 2.0]` 再乘 `10000`
存成 1/100 定点整数。200.00 不是一条 `min(v, 200)` 指令，而是折线**最后一段的终点**
（`1.75 + 0.25 × 1.0 = 2.0`）。把最后一段"比值夹到 1.0"的分支 NOP 掉，值就会继续
线性外推，自然超过 200.00 且保持单调。

完整的逆向过程、参数表、数据结构与验证方法见 **[docs/algorithm.md](docs/algorithm.md)**。

## 兼容性

- 支持 **IIDX 33 Sparkle Shower**（bm2dx.dll 18,467,992 字节）与
  **IIDX 34 ZINRAI**（bm2dx.dll 18,200,216 字节），运行时按模块 SizeOfImage 自动识别。
- DLL 会做字节签名校验，版本不匹配时跳过对应修改并写日志，不会破坏游戏。
- 与 spice2x 插件共存，通过 spice2x 的 `-k` 参数加载。

## 构建

需要 C 编译器（MSVC / clang / zig 均可）：

```bat
zig cc -target x86_64-windows-gnu -shared -O2 -o notesradar.dll src\notesradar.c
```

或直接运行 `build.bat`。

## 安装

1. 把 `notesradar.dll` 和 `notesradar.ini` 放到 `bm2dx.dll` 同目录；
2. 在 spice2x 启动参数里加一行：

   ```
   -k notesradar.dll
   ```

3. 启动游戏，同目录会生成 `notesradar.log`，正常应包含：

   ```
   notesradar loaded (build ...; cap=1 pnr=1 2dx=0)
   cap removal: 3/3 sites patched
   addBinary hook installed ...
   all done
   ```

## 配置

`notesradar.ini`

```ini
EnableCap=1          ; 谱面雷达上限解锁
EnablePlayerCap=1    ; PNR 结算封顶 200.00（在线时必须为 1）
Enable2dxCompat=0    ; 2dxtra 兼容（实验功能）
EnableRecalcKey=1    ; Ctrl+Alt+R 全曲重算
```

## 已知限制

- **2dxtra 的特殊谱面生成**（kichiku / kiraku / all scratch）会读取谱面 radar 并假设
  `≤ 200.00`。开启 `EnableCap` 时切换这些谱面类型可能崩溃，属于两者固有冲突；
  需要使用时把 `EnableCap=0` 后重启游戏。
- `Enable2dxCompat` 为实验功能（拦截 2dxtra 的写回）。由于 2dxtra 的代码不遵循
  Windows x64 ABI（依赖 r9 不被破坏、调用点栈未对齐），多次尝试仍不稳定，**默认关闭**。
- 谱面雷达 >200 时六边形会画到框外（游戏按 `/200` 归一化绘制），属预期现象。

## 免责声明

本项目仅用于**本地 / 离线环境**的研究与个人使用，使用者需自行合法拥有游戏本体。
作者不对因使用本插件造成的任何数据损坏、封号或其它后果负责。
**请勿在在线对战或影响他人体验的场景中使用。**

## License

MIT
