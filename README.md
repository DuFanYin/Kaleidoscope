# Kaleidoscope

个人学习用的 **Kaleidoscope** 小型编译器，跟 LLVM 官方教程 [*My First Language Frontend*](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html) 走。它把文本程序变成 LLVM 能用的中间表示，再交给 LLVM 做优化和代码生成。

你可以把它当作「从零搭一条语言前端」的示例：有词法、语法树、IR，也支持教程里常见的那种**即时执行**和**生成目标文件**两种玩法。教程后半关于调试信息的一章这里没有做。

## 需要什么

本机装好 **LLVM**（要让 `llvm-config` 在终端里能直接跑）。macOS 上常用 Homebrew 安装 LLVM，并把它的 `bin` 加到 `PATH`。

## 怎么编译这个仓库

在项目根目录执行：

```bash
make
```

得到可执行文件 `build/kaleidoscope`。若系统找不到 LLVM，安装或配置好 `PATH` 后重试，或通过 `make LLVM_CONFIG=...` 指定本机的 `llvm-config`。

## 怎么用手里的编译器

程序从**标准输入**读入一整段 Kaleidoscope 源码，读到 **Ctrl+D** 结束。非交互时可以：

```bash
./build/kaleidoscope < examples/sample.kal
```

- **默认模式**：在退出前生成机器相关的目标文件（便于和别的代码链接成最终程序）。  
- **`--jit` 模式**：在进程里直接跑顶层表达式，适合快速试算。  
- **`make run-aot`**：仓库自带一条「先编译再链接再运行」的演示，用来在非 JIT 下看到数值结果（需在源码里提供约定的入口函数；默认试跑用的源码也一并附上）。

`./build/kaleidoscope --help` 可查看简短说明。

## 语言本身

Kaleidoscope 的语法与语义以官方教程为准：函数定义、运算符、控制流、`var` 等教程里逐步加上的内容，在这里按同一套教学路线实现。细节与章节对应关系可直接读 [教程目录](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)。

## 延伸阅读

- [LLVM Kaleidoscope 教程](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)  
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html)
