# Third-party notices

Witcher 3 Remastered VR, a continuation of Witcher 3 VR, contains or is derived
from third-party software. The project's MIT license applies only to the
project's original code. Third-party components remain governed by their
respective terms.

The dependency setup script retrieves exact upstream revisions. Dependency
source trees are not committed to this repository and must not be treated as
being relicensed by this project.

## MIT-licensed components

The following components are incorporated or adapted under the MIT License:

- **DLSSTweaks**
  - Project: https://github.com/emoose/DLSSTweaks
  - Reference commit used for the ForceDLAA approach:
    `1d2fddbe3d1e8f403f795afc17e7db239a83f6a2`
  - Copyright (c) 2023 emoose
  - License: MIT
- **AMD FidelityFX SDK API headers** (`external/fidelityfx-api`: `ffx_api.h`,
  `ffx_api_types.h`, `ffx_upscale.h`, vendored unchanged)
  - Project: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
  - Pinned revision: `60f4ea81909200d8542eca14dccb2628b763a9a3` (tag `v2.3.0`)
  - Copyright (C) 2026 Advanced Micro Devices, Inc.
  - License: MIT (listed as such in the SDK's `Kits/FidelityFX/docs/license.md`)
  - Only the API declarations are used. The FidelityFX runtime DLLs are the
    game's own and are neither modified nor distributed.
- **Microsoft DirectX-Headers**
  - Project: https://github.com/microsoft/DirectX-Headers
  - Pinned revision: `2c305c16da8a4450db8d7f1e7d8d014c8bc665ee`
  - Copyright (c) Microsoft Corporation
  - License: MIT
- **Khronos OpenXR headers**
  - Project: https://github.com/KhronosGroup/OpenXR-SDK
  - Pinned revision: `5267613edf3d937e3d77556a106a65c2f82b25c6`
  - Copyright 2017-2026 The Khronos Group Inc.
  - Header SPDX license: `Apache-2.0 OR MIT`
  - This project relies on the MIT option for the generated OpenXR headers.
- **Khronos OpenXR Loader 1.0.22**
  - Project: https://github.com/KhronosGroup/OpenXR-SDK
  - Pinned revision: `458984d7f59d1ae6dc1b597d94b02e4f7132eaba`
  - Copyright (c) 2017-2022, The Khronos Group Inc.
  - Copyright (c) 2017-2019 Valve Corporation
  - Copyright (c) 2017-2019 LunarG, Inc.
  - Copyright (c) 2019-2021 Collabora, Ltd.
  - Loader SPDX license: `Apache-2.0 OR MIT`
  - The release packages include the 64-bit `openxr_loader.dll`, built from
    this revision and used under the MIT option. The loader statically includes
    JsonCpp, which is listed below.

The MIT License text applying to the components above is reproduced here:

> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES, OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT, OR OTHERWISE, ARISING FROM,
> OUT OF, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## JsonCpp (part of `openxr_loader.dll`)

- Project: https://github.com/open-source-parsers/jsoncpp (vendored in the
  OpenXR SDK at the loader revision above)

Baptiste Lepilleur and The JsonCpp Authors explicitly disclaim copyright in all
jurisdictions which recognize such a disclaimer. In such jurisdictions,
this software is released into the Public Domain.

In jurisdictions which do not recognize Public Domain property (e.g. Germany as of
2010), this software is Copyright (c) 2007-2010 by Baptiste Lepilleur and
The JsonCpp Authors, and is released under the terms of the MIT License (see below).

In jurisdictions which recognize Public Domain property, the user of this
software may choose to accept it either as 1) Public Domain, 2) under the
conditions of the MIT License (see below), or 3) under the terms of dual
Public Domain/MIT License conditions described here, as they choose.

Copyright (c) 2007-2010 Baptiste Lepilleur and The JsonCpp Authors

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use, copy,
modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Development references

