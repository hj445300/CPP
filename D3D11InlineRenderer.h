#pragma once
// Copyright (c) 2010-2018 Lockheed Martin Corporation. All rights reserved.
// Use of this file is bound by the PREPAR3D® SOFTWARE DEVELOPER KIT END USER LICENSE AGREEMENT

// D3D11SampleInlineRenderer
// Description: This Example will render a cursor into Prepar3D as a new Texture or as an Effect.  
#pragma once

#include <ISimObject.h>
#include <ISimObjectAttachments.h>
#include <ISimObjectDIS.h>
#include "IWindowPluginSystem.h"
#include "IUnknownHelper.h"
#include "RenderingPlugin.h"
#include "PdkPlugin.h"
#include "PdkServices.h"

#include <atomic>
#include <thread>
#include <iostream>

#pragma warning ( push )
#pragma warning (disable : 4005)
#include <d3d11.h>
#include <DirectXCollision.h>	// 已包含DirectXMath.h
#include <d3dcompiler.h>
#pragma warning( pop )

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include "ShareMM.h"
#include <wrl/client.h>

using namespace Microsoft::WRL;
using namespace DirectX;

#define SAFE_RELEASE(p) { if ((p)) { (p)->Release(); (p) = nullptr; } }

HRESULT CreateShaderFromFile(
	const WCHAR* csoFileNameInOut,
	const WCHAR* hlslFileName,
	LPCSTR entryPoint,
	LPCSTR shaderModel,
	ID3DBlob** ppBlobOut)
{
	HRESULT hr = S_OK;

	// 寻找是否有已经编译好的顶点着色器
	if (csoFileNameInOut && D3DReadFileToBlob(csoFileNameInOut, ppBlobOut) == S_OK)
	{
		return hr;
	}
	else
	{
		DWORD dwShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
		// 设置 D3DCOMPILE_DEBUG 标志用于获取着色器调试信息。该标志可以提升调试体验，
		// 但仍然允许着色器进行优化操作
		dwShaderFlags |= D3DCOMPILE_DEBUG;

		// 在Debug环境下禁用优化以避免出现一些不合理的情况
		dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
		ID3DBlob* errorBlob = nullptr;
		hr = D3DCompileFromFile(hlslFileName, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, shaderModel,
			dwShaderFlags, 0, ppBlobOut, &errorBlob);
		if (FAILED(hr))
		{
			if (errorBlob != nullptr)
			{
				OutputDebugStringA(reinterpret_cast<const char*>(errorBlob->GetBufferPointer()));
			}
			SAFE_RELEASE(errorBlob);
			return hr;
		}

		// 若指定了输出文件名，则将着色器二进制信息输出
		if (csoFileNameInOut)
		{
			return D3DWriteBlobToFile(*ppBlobOut, csoFileNameInOut, FALSE);
		}
	}

	return hr;
}


// Simple d3d11 renderer which implements the custom render callback interface
// so that it can be registered with Prepar3D for rendering custom textures
// and effects
class D3D11TextureInlineRenderer : public P3D::RenderingPlugin, public P3D::PdkPlugin
{
public:


	D3D11TextureInlineRenderer(std::wstring name, int width, int height);
	~D3D11TextureInlineRenderer();

	/* IRenderingPluginV400 */
	virtual void Render(P3D::IRenderDataV400* pRenderData) override;
	virtual void PreRender(P3D::IRenderDataV400* pRenderData) override;

	void UpdateRTTTexture(unsigned char * data, uint32_t len);

private:

