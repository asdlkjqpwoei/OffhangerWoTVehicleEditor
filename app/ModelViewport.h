#pragma once
#include "Geometry.h"
#include "UiCommon.h"
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi1_3.h>
#include <set>

namespace OffHanger::Ui
{
class ModelViewport : public std::enable_shared_from_this<ModelViewport>
{
	struct DrawRange
	{
		uint32_t Material{}, FirstVertex{}, VertexCount{};
	};
	Controls::SwapChainPanel Panel{nullptr};
	winrt::com_ptr<ID3D11Device> Device;
	winrt::com_ptr<ID3D11DeviceContext> Context;
	winrt::com_ptr<IDXGISwapChain1> SwapChain;
	winrt::com_ptr<ID3D11RenderTargetView> Target;
	winrt::com_ptr<ID3D11DepthStencilView> Depth;
	winrt::com_ptr<ID3D11VertexShader> VertexShader;
	winrt::com_ptr<ID3D11PixelShader> PixelShader;
	winrt::com_ptr<ID3D11InputLayout> InputLayout;
	winrt::com_ptr<ID3D11Buffer> Vertices, Constants;
	winrt::com_ptr<ID3D11RasterizerState> Solid;
	std::vector<DrawRange> Ranges;
	std::shared_ptr<Mesh> Geometry;
	Vector3 Center;
	float Yaw = .5f, Pitch = .25f, Distance = 10, Radius = 3;
	float LastX{}, LastY{}, PressX{}, PressY{};
	uint32_t PixelWidth{}, PixelHeight{};
	bool Dragging{};
	bool Active = true;
	uint32_t SelectedMaterial = UINT32_MAX;
	void Initialize();
	void Resize();
	void Render();
	void Pick(float X, float Y);
	DirectX::XMMATRIX ViewProjection() const;
	Vector3 Eye() const;

  public:
	static std::shared_ptr<ModelViewport> Create();
	Controls::SwapChainPanel Element() const
	{
		return Panel;
	}
	void SetMesh(std::shared_ptr<Mesh> Mesh);
	void Fit();
	void SetActive(bool Enabled);
	void SelectMaterial(uint32_t Index);
	std::function<void(uint32_t)> OnPicked;
	std::function<void(std::string const&)> OnError;
};
} // namespace OffHanger::Ui
