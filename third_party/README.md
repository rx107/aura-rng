# Dear ImGui

Vendored from https://github.com/ocornut/imgui at **v1.93.0**,
commit `c1aaac65e896a45f81f47a4d68f9959e0f86c859`.

Only the core library, Win32 backend and DirectX 11 backend are compiled.
The upstream MIT license is preserved in `imgui/LICENSE.txt` and included in binary packages.
No build-time download or system installation is required.

Japanese text uses a font already installed in Windows (Yu Gothic, MS Gothic or Meiryo).
Windows fonts are not redistributed. If no Japanese font is available, the debug panel reports it.

## cgltf

Vendored from https://github.com/jkuhlmann/cgltf at commit
`85cd62382dfea638278962690cf515023f33ed00`. The MIT license is preserved
in `cgltf/LICENSE` and included in binary packages. Used for GLB/accessor parsing.

## nlohmann/json

Vendored from https://github.com/nlohmann/json at **v3.12.0**.
The MIT license is preserved in `nlohmann/LICENSE.MIT` and included in binary packages.
Used to read VRM extension metadata. No dependency downloads occur during builds.

VRM models and audio tracks are selected locally by the player. They are not
bundled with the program or uploaded by it. `tests/make_vrm_fixture.py` generates
an original four-vertex test card with a two-color texture for automated tests.
