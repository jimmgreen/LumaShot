# LumaText binary dependency

Updated 2026-09-22 from the sibling LumaText out/sdk/Release SDK: optimized x64 Release with static MSVC runtime (/MT). Matching bin, lib, include, licenses and manifest.json are vendored together.

DLL SHA256: 1EAE7113B138FB26C9D47234D954CB53DABFD1FE0824EB10B41F831781D10535
DLL size: 1,648,128 bytes.

Application CMake builds consume this precompiled SDK only. The historical ../lumatext-source snapshot is unchanged and is not part of the application build. scripts/build-lumatext.bat is an optional upstream build helper; it does not promote SDK outputs automatically.

The application retains gamma 0.85 and contrast 1. Known-background rendering and optical compensation remain disabled. Other components still require the packaged MSVC runtime DLLs.
