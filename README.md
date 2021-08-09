# vvmtool
## Inter Quake Model & VVM Exporter

This is the exporter for our extended model format (**VVM**).

It can export both **standard IQM files** and our **extended format called VVM** for use in **FTEQW**.

It's based off Lee Salzman's iqm tool.

**These days, you can find an upgraded version of this tool that supports loads more formats in the
[FTE SRC](https://sourceforge.net/p/fteqw/code/HEAD/tree/), however links against more dependencies**

*For those reasons, we are preserving our version of the original, extended tool here. This is the version that shipped with The Wastes' SDK in 2018.*

It will be updated if we make changes to our format, those will also be pushed upstream to FTE's general purpose model exporter tool.

# Features

Features that the original IQM exporter did not offer:

* Support for external configuration files for IQM generation (ala studiomdl)
* Pre-processor for rotating single or all input files
* Pre-processor for translating/repositioning single or all input files
* Pre-processor for renaming bones inside a single or all input files
* Pre-processor for material prefixes inside a single or all input files

Some features that our extended specification (VVM) has to offer:

* Support for hitmeshes for faster, content-aware collision detection
* Automatic hitmesh generation
* Submodels that allow showing/hiding specific groups of the model via the game-logic
* Support for frame triggered events that can be read by the game-logic (model-events)
* Level-of-detail range flag for references/sub-models
* User-defined surface and contentflags per reference/sub-model

**NOTE: Using any of the VVM features is not supported in any engine other than FTEQW**

The tool's output will let you know if you're outputting an IQM compatible file or not.

# Building

Make sure you have a C++ compiler installed and issue:

```make```

There's a Makefile for mingw cross-compilation as well.

# Usage

Please read the `vvm.txt` file included in this repository for more information.

# Contributing

If you're looking for bug-fixes to the existing tool, stay here and post issues
and send pull requests if you happen to find bugs!

If you want more specialized features, support for more output formats and the like
those should be handled by the tool in [FTE SRC](https://sourceforge.net/p/fteqw/code/HEAD/tree/) as that
specializes in that sort of thing.

Any bugs that get fixed here will be checked against the above tool as well.

# History
In 2016 when we had our first prototype of The Wastes, we started out using DPM for
toolchain related reasons and quickly migrated to IQM. However we needed a better
system for handling reproducible output files. The original tool only handled
compilation via plain command-line parameters which was not good enough.

The input command file (.qc) syntax is obviously inspired by qdata and other tools
such as studiomdl. So if you're familar with those tools you know exactly what to expect.

We then went on and designed extensions that we needed to make the game work,
for example a generic model-events system that'd call events in the game-logic
when a certain keyframe in the model is displayed.

More complicated things were hitmeshes, for more accurate hit detection of body
parts as well as submodels that made the action of shooting body-parts off possible.

We developed this together with David of FTEQW, since this had to be developed
in conjunction with the FTEQW builtins that'd later be exposed to the game-logic.

However, the initial extensions to the IQM format in FTE were designed by us; and this
is the exporter that was used to make our game.

# Notes
Extra resources, such as the pre-release files we shared on inside3d.com:

* [vvm docs](https://www.vera-visions.com/dev/vvm-docs-1.zip)
* [vvm example model sources](https://www.vera-visions.com/dev/vvm_example.zip)
* [July 2017: old sources dated](https://www.vera-visions.com/dev/vvm-src-1.zip)
* [July 2017: old win32 binary](https://www.vera-visions.com/dev/vvm-w32-1.zip)

# LICENSE

Copyright (c) 2010-2016 Lee Salzman

Copyright (c) 2016-2017 Vera Visions

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
