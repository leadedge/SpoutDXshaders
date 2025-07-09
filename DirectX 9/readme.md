## DirectX 9

WinSpoutDirectX9 uses the "SpoutDirectX9" class using lower level functions directly.
The shader file, SpoutDX9shaders.hpp, is included by SpoutDirectX9.h. 

DirectX 9 receiver applications normally require a sender shared
texture of format DXGI_FORMAT_B8G8R8A8_UNORM.

The "CreateSharedDX9Texture" function in SpoutDirectX9.h, includes
a shader copy function "CopyDX11shareHandle" which enables formats 
other than DXGI_FORMAT_B8G8R8A8_UNORM to be be received.

The sender texture is copied to a compatible BGRA texture and the 
share handle to that is returned instead of from the original texture.

The demonstration sender allows the texture format to be selected for testing.
   
## Compute shader performance for DirectX 9

The execution time of the copy compute shader has been tested using a NVIDA Geforce 3060
at 0.03 - 1.0 milliseconds. Tests with textures up to 7680x1440 and with 100% GPU usage,
show no increase but rather a reduction as the GPU clock speed increases under load.

It remains possible that execution time could be affected by other applications
that use pixel shaders which take priority over compute shaders in high load situations. 
As a precaution, the DirectX 9 copy shader includes monitoring of GPU execution time
and system GPU usage. If the shader time exceeds 4 milliseconds, it is not executed again
until the system GPU usage reduces by 20 percent of the peak value when this occurs.
Shader execution time, GPU usage and recovery details are shown on the example display 
after approximately 8 seconds delay to stablise timing.

For testing, system GPU load can be increased with [Geeks 3D GPUTest](https://www.geeks3d.com/gputest/).
Use with caution because some high load tests can cause system shut-down or 
overheating of the GPU and potential damage. It is best to [monitor GPU load](https://www.guru3d.com/download/msi-afterburner-beta-download/)
at the same time and set temperature limits for safety.




