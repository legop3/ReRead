# Local evidence

Place your own stock disk backup and hardware photos here. Everything in this directory except this README is excluded from version control.

The existing build expects `evidence/reader.img`: a complete disk image from the Reader examined in this project, with a size of **4,004,388,864 bytes**. Its recorded SHA-256 is:

```text
7caaba050df8ca886834de5cab8ee451e9b4c41c27d708d45c876d7df8b38d51
```

That hash identifies this owner's backup; another Reader's image can differ. The extraction helper assumes the partition geometry documented in [RESEARCH.md](../RESEARCH.md#image-and-extraction-record). Check your own image before using those assumptions.

Build inputs derived from the dump live in `analysis/stock-*`, `analysis/partitions/`, and `build/stock-initrd/`. They also remain local. The extraction helper is [image-builder/prepare-stock.py](../image-builder/prepare-stock.py), run inside the builder with the workspace at `/work`.
