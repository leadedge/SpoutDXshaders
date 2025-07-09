#pragma once

#include "resource.h"

// DirectX11 sender/receiver functions
// Include first to for SpoutDXshaders.hpp
// #include "..\..\Source\SpoutDX11\SpoutDX.h"

// Include spoutDXshaders if the file exists
// In this case, the same folder as SpoutDX.cpp
#if __has_include("..\Source\SpoutDX11\SpoutDXshaders.hpp")
	#include "..\Source\SpoutDX11\SpoutDXshaders.hpp"
	spoutDXshaders shaders; // Shader class
#endif

#include "..\Source\SpoutDX11\SpoutDX.h"