	typedef struct
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Tex;
	} SIMPLEVERTEX;

	// Init resources and buffers
	HRESULT InitDevice(ID3D11Device* pDevice);

	// Call render and with the RT passed in
	void Render(ID3D11RenderTargetView* pRenderTarget, FLOAT width, FLOAT height);


	ComPtr<ID3D11Device> mDevice;
	ComPtr<ID3D11VertexShader> mVertexShader;
	ComPtr<ID3D11PixelShader> mPixelShader;
	ComPtr<ID3D11InputLayout> mVertexLayout;
	ComPtr<ID3D11Buffer> mVertexBuffer;
	ComPtr<ID3D11Buffer> mIndexBuffer;
	ComPtr<ID3D11SamplerState> mSamplerLinear;
	ComPtr<ID3D11Texture2D> mRTT_Texture2D;
	ComPtr<ID3D11ShaderResourceView> mRTT_ShaderResView;

	bool m_initDeviceSucc = false;
	std::atomic<int> currentReadBufferIndex = 0;
	std::atomic<int> currentWriteBufferIndex = 1;
	ID3D11CommandList* commandBuffers[3] = { nullptr, nullptr, nullptr };

	int m_width = 0;
	int m_height = 0;
	unsigned char* texData = nullptr;
	ShareMM* tex_ShareMM = nullptr;
};

uint32_t index = 0;
ID3D11CommandList* cachedCommandList = nullptr;

bool isBufferUpdated = false;

using namespace P3D;

D3D11TextureInlineRenderer::D3D11TextureInlineRenderer(std::wstring name, int width, int height) :
	m_width(width),
	m_height(height),
	currentReadBufferIndex(0),
	currentWriteBufferIndex(1),
	m_initDeviceSucc(false)
{
	std::thread t([=] {

		do
		{
			Sleep(100);
		} while (!m_initDeviceSucc);

		tex_ShareMM = new ShareMM(name, width * height * 4);
		tex_ShareMM->initialize(FILE_MAP_READ);

		texData = new unsigned char[width * height * 4];

		//while (true)
		//{
		//	UpdateRTTTexture();
		//	Sleep(1);
		//}

		//delete[] tex_ShareMM;
		//delete[] texData;

		});
	t.detach();
}

D3D11TextureInlineRenderer::~D3D11TextureInlineRenderer()
{

}

