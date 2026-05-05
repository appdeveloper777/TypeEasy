(module
  (memory (export "memory") 4)

  (func (export "invert_rgba") (param $ptr i32) (param $len i32)
    (local $i i32)
    (local $a i32)
    (local.set $i (i32.const 0))
    (block $done
      (loop $loop
        (br_if $done (i32.ge_u (local.get $i) (local.get $len)))

        (local.set $a (i32.add (local.get $ptr) (local.get $i)))
        (i32.store8 (local.get $a)
          (i32.sub (i32.const 255) (i32.load8_u (local.get $a))))

        (local.set $a (i32.add (i32.add (local.get $ptr) (local.get $i)) (i32.const 1)))
        (i32.store8 (local.get $a)
          (i32.sub (i32.const 255) (i32.load8_u (local.get $a))))

        (local.set $a (i32.add (i32.add (local.get $ptr) (local.get $i)) (i32.const 2)))
        (i32.store8 (local.get $a)
          (i32.sub (i32.const 255) (i32.load8_u (local.get $a))))

        (local.set $i (i32.add (local.get $i) (i32.const 4)))
        (br $loop)
      )
    )
  )
)
