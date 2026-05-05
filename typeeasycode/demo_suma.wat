(module
  (import "env" "print_i32" (func $print_i32 (param i32)))
  (import "env" "print_i32_ln" (func $print_i32_ln (param i32)))
  (func $main
    (local $x i32)
    (local $y i32)
    i32.const 5
    local.set $x
    i32.const 7
    local.set $y
    local.get $x
    local.get $y
    i32.const 1
    i32.add
    i32.add
    call $print_i32_ln
  )
  (export "main" (func $main))
)
