# Lua, vendored

Upstream: <https://www.lua.org/ftp/lua-5.5.1.tar.gz>
Version: **5.5.1**
SHA-256: `1c4b4068d67061f2a2231ad2b5422e77acea1487ea9890f6320af614f4373dce`
Vendored: 2026-08-24

The official release is present unmodified. Astra compiles the ISO C target;
platform paths and the interpreter entry-point name are supplied as compiler
definitions in `sw/userspace/commands/Makefile`. The native wrapper remains
outside this tree, so upgrading Lua is a clean replacement rather than a
source merge.

Lua is distributed under the MIT license in `doc/readme.html` and the source
file notices.
