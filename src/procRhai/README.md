# procRhai

`procRhai` is a minimal C++/Rust demo:

- C++ app loads a Rust dynamic library at runtime.
- Rust embeds Rhai, parses JSON5 config, compiles script once, and updates `ball.y` each tick.

## Build Rust `animvm` cdylib

From repo root:

```bash
cd src/procRhai/animvm
cargo build --release
```

Output library names:

- Linux: `libanimvm.so`
- macOS: `libanimvm.dylib`
- Windows: `animvm.dll`

## Build C++ app

From repo root:

```bash
cmake --build build --target procRhai
```

On Windows:

```bat
build.bat
```

## Run

Place/copy the Rust library next to the `procRhai` executable, or pass the library path:

```bash
build/procRhai ./src/procRhai/animvm/target/release/libanimvm.so
```

Windows example:

```bat
build-Release\procRhai.exe src\procRhai\animvm\target\release\animvm.dll
```
