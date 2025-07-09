//
//
//			SpoutDX9shaders.hpp
//
//		D3D11 texture copy compute shader for DirectX 9
//
//		Copies from textures of different formats
//		to a BGRA texture compatible with DirectX 9
//		and opens a share handle from that texture.
//		
//		CopyDX11shareHandle
//			o Get the sender DX11 texture from the share handle
//			o Check the sender texture format
//			o If BGRA, return the original sharehandle
//			o For other formats
//				o Copy to a BGRA texture using a compute shader
//			    o Return the sharehandle from the BGRA texture copy
//
//		Used in :
//		    spoutDirectX9::CreateSharedDX9Texture
//		    spoutDX9::CreateSharedDX9Texture
//
// ====================================================================================
//		Revisions :
//
//		29.06.26	- Create a separate file from general DX11 shaders
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
#ifndef __spoutDX9shaders__ 
#define __spoutDX9shaders__

#include <stdio.h>
#include <string>
#include <d3d11.h>
#include <mutex>
#include <ntverp.h>
#include <d3dcompiler.h>  // For compute shader
#include <Pdh.h> // GPU timer
#include <PdhMsg.h>

#pragma comment (lib, "d3d11.lib") // the Direct3D 11 Library file
#pragma comment (lib, "DXGI.lib")  // for CreateDXGIFactory1
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "pdh.lib") // GPU timer

class spoutDX9shaders {

public:
	spoutDX9shaders() {

	}

	~spoutDX9shaders() {
		// Release compute shader SRV and UAV
		ReleaseShaderResources();
		// Release class device if used (for DirectX 9)
		if (m_pd3dDevice && m_bClassdevice)
			CloseDX11();
	}

	//---------------------------------------------------------
	// Function: CopyDX11shareHandle
	// Used by DirectX9
	//   o Get the sender DX11 texture from the share handle
	//   o Get the sender texture format
	//   o If BGRA, return the original sharehandle
	//   o For other formats
	//     o Copy to a BGRA texture using a compute shader
	//     o Return the sharehandle from the BGRA texture copy
	//  m_bCopy - force copy using a compute shader even if BGRA
	//
	HANDLE CopyDX11shareHandle(HANDLE sourceShareHandle,
		unsigned int width, unsigned int height)
	{
		if (!sourceShareHandle)
			return nullptr;

		// For DirectX9 create a class DirectX11 device
		if (!m_pd3dDevice)
			OpenDX11();

		// Return if the handle is unchanged and compatible format
		if (!m_bCopy && sourceShareHandle == m_senderHandle
			&& m_senderFormat == DXGI_FORMAT_B8G8R8A8_UNORM) {
			return sourceShareHandle;
		}

		// Open the sender texture share handle
		// for different handle or incompatible format
		ID3D11Texture2D* sourceTexture = nullptr;
		HANDLE sharehandle = sourceShareHandle; // Source share handle
		// Get a new DirectX 11 shared texture pointer using the sharehandle
		if (OpenDX11shareHandle(m_pd3dDevice, &sourceTexture, sourceShareHandle)) {

			// Save the sender handle for comparison
			m_senderHandle = sourceShareHandle;

			// Check the source texture format
			if (sourceTexture) {

				D3D11_TEXTURE2D_DESC desc {};
				sourceTexture->GetDesc(&desc);

				// Reset shader resources for format change
				if (desc.Format != m_senderFormat
					|| desc.Width != m_senderWidth
					|| desc.Height != m_senderHeight) {
					// Release compute shader resources
					ReleaseShaderResources();
					// Save the sender format for comparison
					m_senderFormat = desc.Format;
					m_senderWidth = desc.Width;
					m_senderHeight = desc.Height;
				}

				// Check for DX9 compatible format (DXGI_FORMAT_B8G8R8A8_UNORM)
				if (m_bCopy || desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
					// If not compatible, create a BGRA destination shared texture
					// if the size has changed or not created yet
					if (!m_dstTexture || width != m_dstWidth || height != m_dstHeight) {
						if (m_dstTexture) m_dstTexture->Release();
						m_dstTexture = nullptr;
						CreateDX11Texture(m_pd3dDevice, width, height,
							DXGI_FORMAT_B8G8R8A8_UNORM,
							D3D11_USAGE_DEFAULT, // Usage
							0, // CPU access flags
							// D3D11_BIND_UNORDERED_ACCESS for the copy shader
							// D3D11_BIND_SHADER_RESOURCE for D3D9 read using the share handle
							D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
							D3D11_RESOURCE_MISC_SHARED, // shared to get the share handle
							&m_dstTexture);
						// Update globals for detection of size change
						m_dstWidth = width;
						m_dstHeight = height;
					}

					if (m_dstTexture && sourceTexture) {
						// Use a compute shader to copy from the source texture
						// and obtain the sharehandle of the destination BGRA texture
						ComputeShader(m_CopyHLSL,
							m_dstTexture, sourceTexture,
							DXGI_FORMAT_B8G8R8A8_UNORM, // dest format
							DXGI_FORMAT_UNKNOWN, // format of the source texture
							width, height, &sharehandle);
					}
				}
				// Finished with the texture
				sourceTexture->Release();
			}
		}
		// Return the sharehandle to get the BGRA copy texture
		return sharehandle;
	}

