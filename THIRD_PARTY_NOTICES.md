# Third-party notices

This project builds on the following components:

1. **Luau GDExtension** by Fern Forest Games Ltd — MIT License. The upstream
   `LICENSE` file must be included with distributed copies.
2. **Luau** by Roblox Corporation and contributors — MIT License. Preserve the
   pinned Luau source distribution's `LICENSE.txt` and `lua_LICENSE.txt`.
3. **godot-cpp** and **Godot Engine** — MIT License. Preserve the license files
   shipped with the exact source and engine builds used for distribution.

The CMake build fetches Luau and godot-cpp rather than vendoring them into this
repository. After configuration, run:

```bash
python3 hardening/scripts/collect_licenses.py --repo .
```

The command copies the license texts found in the configured dependency trees
into `dist/licenses/`. Review that directory before every commercial release.
This notice supplements, and does not replace, the complete license texts.
