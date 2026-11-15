# AGENTS.md — susfs4ksu-lkm 开发向导

面向接手的 AI/开发者（Codex 等）。先读完本文件，再动代码。**每个改动必须 CI + 真机验证**，只写“编译通过”不算完成。

---

## 0. 这是什么

SUSFS 的**可加载内核模块（LKM）版**：不重编译内核，用 `.ko` + loader 在 Android GKI（5.10–6.18）上提供 susfs 的隐藏功能。
当前主线正在做的重构：**所有 hook 统一由中心层 `kernel/susfs_hooks.c` 安装**，各功能层只提供 consumer / 原始探针。

- 仓库：`wunify`（本目录），远端 `inforcqb/susfs4ksu-lkm`（GitHub 已改名为 `susfs4ksu-for-lkm`，SSH 会提示 moved，属正常）
- 当前分支：`feat/lookup-inline`（基于 `feat/susfs-hooks`）
- 设备：OnePlus PJA110，`5.15.180-android13-8`，KernelSU（root 在重启后需要重新授权）
- 记录仓库：`../notes`（另一 git 仓库，所有实测结论写进 `reports/`）

---

## 1. 五分钟上手

```sh
cd wunify
git log --oneline -10                 # 看最近改动
sed -n 1,120p kernel/susfs_hooks.h    # 中心层 ABI
sed -n 1,60p  kernel/susfs_hook_slots.h  # map slot（X-macro）
```

构建在 CI（本机没有内核树）：

```sh
git push origin <branch>
gh run list -R inforcqb/susfs4ksu-lkm --workflow build-lkm-ddk --branch <branch> --limit 1
gh run download <run-id> -R inforcqb/susfs4ksu-lkm -D art \
  -n susfs_guard_lkm.ko-android13-5.15 -n susfs_insmod-ndk
```

CI 矩阵：android12-5.10 / android13-5.10 / android13-5.15 / android14-5.15 / android14-6.1 / android15-6.6 / android16-6.12 / android17-6.18。
`layering` workflow 检查分层白名单（`tools/layer_allowlist.json`）。

---

## 2. 仓库地图

```text
kernel/
  susfs_hooks.h/.c        中心层：target/slot、register/register_assign、安装与卸载
  susfs_hook_slots.h      内核符号 map（X-macro），loader 逐 slot 探测
  HooKern/                hook 引擎：fp/kprobe/kretprobe/inline/LSM/patch（vendored，保持通用）
  susfs_fp_hook.c         FP 表安装（sys_call_table 项替换）
  sus_path.c              sus_path 隐藏规则 + LSM 面 + lookup 面（filename_lookup/path_lookupat/path_openat）
  susfs_kstat.c           kstat 改写（FP + probes）
  sus_map.c               /proc/pid/maps 过滤（probes + TABLE seq_operations）
  sus_mount.c             挂载隐藏（probes）
  susfs_opath.c           路径反向伪装（probes）
  susfs_uname.c           uname 伪装（inline C wrapper / kretprobe fallback）
  susfs_avc_spoof.c       avc 日志伪装（inline / kprobe）
  susfs_hide_syms.c       /proc/modules 隐藏（entry stub）
  susfs_create_gate.c     create 系列 EACCES 门（FP）
  susfs_lookup_stub.S     lookup/open 的 ret hook stub（三个 stub + tramp 变量）
  susfs_vfs_stub.S        vfs_open entry stub
  susfs_hide_stub.S       s_show/m_show entry stub
tools/
  susfs_insmod.c          loader（设备上叫 susfs_insmod_ndk）
  susfs_bench.c           微基准（每 syscall ns、配对可分性）
  layer_allowlist.json    分层白名单
.github/workflows/        build-ddk / layering / module-ci ...
```

---

## 3. 中心层模型（最重要）

### 3.1 注册

```c
struct susfs_hook {
        const char *owner;              /* 功能层名 */
        const char *name;
        enum susfs_hook_target target;  /* 目标表 */
        int (*pre)(void *ctx);          /* inode/path 判定 */
        struct susfs_hook_probe *probes;/* raw 探针（可选） */
        unsigned int nprobes;
        ...
};

susfs_hook_register(&hook);                    /* AUTO：inline → kprobe/probes 回退，0/1 */
susfs_hook_register_assign(&hook, BACKEND_X);  /* 指定后端，0 成功 / -225 失败，不回退 */
susfs_hook_unregister(&hook);
```

