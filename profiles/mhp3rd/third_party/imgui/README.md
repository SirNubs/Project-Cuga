# Dear ImGui

The files here are copies from [Dear ImGui](https://github.com/ocornut/imgui) release `v1.92.9b`, commit `f1cc2ae15e53a861a874c3034aae6798fde194ab`, under the MIT License (see `LICENSE.txt`).

Only what the port uses is kept: the library itself (`imgui*.cpp`, `imgui*.h`, `imconfig.h` and the `imstb_*.h` headers it includes) and two backends from `backends/`, `imgui_impl_sdl3` for window events and gamepads and `imgui_impl_vulkan` for drawing. The demo, the examples and the other backends are left out.

The port's own interface under `host/ui/` is built on it: the in-game menu and the first-run setup screens.

Local patches: `imgui.cpp` formats debug texture IDs with a matching 64-bit hexadecimal format and argument type, preserving IDs above `UINT32_MAX`. The upstream copyright and license notices are unchanged.

`ImTextureData::Create()` validates positive dimensions and a byte count representable by the existing `int` size/pitch API before allocation. It allocates and clears the new buffer before publishing the texture state. This retains the upstream void API: invalid dimensions or allocation failure explicitly terminate in both Debug and Release builds, rather than returning a texture that atlas builders and preview callers would immediately dereference. Recoverable atlas allocation failure is not supported; it would require changing the atlas builder and its callers together.

Successful creation guarantees `Width * Height * BytesPerPixel <= INT_MAX`, so `GetSizeInBytes()`, `GetPitch()` and in-bounds `GetPixelsAt()` calculations remain representable. Glyph bitmap dimensions use widened subtraction and are rejected outside the positive 16-bit rectangle range. Rectangle packing rejects invalid dimensions and padding before narrowing or addition, including in Release builds. Font glyph bitmap dimensions come from a successfully packed rectangle within that texture, and copied row widths cannot exceed its width. Glyph bitmap and row-copy byte counts are computed in `size_t`; glyph bitmap sizes are explicitly checked against `INT_MAX` before rectangle packing and conversion to the scratch vector's `int` size. Yakumo also retains the default 8192-by-8192 atlas limits; mod previews are capped at 1024-by-1024 before texture creation and the game-font preview has fixed dimensions. These bounds do not validate arbitrary font file contents or callers that mutate texture fields or pass out-of-bounds coordinates.

The integer slider multiplication warning is bounded by its actual instantiations: the integer endpoint types are at most 64 bits and the linear interpolation ratio is between zero and one (the endpoints return early). Even the largest possible integer difference times this ratio is far below `FLT_MAX`. Floating-point slider types use the separate `ImLerp` branch. No slider behavior or warning suppression is changed.

Local patch: `imstb_truetype.h` checks bitmap dimensions, row strides, scaled coordinates and SDF padding before signed arithmetic or allocation. It handles failed SDF and rasterizer scratch allocations, clears packed atlases by their actual row stride, and rejects baked glyphs wider than the atlas. These independently written guards preserve Dear ImGui's existing modifications and the upstream license notices.
