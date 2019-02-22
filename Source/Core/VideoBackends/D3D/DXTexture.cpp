// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <cstddef>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

#include "VideoBackends/D3D/D3DState.h"
#include "VideoBackends/D3D/DXTexture.h"

namespace DX11
{
namespace
{
DXGI_FORMAT GetDXGIFormatForHostFormat(AbstractTextureFormat format, bool typeless)
{
  switch (format)
  {
  case AbstractTextureFormat::DXT1:
    return DXGI_FORMAT_BC1_UNORM;
  case AbstractTextureFormat::DXT3:
    return DXGI_FORMAT_BC2_UNORM;
  case AbstractTextureFormat::DXT5:
    return DXGI_FORMAT_BC3_UNORM;
  case AbstractTextureFormat::BPTC:
    return DXGI_FORMAT_BC7_UNORM;
  case AbstractTextureFormat::RGBA8:
    return typeless ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM;
  case AbstractTextureFormat::BGRA8:
    return typeless ? DXGI_FORMAT_B8G8R8A8_TYPELESS : DXGI_FORMAT_B8G8R8A8_UNORM;
  case AbstractTextureFormat::RGBA32F:
    return typeless ? DXGI_FORMAT_R32G32B32A32_TYPELESS : DXGI_FORMAT_R32G32B32A32_FLOAT;
  case AbstractTextureFormat::R16:
    return typeless ? DXGI_FORMAT_R16_TYPELESS : DXGI_FORMAT_R16_UNORM;
  case AbstractTextureFormat::R32F:
    return typeless ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R32_FLOAT;
  case AbstractTextureFormat::D16:
    return DXGI_FORMAT_R16_TYPELESS;
  case AbstractTextureFormat::D24_S8:
    return DXGI_FORMAT_R24G8_TYPELESS;
  case AbstractTextureFormat::D32F:
    return DXGI_FORMAT_R32_TYPELESS;
  case AbstractTextureFormat::D32F_S8:
    return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  default:
    PanicAlert("Unhandled texture format.");
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  }
}
DXGI_FORMAT GetSRVFormatForHostFormat(AbstractTextureFormat format)
{
  switch (format)
  {
  case AbstractTextureFormat::DXT1:
    return DXGI_FORMAT_BC1_UNORM;
  case AbstractTextureFormat::DXT3:
    return DXGI_FORMAT_BC2_UNORM;
  case AbstractTextureFormat::DXT5:
    return DXGI_FORMAT_BC3_UNORM;
  case AbstractTextureFormat::BPTC:
    return DXGI_FORMAT_BC7_UNORM;
  case AbstractTextureFormat::RGBA8:
    return DXGI_FORMAT_R8G8B8A8_UNORM;
  case AbstractTextureFormat::BGRA8:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  case AbstractTextureFormat::RGBA32F:
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  case AbstractTextureFormat::R16:
    return DXGI_FORMAT_R16_UNORM;
  case AbstractTextureFormat::R32F:
    return DXGI_FORMAT_R32_FLOAT;
  case AbstractTextureFormat::D16:
    return DXGI_FORMAT_R16_UNORM;
  case AbstractTextureFormat::D24_S8:
    return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
  case AbstractTextureFormat::D32F:
    return DXGI_FORMAT_R32_FLOAT;
  case AbstractTextureFormat::D32F_S8:
    return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
  default:
    PanicAlert("Unhandled SRV format");
    return DXGI_FORMAT_UNKNOWN;
  }
}
DXGI_FORMAT GetRTVFormatForHostFormat(AbstractTextureFormat format, bool integer)
{
  switch (format)
  {
  case AbstractTextureFormat::RGBA8:
    return integer ? DXGI_FORMAT_R8G8B8A8_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
  case AbstractTextureFormat::BGRA8:
    return DXGI_FORMAT_B8G8R8A8_UNORM;
  case AbstractTextureFormat::RGBA32F:
    return DXGI_FORMAT_R32G32B32A32_FLOAT;
  case AbstractTextureFormat::R16:
    return integer ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R16_UNORM;
  case AbstractTextureFormat::R32F:
    return DXGI_FORMAT_R32_FLOAT;
  default:
    PanicAlert("Unhandled RTV format");
    return DXGI_FORMAT_UNKNOWN;
  }
}
DXGI_FORMAT GetDSVFormatForHostFormat(AbstractTextureFormat format)
{
  switch (format)
  {
  case AbstractTextureFormat::D16:
    return DXGI_FORMAT_D16_UNORM;
  case AbstractTextureFormat::D24_S8:
    return DXGI_FORMAT_D24_UNORM_S8_UINT;
  case AbstractTextureFormat::D32F:
    return DXGI_FORMAT_D32_FLOAT;
  case AbstractTextureFormat::D32F_S8:
    return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  default:
    PanicAlert("Unhandled DSV format");
    return DXGI_FORMAT_UNKNOWN;
  }
}
}  // Anonymous namespace

DXTexture::DXTexture(const TextureConfig& tex_config, ID3D11Texture2D* d3d_texture,
                     ID3D11ShaderResourceView* d3d_srv, ID3D11UnorderedAccessView* d3d_uav)
    : AbstractTexture(tex_config), m_d3d_texture(d3d_texture), m_d3d_srv(d3d_srv),
      m_d3d_uav(d3d_uav)
{
}

DXTexture::~DXTexture()
{
  if (m_d3d_uav)
    m_d3d_uav->Release();

  if (m_d3d_srv)
  {
    if (D3D::stateman->UnsetTexture(m_d3d_srv) != 0)
      D3D::stateman->ApplyTextures();

    m_d3d_srv->Release();
  }
  m_d3d_texture->Release();
}

std::unique_ptr<DXTexture> DXTexture::Create(const TextureConfig& config)
{
  // Use typeless to create the texture when it's a render target, so we can alias it with an
  // integer format (for EFB).
  const DXGI_FORMAT tex_format = GetDXGIFormatForHostFormat(config.format, config.IsRenderTarget());
  const DXGI_FORMAT srv_format = GetSRVFormatForHostFormat(config.format);
  UINT bindflags = D3D11_BIND_SHADER_RESOURCE;
  if (config.IsRenderTarget())
    bindflags |= IsDepthFormat(config.format) ? D3D11_BIND_DEPTH_STENCIL : D3D11_BIND_RENDER_TARGET;
  if (config.IsComputeImage())
    bindflags |= D3D11_BIND_UNORDERED_ACCESS;

  CD3D11_TEXTURE2D_DESC desc(tex_format, config.width, config.height, config.layers, config.levels,
                             bindflags, D3D11_USAGE_DEFAULT, 0, config.samples, 0, 0);
  ID3D11Texture2D* d3d_texture;
  HRESULT hr = D3D::device->CreateTexture2D(&desc, nullptr, &d3d_texture);
  if (FAILED(hr))
  {
    PanicAlert("Failed to create %ux%ux%u D3D backing texture", config.width, config.height,
               config.layers);
    return nullptr;
  }

  ID3D11ShaderResourceView* d3d_srv;
  const CD3D11_SHADER_RESOURCE_VIEW_DESC srv_desc(d3d_texture,
                                                  config.IsMultisampled() ?
                                                      D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY :
                                                      D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
                                                  srv_format, 0, config.levels, 0, config.layers);
  hr = D3D::device->CreateShaderResourceView(d3d_texture, &srv_desc, &d3d_srv);
  if (FAILED(hr))
  {
    PanicAlert("Failed to create %ux%ux%u D3D SRV", config.width, config.height, config.layers);
    d3d_texture->Release();
    return nullptr;
  }

  ID3D11UnorderedAccessView* d3d_uav = nullptr;
  if (config.IsComputeImage())
  {
    const CD3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc(
        d3d_texture, D3D11_UAV_DIMENSION_TEXTURE2DARRAY, srv_format, 0, 0, config.layers);
    hr = D3D::device->CreateUnorderedAccessView(d3d_texture, &uav_desc, &d3d_uav);
    if (FAILED(hr))
    {
      PanicAlert("Failed to create %ux%ux%u D3D UAV", config.width, config.height, config.layers);
      d3d_uav->Release();
      d3d_texture->Release();
      return nullptr;
    }
  }

  return std::make_unique<DXTexture>(config, d3d_texture, d3d_srv, d3d_uav);
}

void DXTexture::CopyRectangleFromTexture(const AbstractTexture* src,
                                         const MathUtil::Rectangle<int>& src_rect, u32 src_layer,
                                         u32 src_level, const MathUtil::Rectangle<int>& dst_rect,
                                         u32 dst_layer, u32 dst_level)
{
  const DXTexture* srcentry = static_cast<const DXTexture*>(src);
  ASSERT(src_rect.GetWidth() == dst_rect.GetWidth() &&
         src_rect.GetHeight() == dst_rect.GetHeight());

  D3D11_BOX src_box;
  src_box.left = src_rect.left;
  src_box.top = src_rect.top;
  src_box.right = src_rect.right;
  src_box.bottom = src_rect.bottom;
  src_box.front = 0;
  src_box.back = 1;

  D3D::context->CopySubresourceRegion(
      m_d3d_texture, D3D11CalcSubresource(dst_level, dst_layer, m_config.levels), dst_rect.left,
      dst_rect.top, 0, srcentry->m_d3d_texture,
      D3D11CalcSubresource(src_level, src_layer, srcentry->m_config.levels), &src_box);
}

void DXTexture::ResolveFromTexture(const AbstractTexture* src, const MathUtil::Rectangle<int>& rect,
                                   u32 layer, u32 level)
{
  const DXTexture* srcentry = static_cast<const DXTexture*>(src);
  DEBUG_ASSERT(m_config.samples > 1 && m_config.width == srcentry->m_config.width &&
               m_config.height == srcentry->m_config.height && m_config.samples == 1);
  DEBUG_ASSERT(rect.left + rect.GetWidth() <= static_cast<int>(srcentry->m_config.width) &&
               rect.top + rect.GetHeight() <= static_cast<int>(srcentry->m_config.height));

  D3D::context->ResolveSubresource(
      m_d3d_texture, D3D11CalcSubresource(level, layer, m_config.levels), srcentry->m_d3d_texture,
      D3D11CalcSubresource(level, layer, srcentry->m_config.levels),
      GetDXGIFormatForHostFormat(m_config.format, false));
}

void DXTexture::Load(u32 level, u32 width, u32 height, u32 row_length, const u8* buffer,
                     size_t buffer_size)
{
  size_t src_pitch = CalculateStrideForFormat(m_config.format, row_length);
  D3D::context->UpdateSubresource(m_d3d_texture, level, nullptr, buffer,
                                  static_cast<UINT>(src_pitch), 0);
}

DXStagingTexture::DXStagingTexture(StagingTextureType type, const TextureConfig& config,
                                   ID3D11Texture2D* tex)
    : AbstractStagingTexture(type, config), m_tex(tex)
{
}

DXStagingTexture::~DXStagingTexture()
{
  if (IsMapped())
    DXStagingTexture::Unmap();
  SAFE_RELEASE(m_tex);
}

std::unique_ptr<DXStagingTexture> DXStagingTexture::Create(StagingTextureType type,
                                                           const TextureConfig& config)
{
  D3D11_USAGE usage;
  UINT cpu_flags;
  if (type == StagingTextureType::Readback)
  {
    usage = D3D11_USAGE_STAGING;
    cpu_flags = D3D11_CPU_ACCESS_READ;
  }
  else if (type == StagingTextureType::Upload)
  {
    usage = D3D11_USAGE_DYNAMIC;
    cpu_flags = D3D11_CPU_ACCESS_WRITE;
  }
  else
  {
    usage = D3D11_USAGE_STAGING;
    cpu_flags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
  }

  CD3D11_TEXTURE2D_DESC desc(GetDXGIFormatForHostFormat(config.format, false), config.width,
                             config.height, 1, 1, 0, usage, cpu_flags);

  ID3D11Texture2D* texture;
  HRESULT hr = D3D::device->CreateTexture2D(&desc, nullptr, &texture);
  CHECK(SUCCEEDED(hr), "Create staging texture");
  if (FAILED(hr))
    return nullptr;

  return std::unique_ptr<DXStagingTexture>(new DXStagingTexture(type, config, texture));
}

void DXStagingTexture::CopyFromTexture(const AbstractTexture* src,
                                       const MathUtil::Rectangle<int>& src_rect, u32 src_layer,
                                       u32 src_level, const MathUtil::Rectangle<int>& dst_rect)
{
  ASSERT(m_type == StagingTextureType::Readback || m_type == StagingTextureType::Mutable);
  ASSERT(src_rect.GetWidth() == dst_rect.GetWidth() &&
         src_rect.GetHeight() == dst_rect.GetHeight());
  ASSERT(src_rect.left >= 0 && static_cast<u32>(src_rect.right) <= src->GetWidth() &&
         src_rect.top >= 0 && static_cast<u32>(src_rect.bottom) <= src->GetHeight());
  ASSERT(dst_rect.left >= 0 && static_cast<u32>(dst_rect.right) <= m_config.width &&
         dst_rect.top >= 0 && static_cast<u32>(dst_rect.bottom) <= m_config.height);

  if (IsMapped())
    DXStagingTexture::Unmap();

  if (static_cast<u32>(src_rect.GetWidth()) == GetWidth() &&
      static_cast<u32>(src_rect.GetHeight()) == GetHeight())
  {
    // Copy whole resource, needed for depth textures.
    D3D::context->CopySubresourceRegion(
        m_tex, 0, 0, 0, 0, static_cast<const DXTexture*>(src)->GetD3DTexture(),
        D3D11CalcSubresource(src_level, src_layer, src->GetLevels()), nullptr);
  }
  else
  {
    CD3D11_BOX src_box(src_rect.left, src_rect.top, 0, src_rect.right, src_rect.bottom, 1);
    D3D::context->CopySubresourceRegion(
        m_tex, 0, static_cast<u32>(dst_rect.left), static_cast<u32>(dst_rect.top), 0,
        static_cast<const DXTexture*>(src)->GetD3DTexture(),
        D3D11CalcSubresource(src_level, src_layer, src->GetLevels()), &src_box);
  }

  m_needs_flush = true;
}

void DXStagingTexture::CopyToTexture(const MathUtil::Rectangle<int>& src_rect, AbstractTexture* dst,
                                     const MathUtil::Rectangle<int>& dst_rect, u32 dst_layer,
                                     u32 dst_level)
{
  ASSERT(m_type == StagingTextureType::Upload);
  ASSERT(src_rect.GetWidth() == dst_rect.GetWidth() &&
         src_rect.GetHeight() == dst_rect.GetHeight());
  ASSERT(src_rect.left >= 0 && static_cast<u32>(src_rect.right) <= GetWidth() &&
         src_rect.top >= 0 && static_cast<u32>(src_rect.bottom) <= GetHeight());
  ASSERT(dst_rect.left >= 0 && static_cast<u32>(dst_rect.right) <= dst->GetWidth() &&
         dst_rect.top >= 0 && static_cast<u32>(dst_rect.bottom) <= dst->GetHeight());

  if (IsMapped())
    DXStagingTexture::Unmap();

  if (static_cast<u32>(src_rect.GetWidth()) == dst->GetWidth() &&
      static_cast<u32>(src_rect.GetHeight()) == dst->GetHeight())
  {
    D3D::context->CopySubresourceRegion(
        static_cast<const DXTexture*>(dst)->GetD3DTexture(),
        D3D11CalcSubresource(dst_level, dst_layer, dst->GetLevels()), 0, 0, 0, m_tex, 0, nullptr);
  }
  else
  {
    CD3D11_BOX src_box(src_rect.left, src_rect.top, 0, src_rect.right, src_rect.bottom, 1);
    D3D::context->CopySubresourceRegion(
        static_cast<const DXTexture*>(dst)->GetD3DTexture(),
        D3D11CalcSubresource(dst_level, dst_layer, dst->GetLevels()),
        static_cast<u32>(dst_rect.left), static_cast<u32>(dst_rect.top), 0, m_tex, 0, &src_box);
  }
}

bool DXStagingTexture::Map()
{
  if (m_map_pointer)
    return true;

  D3D11_MAP map_type;
  if (m_type == StagingTextureType::Readback)
    map_type = D3D11_MAP_READ;
  else if (m_type == StagingTextureType::Upload)
    map_type = D3D11_MAP_WRITE;
  else
    map_type = D3D11_MAP_READ_WRITE;

  D3D11_MAPPED_SUBRESOURCE sr;
  HRESULT hr = D3D::context->Map(m_tex, 0, map_type, 0, &sr);
  CHECK(SUCCEEDED(hr), "Map readback texture");
  if (FAILED(hr))
    return false;

  m_map_pointer = reinterpret_cast<char*>(sr.pData);
  m_map_stride = sr.RowPitch;
  return true;
}

void DXStagingTexture::Unmap()
{
  if (!m_map_pointer)
    return;

  D3D::context->Unmap(m_tex, 0);
  m_map_pointer = nullptr;
}

void DXStagingTexture::Flush()
{
  // Flushing is handled by the API.
  m_needs_flush = false;
}

DXFramebuffer::DXFramebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment,
                             AbstractTextureFormat color_format, AbstractTextureFormat depth_format,
                             u32 width, u32 height, u32 layers, u32 samples,
                             ID3D11RenderTargetView* rtv, ID3D11RenderTargetView* integer_rtv,
                             ID3D11DepthStencilView* dsv)
    : AbstractFramebuffer(color_attachment, depth_attachment, color_format, depth_format, width,
                          height, layers, samples),
      m_rtv(rtv), m_integer_rtv(integer_rtv), m_dsv(dsv)
{
}