- `register` 返回 `SUSFS_HOOK_OK`(0) 或 `SUSFS_HOOK_FALLBACK`(1)（用了回退后端），负数是安装失败。
- `register_assign` **不回退**：要么指定后端装好，要么 `-225`。

### 3.2 后端

| 后端 | 适用 | 说明 |
|---|---|---|
| `FP` | sys_call_table 项 | `susfs_fp_install/remove` |
| `KPROBE` | 导出/static 函数 | 结构化 target 上自动回退；feature 的 raw probes 优先 |
| `KRETPROBE` | 需要返回值 | 与 KPROBE 共用 raw probes |
| `INLINE` | 函数入口 | C wrapper（非 PAC）或 entry stub（PAC）或 ret hook（stub 调 trampoline） |
| `LSM` | security hooks | 每 consumer 独立注册 |
| `TABLE` | 函数指针表项（seq_operations） | 中心层 patch `seq_op->show`，卸载恢复 `seq_orig` |

### 3.3 inline 三种形态（PAC/BTI 规则）

1. **C wrapper**：`hk_inline_hook(&t->inline_hook, sym, wrapper)`。只用于入口**没有** `paciasp/pacibsp` 的函数（`avc`、`inode_permission`）。有 PAC 会被拒绝——不要绕过。
2. **entry stub**：汇编 stub 保存 x0–x18/x30 到 176 字节窗口，`blr trampoline` 调用原函数（trampoline 重放入口指令后落入 body），body `ret` 回到 stub，stub 恢复 caller LR 返回。用于 `vfs_open`、`s_show/m_show`、`path_lookupat`、`filename_lookup`、`path_openat`。
   - PAC 安全的原因：trampoline 的 `paciasp` 用 stub 的 SP/LR，body 的 `autiasp` 认证同一对。
   - BTI 由 `hk_inline_apply()` 处理：入口 `bti jc; ldr x17; ret x17`，trampoline 首字 `bti jc`；`ret x17` 进入 stub 不要求 landing pad，**stub 本身不需要 `bti`**。
3. **ret hook**：stub 调 trampoline 拿到真实返回值，再调 C decide 修改返回值（`path_lookupat/filename_lookup` 返回 `-ENOENT` 并 `path_put`；`path_openat` 返回 `ERR_PTR(-ENOENT)` 前 `fput`）。这是唯一能“改返回结果”的形态。

### 3.4 map slot

`kernel/susfs_hook_slots.h` 是 X-macro 列表：每行一个内核符号，顺序即 slot 号。
loader（`tools/susfs_insmod.c`）在加载前用 tracefs kprobe 逐 slot 探测 1000 轮，把命中的 bit 写进 bitmap；模块用 `susfs_hook_slot_enabled(slot)` 决定 inline 能否装。
**新 inline 目标必须同时加 slot**（否则 `install_inline` 直接失败），并考虑 loader 的触发是否覆盖该符号。

---

## 4. 铁律（违反过，都 panic 过）

1. **CFI**：任何间接调用内核符号（orig/trampoline/回调）必须位于 `__nocfi` 函数内；kprobe pre handler 里间接调用也要 `__nocfi`。
2. **LTO 内联**：kprobe 计数为 0 的符号不可用（`filename_lookup` 在 5.15 上就是例子：单装时不覆盖 stat）。选落地函数前先看 map 探测计数。
3. **kprobe handler 不能睡眠**，不能分配内存；`path_lookupat` 的 path 引用释放用 `path_put`（不会睡眠）。
4. **stub 窗口**：保存区必须在 stub 的 SP 之上（body 的帧在 SP 之下），否则 body 覆盖保存区 → 崩溃。`176 = 160 参数窗 + 8 返回值 + 8 对齐`。
5. **版本差异**：`__get_user_pages_remote` 在 6.2 起不存在（用 `pin_user_pages_remote`）；`attach_recursive_mnt` 6.18 只有 2 个参数；签名以对应内核源码为准，不要照抄 5.15。
6. **日志**：所有 `pr_*` 必须带 `susfs_guard_lkm:` 前缀（HooKern 的 3 个文件已定义 `pr_fmt`）。
7. **不要**在 `susfs_hooks.c` 之外直接 `hk_inline_hook*` / `register_kprobe` / `ksu_patch_text` 装 hook（feature 层只注册 consumer）。

---

## 5. 设备流程

