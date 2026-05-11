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
./build/kaleidoscope < examples/amortization.kal
./build/kaleidoscope --jit < examples/cubic_root.kal
```

- **默认模式**：在退出前生成机器相关的目标文件（便于和别的代码链接成最终程序）。  
- **`--jit` 模式**：在进程里直接跑顶层表达式，适合快速试算。  
- **`make run-aot`**：默认用 `examples/amortization.kal`（内含 `entry()`）；先编译 Kaleidoscope 源码为 `build/output.o` 再与宿主运行时链接运行。

**`examples/`** 三个程序，各自是一条完整主线（不是小函数拼盘）：`amortization.kal`（按揭余额、加息档、二分最低月供、多笔参数对比 + `entry()`）、`cubic_root.kal`（方程 `x^3 - 2x - 5 = 0`：二分、阻尼牛顿、全牛顿、混合求根并验残差）、`logistic_bifurcation.kal`（有界 Logistic 长迭代 + 双参数平面扫描 + 尾迹差分）。

`./build/kaleidoscope --help` 可查看简短说明。

## 这个 mini lang 有什么能力

- **强类型**：每个函数形参都必须写 `:类型`（`int`、`bool`、`double`）。宿主 `extern` 同样（如 `extern printd(x:double)`）。若全部形参都是整型类，则返回值按整型推断、体内用 i32；只要有一个 `double` 形参，则整条函数仍是 double 语义。
- **函数定义与调用**：`def`、`extern`；调用时按形参类型做必要的拓宽/收窄。
- **算术**：`+`、`-`、`*`、`<`，以及 IEEE 浮点 `/`（`double` 路径为 `fdiv`，整型路径为 `sdiv`）。
- **控制流**：`if/then/else`，`for/in`，`while/in`，以及 `break` / `continue`。
- **局部变量**：只允许「声明 + 初始化」一体：`var name:type = expr`（可多条，逗号分隔），**不支持**单独的未初始化声明。
- **自定义运算符**：`unary -(x:double)` / `binary +(a:double, b:double)` —— 运算分量目前必须为 `double`。
- **字面量**：`true`/`false` 在无小数点的整数语境下按整型；整函数体内禁止带小数点的浮点字面量（须写整数字面量）。
- **两种执行方式**：既可以 JIT 直接求值，也可以编译为目标文件再链接执行。

一个很小的例子（声明宿主打印函数 + 定义函数）：

```text
extern printd(x:double);
def add(a:double b:double) a + b;
printd(add(3, 4));
var n:int = 10 in printd(add(n, 2));
```
（与 `examples/` 中文件一致：每个形参写 `:类型`，`var` 必须 `名:类型 = 初值`。）

语言能力整体与 LLVM 官方 Kaleidoscope 教学路线一致，可在 [教程目录](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html) 中对照阅读。

## 延伸阅读

- [LLVM Kaleidoscope 教程](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)  
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html)
