/*
 * Copyright (C) 2022 Patrick Mours
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "hook_manager.hpp"

#define ID3D10Resource_GetDevice reshade::hooks::call_vtable<3, HRESULT, ID3D10Resource, ID3D10Device **>

#define ID3D10Buffer_Map(p, a, b, c) (p)->Map(a, b, c)
#define ID3D10Buffer_Unmap(p) (p)->Unmap()

#define ID3D10Texture1D_Map(p, a, b, c, d) (p)->Map(a, b, c, d)
#define ID3D10Texture1D_Unmap(p, a) (p)->Unmap(a)

#define ID3D10Texture2D_Map(p, a, b, c, d) (p)->Map(a, b, c, d)
#define ID3D10Texture2D_Unmap(p, a) (p)->Unmap(a)

#define ID3D10Texture3D_Map(p, a, b, c, d) (p)->Map(a, b, c, d)
#define ID3D10Texture3D_Unmap(p, a) (p)->Unmap(a)
