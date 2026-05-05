const fs = require('fs');

const wasmPath = process.argv[2];

if (!wasmPath) {
  console.error('Uso: node tools/wasm_runner/run_wasm.js archivo.wasm');
  process.exit(1);
}

const bytes = fs.readFileSync(wasmPath);

WebAssembly.instantiate(bytes, {
  env: {
    print_i32: value => process.stdout.write(String(value)),
    print_i32_ln: value => console.log(value),
  },
}).then(({ instance }) => {
  if (!instance.exports.main) {
    throw new Error('El modulo Wasm no exporta main');
  }
  instance.exports.main();
}).catch(error => {
  console.error(error);
  process.exit(1);
});