```sh
export MSYS_NO_PATHCONV=1    # Git Bash 下必须，否则 /data/... 被转成 Windows 路径
adb devices
adb shell 'su -c id'        # 重启后 KernelSU 可能未授权，先确认
adb push art/susfs_guard_lkm.ko-android13-5.15/susfs_guard_lkm.ko /data/local/tmp/m.ko
adb push art/susfs_insmod-ndk/susfs_insmod_ndk /data/local/tmp/susfs_insmod_ndk
adb shell 'chmod 644 /data/local/tmp/m.ko; chmod 755 /data/local/tmp/susfs_insmod_ndk'

adb shell 'su -c "dmesg -C; /data/local/tmp/susfs_insmod_ndk -p /data/local/tmp/m.ko uname_spoof_enabled=1 release=x version=y"'
adb shell 'su -c "dmesg | grep -E \"susfs_guard_lkm|lkmhook\""'
adb shell 'su -c "rmmod susfs_guard_lkm; echo rmmod=\$?"'
```

- `-p`：加载前跑 map 探测（1000 轮触发，约 2 分钟）；不带 `-p` 会跳过探测，inline 目标可能因 slot 未置位而不可用。
- 常规验证：`stat/cat/rm` 隐藏路径 → ENOENT；`/proc/modules | grep -c susfs` → 0；`uname -r`；`rmmod=0`。
- 完整功能 + 并发压力：`../dev/sufs_full_pressure.sh`（push 到设备后 `su -c sh ...`）。
- 性能：`tools/susfs_bench`（需 CI 构建；`susfs_bench <path> 200000`，`-p 3 1000 120 A B` 配对可分性）。

---

## 6. 工作纪律

1. 分支：`feat/<topic>`；一次一个主题。
2. commit：英文，说明**测得的结果**（不是“修改了 X”），例如 “path_lookupat measured 315 hits; filename_lookup fully inlined (0)”。
3. 每个改动：CI 绿 → 真机验证 → 在 `../notes/reports/` 写报告（环境、命令、结果）→ notes 仓库提交。
4. 不把“编译通过”写成完成；不把 6.x 的推断写成已验证。
5. 设备上 `rmmod` 前先确认没有并发测试在跑；模块加载失败先看 `dmesg | grep lkmhook`。

---

## 7. 当前路线图 / 已知问题

- [ ] `filename_lookup` 的覆盖未证实（不覆盖 stat）；`path_lookupat` 单装是否覆盖 statx/faccessat 待测，决定去留。
- [ ] stub 缩窗：176 → 48 字节（只保存 path/file + x30 + ret）。这是目前唯一能显著降固定成本（~300 ns/层）的项。
- [ ] 6.1/6.6/6.12/6.18 只有 CI 编译，没有真机验证。
- [ ] `inode_permission` 结构化 target 已预留，未启用（sus_path 用 lookup 面 + LSM）。
- [ ] `vfs_open` 作为 `path_openat` 的 fallback（path_openat 装不上时才装）。
- [ ] bench 噪声大（CV 6–19%），跨构建对比要用 A/B 交替，不要单次下结论。

---

## 8. 排障表

| 现象 | 先看 |
|---|---|
| `Oops - FPAC`，`Code: ... d50323bf` | 入口有 PAC 却用了 C wrapper → 改 entry stub |
| `hide_syms init failed -95` | PAC 检测拒绝 → 已改 stub；确认 stub 路径 |
| `VFS 被写坏`/所有 open 返回 ENOENT | stub 在恢复寄存器前判断 w0 → `cbz w0,.Lallow` 必须在 restore 之前 |
| loader `Unknown symbol __stack_chk_guard` | 5.10 内核的 stack protector 配置（kernel/Makefile 已处理） |
| 模块加载成功但功能不生效 | map bit 是否为 0（loader 不带 `-p` 或符号内联） |
| `susfs_insmod_ndk: Permission denied` | loader 需要 755，ko 644 |
| dmesg 里 `[lkmhook]` 无前缀 | 对应文件缺 `pr_fmt`（现在 3 个文件都有） |

---

## 9. 相关文档

- `README.md` / `PROC_INTERFACES.md`：用户接口与用法。
- `../notes/reports/2026-11-14-hook-centre-consolidation.md`：中心层汇聚的完整清单。
- `../notes/reports/2026-11-14-lookup-ret-hook.md`：lookup ret hook 设计与实测。
- `../notes/reports/2026-11-14-hook-prototypes.md`：上游 patch 与 6.18 原型核对。
- `../notes/reports/2026-11-14-hook-overhead.md`：性能/方差/可分性数据。
- `../notes/reports/2026-11-14-full-pressure.md`：功能 + 并发压力结果。
