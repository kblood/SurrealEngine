# SurrealVideo

This project is a fork of ffmpeg that strips down the codebase to only contain the
indeo5 codec used by Klingon Honor Guard.

As a fork of ffmpeg, SurrealVideo is licensed under [GNU Lesser General Public License (LGPL) version 2.1](http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html) or later.

Please note that while SurrealEngine itself is using a ZLIB style license (along
some BSD/MIT licenses for other dependencies), anything within this folder falls
under the LGPL.

Native SurrealEngine builds link against the SurrealVideo dynamic library.
The Emscripten browser build instead compiles these sources into the WebAssembly
executable. Browser release packages therefore include or identify the exact
complete source tree and whole-program rebuild instructions; see
`Docs/BROWSER_STATIC_RELINKING.md`. The final distribution mechanism and terms
must still receive human/legal review.

SurrealEngine could have linked against FFmpeg as well, but that library is
literally a 100 MB binary. SurrealVideo is about 100 KB.

We only need ffmpeg to play that one video codec.
