# Konata 管线可视化

ARM CPU 仿真器的交互式管线时间线查看器，纯前端实现，无需后端服务。

## 使用方法

1. 用浏览器打开 `index.html`
2. 点击 **"Load JSON File"** 上传仿真器输出的 JSON 文件
3. 管线视图自动渲染

## 文件结构

```
viz_server/
├── index.html          # 主页面，文件上传入口
├── style.css           # 页面样式
├── pipeline_view.js    # Konata 管线视图控制器（Canvas 渲染）
├── app_static.js       # 文件加载、指标面板、状态管理
├── viz_demo_data.json  # 演示数据
└── konata/             # Konata 渲染器核心
    ├── konata_renderer.js  # 渲染器主入口
    ├── op.js               # 指令操作（Instruction Op）模型
    └── stage.js            # 管线阶段（Stage）模型
```

## 管线阶段

每条指令在以下阶段之间流转：

```
Fetch → Decode → Rename → Dispatch → Issue → Execute → Commit
```

## 功能

- **缩放与平移** — 鼠标滚轮缩放时间轴，拖拽平移
- **指令搜索** — 搜索特定指令或地址
- **性能指标** — 页面顶部显示 IPC、CPI、缓存命中率等摘要
- **颜色编码** — 不同指令类型（ALU、Load/Store、分支、浮点）使用不同颜色
