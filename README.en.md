[中文](README.md)

[x64 Device Compatibility](https://github.com/ymuuuu/susfs4ksu-lkm-x64)

<p align="center">
  <img src="assets/title.svg" alt="susfs4ksu-lkm — SUSFS as a loadable kernel module" width="760">
</p>

[![QQ 群 1125256073](https://img.shields.io/badge/QQ-1125256073-12B7F5?logo=qq&logoColor=white)](https://qm.qq.com/q/yMYVB9m0VO)
![Android 12+](https://img.shields.io/badge/Android-12%2B-3DDC84?logo=android&logoColor=white)
![arm64-v8a](https://img.shields.io/badge/arm64--v8a-6E7681?logo=arm&logoColor=white)
![GKI 5.10 ~ 6.18](https://img.shields.io/badge/GKI-5.10%20~%206.18-1F6FEB?logo=linux&logoColor=white)
![KernelSU](https://img.shields.io/badge/KernelSU-LKM-12B7F5)
![GPL-2.0-Only](https://img.shields.io/badge/License-GPL--2.0-3DA639?logo=gnu&logoColor=white)

> 🚀 A loadable kernel module (LKM) port of SUSFS: run susfs **without recompiling the kernel**.

## 📥 Loading

Pick the `.ko` that matches the device kernel, e.g. a 5.15 device uses `susfs_guard_lkm-android13-5.15.ko`.

**📱 KernelSU devices**:

```sh
ksud insmod /data/adb/loader/susfs_guard_lkm.ko
```

**🧰 Non-KernelSU devices**: use the `susfs_insmod` loader shipped with the release:

```sh
susfs_insmod /data/adb/loader/susfs_guard_lkm.ko
```

🧹 Uninstall:

```sh
rmmod susfs_guard_lkm
```

## 🧩 Flashing as a KernelSU module

To skip picking the `.ko` by hand, each release also ships a packaged module `susfs_guard_lkm-module-<tag>-arm64.zip`: flash it in a KernelSU manager and reboot. The module picks the `.ko` for the device kernel (`/proc/sys/kernel/osrelease`), loads it with `expose_proc=1`, applies no susfs rules, and skips if susfs is already present. arm64 and KernelSU-compatible managers only. `expose_proc=1` creates the `/proc/susfs_*` control nodes; sus rules are applied through them. To remove it: disable or remove the module in the KSU manager (takes effect after a reboot), or `rmmod susfs_guard_lkm` to unload it live.

💡 To choose a KMI manually, edit the module's `kmi_override` and set it to the desired KMI.

Loading normally runs in the module's `post-fs-data` stage. To opt into loading from the earlier common `post-fs-data.d` stage, run as root:

```sh
touch /data/adb/modules/susfs_guard_lkm/early_load
```

Reboot to apply. Remove that file and reboot to return to normal loading; upgrades preserve it. Both stages use the same loader, skip an existing SUSFS interface, and respect safe mode and disabled/removed modules. Uninstall removes only this module's common dispatcher; it does not unload the running LKM.

Use early loading when SUSFS rules or original kstat snapshots must be configured before module post-fs-data scripts and mounts. It makes the interface available sooner, so those operations can run at the required boot stage. If the manager applies SUSFS settings before common scripts, an earlier loading stage is still required. The module only loads the LKM; the manager applies settings.

## 📚 Interface docs

All `/proc` control nodes: see **[PROC_INTERFACES.en.md](PROC_INTERFACES.en.md)**.


## ⚖️ License

GPL-2.0, see [LICENSE](LICENSE). 

Ported from: [susfs4ksu](https://gitlab.com/simonpunk/susfs4ksu) (feature logic and hook points)

[SukiSU-Ultra](https://github.com/SukiSU-Ultra/SukiSU-Ultra) (`patch_memory` / `lsm_hook` / `symbol_resolver`)

[KallRecon](https://github.com/Dere3046/KallRecon)  

## ⚠️ Disclaimer

For technical research and study only. Use at your own risk.
