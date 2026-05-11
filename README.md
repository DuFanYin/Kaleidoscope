# Kaleidoscope

A small educational compiler for the **Kaleidoscope** language from the [LLVM “My First Language Frontend” tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html). It lexes and parses input, builds an AST, and generates LLVM IR. **Two drivers:** by default it writes a native object file (`output.o`); with **`--jit`** it uses the tutorial Orc JIT to run top-level expressions in process (no `output.o` in that run).

Sources are split under [`src/`](src/) (lexer, parser, AST headers, codegen, and `main`). After each function is built, a **legacy function pass manager** runs **Mem2Reg**, then a **hand-written** [`KaleidoscopeAlgebraicSimplifyPass`](src/passes/AlgebraicSimplifyPass.cpp) ([`Passes.h`](src/passes/Passes.h)) (`x±0`, `x*1`, `x*0`, … on `double` IR), then LLVM’s **InstCombine**, **Reassociate**, **GVN**, and **CFG simplification** (tutorial Chapter 4 bundle); finally IR goes to the JIT or `output.o`.

## Prerequisites

- **LLVM** with development headers and `llvm-config` on your `PATH`  
  - **macOS (Homebrew):** `brew install llvm`  
    Then either use the Homebrew clang (`$(brew --prefix llvm)/bin/clang++`) or put LLVM’s `bin` on `PATH`, for example:
    ```bash
    export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
    ```
  - **Linux:** install `llvm-dev` / `llvm-<version>-dev` (package names vary by distro).

Verify:

```bash
llvm-config --version
make check-llvm
```

## Build

```bash
make
```

Produces **`build/kaleidoscope`** (see [`Makefile`](Makefile)). If `llvm-config` is not on your `PATH`, pass the real binary (not a placeholder):

```bash
# macOS Homebrew LLVM — adjust if your prefix differs
make LLVM_CONFIG="$(brew --prefix llvm)/bin/llvm-config"
```

Clean build artifacts:

```bash
make clean
```

## Run

The driver reads the whole program from **standard input** until EOF (one `ready>` prompt at startup). Paste your program, then press **Ctrl+D** (Unix) to finish.

```bash
./build/kaleidoscope < examples/sample.kal
```

**JIT mode** (tutorial Chapter 4–style: `addModule` / `lookup`, evaluates top-level expressions such as `4+5;`):

```bash
printf '4+5;\n' | ./build/kaleidoscope --jit
```

Or interactively:

```bash
./build/kaleidoscope
# type expressions; end with Ctrl+D
```

Use `./build/kaleidoscope --help` for options.

### Language tips

- **`def`** parameter lists use **spaces**, not commas: `def add(a b) a + b;`
- **Calls** use commas: `add(1, 2);`
- Declare runtime helpers from C before use, e.g. `extern printd(x);` (see `putchard` / `printd` in [`src/main.cpp`](src/main.cpp)).

Without **`--jit`**, the driver only generates IR and emits **`output.o`** at exit (top-level expressions are compiled but not executed). With **`--jit`**, **`src/JIT.h`** matches upstream LLVM’s Orc helper (upgrade this header when you bump LLVM); definitions are **`addModule`**’d as in the tutorial, and anonymous top-level expressions are **`lookup`**’d and called—there is **no** `output.o` on **`--jit`** runs (emit-ahead-of-time and JIT are separate pipelines in one binary).

## Repository layout

| Path | Description |
|------|-------------|
| [`src/`](src/) | Frontend sources above; [`passes/`](src/passes/) contains **custom LLVM `FunctionPass` IR transforms**; [`JIT.h`](src/JIT.h) (upstream Orc JIT helper; use **`--jit`**) |
| [`Makefile`](Makefile) | Build into `build/` using `llvm-config` |
| [`docs/`](docs/) | Personal study notes (LLVM pipeline, C++) |
| [`examples/`](examples/) | Sample inputs and unrelated small demos |

The `build/` directory (and stray legacy binaries/objects in the repo root), local tooling caches, and machine-specific files are excluded via [`.gitignore`](.gitignore).

## References

- [LLVM Kaleidoscope tutorial](https://llvm.org/docs/tutorial/MyFirstLanguageFrontend/index.html)
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html)
