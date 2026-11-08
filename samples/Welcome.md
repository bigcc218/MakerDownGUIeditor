# 视觉写作，从此简单

MDGE Studio 把 Markdown 的秩序感，变成可以直接书写与拖动的画布。 你**不需要记住语法**，只需要专注于表达。

## 拖一拖，结构就出现了

在左侧选择“项目列表”或“编号列表”，再拖动排序。想了解 Markdown，可以访问 [CommonMark 规范](https://commonmark.org/)。

- 从左侧拖入内容块，放到画布中的任意位置
- 拖动行首把手，调整段落顺序
- 把图片文件直接拖进画布，即刻完成插图

1. 在画布中直接输入文字，Enter 拆分段落
2. 按 Ctrl+S 保存项目，右上角可以导出 Markdown

> 少一点格式负担，多一点表达本身。

## 只用描述，就能生成 Markdown

导出的文件是标准 `.md` 文档，而项目本身保存在自有的 `.mdge` 容器中。

```cpp
mdge::Document doc = mdge::MakeStarterDocument();
doc.title = L"我的下一篇文档";

if (!mdge::SaveProjectFile(L"story.mdge", doc, error)) {
    return 1;
}
```

### 项目图标支持绝对路径与相对路径

![当前项目使用相对路径载入图标](assets/mdge-mark.png)

---

### 准备好开始了吗

按 Ctrl+N 新建项目，或直接修改这篇示例。所有操作都可以撤销，记得在离开前保存。