	//---------------------------------------------------------
	// Function: ReleaseShaderResources
	//   Release compute shader SRV and UAV and width/height parameters
	bool ReleaseShaderResources()
	{
		if (m_uav) m_uav->Release();
		if (m_srv) m_srv->Release();
		m_uav = nullptr;
		m_srv = nullptr;

		// Release shader parameter buffer
		if (m_pShaderBuffer) m_pShaderBuffer->Release();
		m_pShaderBuffer = nullptr;

		// Parameters comparsion
		m_oldParams = { 0 };

		// DX9 BGRA texture
		if (m_dstTexture) m_dstTexture->Release();
		m_dstTexture = nullptr;

		return true;
	}

	//---------------------------------------------------------
	// Function: UpdateShaderResources
	//    Update shader resources to use new textures
	bool UpdateResources(ID3D11Device * pDevice, ID3D11DeviceContext * pImmediateContext,
		ID3D11Texture2D * destTexture, ID3D11Texture2D * sourceTexture) {
		// Create a DirectX11 device if not already
		// Use the application device and context if passed in
		if (!m_pd3dDevice)
			OpenDX11(pDevice, pImmediateContext);

		// Release shader resources
		if (!ReleaseShaderResources())
			return false;

		// Get the source and destination texture details
		// texture width and height are the same
		D3D11_TEXTURE2D_DESC desc {};
		destTexture->GetDesc(&desc);
		DXGI_FORMAT destFormat = desc.Format;
		sourceTexture->GetDesc(&desc);

		// Re-create srv and uav for the new textures
		return (CreateShaderResources(m_pd3dDevice, destTexture, sourceTexture,
			destFormat, desc.Format, desc.Width, desc.Height));
	}

