# Core-engine patches

This module is (deliberately) shipped without the AzerothCore engine source
tree -- it's ~600MB and not something this repo tracks or should track.
Almost everything Living Gear needs lives entirely in `modules/mod-living-gear/`
and gets picked up by the normal module build, no core changes required.

The one exception on record is here as a small, self-contained patch file
instead of a tracked copy of the whole engine file it touches. Named
`.core-patch` rather than `.patch`/`.diff` only because this repo's
`.gitignore` excludes those two extensions (unrelated scratch-diff
files elsewhere) -- it's an ordinary unified diff, `git apply` doesn't
care about the extension.

## Applying a patch

From the root of your own AzerothCore checkout:

```
git apply --directory=. path/to/this/repo/modules/mod-living-gear/core-patches/0001-shadow-clone-nameplate.core-patch
```

or by hand -- each patch's header explains exactly what it does and why;
they're short enough to eyeball and apply manually if `git apply` doesn't
like your tree's exact line numbers.

## Patches

- **0001-shadow-clone-nameplate.core-patch** -- `QueryHandler.cpp`. Makes the
  Living Gear Rogue Subtlety "Shadow Clone" pet show its owner's real
  character name instead of the generic "Shadow Clone" name every
  instance would otherwise share (the query is keyed by creature
  *template* entry, not GUID). Purely cosmetic -- skip it if you don't
  care about that nameplate, the rest of the module works fine without it.