DXFramebuffer::~DXFramebuffer()
{
  if (m_rtv)
    m_rtv->Release();
  if (m_integer_rtv)
    m_integer_rtv->Release();
  if (m_dsv)
    m_dsv->Release();
}

std::unique_ptr<DXFramebuffer> DXFramebuffer::Create(DXTexture* color_attachment,
                                                     DXTexture* depth_attachment)
{
  if (!ValidateConfig(color_attachment, depth_attachment))
    return nullptr;

  const AbstractTextureFormat color_format =
      color_attachment ? color_attachment->GetFormat() : AbstractTextureFormat::Undefined;
  const AbstractTextureFormat depth_format =
      depth_attachment ? depth_attachment->GetFormat() : AbstractTextureFormat::Undefined;
  const DXTexture* either_attachment = color_attachment ? color_attachment : depth_attachment;
  const u32 width = either_attachment->GetWidth();
  const u32 height = either_attachment->GetHeight();
  const u32 layers = either_attachment->GetLayers();
  const u32 samples = either_attachment->GetSamples();

  ID3D11RenderTargetView* rtv = nullptr;
  ID3D11RenderTargetView* integer_rtv = nullptr;
  if (color_attachment)
  {
    CD3D11_RENDER_TARGET_VIEW_DESC desc(
        color_attachment->IsMultisampled() ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY :
                                             D3D11_RTV_DIMENSION_TEXTURE2DARRAY,
        GetRTVFormatForHostFormat(color_attachment->GetFormat(), false), 0, 0,
        color_attachment->GetLayers());
    HRESULT hr =
        D3D::device->CreateRenderTargetView(color_attachment->GetD3DTexture(), &desc, &rtv);
    CHECK(SUCCEEDED(hr), "Create render target view for framebuffer");

    // Only create the integer RTV on Win8+.
    DXGI_FORMAT integer_format = GetRTVFormatForHostFormat(color_attachment->GetFormat(), true);
    if (D3D::device1 && integer_format != desc.Format)
    {
      desc.Format = integer_format;
      hr = D3D::device->CreateRenderTargetView(color_attachment->GetD3DTexture(), &desc,
                                               &integer_rtv);
      CHECK(SUCCEEDED(hr), "Create integer render target view for framebuffer");
    }
  }

  ID3D11DepthStencilView* dsv = nullptr;
  if (depth_attachment)
  {
    const CD3D11_DEPTH_STENCIL_VIEW_DESC desc(
        depth_attachment->GetConfig().IsMultisampled() ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY :
                                                         D3D11_DSV_DIMENSION_TEXTURE2DARRAY,
        GetDSVFormatForHostFormat(depth_attachment->GetFormat()), 0, 0,
        depth_attachment->GetLayers(), 0);
    HRESULT hr =
        D3D::device->CreateDepthStencilView(depth_attachment->GetD3DTexture(), &desc, &dsv);
    CHECK(SUCCEEDED(hr), "Create depth stencil view for framebuffer");
  }

  return std::make_unique<DXFramebuffer>(color_attachment, depth_attachment, color_format,
                                         depth_format, width, height, layers, samples, rtv,
                                         integer_rtv, dsv);
}

}  // namespace DX11
