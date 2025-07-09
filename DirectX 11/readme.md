# DirectX 11

## Project

WinSpoutDX11 uses the "SpoutDX" class which simplifies application code
by handling lower level functions within the class. 

## Purpose

The application demonstrates D3D11 compute shaders that can be used
by DirectX 11 receivers. A dialog provides controls for changing the image brightness,
contrast, saturation, gamma and temperature as well as options for image blur, sharpen, 
flip, mirror or swap red/green. Shaders for other effects could be added.

Shader functions are called from the main application file. The functions and HLSL shader
source are contained within "SpoutDXshaders.hpp. Compare with the equivalent OpenGL
compute shaders in the [advanced demonstration program](https://github.com/leadedge/ofSpoutDemo).





