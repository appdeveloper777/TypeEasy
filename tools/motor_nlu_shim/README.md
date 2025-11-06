Rust NLU shim (motor_nlu_shim)

This crate builds a dynamic library exposing a simple C ABI so the TypeEasy runtime
can call `parse(const char* input)` and obtain a C string with the NLU result.

Build and produce a shared library (Linux example):

```bash
cd tools/motor_nlu_shim
cargo build --release
# The built library will be in target/release, e.g. libmotor_nlu_shim.so
```

Notes:
- The shim forwards requests to the HTTP NLU adapter at http://nlu:5000/parse.
- The runtime must load the resulting shared library (name/path depends on platform).
- The runtime must call `free_string` after using the returned string to avoid leaks.