[REFramework](https://github.com/praydog/REFramework) and
[UEVR](https://github.com/praydog/UEVR) by praydog were consulted as DX12,
hooking, OpenXR, and VR architecture references during initial development.
Their source trees are not compiled, committed, or distributed as part of
Witcher 3 VR or this project.

Early private builds obtained MinHook through REFramework's dependency tree.
The public dependency setup instead retrieves MinHook directly from its
official upstream repository under the license reproduced below.

## MinHook and Hacker Disassembler Engine

- Project: https://github.com/TsudaKageyu/minhook
- Pinned revision: `98b74f1fc12d00313d91f10450e5b3e0036175e3`

MinHook - The Minimalistic API Hooking Library for x64/x86  
Copyright (C) 2009-2017 Tsuda Kageyu.  
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.

### Hacker Disassembler Engine 32 C

Copyright (c) 2008-2009, Vyacheslav Patkov.  
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

### Hacker Disassembler Engine 64 C

Copyright (c) 2008-2009, Vyacheslav Patkov.  
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## NVIDIA NGX SDK headers

- Source repository: https://github.com/NVIDIA-RTX/Streamline
- Pinned revision: `e8aaa6eaac968711fb62473d4ae8256dde20919b`
- Governing license:
  https://github.com/NVIDIA-RTX/Streamline/blob/main/external/ngx-sdk/license.txt

The dependency setup retrieves NVIDIA NGX SDK interface headers from the
official Streamline repository. NVIDIA SDK materials are not licensed under
this project's MIT License and must not be redistributed as a standalone
SDK.

Required NVIDIA notice:

> This software contains source code provided by NVIDIA Corporation.

No NVIDIA game DLL or standalone SDK package is distributed by this project.
Users must rely on the DLSS/NGX runtime supplied with their legally installed
game and compatible NVIDIA driver.

The NGX headers are governed by the NVIDIA RTX SDKs License (v. March 14, 2024)
that ships in the pinned Streamline revision. They are downloaded at build time
and are never committed to this repository.

When the game runs under Proton, the mod disables one check in the game
executable's own code that turns off the game's Streamline integration under
Wine. It does not modify, bypass or redistribute any NVIDIA component.

## RenderDoc in-application API

- Source repository: https://github.com/baldurk/renderdoc
- Pinned revision: `e43b7c14d3c37fab664391db94f7586be29e49a0`
- Vendored file: `renderdoc/api/app/renderdoc_app.h`
- Copyright (c) 2015-2026 Baldur Karlsson

The RenderDoc API header is distributed under the MIT license included in the
header. It is used for optional frame-capture support. The RenderDoc runtime
addon of the original release is not included (see below).

## WiVRn (headless test runtime only)

- Project: https://github.com/WiVRn/WiVRn
- Reference: `server/driver/app_pacer.cpp`, WiVRn 26.9
- Copyright (C) 2025 Patrick Nicolas
- License: GPL-3.0-or-later (text in `licenses/GPL-3.0.txt`)

`tools/w3vr-nullxr/w3vr_nullxr.cpp` reproduces WiVRn's application frame pacer,
so headless tests show the same frame-rate steps as a headset. That tool is
therefore licensed under GPL-3.0-or-later. It is built separately from
`dxgi.dll`, is not linked into the mod, and is not included in release
packages. The rest of the project remains under the MIT License.

## Components of the original release that are not included

The original Witcher 3 VR release
(https://github.com/tig3rmast3r/witcher3-vr/releases) also ships these optional
components:

- OptiScaler V23244 (GPL-3.0; the OptiScaler team) with its DLSS Neural
  Rendering integration (Dagherbou's OptiScaler_DLSSNR, sadbee166's pre-DLSS
  change, tig3rmast3r's stereo build)
- the AMD FidelityFX frame-generation DLLs (MIT)
- OFXR Bridge 0.2.1 (LGPL-3.0-or-later; tig3rmast3r)
- ReShade VR 6.8.0 (BSD-3-Clause; tig3rmast3r's build of ReShade by Patrick
  Mours and contributors)
- Dear ImGui overlay components (MIT; Omar Cornut and contributors)
- PureDark's `PDAFWPlugin.dll`
- the RenderDoc runtime addon
- the Fast Transitions DLC, based on the **Next Gen Movement Input Lag Fix —
  Fumio Edition** (Nexus Mods project 7586)

Some of them are not ported to Remastered 5.x, and this project's packages
include none of them. Their credits, licenses and corresponding sources are
documented in that release.

## CD PROJEKT RED fan content

Witcher 3 Remastered VR is an unofficial fan work and is not approved or
endorsed by CD PROJEKT RED and requires a legally obtained copy of The Witcher
3: Wild Hunt. REDengine game data is not relicensed under this project's MIT
License.

Fan Content Guidelines:
https://www.cdprojektred.com/en/fan-content