void D3D11TextureInlineRenderer::UpdateRTTTexture(unsigned char* data, uint32_t len)
{
	if (!mDevice) {
		return;
	}

	CComPtr<ID3D11DeviceContext> pDeviceContext;
	mDevice->CreateDeferredContext(0, &pDeviceContext);

	// Check if pDeviceContext is null
	if (!pDeviceContext) {
		OutputDebugStringA("Failed to create deferred context.\n");
		return;
	}

	// 开始计时
	auto cpuStart = std::chrono::high_resolution_clock::now();
	OutputDebugStringA(std::string("Start UpdateRTTTexture \n").c_str());

	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT hr = pDeviceContext->Map(mRTT_Texture2D.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (SUCCEEDED(hr)) {
		memcpy(mappedResource.pData, data, len);
		pDeviceContext->Unmap(mRTT_Texture2D.Get(), 0);
		isBufferUpdated = true;
	}
	else {
		isBufferUpdated = false;
	}

	pDeviceContext->FinishCommandList(FALSE, &cachedCommandList);

	if (isBufferUpdated) {
		int writeIndex = currentWriteBufferIndex.load(std::memory_order_acquire);
		if (commandBuffers[writeIndex]) {
			commandBuffers[writeIndex]->Release();
		}
		commandBuffers[writeIndex] = cachedCommandList;

		int oldReadIndex = currentReadBufferIndex.exchange(writeIndex, std::memory_order_acq_rel);
		currentWriteBufferIndex.store(1 - oldReadIndex, std::memory_order_release);
	}

	index++;
	//auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	//OutputDebugStringA(std::string("ts " + std::to_string(ts) + " index is " + std::to_string(index) + +"\n").c_str());

	// 渲染结束，计时结束
	auto cpuEnd = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double, std::milli> cpuTime = cpuEnd - cpuStart;
	OutputDebugStringA(std::string("CPU execution time: " + std::to_string(cpuTime.count()) + " ms\n").c_str());
}

//--------------------------------------------------------------------------------------
// Render callback
//--------------------------------------------------------------------------------------
void D3D11TextureInlineRenderer::Render(IRenderDataV400* pRenderData)
{
	if (!pRenderData)
	{
		return;
	}
	ComPtr<ID3D11RenderTargetView> spRenderTarget = pRenderData->GetOutputColor();
	if (spRenderTarget == NULL)
	{
		return;
	}

	CComPtr<ID3D11DeviceContext> pDeviceContext = NULL;
	mDevice->GetImmediateContext(&pDeviceContext);

	if (pDeviceContext == NULL)
	{
		return;
	}

	int readIndex = currentReadBufferIndex.load(std::memory_order_acquire);

	if (commandBuffers[readIndex]) {
		pDeviceContext->ExecuteCommandList(commandBuffers[readIndex], FALSE);
		commandBuffers[readIndex]->Release();
		commandBuffers[readIndex] = nullptr;
	}

	// Setup the viewport
	D3D11_VIEWPORT vp;
	vp.Width = pRenderData->GetTextureWidth();
	vp.Height = pRenderData->GetTextureHeight();
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;

	pDeviceContext->ClearState();
	pDeviceContext->RSSetViewports(1, &vp);

	static float ClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f }; // red,green,blue,alpha

	pDeviceContext->ClearRenderTargetView(spRenderTarget.Get(), ClearColor);

	pDeviceContext->OMSetRenderTargets(1, spRenderTarget.GetAddressOf(), NULL);

	pDeviceContext->IASetInputLayout(mVertexLayout.Get());

	// Set vertex buffer
	UINT stride = sizeof(SIMPLEVERTEX);
	UINT offset = 0;
	pDeviceContext->IASetVertexBuffers(0, 1, mVertexBuffer.GetAddressOf(), &stride, &offset);
	// Set index buffer
	pDeviceContext->IASetIndexBuffer(mIndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
	// Set primitive topology
	pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	pDeviceContext->VSSetShader(mVertexShader.Get(), NULL, 0);

	pDeviceContext->PSSetShader(mPixelShader.Get(), NULL, 0);
	pDeviceContext->PSSetShaderResources(0, 1, mRTT_ShaderResView.GetAddressOf());
	pDeviceContext->PSSetSamplers(0, 1, mSamplerLinear.GetAddressOf());
	pDeviceContext->DrawIndexed(6, 0, 0);

	pDeviceContext->OMSetRenderTargets(0, NULL, NULL);

	//auto ts = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	//OutputDebugStringA(std::string("Bts " + std::to_string(ts) + " index is " + std::to_string(index) + +"\n").c_str());
	OutputDebugStringA(std::string("Now render the texture \n").c_str());
}


//--------------------------------------------------------------------------------------
// PreRender callback
//--------------------------------------------------------------------------------------
bool isExcuted = false;

void D3D11TextureInlineRenderer::PreRender(P3D::IRenderDataV400* pRenderData)
{
	if (!pRenderData) {
		return;
	}

	if (mDevice == NULL) {
		mRenderFlags.RenderingIsEnabled = false;
		pRenderData->SetRenderFlags(mRenderFlags);
	}

	CComPtr<ID3D11Device> spDevice = pRenderData->GetDevice();
	if (spDevice == NULL) {
		return;
	}

	if (!isExcuted) {
		if (FAILED(InitDevice(spDevice))) {
			return;
		}
		isExcuted = true;
	}

	mRenderFlags.RenderingIsEnabled = (pRenderData->GetRenderPass() == RenderPassDefault);

	mRenderFlags.WillReadColor = false;

	pRenderData->SetRenderFlags(mRenderFlags);
}


static const wchar_t MOUDLE_NAME[] = L"SharedMMTexture.dll";
static const wchar_t HLSL_FILE[] = L"ShadersHLSL\\Example.fx";
//--------------------------------------------------------------------------------------
// Create Direct3D device and swap chain
//--------------------------------------------------------------------------------------
HRESULT D3D11TextureInlineRenderer::InitDevice(ID3D11Device* pDevice)
{
	HRESULT hr = S_OK;
	if (mDevice.Get() == pDevice) return hr;
	mDevice = pDevice;

	// get full file path to this dll
	wchar_t path[2048];
	GetModuleFileNameW(GetModuleHandleW(MOUDLE_NAME), path, 2048);
	// subtract length of dll filename to get the start index for a local file.
	size_t folderIndex = wcslen(path) - wcslen(MOUDLE_NAME);
	// copy shader file name to create full path
	size_t uSize = max(ARRAYSIZE(path) - folderIndex - 1, 0);  //How much space available to write to.
	wcscpy_s(&path[folderIndex], uSize, &HLSL_FILE[0]);

	// Compile the vertex shader
	ID3DBlob* pVSBlob = NULL;
	hr = CreateShaderFromFile(NULL, path, "VS", "vs_4_0", &pVSBlob);
	if (FAILED(hr))
	{
		return hr;
	}

	// Create the vertex shader
	hr = pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), NULL, &mVertexShader);
	if (FAILED(hr))
	{
		pVSBlob->Release();
		return hr;
	}
	// Define the input layout
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT   , 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = ARRAYSIZE(layout);

	// Create the input layout
	hr = pDevice->CreateInputLayout(layout, numElements, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &mVertexLayout);
	pVSBlob->Release();
	if (FAILED(hr))
	{
		return hr;
	}

	// Compile the pixel shader
	ID3DBlob* pPSBlob = NULL;
	hr = CreateShaderFromFile(NULL, path, "PS", "ps_4_0", &pPSBlob);
	if (FAILED(hr))
	{
		return hr;
	}

	// Create the pixel shader
	hr = pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), NULL, mPixelShader.GetAddressOf());
	pPSBlob->Release();
	if (FAILED(hr))
	{
		return hr;
	}

	// Create vertex buffer
	float u = (float)1, v = (float)1;
	SIMPLEVERTEX vertices[] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, 0.5f), XMFLOAT2(0, 0) },
		{ XMFLOAT3(1.0f, -1.0f, 0.5f), XMFLOAT2(1, 0) },
		{ XMFLOAT3(1.0f,  1.0f, 0.5f), XMFLOAT2(1, 1) },
		{ XMFLOAT3(-1.0f,  1.0f, 0.5f), XMFLOAT2(0, 1) },
	};

	D3D11_BUFFER_DESC bd;
	ZeroMemory(&bd, sizeof(bd));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(SIMPLEVERTEX) * 4;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bd.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA InitData;
	ZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = vertices;

	hr = pDevice->CreateBuffer(&bd, &InitData, mVertexBuffer.GetAddressOf());
	if (FAILED(hr))
	{
		return hr;
	}

	WORD indices[] =
	{
		3,1,0,
		2,1,3,
	};

	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.ByteWidth = sizeof(WORD) * 6;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bd.CPUAccessFlags = 0;
	InitData.pSysMem = indices;
	hr = pDevice->CreateBuffer(&bd, &InitData, mIndexBuffer.GetAddressOf());
	if (FAILED(hr))
	{
		return hr;
	}

	// Create the sample state
	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = pDevice->CreateSamplerState(&sampDesc, mSamplerLinear.GetAddressOf());
	if (FAILED(hr))
	{
		return hr;
	}

	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.Width = m_width;
	texDesc.Height = m_height;
	texDesc.MipLevels = 1;
	texDesc.ArraySize = 1;
	texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DYNAMIC;
	texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	texDesc.MiscFlags = 0;

	hr = pDevice->CreateTexture2D(&texDesc, nullptr, mRTT_Texture2D.GetAddressOf());
	if (FAILED(hr))
	{
		return hr;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = (UINT)-1;
	srvDesc.Texture2D.MostDetailedMip = 0;

	hr = pDevice->CreateShaderResourceView(mRTT_Texture2D.Get(), &srvDesc, mRTT_ShaderResView.GetAddressOf());
	if (FAILED(hr))
	{
		return hr;
	}

	if (!m_initDeviceSucc) {
		m_initDeviceSucc = true;
	}

	return S_OK;
}