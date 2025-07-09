#pragma once

#include "resource.h"

// Change include paths as required

//
// DirectX9 sender/receiver functions
//
// SpoutDirectX9.h includes "SpoutDX9shaders.hpp" if it exists in the same folder
// and this activates a shader copy function in "CreateSharedDX9Texture".
// If the sender texture format is not DXGI_FORMAT_B8G8R8A8_UNORM the received
// texture is copied to a compatible BGRA texture and the share handle to that
// is used to create the DirectX9 texture.
//
#include "..\Source\SpoutDX9\SpoutDirectX9.h"

// Sender functions
#include "..\Source\SpoutSDK\SpoutSenderNames.h" // sender creation and update
#include "..\Source\SpoutSDK\SpoutFrameCount.h" // mutex lock and new frame signal
#include "..\Source\SpoutSDK\SpoutUtils.h" // logging utilites

// DirectX11 functions for GetCopyPixels
#include "..\Source\SpoutSDK\SpoutDirectX.h"
#include "..\Source\SpoutSDK\SpoutCopy.h"
