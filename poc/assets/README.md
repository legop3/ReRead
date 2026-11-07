# Console fonts

`font-large.psf` and `font-tall.psf` derive from the kbd project's `Mik_8x16.gz` console bitmap font, supplied locally by Fedora's `kbd-misc` 2.8.0 package.

- The large font doubles each bitmap row horizontally and vertically, producing 16×32 glyphs in PSF2 format.
- The tall fallback doubles rows vertically, producing 8×32 glyphs in PSF1 format.
- Both include explicit Unicode mappings for the ASCII text used by the POC.

Upstream: [kbd project](https://kbd-project.org/) and [source tree](https://github.com/legionus/kbd). The installed package and upstream README identify its license as **GPL version 2 or later**. The accompanying [COPYING](COPYING) reproduces the supplied GPL version 2 text. This notice applies to the derived font assets; it does not select a license for the rest of this project.
