# Vendored libraries

Copied unchanged from the npm registry (`npm pack`, integrity checked by npm),
only the `sourceMappingURL` comment removed. Not edited by hand.

| File | Package | Source file | npm integrity |
|---|---|---|---|
| preact.mjs | preact 11.2.0 | dist/preact.mjs | sha512-/HfHI/7N84e6GrWBJ3J280z3mLaRJiDcSvj/A+y/4AbLnND/1UlGHe1sR6oYuTGwId0nP7dqDU9OnLxMPZ7xBg== |
| hooks.mjs | preact 11.2.0 | hooks/dist/hooks.mjs | (same package) |
| htm.mjs | htm 3.1.1 | dist/htm.module.js | sha512-983Vyg8NwUE7JkZ6NmOqpCZ+sh1bKv2iYTlUkzlWmA5JD2acKoxd4KVxbMmxX/85mtfdnDmTFoNKcg5DGAvxNQ== |

After an update, raise the version in `components/webui/sbom_preact.yml` or `sbom_htm.yml`
too (SBOM, see docs/technical.md).

Licenses: LICENSE-preact (MIT), LICENSE-htm (Apache-2.0). `hooks.mjs` imports the bare
specifier `preact`, resolved by the import map in `index.html`.
