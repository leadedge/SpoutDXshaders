//
//
//			spoutDXshaders.hpp
//
//		Functions to manage DirectX 11 compute shaders
//
// ====================================================================================
//		Revisions :
//
//		27.05.25	- create project
//		31.05.25	- Changed name from SpoutDX11shaders to SpoutDXshaders.
//					  Remove CPU copy function and global staging textures
//					  Add OpenDX11, CloseDX11, CreateDX11device, OpenDX11shareHandle,
//					  CreateDX11Texture and Wait to be independent of SpoutDirectX
//					  for use with SpoutDX9
//		07.06.25	- Add "__DX9__" define for include by SpoutDX9 or SpoutDirectX9
//		20.06.25	- Cleanup and test for both DX11 and DX9
//		30.06.25	- Move DirectX9 functions to a separate file SpoutDX9shaders.hpp
//
// ====================================================================================
/*

	Copyright (c) 2025. Lynn Jarvis. All rights reserved.

	Redistribution and use in source and binary forms, with or without modification, 
	are permitted provided that the following conditions are met:

		1. Redistributions of source code must retain the above copyright notice, 
		   this list of conditions and the following disclaimer.

		2. Redistributions in binary form must reproduce the above copyright notice, 
		   this list of conditions and the following disclaimer in the documentation 
		   and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"	AND ANY 
	EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
	OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE	ARE DISCLAIMED. 
	IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
	PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
	INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
	LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/
#pragma once

#ifndef __spoutDXshaders__ 
#define __spoutDXshaders__

#include <d3d11.h>
#include <d3d11_1.h>
#include <ntverp.h>
#include <string>
#include <fstream>
#include <d3dcompiler.h>  // For compute shader
#include <Pdh.h> // GPU timer
#include <PdhMsg.h>

#pragma comment (lib, "d3d11.lib") // the Direct3D 11 Library file
#pragma comment (lib, "DXGI.lib")  // for CreateDXGIFactory1
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "pdh.lib") // GPU timer

// For DirectX 9 specific functions, the file is
// included by SpoutDX, SpoutDX9 or SpoutDirectX9
#if defined(__spoutDX9__)
#define __DX9__
#elif defined (__spoutDirectX9__)
#define __DX9__
#endif

class spoutDXshaders {

public:
	spoutDXshaders() {

	}

	~spoutDXshaders() {

		// Release the DirectX11 shader programs
		if (m_CopyProgram) m_CopyProgram->Release();
		if (m_FlipProgram) m_FlipProgram->Release();
		if (m_MirrorProgram) m_MirrorProgram->Release();
		if (m_SwapProgram) m_SwapProgram->Release();
		if (m_BlurProgram) m_BlurProgram->Release();
		if (m_SharpenProgram) m_SharpenProgram->Release();
		if (m_AdjustProgram) m_AdjustProgram->Release();
		if (m_TempProgram) m_TempProgram->Release();
		if (m_CasProgram) m_CasProgram->Release();

		// Release compute shader SRV and UAV
		ReleaseShaderResources();

		// Release class device if used
		CloseDX11();
	}

	//---------------------------------------------------------
	// Function: ReleaseShaderResources
	//    Release compute shader SRV and UAV and parameters
	bool ReleaseShaderResources()
	{
		if (m_uav) m_uav->Release();
		if (m_srv) m_srv->Release();
		m_uav = nullptr;
		m_srv = nullptr;

		// Release shader parameter buffer
		if (m_pShaderBuffer) m_pShaderBuffer->Release();
		m_pShaderBuffer = nullptr;
		// And the parameters comparsion
		m_oldParams={0};
		return true;
	}

	//---------------------------------------------------------
	// Function: UpdateShaderResources
	//    Update shader resources to use new textures
	bool UpdateResources(ID3D11Device* pDevice, ID3D11DeviceContext* pImmediateContext,
						ID3D11Texture2D* destTexture, ID3D11Texture2D* sourceTexture)
	{
		// Create a DirectX11 device if not already
		// Use the application device and context if passed in
		if (!m_pd3dDevice)
			OpenDX11(pDevice, pImmediateContext);

		// Release shader resources
		if (!ReleaseShaderResources())
			return false;

		// Get the source and destination texture details
		// texture width and height are the same
		D3D11_TEXTURE2D_DESC desc{};
		destTexture->GetDesc(&desc);
		DXGI_FORMAT destFormat = desc.Format;
		sourceTexture->GetDesc(&desc);

		// Re-create srv and uav for the new textures
		return (CreateShaderResources(m_pd3dDevice, destTexture, sourceTexture,
				destFormat, desc.Format, desc.Width, desc.Height,
				0.0f, 0.0f, 0.0f, 0.0f));
	
	}

	//---------------------------------------------------------
	// Function: CreateShaderResources
	//    Create compute shader SRV and UAV
	//    Create buffer for shader parameters
	bool CreateShaderResources(ID3D11Device* pDevice,
		ID3D11Texture2D* destTexture, ID3D11Texture2D* sourceTexture,
		DXGI_FORMAT destFormat, DXGI_FORMAT sourceFormat,
		unsigned int width, unsigned int height,
		float value1, float value2, float value3, float value4)
	{
		//
		// Create SRV and UAV
		//
		if (!m_srv && sourceTexture) {
			// Create a shader resource view (SRV) for the source texture
			// srvDesc.Format should be set to the format of the source texture
			// However, it can be set to zero (DXGI_FORMAT_UNKNOWN) to use the
			// format the resource was created with.
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc {};
			srvDesc.Format = sourceFormat;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;
			pDevice->CreateShaderResourceView(sourceTexture, &srvDesc, &m_srv);
			// printf("m_srv = 0x%X\n", PtrToUint(m_srv));
		}

		// Create an unordered access view (UAV) for the destination texture
		// A format must be explicitly specified for Unordered Access Views
		if (!m_uav && destTexture) {
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc {};
			uavDesc.Format = destFormat; // Must match the destination format
			uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
			uavDesc.Texture2D.MipSlice = 0;
			pDevice->CreateUnorderedAccessView(destTexture, &uavDesc, &m_uav);
			// printf("m_uav = 0x%X\n", PtrToUint(m_uav));
		}

		// Create shader parameter buffer
		if (!m_pShaderBuffer) {
			D3D11_BUFFER_DESC cbd{};
			cbd.Usage = D3D11_USAGE_DYNAMIC;
			cbd.ByteWidth = sizeof(m_ShaderParams);
			cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			HRESULT hr = pDevice->CreateBuffer(&cbd, nullptr, &m_pShaderBuffer);
			if (FAILED(hr)) {
				printf("spoutDXshaders::CreateShaderResources - failed to create buffer for shader parameters\n");
				return false;
			}
		}

		// Map the buffer to fill it
		m_ShaderParams params{};
		params.value1 = value1;
		params.value2 = value2;
		params.value3 = value3;
		params.value4 = value4;
		params.width  = width;
		params.height = height;

		// Fill only if values have changed
		// to avoid unnecessary constant buffer update
		if (memcmp(&params, &m_oldParams, sizeof(m_ShaderParams)) != 0) {
			D3D11_MAPPED_SUBRESOURCE mapped{};
			m_pImmediateContext->Map(m_pShaderBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			memcpy(mapped.pData, &params, sizeof(m_ShaderParams));
			m_pImmediateContext->Unmap(m_pShaderBuffer, 0);
		}

		return true;
	}


	//
	// D3D11 shader functions
	//

	// Copy textures
	bool Copy(ID3D11Texture2D* destTexture, ID3D11Texture2D* sourceTexture,
			DXGI_FORMAT destFormat, DXGI_FORMAT sourceFormat,
			unsigned int width, unsigned int height)
	{
		return ComputeShader(m_CopyHLSL, // shader source
					destTexture, sourceTexture,
					destFormat, sourceFormat,
					width, height);
	}

	// Single pass gaussian blur
	bool Blur(ID3D11Texture2D* destTexture, ID3D11Texture2D* sourceTexture,
			DXGI_FORMAT sourceFormat, unsigned int width, unsigned int height,
			float amount)
	{
		return ComputeShader(m_BlurHLSL,
					destTexture, sourceTexture,
					sourceFormat, sourceFormat,
					width, height, amount);
	}

	// Sharpen using unsharp mask
	//     sharpenWidth     - 1 (3x3), 2 (5x5), 3 (7x7)
	//     sharpenStrength  - 1 - 3 typical
	bool Sharpen(ID3D11Texture2D* destTexture, ID3D11Texture2D* sourceTexture,
			DXGI_FORMAT sourceFormat, unsigned int width, unsigned int height,
			float sharpenWidth, float sharpenStrength)
	{
		return ComputeShader(m_SharpenHLSL, // shader source
					destTexture, sourceTexture,
					sourceFormat, sourceFormat,
					width, height, sharpenWidth, sharpenStrength);
	}

	
	// Sharpen using Contrast Adaptive sharpen algorithm
	//	  level - 0 > 1
	bool AdaptiveSharpen(ID3D11Texture2D* destTexture, ID3D11Texture2D* sourceTexture,
		DXGI_FORMAT sourceFormat, unsigned int width, unsigned int height,
		float casWidth, float casLevel)
	{
		return ComputeShader(m_CasHLSL, // shader source
					destTexture, sourceTexture,
					sourceFormat, sourceFormat,
					width, height, casWidth, casLevel);
	}

	// Brightness/Contrast/Saturation/Gamma
	//     Brighness  (-1 to 1), default 0
	//     Contrast   ( 0 to 2), default 1
	//     Saturation ( 0 to 4), default 1
	//     Gamma      ( 0 to 1), default 1
	bool Adjust(ID3D11Texture2D* destTexture,
		DXGI_FORMAT sourceFormat,
		unsigned int width, unsigned int height,
		float brightness, float contrast, float saturation, float gamma)
	{
		return ComputeShader(m_AdjustHLSL, // shader source
					destTexture, nullptr,
					sourceFormat, sourceFormat,	width, height,
					brightness, contrast, saturation, gamma);
	}

	// Temperature : 3500 - 9500  (default 6500 daylight)
	bool Temperature(ID3D11Texture2D* destTexture, DXGI_FORMAT sourceFormat,
		unsigned int width, unsigned int height, float temperature)
	{
		return ComputeShader(m_TempHLSL, // shader source
					destTexture, nullptr,
					sourceFormat, sourceFormat,
					width, height, temperature);
	}


	// Flip vertically
	bool Flip(ID3D11Texture2D* destTexture, DXGI_FORMAT destFormat,
			unsigned int width, unsigned int height,
			bool bSwap = false)
	{
		return ComputeShader(m_FlipHLSL, // shader source
					destTexture, nullptr,
					destFormat, destFormat,
					width, height, (float)bSwap);
	}

	// Mirror horizontally
	bool Mirror(ID3D11Texture2D* destTexture, DXGI_FORMAT destFormat,
			unsigned int width, unsigned int height, bool bSwap = false)
	{
		return ComputeShader(m_MirrorHLSL, // shader source
					destTexture, nullptr,
					destFormat, destFormat,
					width, height, (float)bSwap);
	}

	// Swap RGBA <> BGRA
	bool Swap(ID3D11Texture2D* destTexture, DXGI_FORMAT destFormat,
		unsigned int width, unsigned int height)
	{
		return ComputeShader(m_SwapHLSL, // shader source
					destTexture, nullptr,
					destFormat, destFormat,
					width, height);
	}


	// Create a DirectX texture with specific usage, cpu, bind and misc flags 
	bool CreateDX11Texture(ID3D11Device* pd3dDevice,
		unsigned int width, unsigned int height,
		DXGI_FORMAT format,
		D3D11_USAGE usage,
		UINT cpuFlags, UINT bindFlags, UINT miscFlags,
		ID3D11Texture2D** ppTexture)
	{
		if (width == 0 || height == 0)
			return false;

		if (!pd3dDevice) {
			printf("spoutDXshaders::CreateDX11Texture NULL device\n");
			return false;
		}

		if (!ppTexture) {
			printf("spoutDXshaders::CreateDX11Texture NULL ppTexture\n");
			return false;
		}

		printf("spoutDXshaders::CreateDX11Texture(0x%.X, %d, %d, %d)\n", PtrToUint(pd3dDevice), width, height, format);

		ID3D11Texture2D* pTexture = *ppTexture;
		if (pTexture) pTexture->Release();

		// Use the format passed in
		DXGI_FORMAT texformat = format;

		// If that is zero or DX9 format, use the default format
		if (format == 0 || format == 21 || format == 22) // D3DFMT_A8R8G8B8 = 21 D3DFMT_X8R8G8B8 = 22
			texformat = DXGI_FORMAT_B8G8R8A8_UNORM;

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = width;
		desc.Height = height;
		desc.BindFlags = bindFlags;
		desc.MiscFlags = miscFlags;
		desc.CPUAccessFlags = cpuFlags;
		desc.Format = texformat;
		desc.Usage = usage;
		desc.SampleDesc.Quality = 0;
		desc.SampleDesc.Count = 1;
		desc.MipLevels = 1;
		desc.ArraySize = 1;

		HRESULT res = 0;
		res = pd3dDevice->CreateTexture2D(&desc, NULL, ppTexture);

		if (FAILED(res)) {
			char tmp[256] = {};
			const int error = static_cast<int>((LOWORD(res)));
			sprintf_s(tmp, 256, "spoutDXshaders::CreateDX11Texture ERROR - %d (0x%.X) : ", error, error);
			switch (res) {
			case DXGI_ERROR_INVALID_CALL:
				strcat_s(tmp, 256, "DXGI_ERROR_INVALID_CALL");
				break;
			case E_INVALIDARG:
				strcat_s(tmp, 256, "E_INVALIDARG");
				break;
			case E_OUTOFMEMORY:
				strcat_s(tmp, 256, "E_OUTOFMEMORY");
				break;
			default:
				strcat_s(tmp, 256, "Unlisted error");
				break;
			}
			printf("%s\n", tmp);
			return false;
		}

		return true;

	} // end CreateDX11Texture


	// Initialize and prepare Directx 11
	// Retain a class device and context
	bool OpenDX11(ID3D11Device* pDevice = nullptr, ID3D11DeviceContext* pImmediateContext = nullptr)
	{
		// Quit if already initialized
		if (m_pd3dDevice) {
			printf("spoutDXshaders::OpenDX11(0x%.7X) - device already initialized\n", PtrToUint(m_pd3dDevice));
			return true;
		}

		// Use the application device if passed in
		if (pDevice && pImmediateContext) {
			m_pd3dDevice = pDevice;
			m_pImmediateContext = pImmediateContext;
			printf("spoutDXshaders::OpenDX11(0x%.7X) - application device\n", PtrToUint(m_pd3dDevice));
		}
		else {
			// Create a DirectX 11 device
			// m_pImmediateContext is also created by CreateDX11device
			m_pd3dDevice = CreateDX11device();
			if (!m_pd3dDevice) {
				printf("spoutDXshaders::OpenDX11 - could not create device\n");
				return false;
			}
			m_bClassdevice = true; // Flag for device and context release in CloseDX11
			printf("spoutDXshaders::OpenDX11(0x%.7X) - class device\n", PtrToUint(m_pd3dDevice));
		}
		return true;
	}

	// Release DirectX 11 device and context
	void CloseDX11()
	{
		// Quit if already released
		if (!m_pd3dDevice) {
			printf("spoutDXshaders::CloseDX11() - device already released\n");
			return;
		}

		// Release context and device only for a class device
		if (m_bClassdevice) {
			if (m_pImmediateContext) {
				m_pImmediateContext->ClearState();
				m_pImmediateContext->Flush();
				m_pImmediateContext->Release();
				m_pImmediateContext = nullptr;
			}
			printf("spoutDXshaders::CloseD11(0x%.7X)\n", PtrToUint(m_pd3dDevice));
			if (m_pd3dDevice)
				m_pd3dDevice->Release();
		}

	}

	//
	// Save texture as a bitmap image (used for debugging)
	// pTexture must have format DXGI_FORMAT_B8G8R8A8_UNORM
	// Use as follows with done-once flag :
	//
	//		if (!bDoneOnce) {
	//			std::string str = GetExePath();
	//			str += "receivedTexture.bmp";
	//			printf("%s\n", str.c_str());
	//			// Assuming DXGI_FORMAT_B8G8R8A8_UNORM
	//			SaveAsBMP(str.c_str(), receivedTexture);
	//			bDoneOnce = true;
	//		}
	//
	void SaveAsBMP(const char* filename, ID3D11Texture2D* pTexture)
	{

		D3D11_TEXTURE2D_DESC desc = {};
		pTexture->GetDesc(&desc);

		// Create a staging destination texture
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.BindFlags = 0;
		desc.MiscFlags = 0;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		ID3D11Texture2D* stagingTex = nullptr;

		HRESULT hr = m_pd3dDevice->CreateTexture2D(&desc, nullptr, &stagingTex);
		if (!stagingTex || hr != 0) {
			printf("Could not create staging texture - error 0x%X\n", hr);
			return;
		}

		// Copy the texture to it
		m_pImmediateContext->CopyResource(stagingTex, pTexture);

		// Map and read the pixels
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		m_pImmediateContext->Map(stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
		uint8_t * pixels = static_cast<uint8_t *>(mapped.pData);

		// 1. File header
		BITMAPFILEHEADER fileHeader = {};
		fileHeader.bfType = 0x4D42; // 'BM'
		fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
		fileHeader.bfSize = fileHeader.bfOffBits + mapped.RowPitch * desc.Height;

		// 2. Info header
		BITMAPINFOHEADER infoHeader = {};
		infoHeader.biSize = sizeof(BITMAPINFOHEADER);
		infoHeader.biWidth = desc.Width;
		infoHeader.biHeight = desc.Height; // Positive = bottom-up
		infoHeader.biPlanes = 1;
		infoHeader.biBitCount = 32; // BGRA
		infoHeader.biCompression = BI_RGB; // No compression

		std::ofstream file(filename, std::ios::out | std::ios::binary);
		if (!file) return;

		file.write(reinterpret_cast<const char *>(&fileHeader), sizeof(fileHeader));
		file.write(reinterpret_cast<const char *>(&infoHeader), sizeof(infoHeader));

		// BMP is bottom-up, so write rows in reverse order
		for (int y = (int)desc.Height - 1; y >= 0; y--) {
			const uint8_t * row = pixels + y * mapped.RowPitch;
			file.write(reinterpret_cast<const char *>(row), mapped.RowPitch);
		}

		file.close();
	}

protected:

	//---------------------------------------------------------
	// Function: ComputeShader
	//     Use a compute shader to copy differing format textures to BRGA
	//     RGBA, 16bit RGBA, 10bit RGBA float, 16bit RGBA float, 32bit RGBA float
	//     Obtain the sharehandle of the destination BGRA texture for DirectX9
	bool ComputeShader(std::string shaderSource, // shader source code string
			ID3D11Texture2D* destTexture,
			ID3D11Texture2D* sourceTexture,
			DXGI_FORMAT destFormat,
			DXGI_FORMAT sourceFormat,
			unsigned int sourceWidth, unsigned int sourceHeight,
			float value1 = 0.0f, float value2 = 0.0f,
			float value3 = 0.0f, float value4 = 0.0f)
	{
		// Source texture can be null if reading and writing to dest
		if (shaderSource.empty() || !destTexture || !m_pd3dDevice)
			return false;

		// Shaders may not use all 4 parameters
		// Prevent unreferenced parameter errors
		value1 = value1;
		value2 = value2;
		value3 = value3;
		value4 = value4;

		// Update or create shader resources
		//   Shader resource view (SRV) for the source texture
		//   Unordered access view (UAV) for the destination texture
		if(!CreateShaderResources(m_pd3dDevice, destTexture, sourceTexture,
			destFormat, sourceFormat, sourceWidth, sourceHeight,
			value1, value2, value3, value4)) {
			printf("spoutDXshaders::ComputeShader - CreateShaderResources failed\n");
			return false;
		}

		// Find the shader program from the source passed in
		ID3D11ComputeShader* shaderProgram = nullptr;
		if (shaderSource == m_CopyHLSL)         shaderProgram = m_CopyProgram;
		else if (shaderSource == m_FlipHLSL)    shaderProgram = m_FlipProgram;
		else if (shaderSource == m_MirrorHLSL)  shaderProgram = m_MirrorProgram;
		else if (shaderSource == m_SwapHLSL)    shaderProgram = m_SwapProgram;
		else if (shaderSource == m_BlurHLSL)    shaderProgram = m_BlurProgram;
		else if (shaderSource == m_SharpenHLSL) shaderProgram = m_SharpenProgram;
		else if (shaderSource == m_AdjustHLSL)  shaderProgram = m_AdjustProgram;
		else if (shaderSource == m_TempHLSL)    shaderProgram = m_TempProgram;
		else if (shaderSource == m_CasHLSL)     shaderProgram = m_CasProgram;
		else
			return false;

		// Create the compute shader program from source
		if (!shaderProgram) {
			shaderProgram = CreateDXcomputeShader(m_pd3dDevice, shaderSource.c_str());
			if (shaderProgram) {
				// Save shader programs for re-use
				if (shaderSource == m_CopyHLSL)	   m_CopyProgram    = shaderProgram;
				if (shaderSource == m_FlipHLSL)	   m_FlipProgram    = shaderProgram;
				if (shaderSource == m_MirrorHLSL)  m_MirrorProgram  = shaderProgram;
				if (shaderSource == m_SwapHLSL)	   m_SwapProgram    = shaderProgram;
				if (shaderSource == m_BlurHLSL)    m_BlurProgram    = shaderProgram;
				if (shaderSource == m_SharpenHLSL) m_SharpenProgram = shaderProgram;
				if (shaderSource == m_AdjustHLSL)  m_AdjustProgram  = shaderProgram;
				if (shaderSource == m_TempHLSL)    m_TempProgram    = shaderProgram;
				if (shaderSource == m_CasHLSL)     m_CasProgram     = shaderProgram;
			}
		}

		//
		// Activate the a compute shader
		//

		// Bind the shader Constant Buffer to the Compute Shader
		m_pImmediateContext->CSSetConstantBuffers(0, 1, &m_pShaderBuffer);
		// Bind SRV and UAV
		if(m_srv) m_pImmediateContext->CSSetShaderResources(0, 1, &m_srv);
		m_pImmediateContext->CSSetUnorderedAccessViews(0, 1, &m_uav, nullptr);
		// Set the current shader program
		m_pImmediateContext->CSSetShader(shaderProgram, nullptr, 0);
		// Dispatch: with 16x16 threads
		m_pImmediateContext->Dispatch((sourceWidth+15)/16, (sourceHeight+15)/16, 1);
		// Unbind SRV and UAV
		if (m_srv) {	  
			ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
			m_pImmediateContext->CSSetShaderResources(0, 1, nullSRV);
		}
		ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
		m_pImmediateContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

		// Flush to make sure the result is ready immediately.
		// (0.1 - 0.2 msec overhead)
		m_pImmediateContext->Flush();

		return true;
	}

	
	//---------------------------------------------------------
	// Function: CheckUAVStoreSupport
	//     Check GPU support for UAV typed store for a texture format
	bool CheckUAVStoreSupport(ID3D11Device* device, DXGI_FORMAT format)
	{
		D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support2 {};
		support2.InFormat = format;
		HRESULT hr = device->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2,
			&support2, sizeof(support2));
		return SUCCEEDED(hr) && (support2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE);
	}

	
	//---------------------------------------------------------
	// Function: CreateComputeShader
	//     Create and compile compute shader
	ID3D11ComputeShader* CreateDXcomputeShader(
		ID3D11Device* device, const char* hlslSource,
		const char* entryPoint = "CSMain",
		const char* targetProfile = "cs_5_0")
	{
		if (!device || !hlslSource) {
			printf("spoutDXshaders::CreateDXcomputeShader - no device or source\n");
			return nullptr;
		}

		ID3DBlob* shaderBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;

		HRESULT hr = D3DCompile(hlslSource, strlen(hlslSource),
			nullptr, // filename (for error messages)
			nullptr, // macros
			nullptr, // include handler
			entryPoint, targetProfile,
			0, // compile flags
			0,
			&shaderBlob, &errorBlob);

		if (FAILED(hr)) {
			if (errorBlob) {
				printf("spoutDXshaders::CreateComputeShader : D3DCompile failed: [%s]\n", (const char *)errorBlob->GetBufferPointer());
				errorBlob->Release();
			}
			return nullptr;
		}

		ID3D11ComputeShader * shader = nullptr;
		hr = device->CreateComputeShader(
			shaderBlob->GetBufferPointer(),
			shaderBlob->GetBufferSize(),
			nullptr,
			&shader);

		shaderBlob->Release();

		if (FAILED(hr)) {
			printf("spoutDXshaders::CreateComputeShader : CreateComputeShader failed\n");
			return nullptr;
		}

		return shader;
	}

	//
	// Global variables
	//

	ID3D11Device* m_pd3dDevice = nullptr; // DX11 device
	ID3D11DeviceContext* m_pImmediateContext = nullptr; // Context
	bool m_bClassdevice = false; // Use application device

	// Destination BGRA texture for D3D9 compute shader copy
	ID3D11Texture2D* m_dstTexture = nullptr;
	unsigned int m_dstWidth = 0;
	unsigned int m_dstHeight = 0;
	DXGI_FORMAT m_senderFormat = DXGI_FORMAT_UNKNOWN; // For detection of source format change
	unsigned int m_senderWidth = 0;
	unsigned int m_senderHeight = 0;
	HANDLE m_senderHandle = 0; // For detection of share handle change

	// Compute shader
	ID3D11UnorderedAccessView* m_uav = nullptr;
	ID3D11ShaderResourceView* m_srv = nullptr;

	// Shader programs
	ID3D11ComputeShader* m_CopyProgram = nullptr;
	ID3D11ComputeShader* m_FlipProgram = nullptr;
	ID3D11ComputeShader* m_MirrorProgram = nullptr;
	ID3D11ComputeShader* m_SwapProgram = nullptr;
	ID3D11ComputeShader* m_BlurProgram = nullptr;
	ID3D11ComputeShader* m_SharpenProgram = nullptr;
	ID3D11ComputeShader* m_AdjustProgram = nullptr;
	ID3D11ComputeShader* m_TempProgram = nullptr;
	ID3D11ComputeShader* m_CasProgram = nullptr;
	
	// Shader parameters
	struct m_ShaderParams
	{
		float value1;  // float argument 1
		float value2;  // float argument 2
		float value3;  // float argument 3
		float value4;  // float argument 4
		UINT width;    // image width
		UINT height;   // image height
		UINT padding1; // Padding retains 16 byte alignment
		UINT padding2;
	};

	// Constant Buffer for shader parameters
	ID3D11Buffer* m_pShaderBuffer = nullptr;
	m_ShaderParams m_oldParams{}; // Comparison parameters

	//
	// HLSL source
	//

	// Copy source texture to a destination texure
	// If the source is RGBA and destination BGRA as required for D3D9,
	// the shader writes r g b a, and hardware stores as BGRA in memory
	const char* m_CopyHLSL = R"(
		Texture2D<float4> src : register(t0); // UNORM source
		RWTexture2D<float4> dst : register(u0); // UNORM destination
		cbuffer params : register(b0)
		{
			float value1;
			float value2;
			float value3;
			float value4;
			uint width;  // source width
			uint height; // source height
		};
		// 16x16 threads per group for better occupancy on modern GPUs
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			// Avoid writing past the edge on non-divisible sizes
			if (DTid.x >= width || DTid.y >= height)
				return;
			dst[DTid.xy] = src.Load(uint3(DTid.xy, 0));
		}
	)";

	// Flip image vertically in place
	const char* m_FlipHLSL = R"(
		RWTexture2D<float4> dst : register(u0);
		cbuffer params : register(b0)
		{
			float value1;
			float value2;
			float value3;
			float value4;
			uint width;
			uint height;
		};
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			// Check bounds	
			if(DTid.y > height/2 || DTid.x >= width) // Half image
				return;

			uint ypos = height-DTid.y; // Flip y position
			float4 c0 = dst.Load(int3(DTid.xy, 0));      // This pixel
			float4 c1 = dst.Load(int3(DTid.x, ypos, 0)); // Flipped pixel

			// Optional RGBA <-> BGRA swap
			if (value1 == 1.0) {
				c0 = c0.bgra;
				c1 = c1.bgra;
			}

			dst[uint2(DTid.x, ypos)] = c0;    // Move this pixel to flip position
			dst[uint2(DTid.x, DTid.y)] = c1;  // Move flip pixel to this position
		}
	)";

	// Mirror horizontally in place
	const char* m_MirrorHLSL = R"(
		RWTexture2D<float4> dst : register(u0);
		cbuffer params : register(b0)
		{
			float value1;
			float value2;
			float value3;
			float value4;
			uint width;
			uint height;
		};
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			if (DTid.x >= width/2 || DTid.y >= height)
				return;

			uint xpos = width - DTid.x;
			float4 c0 = dst.Load(int3(DTid.xy, 0));      // Current pixel
			float4 c1 = dst.Load(int3(xpos, DTid.y, 0)); // Mirror pixel
			// RGBA <-> BGRA swap
			if (value1 == 1.0) {
				c0 = c0.bgra;
				c1 = c1.bgra;
			}
			// Write to destination
			dst[uint2(xpos, DTid.y)] = c0;
			dst[uint2(DTid.x, DTid.y)] = c1;
		}

	)";

	// Swap red and blue
	const char* m_SwapHLSL = R"(
		RWTexture2D<float4> dst : register(u0); // UNORM source/dest
		cbuffer params : register(b0)
		{
			float value1;
			float value2;
			float value3;
			float value4;
			uint width;
			uint height;
		};
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			if(DTid.x >= width || DTid.y >= height)
				return;
			float4 color = dst.Load(uint3(DTid.xy, 0));
			dst[DTid.xy] = color.bgra; // Swap red and blue
		}
	)";


	// Single pass 5x5 blur
	// Source and destination required for SamplerState
	const char* m_BlurHLSL = R"(
		Texture2D<float4> src : register(t0);
		RWTexture2D<float4> dst : register(u0);
		SamplerState LinearClampSampler : register(s0);
		cbuffer params : register(b0)
		{
			float value1;
			float value2;
			float value3;
			float value4;
			uint width;
			uint height;
		};
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			// Input
			float Amount = value1; // blur radius
			float2 TexelSize = float2(1.0/width, 1.0/height);

			float2 uv = DTid.xy*TexelSize;
			float2 offset = TexelSize*Amount;

			float4 color = float4(0, 0, 0, 0);
			float weightSum = 0.0;

			float weights[5] = { 0.204164, 0.304005, 0.093913, 0.010381, 0.001097 };

			for (int y = -2; y <= 2; ++y)
			{
				for (int x = -2; x <= 2; ++x)
				{
					float w = weights[abs(x)] * weights[abs(y)];
					float2 sampleUV = uv + float2(x, y) * offset;
					color += w * src.SampleLevel(LinearClampSampler, sampleUV, 0);
					weightSum += w;
				}
			}

			dst[DTid.xy] = color / weightSum;

		}
	)";


	// Sharpen - unsharp mask
	// Source and destination required for neighbourhood
	const char* m_SharpenHLSL = R"(
		Texture2D<float4> src : register(t0);
		RWTexture2D<float4> dst : register(u0);
		cbuffer params : register(b0)
		{
			float value1;
			float value2;
			float value3;
			float value4;
			uint width;
			uint height;
		};
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			int2 coord = int2(DTid.xy);

			float dx = value1; // sharpen width
			float dy = value1; // sharpen width

			float4 orig = src.Load(int3(coord, 0));
			float4 c1 = src.Load(int3(coord + int2(-dx, -dy), 0));
			float4 c2 = src.Load(int3(coord + int2( 0,  -dy), 0));
			float4 c3 = src.Load(int3(coord + int2( dx, -dy), 0));
			float4 c4 = src.Load(int3(coord + int2(-dx,  0), 0));
			float4 c5 = src.Load(int3(coord + int2( dx,  0), 0));
			float4 c6 = src.Load(int3(coord + int2(-dx,  dy), 0));
			float4 c7 = src.Load(int3(coord + int2( 0,   dy), 0));
			float4 c8 = src.Load(int3(coord + int2( dx,  dy), 0));

		    float4 blur = ((c1 + c3 + c6 + c8) +
				           2.0 * (c2 + c4 + c5 + c7) +
						   4.0 * orig) / 16.0;

			float4 coeff_blur = value2; // sharpen strength;
			float4 coeff_orig = 1.0 + coeff_blur;

		    float4 c9 = coeff_orig*orig - coeff_blur*blur;

			dst[coord] = c9;

		}
	)";

	
	// Contrast Adaptive sharpening
	//   AMD FidelityFX https://gpuopen.com/fidelityfx-cas/
	//   Adapted from  https://www.shadertoy.com/view/ftsXzM
	const char* m_CasHLSL = R"(
		Texture2D<float4> src : register(t0);
		RWTexture2D<float4> dst : register(u0);
		cbuffer params : register(b0)
		{
			float value1; // casWidth - pixel offset (1.0, 2.0, 3.0)
			float value2; // casLevel - sharpening level (0.0 to 1.0)
			float value3;
			float value4;
			uint width;
			uint height;
		};

		// Luminance calculation
		float luminance(float3 col)
		{
			return dot(col, float3(0.2126, 0.7152, 0.0722));
		}

		// Compute shader entry point
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			int2 coord = int2(DTid.xy);

			// Offsets 1, 2, 3
			float dx = value1;
			float dy = value2;
			float casLevel = value2;

			//
			// Neighbourhood
			//
			//     b
			//  a  x  c
			//     d
			//

			// Central pixel (rgba)
			float4 c0 = src.Load(int3(coord, 0));
			// Centre pixel (rgb)
			float3 col = c0.rgb;

		    float max_g = luminance(col);
			float min_g = max_g;

		    float3 col1, colw;

			// Pixel a (-dx, 0)
			col1 = src.Load(int3(coord + int2(-dx, 0), 0)).rgb;
			max_g = max(max_g, luminance(col1));
			min_g = min(min_g, luminance(col1));
			colw = col1;

			// Pixel b (0, dy)
			col1 = src.Load(int3(coord + int2(0, dy), 0)).rgb;
			max_g = max(max_g, luminance(col1));
			min_g = min(min_g, luminance(col1));
			colw += col1;

			// Pixel c (+dx, 0)
			col1 = src.Load(int3(coord + int2(dx, 0), 0)).rgb;
			max_g = max(max_g, luminance(col1));
			min_g = min(min_g, luminance(col1));
			colw += col1;

			// Pixel d (0, dy) — note: repeated access in original GLSL, likely a mistake; we’ll use (0, -dy)
			col1 = src.Load(int3(coord + int2(0, -dy), 0)).rgb;
			max_g = max(max_g, luminance(col1));
			min_g = min(min_g, luminance(col1));
			colw += col1;

			//
			// CAS algorithm
			//
			float d_min_g = min_g;
			float d_max_g = 1.0 - max_g;
			float A;
			if (d_max_g < d_min_g)
				A = d_max_g / max_g;
			else
				A = d_min_g / max_g;

		    A = sqrt(A);
			A *= lerp(-0.125, -0.2, casLevel); // level - CAS level 0-1

			// Sharpened result
		    float3 col_out = (col + colw * A) / (1.0 + 4.0 * A);

		    // Output result
			dst[coord] = float4(col_out, c0.a);
			

		}

	)";

	// Brightness/Contrast/Saturation/Gamma
	//     Brighness  (-1 to 1), default 0
	//     Contrast   ( 0 to 2), default 1
	//     Saturation ( 0 to 4), default 1
	//     Gamma      ( 0 to 1), default 1
	const char* m_AdjustHLSL = R"(
		RWTexture2D<float4> dst : register(u0);
		cbuffer params : register(b0)
		{
			float value1; // Brighness
			float value2; // Contrast
			float value3; // Saturation
			float value4; // Gamma
			uint width;
			uint height;
		};
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			float4 c1 = dst[DTid.xy];

			// Gamma correction
			float3 c2 = pow(c1.rgb, 1.0 / value4);

			// Saturation
			float luminance = dot(c2, float3(0.2125, 0.7154, 0.0721));
			c2 = lerp(float3(luminance, luminance, luminance), c2, value3);

			// Contrast
			c2 = (c2 - 0.5) * value2 + 0.5;

			// Brightness
			c2 += value1;

			dst[DTid.xy] = float4(c2, c1.a);

		}
	)";


	// Temperature : 3500 - 9500  (default 6500 daylight)
	const char* m_TempHLSL = R"(
		RWTexture2D<float4> dst : register(u0);
		cbuffer params : register(b0)
		{
			float value1; // Temperature
			float value2;
			float value3;
			float value4;
			uint width;
			uint height;
		};

		// Convert RGB to HSV
		float3 rgb2hsv(float3 c)
		{
			float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
			float4 p = (c.g < c.b) ? float4(c.bg, K.wz) : float4(c.gb, K.xy);
			float4 q = (c.r < p.x) ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
			float d = q.x - min(q.w, q.y);
			float e = 1.0e-10;
			return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
		}

		// Convert HSV to RGB
		float3 hsv2rgb(float3 c)
		{
			float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
			float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
			return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
		}

		// Convert Kelvin temperature to RGB
		float3 kelvin2rgb(float K)
		{
			float t = K / 100.0;
			float3 o1, o2;
			float tg1 = t - 2.0;
			float tb1 = t - 10.0;
			float tr2 = t - 55.0;
			float tg2 = t - 50.0;

		    o1.r = 1.0;
			o1.g = (-155.25485562709179 - 0.44596950469579133 * tg1 + 104.49216199393888 * log(tg1)) / 255.0;
			o1.b = (-254.76935184120902 + 0.8274096064007395 * tb1 + 115.67994401066147 * log(tb1)) / 255.0;
			o1.b = lerp(0.0, o1.b, step(2001.0, K));

		    o2.r = (351.97690566805693 + 0.114206453784165 * tr2 - 40.25366309332127 * log(tr2)) / 255.0;
			o2.g = (325.4494125711974 + 0.07943456536662342 * tg2 - 28.0852963507957 * log(tg2)) / 255.0;
			o2.b = 1.0;

		    o1 = clamp(o1, 0.0, 1.0);
			o2 = clamp(o2, 0.0, 1.0);
	
		    return lerp(o1, o2, step(66.0, t));
		}

		// Apply color temperature
		float3 temperature(float3 c_in, float K)
		{
			float3 chsv_in = rgb2hsv(c_in);
			float3 c_temp = kelvin2rgb(K);
			float3 c_mult = c_temp * c_in;
			float3 chsv_mult = rgb2hsv(c_mult);
			return hsv2rgb(float3(chsv_mult.x, chsv_mult.y, chsv_in.z));
		}

		// Compute shader entry point
		[numthreads(16, 16, 1)]
		void CSMain(uint3 DTid : SV_DispatchThreadID)
		{
			float4 c1 = dst.Load(int3(DTid.xy, 0));
			float3 c_out = temperature(c1.rgb, value1);
			dst[DTid.xy] = float4(c_out, c1.a);
		}
	)";

	//
	// Equivalents to SpoutDirectX so that this class can be used independently
	//

	// Create DX11 device
	ID3D11Device* CreateDX11device()
	{
		ID3D11Device* pd3dDevice = nullptr;
		HRESULT hr = S_OK;
		UINT createDeviceFlags = 0;

		// GL/DX interop Spec
		// ID3D11Device can only be used on WDDM operating systems : Must be multithreaded
		// D3D11_CREATE_DEVICE_FLAG createDeviceFlags
		const D3D_DRIVER_TYPE driverTypes[] = {
			D3D_DRIVER_TYPE_HARDWARE,
			D3D_DRIVER_TYPE_WARP,
			D3D_DRIVER_TYPE_REFERENCE,
		};

		const UINT numDriverTypes = ARRAYSIZE(driverTypes);

		// These are the feature levels that we will accept.
		// m_featureLevel is the maximum supported feature level used
		// 11.0 is the highest level currently supported for Spout
		// because 11.1 limits compatibility
		D3D_DRIVER_TYPE m_driverType = D3D_DRIVER_TYPE_NULL;
		D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
		const D3D_FEATURE_LEVEL featureLevels[] = {
			D3D_FEATURE_LEVEL_11_1, // 0xb001
			D3D_FEATURE_LEVEL_11_0, // 0xb000
			D3D_FEATURE_LEVEL_10_1, // 0xa100
			D3D_FEATURE_LEVEL_10_0, // 0xa000
		};

		const UINT numFeatureLevels = ARRAYSIZE(featureLevels);

		for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++) {

			// First driver type is D3D_DRIVER_TYPE_HARDWARE which should pass
			m_driverType = driverTypes[driverTypeIndex];

			hr = D3D11CreateDevice(NULL,
				m_driverType,
				NULL,
				createDeviceFlags,
				featureLevels,
				numFeatureLevels,
				D3D11_SDK_VERSION,
				&pd3dDevice,
				&m_featureLevel,
				&m_pImmediateContext);

			// Break as soon as something passes
			if (SUCCEEDED(hr))
				break;
		}

		// Quit if nothing worked
		if (FAILED(hr)) {
			printf("spoutDXshaders::CreateDX11device - NULL device\n");
			return NULL;
		}

		// All OK - return the device pointer to the caller
		// m_pImmediateContext has also been created by D3D11CreateDevice
		printf("spoutDXshaders::CreateDX11device - device (0x%.7X) context (0x%.7X)\n", PtrToUint(pd3dDevice), PtrToUint(m_pImmediateContext));

		return pd3dDevice;

	} // end CreateDX11device


	// Retrieve the pointer of a DirectX11 shared texture
	bool OpenDX11shareHandle(ID3D11Device* pDevice, ID3D11Texture2D** ppSharedTexture, HANDLE dxShareHandle)
	{
		if (!pDevice || !ppSharedTexture || !dxShareHandle) {
			printf("spoutDXshaders::OpenDX11shareHandle - null sources\n");
			return false;
		}

		// This can crash if the share handle has been created using a different graphics adapter
		HRESULT hr = 0;
		try {
			hr = pDevice->OpenSharedResource(dxShareHandle, __uuidof(ID3D11Texture2D), (void **)(ppSharedTexture));
		}
		catch (...) {
			// Catch any exception
			printf("spoutDXshaders::OpenDX11shareHandle - exception opening share handle\n");
			return false;
		}

		if (FAILED(hr)) {
			printf("spoutDXshaders::OpenDX11shareHandle (0x%.7X) failed : error = %d (0x%.7X)\n", LOWORD(dxShareHandle), LOWORD(hr), LOWORD(hr));
			return false;
		}
		return true;
	}

	// Wait for completion after flush
	void Wait(ID3D11Device* pd3dDevice, ID3D11DeviceContext* pImmediateContext)
	{
		if (!pd3dDevice || !pImmediateContext)
			return;

		// https://msdn.microsoft.com/en-us/library/windows/desktop/ff476578%28v=vs.85%29.aspx
		// When the GPU is finished, ID3D11DeviceContext::GetData will return S_OK.
		// When using this type of query, ID3D11DeviceContext::Begin is disabled.
		D3D11_QUERY_DESC queryDesc{};
		ID3D11Query* pQuery = nullptr;
		ZeroMemory(&queryDesc, sizeof(queryDesc));
		queryDesc.Query = D3D11_QUERY_EVENT;
		pd3dDevice->CreateQuery(&queryDesc, &pQuery);
		if (pQuery) {
			pImmediateContext->End(pQuery);
			while (S_OK != pImmediateContext->GetData(pQuery, NULL, 0, 0)) {
				// Yield to reduce CPU load polling GetData()
				Sleep(0);
			}
			pQuery->Release();
			pImmediateContext->Flush();
		}
	}


}; // endif _spoutDXshaders_

#endif
