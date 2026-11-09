# SUSFS Guard LKM — 项目展示站

基于 [`supervooc/susfs4ksu-lkm`](https://github.com/supervooc/susfs4ksu-lkm) 制作的独立静态展示站，面向 Android GKI / KernelSU 用户。

- 线上地址：<https://supervooc.github.io/susfs4ksu-lkm-site/>
- 上游项目：<https://github.com/inforcqb/susfs4ksu-for-lkm>
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

仓库使用 GitHub Pages 从 `main` 分支根目录发布。推送到 `main` 后，Pages 会自动重新构建。

## 说明

本站是独立展示页，不代表上游官方。模块源码与二进制遵循 GPL-2.0-only；站点只链接上游 Release，不镜像或重打包二进制文件。
