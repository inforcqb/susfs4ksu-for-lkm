# SUSFS Guard LKM — 项目展示站

基于 [`inforcqb/susfs4ksu-for-lkm`](https://github.com/inforcqb/susfs4ksu-for-lkm) 制作的静态展示站，面向 Android GKI / KernelSU 用户。

- 线上地址：<https://inforcqb.github.io/susfs4ksu-for-lkm/>
- 项目主仓库：<https://github.com/inforcqb/susfs4ksu-for-lkm>
- 部署分支：`gh-pages`（不修改 `main` 中的模块源码）
- 当前展示版本：`v2.3.0-r18.9`（上游 Release tag `v2.3.0-r18.9-1`）

## 内容

- 项目定位与 LKM 加载方式
- KMI 兼容矩阵（8 个 GKI 变体）
- KernelSU / 独立加载器 / 模块 ZIP 三种安装路径
- `/proc/susfs_*` 接口概览
- 下载入口、常见问题与风险说明

站点为纯静态 HTML / CSS / JavaScript，无外部 CDN、无构建依赖。

## 本地预览

```sh
python -m http.server 8080
```

然后打开 <http://localhost:8080/>。

## 目录

```text
.
├── index.html          # 单页站点
├── 404.html            # 404 页面
├── assets/
│   ├── styles.css
│   ├── main.js
│   ├── favicon.svg
│   ├── og.svg
│   └── og.png
├── manifest.webmanifest
├── robots.txt
├── sitemap.xml
└── .nojekyll
```

## 部署

线上页面由 `inforcqb/susfs4ksu-for-lkm` 的独立 `gh-pages` 分支发布，`main` 中的模块源码不受影响。更新 `gh-pages` 分支后，GitHub Pages 会自动重新构建。

## 说明

站点内容基于项目的 README 与 `/proc` 接口文档整理。模块源码和二进制遵循 GPL-2.0-only；站点只链接 GitHub Release，不镜像或重打包二进制文件。
