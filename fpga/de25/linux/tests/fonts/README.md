# Variable-font test fixture

`STIXTwoText.ttf` was copied unchanged from a macOS font installation. Its
metadata identifies STIX Two Text 2.13 b171. It contains a
default design and named Regular, Medium, SemiBold, and Bold instances; Regular
has the default coordinates, so Astra imports four distinct AFNT faces.

Copyright 2001–2021 The STIX Fonts Project Authors. Distributed under the SIL
Open Font License 1.1 in [OFL.txt](OFL.txt), matching the
[upstream license](https://github.com/stipub/stixfonts/blob/master/OFL.txt).
This file is a test input only;
it is not installed in the Astra system image.

`SourceSans3-Regular.otf` is Adobe Source Sans 3, copied unchanged from the
2026-10-15 conversion spike (SHA-256
`08df266400933d3178d081a45f94a08814c3e55b4b7dd2e0ff69cb1329f13ab6`).
Copyright 2010-2024 Adobe; Reserved Font Name `Source`. Its SIL Open Font
License 1.1 is in [SourceSans3-LICENSE.md](SourceSans3-LICENSE.md). It is a
test input only, never installed in the Astra system image.

`AtkinsonColor-test.ttf` is a test-only derivative of the included Atkinson
Hyperlegible Next Regular TTF. FontTools added one COLR/CPAL layer for `A`;
all other source glyphs came from the original. Its SIL Open Font License 1.1
is at `sw/userspace/graphics/fonts/atkinson-hyperlegible-next/OFL.txt`. The
native importer must reject it rather than silently discard color.
