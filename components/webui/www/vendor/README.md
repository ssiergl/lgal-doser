# Vendored libraries

Copied unchanged from the npm registry (`npm pack`, integrity checked by npm),
only the `sourceMappingURL` comment removed. Not edited by hand.

| File | Package | Source file | npm integrity |
|---|---|---|---|
| preact.mjs | preact 10.29.8 | dist/preact.module.js | sha512-ej2aVZ+vZ8WO7tvlQWRM9N63A0KzF9q4mWJfDUHgYaIofWY9hu74QdnQrjoPMmZi2/nZ5gN0bJCQF49xQqx09Q== |
| hooks.mjs | preact 10.29.8 | hooks/dist/hooks.module.js | (same package) |
| htm.mjs | htm 3.1.1 | dist/htm.module.js | sha512-983Vyg8NwUE7JkZ6NmOqpCZ+sh1bKv2iYTlUkzlWmA5JD2acKoxd4KVxbMmxX/85mtfdnDmTFoNKcg5DGAvxNQ== |

Licenses: LICENSE-preact (MIT), LICENSE-htm (Apache-2.0). `hooks.mjs` imports the bare
specifier `preact`, resolved by the import map in `index.html`.