	//---------------------------------------------------------
	// Function: CreateShaderResources
	//    Create compute shader SRV and UAV
	//    Create buffer for shader parameters
	bool CreateShaderResources(ID3D11Device * pDevice,
		ID3D11Texture2D * destTexture, ID3D11Texture2D * sourceTexture,
		DXGI_FORMAT destFormat, DXGI_FORMAT sourceFormat,
		unsigned int width, unsigned int height) {
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
			D3D11_BUFFER_DESC cbd {};
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
		m_ShaderParams params {};
		params.width = width;
		params.height = height;

		// Fill only if values have changed
		// to avoid unnecessary constant buffer update
		if (memcmp(&params, &m_oldParams, sizeof(m_ShaderParams)) != 0) {
			D3D11_MAPPED_SUBRESOURCE mapped {};
			m_pImmediateContext->Map(m_pShaderBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
			memcpy(mapped.pData, &params, sizeof(m_ShaderParams));
			m_pImmediateContext->Unmap(m_pShaderBuffer, 0);
		}

		return true;
	}

	//---------------------------------------------------------
	// Function: CreateDX11Texture
	//   Create a DirectX texture with specific usage, cpu, bind and misc flags
	bool CreateDX11Texture(ID3D11Device * pd3dDevice,
		unsigned int width, unsigned int height,
		DXGI_FORMAT format,
		D3D11_USAGE usage,
		UINT cpuFlags, UINT bindFlags, UINT miscFlags,
		ID3D11Texture2D ** ppTexture) {
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

		ID3D11Texture2D * pTexture = *ppTexture;
		if (pTexture) pTexture->Release();

		// Use the format passed in
		DXGI_FORMAT texformat = format;

		// If that is zero or DX9 format, use the default format
		if (format == 0 || format == 21 || format == 22) // D3DFMT_A8R8G8B8 = 21 D3DFMT_X8R8G8B8 = 22
			texformat = DXGI_FORMAT_B8G8R8A8_UNORM;

		D3D11_TEXTURE2D_DESC desc {};
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

	//---------------------------------------------------------
	// Function: OpenDX11
	//   Initialize and prepare Directx 11
	//   Retain a class device and context
	bool OpenDX11(ID3D11Device * pDevice = nullptr, ID3D11DeviceContext * pImmediateContext = nullptr) {
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
		} else {
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
	} // end OpenDX11

	//---------------------------------------------------------
	// Function: CloseDX11
	//   Release DirectX 11 device and context
	void CloseDX11() {
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
			m_pd3dDevice = nullptr;
		}
	} // end CloseDX11

	// Sender shared texture format
	DXGI_FORMAT GetDX11format()
	{
		return m_senderFormat;
	}

	// Shader class DX11 device
	ID3D11Device* GetDX11device() {
		return m_pd3dDevice;
	}

	// Shader class DX11 context
	ID3D11DeviceContext* GetDX11context() {
		return m_pImmediateContext;
	}
	
	// Return BGRA copy texture used for shader copy
	ID3D11Texture2D* GetCopyTexture() {
		return m_dstTexture;
	}

	// Force compute shader and BGRA copy texture
	void SetCopyTexture(bool bCopy)
	{
		m_bCopy = bCopy;
	}

	
//---------------------------------------------------------
// Function: GetGPUtimer
//   GPU timer shader duration
double GetGPUtimer()
{
	return m_GPUframeTime;
}

// Function: GetGPUlatestUsage
//   Percent GPU usage
double GetGPUlatestUsage()
{
	return m_GPUlatestUsage;
}

// Function: GetGPUpeakUsage
//   Percent peak GPU usage at excess shader time
double GetGPUpeakUsage()
{
	return m_GPUpeakUsage;
}

// Function: GetGPUrecovery
//   Percent GPU usage required for recovery
double GetGPUrecovery()
{
	return m_GPUrecovery;
}


protected:

	//---------------------------------------------------------
	// Function: InitializeGPUquery
	//   Initialize GPU performance query
	//   Use a thread to avoid delaying the main program
	//   due to Sleep required for prime. 
	void InitializeGPUquery()
	{
		std::call_once(g_GPUInitOnce, [this]() {

			std::thread([this]() {

				HQUERY query;
				HCOUNTER counter;

				if (PdhOpenQuery(NULL, 0, &query) == ERROR_SUCCESS) {

					if (PdhAddCounter(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) == ERROR_SUCCESS) {

						PdhCollectQueryData(query); // Prime once
						Sleep(100);
						PdhCollectQueryData(query); // Prime again if needed

						// Assign variables
						m_GPUquery = query;
						m_GPUcounter = counter;
						m_GPUlastSampleTime = GetTickCount64();
						m_GPUhasPrimedSample = false;
						m_GPUQueryReady.store(true);
					}
					else {
						PdhCloseQuery(query);
					}
				}
			}).detach();
		});
	}

	//
	// GPU timing
	//

	//---------------------------------------------------------
	// Function: StartGPUtiming
	//   Begin GPU timer every 120 frames
	void StartGPUtiming() {

		// Initialize GPU timer
		if (m_frameCount == 0) {
			D3D11_QUERY_DESC desc = {};
			desc.Query = D3D11_QUERY_TIMESTAMP;
			// Start/End times
			m_pd3dDevice->CreateQuery(&desc, &m_gpuTimer.timestampStart);
			m_pd3dDevice->CreateQuery(&desc, &m_gpuTimer.timestampEnd);
			// Disjoint validates timestamp data
			desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
			m_pd3dDevice->CreateQuery(&desc, &m_gpuTimer.disjoint);
		}

		// Wait 1 second at 60 fps at start before timing tests
		// Read every 2 seconds (120 frames at 60 fps)
		GpuTimerQuerySet currentQuery {};
		if (!m_readPending && m_frameCount > 60 && m_frameCount % 120 == 0) {
			// Begin GPU timing for this frame
			m_pImmediateContext->Begin(m_gpuTimer.disjoint);
			m_pImmediateContext->End(m_gpuTimer.timestampStart);
		}
	}
	
	//---------------------------------------------------------
	// Function: StartGPUtiming
	//   End timing every 120 frames
	void EndGPUtiming() {

		if (!m_readPending && m_frameCount > 60 && m_frameCount % 120 == 0) {
			m_pImmediateContext->End(m_gpuTimer.timestampEnd);
			m_pImmediateContext->End(m_gpuTimer.disjoint);
			m_readPending = true; // Ready to read counters
			m_frameEnd = m_frameCount; // Record the frame number at timing end
		}
	}

	//---------------------------------------------------------
	// Function: ReadGPUtiming
	//   Read timing counters 8 frames after timing end to avoid GPU latency
	void ReadGPUtiming()
	{
		if (m_readPending && (m_frameCount - m_frameEnd) >= 8) {

			D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData = {};
			UINT64 startTime = 0, endTime = 0;
			HRESULT hrDisjoint = m_pImmediateContext->GetData(m_gpuTimer.disjoint, &disjointData, sizeof(disjointData), 0);
			HRESULT hrStart = m_pImmediateContext->GetData(m_gpuTimer.timestampStart, &startTime, sizeof(startTime), 0);
			HRESULT hrEnd = m_pImmediateContext->GetData(m_gpuTimer.timestampEnd, &endTime, sizeof(endTime), 0);
			// printf("hrDisjoint = 0x%X hrStart = 0x%X hrEnd = 0x%X\n", hrDisjoint, hrStart, hrEnd);
			if (hrDisjoint == S_OK && hrStart == S_OK && hrEnd == S_OK && !disjointData.Disjoint) {
				double frequency = static_cast<double>(disjointData.Frequency);
				double timeMs = (endTime - startTime) / frequency * 1000.0;
				m_GPUframeTime = timeMs;

				// Monitor GPU usage
				GpuMonitor();
				// printf("GPU time = %.3f ms m_GPUlatestUsage = %.2f\n", m_GPUframeTime, m_GPUlatestUsage);
				// Testing shows GPU time per cycle between 0.3 and 1.0 msec for the current shader
				// If the frame time exceeds 4 msec it is likely throttled
				if (m_GPUframeTime > 4.0 && m_GPUlatestUsage > 0.0) {
					// Increment the number of frames with excessive time
					m_nGPUex++;
					// Repeat the test 120 frames later to avoid spikes
					// Record peak GPU usage and skip the shader until it decreases.
					if (m_nGPUex > 1) {
						// Record the peak GPU usage at this time
						m_GPUpeakUsage = m_GPUlatestUsage;
						// Wait for GPU usage to drop 20% lower but not less than 50%
						m_GPUrecovery = max(m_GPUpeakUsage-0.20f, 0.5f);
						// printf("Excess at %.3f pct - waiting for %.3f pct \n", m_GPUpeakUsage, m_GPUrecovery);
						bGPUex = true;
						m_nGPUex = 0;
					} // endif (m_nGPUex > 1)
				} // endif m_GPUframeTime > 4.0
				m_readPending = false; // Start timing again
			}
		}
	}

	//---------------------------------------------------------
	// Function: GPUmonitor
	//    Monitor GPU load and calculate percent usage
	void GpuMonitor()
	{
		// Initialize GPU performance query thread
		InitializeGPUquery();

		// Wait for the thread to complete
		if (!m_GPUQueryReady.load())
			return;

		// Skip the function if no success
		if (!m_GPUquery)
			return;

		if (PdhCollectQueryData(m_GPUquery) != ERROR_SUCCESS)
			return;

		if (!m_GPUhasPrimedSample) {
			m_GPUhasPrimedSample = true;
			return; // 2 samples are required before valid data
		}

		DWORD bufferSize = 0, itemCount = 0;
		PDH_STATUS status = PdhGetFormattedCounterArray(m_GPUcounter,
			PDH_FMT_DOUBLE, &bufferSize, &itemCount, NULL);

		if (status != PDH_MORE_DATA || bufferSize == 0)
			return;

		PDH_FMT_COUNTERVALUE_ITEM* items = (PDH_FMT_COUNTERVALUE_ITEM*)malloc(bufferSize);
		if (!items)
			return;

		status = PdhGetFormattedCounterArray(m_GPUcounter,
			PDH_FMT_DOUBLE, &bufferSize, &itemCount, items);

		if (status != ERROR_SUCCESS) {
			free(items);
			return;
		}

		double totalUsage = 0.0;
		for (DWORD i = 0; i < itemCount; ++i) {
			totalUsage += items[i].FmtValue.doubleValue;
		}

		m_GPUlatestUsage = totalUsage;
		free(items);
		
		// Wait until the GPU usage drops to the recovery level
		if (m_GPUrecovery > 0.0 && m_GPUlatestUsage <= m_GPUrecovery) {
			// printf("Recovering from GPU usage\n");
			m_GPUrecovery = 0.0;
			bGPUex = false; // Return to using compute shader
		}

	}

	//---------------------------------------------------------
	// Function: ComputeShader
	//     Use a compute shader to copy differing format textures to BRGA
	//     RGBA, 16bit RGBA, 10bit RGBA float, 16bit RGBA float, 32bit RGBA float
	//     Obtain the sharehandle of the destination BGRA texture for DirectX9
	bool ComputeShader(std::string shaderSource, // shader source code string
		ID3D11Texture2D * destTexture,
		ID3D11Texture2D * sourceTexture,
		DXGI_FORMAT destFormat,
		DXGI_FORMAT sourceFormat,
		unsigned int sourceWidth, unsigned int sourceHeight,
		HANDLE * shareHandle = nullptr)
 {
		// Source texture can be null if reading and writing to dest
		if (shaderSource.empty() || !destTexture || !m_pd3dDevice)
			return false;

		// Make sure the GPU supports UAV typed store for BGRA texture format
		if(!CheckUAVStoreSupport(m_pd3dDevice, DXGI_FORMAT_B8G8R8A8_UNORM))
			return false;

		// Bypass for compute shader timeout
		if (bGPUex) {
			// Monitor GPU usage every 2 seconds for recovery
			if (m_frameCount % 120 == 0) {
				GpuMonitor();
			}
			m_frameCount++;
			return false;
		}

		// Update or create shader resources
		//   Shader resource view (SRV) for the source texture
		//   Unordered access view (UAV) for the destination texture
		if (!CreateShaderResources(m_pd3dDevice, destTexture, sourceTexture,
				destFormat, sourceFormat, sourceWidth, sourceHeight)) {
			printf("spoutDXshaders::ComputeShader - CreateShaderResources failed\n");
			return false;
		}

		// Create the compute shader copy program from source passed in
		if (!m_CopyProgram)
			m_CopyProgram = CreateDXcomputeShader(m_pd3dDevice, shaderSource.c_str());

		// Return if failed
		if (!m_CopyProgram)
			return false;

		// Start gpu timing every 120 frames
		StartGPUtiming();

		// Bind the shader Constant Buffer to the Compute Shader
		m_pImmediateContext->CSSetConstantBuffers(0, 1, &m_pShaderBuffer);
		// Bind SRV and UAV
		if (m_srv) m_pImmediateContext->CSSetShaderResources(0, 1, &m_srv);
		m_pImmediateContext->CSSetUnorderedAccessViews(0, 1, &m_uav, nullptr);
		// Set the current shader program
		m_pImmediateContext->CSSetShader(m_CopyProgram, nullptr, 0);
		// Dispatch: with 16x16 threads
		m_pImmediateContext->Dispatch((sourceWidth + 15) / 16, (sourceHeight + 15) / 16, 1);
		// Unbind SRV and UAV
		if (m_srv) {
			ID3D11ShaderResourceView * nullSRV[1] = { nullptr };
			m_pImmediateContext->CSSetShaderResources(0, 1, nullSRV);
		}
		ID3D11UnorderedAccessView * nullUAV[1] = { nullptr };
		m_pImmediateContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

		// End GPU timing every 120 frames
		EndGPUtiming();

		// Read counters 4 frames later to avoid GPU latency
		// If shader execution exceeds 4 seconds set a bypass flag,
		// record the GPU usage at that time, monitor GPU usage
		// thereafter and recover when it has reduced by 20%.
		ReadGPUtiming();

		m_frameCount++;

		// Extract the share handle used for DirectX 9
		if (shareHandle) {
			HANDLE sh = nullptr;
			IDXGIResource * dxgiResource = nullptr;
			HRESULT hr = destTexture->QueryInterface(__uuidof(IDXGIResource), (void **)&dxgiResource);
			if (SUCCEEDED(hr) && dxgiResource) {
				hr = dxgiResource->GetSharedHandle(&sh);
				dxgiResource->Release();
				// Return the sharehandle for DX9 to get the texture copy
				*shareHandle = sh;
			}
			else {
				printf("spoutDXshaders::ComputeShader : GetSharedHandle failed\n");
				return false;
			}
		}

		// Flush to make sure the result is ready immediately.
		// (0.1 - 0.2 msec overhead)
		m_pImmediateContext->Flush();

		return true;
	}


	//---------------------------------------------------------
	// Function: CheckUAVStoreSupport
	//    Check GPU support for UAV typed store for a texture format
	bool CheckUAVStoreSupport(ID3D11Device* pDevice, DXGI_FORMAT format)
	{
		if (!pDevice)
			return false;

		D3D11_FEATURE_DATA_FORMAT_SUPPORT2 support2 {};
		support2.InFormat = format;
		HRESULT hr = pDevice->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2,
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

	// GPU timers
	ULONG m_frameCount = 0UL;
	ULONG m_frameEnd = 0UL; // Frame number at timing end
	double m_GPUframeTime = 0.0;
	bool m_readPending = false; // Ready to read counters
	bool bGPUex = false; // GPU usage excessive
	int m_nGPUex = 0; // Number of excessive frames

	// Queries and counters
	PDH_HQUERY m_GPUquery;
	PDH_HCOUNTER m_GPUcounter;

	PDH_STATUS m_GPUstatus;
		struct GpuTimerQuerySet {
		ID3D11Query* timestampStart;
		ID3D11Query* timestampEnd;
		ID3D11Query* disjoint;
	};
	GpuTimerQuerySet m_gpuTimer;

	// GPU performance
	ULONGLONG m_GPUlastSampleTime = 0;
	bool m_GPUhasPrimedSample = false;
	double m_GPUlatestUsage = 0.0;
	double m_GPUpeakUsage = 0.0;
	double m_GPUrecovery = 0.0;

	// Initialization thread
	std::once_flag g_GPUInitOnce;
	std::atomic<bool> m_GPUQueryReady = false;

	// Destination BGRA texture for D3D9 compute shader copy
	ID3D11Texture2D* m_dstTexture = nullptr;
	unsigned int m_dstWidth = 0;
	unsigned int m_dstHeight = 0;
	DXGI_FORMAT m_senderFormat = DXGI_FORMAT_UNKNOWN; // For detection of source format change
	unsigned int m_senderWidth = 0;
	unsigned int m_senderHeight = 0;
	HANDLE m_senderHandle = 0; // For detection of share handle change
	bool m_bCopy = false; // Force shader copy

	// Compute shader resources
	ID3D11UnorderedAccessView* m_uav = nullptr;
	ID3D11ShaderResourceView* m_srv = nullptr;

	// Copy shader program for DirectX 9
	ID3D11ComputeShader* m_CopyProgram = nullptr;
	
	// Shader parameters
	struct m_ShaderParams
	{
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

	//
	// Equivalents to SpoutDirectX so that this class can be used independently
	//

	// Create DX11 device
	ID3D11Device* CreateDX11device()
	{
		ID3D11Device* pd3dDevice = nullptr;
		HRESULT hr = S_OK;
		UINT createDeviceFlags = 0;

		// D3D11_CREATE_DEVICE_FLAG createDeviceFlags
		const D3D_DRIVER_TYPE driverTypes[] = {
			D3D_DRIVER_TYPE_HARDWARE,
			D3D_DRIVER_TYPE_WARP,
			D3D_DRIVER_TYPE_REFERENCE,
		};

		const UINT numDriverTypes = ARRAYSIZE(driverTypes);

		// These are the feature levels that we will accept.
		// m_featureLevel is the maximum supported feature level used
		// 11.0 is the highest level supported
		// 11.1 is not compatible with DirectX 9 applications
		// that use the Microsoft DirectX SDK (June 2010)
		D3D_DRIVER_TYPE m_driverType = D3D_DRIVER_TYPE_NULL;
		D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;
		const D3D_FEATURE_LEVEL featureLevels[] = {
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

}; // endif _spoutDX9shaders_

#endif
