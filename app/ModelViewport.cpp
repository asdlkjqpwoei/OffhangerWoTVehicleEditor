#include "ModelViewport.h"
#include <d3dcompiler.h>
#include <microsoft.ui.xaml.media.dxinterop.h>
#include <winrt/Microsoft.UI.Input.h>

namespace OffHanger::Ui
{
using namespace DirectX;
using namespace winrt;
namespace
{
struct GpuVertex
{
	Vector3 Position, Normal;
};
struct alignas(16) GpuConstants
{
	XMFLOAT4X4 Matrix;
	XMFLOAT4 Color;
};
constexpr char ShaderSource[] = R"(
cbuffer Frame : register(b0) { row_major float4x4 WorldViewProjection; float4 MaterialColor; };
struct VertexInput { float3 Position : POSITION; float3 Normal : NORMAL; };
struct PixelInput { float4 Position : SV_POSITION; float3 Normal : NORMAL; };
PixelInput VertexMain(VertexInput Input) { PixelInput Output; Output.Position = mul(float4(Input.Position,1), WorldViewProjection); Output.Normal = Input.Normal; return Output; }
float4 PixelMain(PixelInput Input) : SV_TARGET { float Light = .58 + .42 * abs(dot(normalize(Input.Normal), normalize(float3(.3,.8,-.5)))); return float4(MaterialColor.rgb * Light,1); }
)";
com_ptr<ID3DBlob> Compile(char const* Entry, char const* Target)
{
	com_ptr<ID3DBlob> Code, Error;
	auto Result = D3DCompile(
		ShaderSource, sizeof(ShaderSource) - 1, "OffHanger.ArmorViewport", nullptr, nullptr, Entry, Target,
		D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, Code.put(), Error.put());
	if (FAILED(Result) && Error)
		throw std::runtime_error(
			std::string(static_cast<char const*>(Error->GetBufferPointer()), Error->GetBufferSize()));
	check_hresult(Result);
	return Code;
}
} // namespace
std::shared_ptr<ModelViewport> ModelViewport::Create()
{
	auto Result = std::shared_ptr<ModelViewport>(new ModelViewport);
	Result->Initialize();
	return Result;
}
void ModelViewport::Initialize()
{
	Panel = Controls::SwapChainPanel();
	Panel.MinHeight(250);
	auto Weak = weak_from_this();
	Panel.Loaded(
		[Weak](auto&&, auto&&)
		{
			if (auto Self = Weak.lock())
				Self->Render();
		});
	Panel.SizeChanged(
		[Weak](auto&&, auto&&)
		{
			if (auto Self = Weak.lock())
				Self->Render();
		});
	Panel.PointerPressed(
		[Weak](auto&&, Xaml::Input::PointerRoutedEventArgs const& Arguments)
		{
			if (auto Self = Weak.lock())
			{
				auto Point = Arguments.GetCurrentPoint(Self->Panel);
				if (!Point.Properties().IsLeftButtonPressed())
					return;
				Self->LastX = Self->PressX = Point.Position().X;
				Self->LastY = Self->PressY = Point.Position().Y;
				Self->Dragging = true;
				Self->Panel.CapturePointer(Arguments.Pointer());
				Arguments.Handled(true);
			}
		});
	Panel.PointerMoved(
		[Weak](auto&&, Xaml::Input::PointerRoutedEventArgs const& Arguments)
		{
			if (auto Self = Weak.lock(); Self && Self->Dragging)
			{
				auto Position = Arguments.GetCurrentPoint(Self->Panel).Position();
				auto DeltaX = Position.X - Self->LastX, DeltaY = Position.Y - Self->LastY;
				Self->Yaw += DeltaX * .008f;
				Self->Pitch = std::clamp(Self->Pitch + DeltaY * .008f, -1.5f, 1.5f);
				Self->LastX = Position.X;
				Self->LastY = Position.Y;
				Self->Render();
			}
		});
	Panel.PointerReleased(
		[Weak](auto&&, Xaml::Input::PointerRoutedEventArgs const& Arguments)
		{
			if (auto Self = Weak.lock(); Self && Self->Dragging)
			{
				auto Point = Arguments.GetCurrentPoint(Self->Panel);
				if (Point.Properties().IsLeftButtonPressed())
					return;
				auto Position = Point.Position();
				bool Click = std::hypot(Position.X - Self->PressX, Position.Y - Self->PressY) < 4;
				Self->Dragging = false;
				Self->Panel.ReleasePointerCapture(Arguments.Pointer());
				if (Click)
					Self->Pick(Position.X, Position.Y);
				Arguments.Handled(true);
			}
		});
	Panel.PointerCaptureLost(
		[Weak](auto&&, auto&&)
		{
			if (auto Self = Weak.lock())
				Self->Dragging = false;
		});
	Panel.PointerWheelChanged(
		[Weak](auto&&, Xaml::Input::PointerRoutedEventArgs const& Arguments)
		{
			if (auto Self = Weak.lock())
			{
				auto Delta = Arguments.GetCurrentPoint(Self->Panel).Properties().MouseWheelDelta();
				Self->Distance = std::clamp(Self->Distance * std::exp(-Delta * .001f), Self->Radius * .1f,
											Self->Radius * 50);
				Self->Render();
				Arguments.Handled(true);
			}
		});
	D3D_FEATURE_LEVEL Level{};
	auto Result =
		D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
						  nullptr, 0, D3D11_SDK_VERSION, Device.put(), &Level, Context.put());
	if (FAILED(Result))
		check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
										D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
										Device.put(), &Level, Context.put()));
	auto VertexCode = Compile("VertexMain", "vs_4_0"), PixelCode = Compile("PixelMain", "ps_4_0");
	check_hresult(Device->CreateVertexShader(VertexCode->GetBufferPointer(), VertexCode->GetBufferSize(),
											 nullptr, VertexShader.put()));
	check_hresult(Device->CreatePixelShader(PixelCode->GetBufferPointer(), PixelCode->GetBufferSize(),
											nullptr, PixelShader.put()));
	D3D11_INPUT_ELEMENT_DESC Elements[] = {
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}};
	check_hresult(Device->CreateInputLayout(Elements, UINT(std::size(Elements)),
											VertexCode->GetBufferPointer(), VertexCode->GetBufferSize(),
											InputLayout.put()));
	D3D11_BUFFER_DESC Buffer{};
	Buffer.ByteWidth = sizeof(GpuConstants);
	Buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	Buffer.Usage = D3D11_USAGE_DEFAULT;
	check_hresult(Device->CreateBuffer(&Buffer, nullptr, Constants.put()));
	D3D11_RASTERIZER_DESC Raster{};
	Raster.FillMode = D3D11_FILL_SOLID;
	Raster.CullMode = D3D11_CULL_NONE;
	Raster.DepthClipEnable = true;
	check_hresult(Device->CreateRasterizerState(&Raster, Solid.put()));
}
void ModelViewport::Resize()
{
	auto Width = UINT(std::max(1., Panel.ActualWidth() * Panel.CompositionScaleX()));
	auto Height = UINT(std::max(1., Panel.ActualHeight() * Panel.CompositionScaleY()));
	if (Width == PixelWidth && Height == PixelHeight && SwapChain)
		return;
	Context->OMSetRenderTargets(0, nullptr, nullptr);
	Target = nullptr;
	Depth = nullptr;
	if (SwapChain)
		check_hresult(SwapChain->ResizeBuffers(2, Width, Height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
	else
	{
		auto DxgiDevice = Device.as<IDXGIDevice>();
		com_ptr<IDXGIAdapter> Adapter;
		check_hresult(DxgiDevice->GetAdapter(Adapter.put()));
		com_ptr<IDXGIFactory2> Factory;
		check_hresult(Adapter->GetParent(IID_PPV_ARGS(Factory.put())));
		DXGI_SWAP_CHAIN_DESC1 Description{};
		Description.Width = Width;
		Description.Height = Height;
		Description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		Description.SampleDesc.Count = 1;
		Description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		Description.BufferCount = 2;
		Description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
		Description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
		check_hresult(
			Factory->CreateSwapChainForComposition(Device.get(), &Description, nullptr, SwapChain.put()));
		check_hresult(Panel.as<ISwapChainPanelNative>()->SetSwapChain(SwapChain.get()));
	}
	com_ptr<ID3D11Texture2D> BackBuffer;
	check_hresult(SwapChain->GetBuffer(0, IID_PPV_ARGS(BackBuffer.put())));
	check_hresult(Device->CreateRenderTargetView(BackBuffer.get(), nullptr, Target.put()));
	D3D11_TEXTURE2D_DESC Description{};
	Description.Width = Width;
	Description.Height = Height;
	Description.MipLevels = Description.ArraySize = 1;
	Description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	Description.SampleDesc.Count = 1;
	Description.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	com_ptr<ID3D11Texture2D> DepthTexture;
	check_hresult(Device->CreateTexture2D(&Description, nullptr, DepthTexture.put()));
	check_hresult(Device->CreateDepthStencilView(DepthTexture.get(), nullptr, Depth.put()));
	PixelWidth = Width;
	PixelHeight = Height;
	DXGI_MATRIX_3X2_F Scale{1.f / Panel.CompositionScaleX(), 0, 0, 1.f / Panel.CompositionScaleY(), 0, 0};
	check_hresult(SwapChain.as<IDXGISwapChain2>()->SetMatrixTransform(&Scale));
}
Vector3 ModelViewport::Eye() const
{
	return Center +
		   Vector3{std::cos(Pitch) * std::sin(Yaw), std::sin(Pitch), std::cos(Pitch) * std::cos(Yaw)} *
			   Distance;
}
XMMATRIX ModelViewport::ViewProjection() const
{
	auto Camera = Eye();
	auto View = XMMatrixLookAtLH(XMVectorSet(Camera.X, Camera.Y, Camera.Z, 1),
								 XMVectorSet(Center.X, Center.Y, Center.Z, 1), XMVectorSet(0, 1, 0, 0));
	auto Aspect = float(std::max(1., Panel.ActualWidth()) / std::max(1., Panel.ActualHeight()));
	return View * XMMatrixPerspectiveFovLH(XM_PIDIV4, Aspect, std::max(.001f, Radius * .001f), Radius * 200);
}
void ModelViewport::Render()
{
	try
	{
		if (!Active || Panel.ActualWidth() < 1 || Panel.ActualHeight() < 1)
			return;
		Resize();
		float Background[] = {.055f, .067f, .09f, 1};
		Context->ClearRenderTargetView(Target.get(), Background);
		Context->ClearDepthStencilView(Depth.get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
		auto TargetPointer = Target.get();
		Context->OMSetRenderTargets(1, &TargetPointer, Depth.get());
		D3D11_VIEWPORT Viewport{0, 0, float(PixelWidth), float(PixelHeight), 0, 1};
		Context->RSSetViewports(1, &Viewport);
		Context->RSSetState(Solid.get());
		if (Geometry && Vertices)
		{
			auto VertexBuffer = Vertices.get();
			UINT Stride = sizeof(GpuVertex), Offset{};
			Context->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);
			Context->IASetInputLayout(InputLayout.get());
			Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			Context->VSSetShader(VertexShader.get(), nullptr, 0);
			Context->PSSetShader(PixelShader.get(), nullptr, 0);
			auto Buffer = Constants.get();
			Context->VSSetConstantBuffers(0, 1, &Buffer);
			Context->PSSetConstantBuffers(0, 1, &Buffer);
			GpuConstants Frame{};
			XMStoreFloat4x4(&Frame.Matrix, ViewProjection());
			for (auto const& Range : Ranges)
			{
				Frame.Color = {1, 1, 1, 1};
				if (Range.Material == SelectedMaterial)
					Frame.Color = {1, .8f, .25f, 1};
				Context->UpdateSubresource(Constants.get(), 0, nullptr, &Frame, 0, 0);
				Context->Draw(Range.VertexCount, Range.FirstVertex);
			}
		}
		check_hresult(SwapChain->Present(1, 0));
	}
	catch (hresult_error const& Error)
	{
		if (OnError)
			OnError(String(Error.message()));
	}
	catch (std::exception const& Error)
	{
		if (OnError)
			OnError(Error.what());
	}
}
void ModelViewport::SetMesh(std::shared_ptr<Mesh> Input)
{
	std::vector<std::vector<GpuVertex>> ByMaterial(Input->Materials.size());
	for (auto const& Triangle : Input->Triangles)
	{
		auto Normal = Normalized(Cross(Triangle.Positions[1] - Triangle.Positions[0],
									   Triangle.Positions[2] - Triangle.Positions[0]));
		for (size_t Corner = 0; Corner < 3; ++Corner)
			ByMaterial.at(Triangle.Material).push_back({Triangle.Positions[Corner], Normal});
	}
	std::vector<GpuVertex> Data;
	Ranges.clear();
	for (uint32_t Material = 0; Material < ByMaterial.size(); ++Material)
	{
		Ranges.push_back({Material, uint32_t(Data.size()), uint32_t(ByMaterial[Material].size())});
		Data.insert(Data.end(), ByMaterial[Material].begin(), ByMaterial[Material].end());
	}
	if (Data.empty())
		throw std::runtime_error("The selected model has no triangles");
	D3D11_BUFFER_DESC Buffer{};
	Buffer.ByteWidth = UINT(Data.size() * sizeof(GpuVertex));
	Buffer.Usage = D3D11_USAGE_IMMUTABLE;
	Buffer.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA Initial{Data.data(), 0, 0};
	Vertices = nullptr;
	check_hresult(Device->CreateBuffer(&Buffer, &Initial, Vertices.put()));
	Geometry = std::move(Input);
	SelectedMaterial = UINT32_MAX;
	Fit();
}
void ModelViewport::Fit()
{
	if (!Geometry)
		return;
	Center = (Geometry->Minimum + Geometry->Maximum) * .5f;
	auto Extent = Geometry->Maximum - Geometry->Minimum;
	Radius = std::max(.1f, std::sqrt(Dot(Extent, Extent)) * .5f);
	Distance = Radius * 2.8f;
	Render();
}
void ModelViewport::SetActive(bool Enabled)
{
	Active = Enabled;
	if (Enabled)
		Render();
}
void ModelViewport::SelectMaterial(uint32_t Index)
{
	SelectedMaterial = Index;
	Render();
}
void ModelViewport::Pick(float X, float Y)
{
	if (!Geometry)
		return;
	auto Width = float(std::max(1., Panel.ActualWidth())), Height = float(std::max(1., Panel.ActualHeight()));
	auto Inverse = XMMatrixInverse(nullptr, ViewProjection());
	auto Near = XMVector3TransformCoord(XMVectorSet(2 * X / Width - 1, 1 - 2 * Y / Height, 0, 1), Inverse);
	auto Far = XMVector3TransformCoord(XMVectorSet(2 * X / Width - 1, 1 - 2 * Y / Height, 1, 1), Inverse);
	XMFLOAT3 Start{}, Direction{};
	XMStoreFloat3(&Start, Near);
	XMStoreFloat3(&Direction, XMVector3Normalize(Far - Near));
	auto Hits =
		IntersectArmor(*Geometry, {Start.x, Start.y, Start.z}, {Direction.x, Direction.y, Direction.z});
	SelectedMaterial = Hits.empty() ? UINT32_MAX : Hits[0].Material;
	Render();
	if (OnPicked)
		OnPicked(SelectedMaterial);
}
} // namespace OffHanger::Ui
