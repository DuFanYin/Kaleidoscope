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
./build/kaleidoscope < examples/pricing.kal
./build/kaleidoscope --jit < examples/credit_policy.kal
```

- **默认模式**：在退出前生成机器相关的目标文件（便于和别的代码链接成最终程序）。  
- **`--jit` 模式**：在进程里直接跑顶层表达式，适合快速试算。  
- **`make run-aot`**：默认用 `examples/pricing.kal`（内含 `entry()`）；先编译 Kaleidoscope 源码为 `build/output.o` 再与宿主运行时链接运行。

**`examples/`** 里的小场景脚本（不是靠炫语法堆砌，而是为了一件事算到底）：阶梯计费（`pricing.kal`）、信贷策略（`credit_policy.kal`）、产线巡检 + 日历（`factory_calendar.kal`）、定投复利与达标月数（`savings_plan.kal`）。

`./build/kaleidoscope --help` 可查看简短说明。

## 这个 mini lang 有什么能力

- **数值计算**：核心数值类型是 `double`，适合表达式计算与函数式组合。
- **函数定义与调用**：支持 `def` 定义函数、按参数调用、通过 `extern` 声明宿主函数。
- **控制流**：`if/then/else`，`for/in`，`while/in`，以及 `break` / `continue`。
- **变量与作用域**：支持 `var ... in ...` 局部变量绑定，以及赋值操作。
- **自定义运算符**：可定义 `unary` / `binary` 运算符并设置二元运算符优先级。
- **布尔字面量**：支持 `true` / `false`（在语言里映射为 1.0 / 0.0）。
- **两种执行方式**：既可以 JIT 直接求值，也可以编译为目标文件再链接执行。

一个很小的例子（声明宿主打印函数 + 定义函数）：

```text
extern printd(x);
def add(a b) a + b;
printd(add(3, 4));
```

语言能力整体与 LLVM 官方 Kaleidoscope 教学路线一致，可在 [教程目录](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html) 中对照阅读。

## 延伸阅读

- [LLVM Kaleidoscope 教程](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)  
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html)
