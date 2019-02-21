// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "VideoBackends/D3D/Render.h"

#include <array>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <strsafe.h>
#include <tuple>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"

#include "Core/Core.h"

#include "VideoBackends/D3D/BoundingBox.h"
#include "VideoBackends/D3D/D3DBase.h"
#include "VideoBackends/D3D/D3DState.h"
#include "VideoBackends/D3D/DXPipeline.h"
#include "VideoBackends/D3D/DXShader.h"
#include "VideoBackends/D3D/DXTexture.h"

#include "VideoCommon/BPFunctions.h"
#include "VideoCommon/FramebufferManager.h"
#include "VideoCommon/PostProcessing.h"
#include "VideoCommon/RenderState.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

namespace DX11
{
// Nvidia stereo blitting struct defined in "nvstereo.h" from the Nvidia SDK
typedef struct _Nv_Stereo_Image_Header
{
  unsigned int dwSignature;
  unsigned int dwWidth;
  unsigned int dwHeight;
  unsigned int dwBPP;
  unsigned int dwFlags;
} NVSTEREOIMAGEHEADER, *LPNVSTEREOIMAGEHEADER;

#define NVSTEREO_IMAGE_SIGNATURE 0x4433564e

Renderer::Renderer(int backbuffer_width, int backbuffer_height, float backbuffer_scale)
    : ::Renderer(backbuffer_width, backbuffer_height, backbuffer_scale,
                 AbstractTextureFormat::RGBA8)
{
  m_last_fullscreen_state = D3D::GetFullscreenState();
}

Renderer::~Renderer() = default;

void Renderer::Create3DVisionTexture(int width, int height)
{
  // Create a staging texture for 3D vision with signature information in the last row.
  // Nvidia 3D Vision supports full SBS, so there is no loss in resolution during this process.
  NVSTEREOIMAGEHEADER header;
  header.dwSignature = NVSTEREO_IMAGE_SIGNATURE;
  header.dwWidth = static_cast<u32>(width * 2);
  header.dwHeight = static_cast<u32>(height + 1);
  header.dwBPP = 32;
  header.dwFlags = 0;

  const u32 pitch = static_cast<u32>(4 * width * 2);
  const auto memory = std::make_unique<u8[]>((height + 1) * pitch);
  u8* image_header_location = &memory[height * pitch];
  std::memcpy(image_header_location, &header, sizeof(header));

  D3D11_SUBRESOURCE_DATA sys_data;
  sys_data.SysMemPitch = pitch;
  sys_data.pSysMem = memory.get();

  CD3D11_TEXTURE2D_DESC texture_desc(DXGI_FORMAT_R8G8B8A8_UNORM, width * 2, height + 1, 1, 1,
                                     D3D11_BIND_RENDER_TARGET, D3D11_USAGE_DEFAULT, 0, 1, 0, 0);
  ID3D11Texture2D* texture;
  HRESULT hr = D3D::device->CreateTexture2D(&texture_desc, &sys_data, &texture);
  CHECK(SUCCEEDED(hr), "Create 3D Vision Texture");
  m_3d_vision_texture = std::make_unique<DXTexture>(TextureConfig(width * 2, height + 1, 1, 1, 1,
                                                                  AbstractTextureFormat::RGBA8,
                                                                  AbstractTextureFlag_RenderTarget),
                                                    texture, nullptr, nullptr);
  m_3d_vision_framebuffer =
      DXFramebuffer::Create(static_cast<DXTexture*>(m_3d_vision_texture.get()), nullptr);
}

bool Renderer::IsHeadless() const
{
  return D3D::swapchain == nullptr;
}

std::unique_ptr<AbstractTexture> Renderer::CreateTexture(const TextureConfig& config)
{
  return DXTexture::Create(config);
}

std::unique_ptr<AbstractStagingTexture> Renderer::CreateStagingTexture(StagingTextureType type,
                                                                       const TextureConfig& config)
{
  return DXStagingTexture::Create(type, config);
}

std::unique_ptr<AbstractFramebuffer> Renderer::CreateFramebuffer(AbstractTexture* color_attachment,
                                                                 AbstractTexture* depth_attachment)
{
  return DXFramebuffer::Create(static_cast<DXTexture*>(color_attachment),
                               static_cast<DXTexture*>(depth_attachment));
}

std::unique_ptr<AbstractShader> Renderer::CreateShaderFromSource(ShaderStage stage,
                                                                 const char* source, size_t length)
{
  return DXShader::CreateFromSource(stage, source, length);
}

std::unique_ptr<AbstractShader> Renderer::CreateShaderFromBinary(ShaderStage stage,
                                                                 const void* data, size_t length)
{
  return DXShader::CreateFromBinary(stage, data, length);
}

std::unique_ptr<AbstractPipeline> Renderer::CreatePipeline(const AbstractPipelineConfig& config)
{
  return DXPipeline::Create(config);
}

void Renderer::SetPipeline(const AbstractPipeline* pipeline)
{
  const DXPipeline* dx_pipeline = static_cast<const DXPipeline*>(pipeline);
  if (m_current_pipeline == dx_pipeline)
    return;

  if (dx_pipeline)
  {
    D3D::stateman->SetRasterizerState(dx_pipeline->GetRasterizerState());
    D3D::stateman->SetDepthState(dx_pipeline->GetDepthState());
    D3D::stateman->SetBlendState(dx_pipeline->GetBlendState());
    D3D::stateman->SetPrimitiveTopology(dx_pipeline->GetPrimitiveTopology());
    D3D::stateman->SetInputLayout(dx_pipeline->GetInputLayout());
    D3D::stateman->SetVertexShader(dx_pipeline->GetVertexShader());
    D3D::stateman->SetGeometryShader(dx_pipeline->GetGeometryShader());
    D3D::stateman->SetPixelShader(dx_pipeline->GetPixelShader());
    D3D::stateman->SetIntegerRTV(dx_pipeline->UseLogicOp());
  }
  else
  {
    // These will be destroyed at pipeline destruction.
    D3D::stateman->SetInputLayout(nullptr);
    D3D::stateman->SetVertexShader(nullptr);
    D3D::stateman->SetGeometryShader(nullptr);
    D3D::stateman->SetPixelShader(nullptr);
  }
}

void Renderer::SetScissorRect(const MathUtil::Rectangle<int>& rc)
{
  // TODO: Move to stateman
  const CD3D11_RECT rect(rc.left, rc.top, std::max(rc.right, rc.left + 1),
                         std::max(rc.bottom, rc.top + 1));
  D3D::context->RSSetScissorRects(1, &rect);
}

void Renderer::SetViewport(float x, float y, float width, float height, float near_depth,
                           float far_depth)
{
  // TODO: Move to stateman
  const CD3D11_VIEWPORT vp(x, y, width, height, near_depth, far_depth);
  D3D::context->RSSetViewports(1, &vp);
}

void Renderer::Draw(u32 base_vertex, u32 num_vertices)
{
  D3D::stateman->Apply();
  D3D::context->Draw(num_vertices, base_vertex);
}

void Renderer::DrawIndexed(u32 base_index, u32 num_indices, u32 base_vertex)
{
  D3D::stateman->Apply();
  D3D::context->DrawIndexed(num_indices, base_index, base_vertex);
}

void Renderer::DispatchComputeShader(const AbstractShader* shader, u32 groups_x, u32 groups_y,
                                     u32 groups_z)
{
  D3D::stateman->SetComputeShader(static_cast<const DXShader*>(shader)->GetD3DComputeShader());
  D3D::stateman->SyncComputeBindings();
  D3D::context->Dispatch(groups_x, groups_y, groups_z);
}

void Renderer::BindBackbuffer(const ClearColor& clear_color)
{
  CheckForSurfaceChange();
  CheckForSurfaceResize();
  SetAndClearFramebuffer(D3D::GetSwapChainFramebuffer(), clear_color);
}

void Renderer::PresentBackbuffer()
{
  D3D::Present();
}

void Renderer::OnConfigChanged(u32 bits)
{
}

void Renderer::CheckForSurfaceChange()
{
  if (!m_surface_changed.TestAndClear())
    return;

  m_3d_vision_framebuffer.reset();
  m_3d_vision_texture.reset();

  D3D::Reset(reinterpret_cast<HWND>(m_new_surface_handle));
  m_new_surface_handle = nullptr;

  UpdateBackbufferSize();
}

void Renderer::CheckForSurfaceResize()
{
  const bool fullscreen_state = D3D::GetFullscreenState();
  const bool exclusive_fullscreen_changed = fullscreen_state != m_last_fullscreen_state;
  if (!m_surface_resized.TestAndClear() && !exclusive_fullscreen_changed)
    return;

  m_3d_vision_framebuffer.reset();
  m_3d_vision_texture.reset();

  m_last_fullscreen_state = fullscreen_state;
  if (D3D::swapchain)
    D3D::ResizeSwapChain();
  UpdateBackbufferSize();
}

void Renderer::UpdateBackbufferSize()
{
  if (D3D::swapchain)
  {
    DXGI_SWAP_CHAIN_DESC1 desc = {};
    D3D::swapchain->GetDesc1(&desc);
    m_backbuffer_width = std::max(desc.Width, 1u);
    m_backbuffer_height = std::max(desc.Height, 1u);
  }
  else
  {
    m_backbuffer_width = 1;
    m_backbuffer_height = 1;
  }
}

void Renderer::SetFramebuffer(AbstractFramebuffer* framebuffer)
{
  if (m_current_framebuffer == framebuffer)
    return;

  // We can't leave the framebuffer bound as a texture and a render target.
  DXFramebuffer* fb = static_cast<DXFramebuffer*>(framebuffer);
  if ((fb->GetColorAttachment() &&
       D3D::stateman->UnsetTexture(
           static_cast<DXTexture*>(fb->GetColorAttachment())->GetD3DSRV()) != 0) ||
      (fb->GetDepthAttachment() &&
       D3D::stateman->UnsetTexture(
           static_cast<DXTexture*>(fb->GetDepthAttachment())->GetD3DSRV()) != 0))
  {
    D3D::stateman->ApplyTextures();
  }

  D3D::stateman->SetFramebuffer(fb);
  m_current_framebuffer = fb;
}

void Renderer::SetAndDiscardFramebuffer(AbstractFramebuffer* framebuffer)
{
  SetFramebuffer(framebuffer);
}

void Renderer::SetAndClearFramebuffer(AbstractFramebuffer* framebuffer,
                                      const ClearColor& color_value, float depth_value)
{
  SetFramebuffer(framebuffer);
  D3D::stateman->Apply();

  if (framebuffer->GetColorFormat() != AbstractTextureFormat::Undefined)
  {
    D3D::context->ClearRenderTargetView(
        static_cast<const DXFramebuffer*>(framebuffer)->GetRTVArray()[0], color_value.data());
  }
  if (framebuffer->GetDepthFormat() != AbstractTextureFormat::Undefined)
  {
    D3D::context->ClearDepthStencilView(static_cast<const DXFramebuffer*>(framebuffer)->GetDSV(),
                                        D3D11_CLEAR_DEPTH, depth_value, 0);
  }
}

void Renderer::SetTexture(u32 index, const AbstractTexture* texture)
{
  D3D::stateman->SetTexture(index, texture ? static_cast<const DXTexture*>(texture)->GetD3DSRV() :
                                             nullptr);
}

void Renderer::SetSamplerState(u32 index, const SamplerState& state)
{
  D3D::stateman->SetSampler(index, m_state_cache.Get(state));
}

void Renderer::SetComputeImageTexture(AbstractTexture* texture, bool read, bool write)
{
  D3D::stateman->SetComputeUAV(texture ? static_cast<DXTexture*>(texture)->GetD3DUAV() : nullptr);
}

void Renderer::UnbindTexture(const AbstractTexture* texture)
{
  if (D3D::stateman->UnsetTexture(static_cast<const DXTexture*>(texture)->GetD3DSRV()) != 0)
    D3D::stateman->ApplyTextures();
}

u16 Renderer::BBoxRead(int index)
{
  // Here we get the min/max value of the truncated position of the upscaled framebuffer.
  // So we have to correct them to the unscaled EFB sizes.
  int value = BBox::Get(index);

  if (index < 2)
  {
    // left/right
    value = value * EFB_WIDTH / m_target_width;
  }
  else
  {
    // up/down
    value = value * EFB_HEIGHT / m_target_height;
  }
  if (index & 1)
    value++;  // fix max values to describe the outer border

  return value;
}

void Renderer::BBoxWrite(int index, u16 _value)
{
  int value = _value;  // u16 isn't enough to multiply by the efb width
  if (index & 1)
    value--;
  if (index < 2)
  {
    value = value * m_target_width / EFB_WIDTH;
  }
  else
  {
    value = value * m_target_height / EFB_HEIGHT;
  }

  BBox::Set(index, value);
}

void Renderer::RenderXFBToScreen(const AbstractTexture* texture, const EFBRectangle& rc)
{
  if (g_ActiveConfig.stereo_mode != StereoMode::Nvidia3DVision)
    return ::Renderer::RenderXFBToScreen(texture, rc);

  if (!m_3d_vision_texture)
    Create3DVisionTexture(m_backbuffer_width, m_backbuffer_height);

  // Render to staging texture which is double the width of the backbuffer
  SetAndClearFramebuffer(m_3d_vision_framebuffer.get());

  const auto target_rc = GetTargetRectangle();
  m_post_processor->BlitFromTexture(target_rc, rc, texture, 0);
  m_post_processor->BlitFromTexture(
      MathUtil::Rectangle<int>(target_rc.left + m_backbuffer_width, target_rc.top,
                               target_rc.right + m_backbuffer_width, target_rc.bottom),
      rc, texture, 1);

  // Copy the left eye to the backbuffer, if Nvidia 3D Vision is enabled it should
  // recognize the signature and automatically include the right eye frame.
  const CD3D11_BOX box(0, 0, 0, m_backbuffer_width, m_backbuffer_height, 1);
  D3D::context->CopySubresourceRegion(D3D::GetSwapChainTexture()->GetD3DTexture(), 0, 0, 0, 0,
                                      m_3d_vision_texture->GetD3DTexture(), 0, &box);

  // Restore render target to backbuffer
  SetFramebuffer(D3D::GetSwapChainFramebuffer());
}

bool Renderer::ChangeFullscreenState(bool enabled, float refresh_rate)
{
  if (g_ActiveConfig.bBorderlessFullscreen)
    return ::Renderer::ChangeFullscreenState(enabled, refresh_rate);

  if (!D3D::SetFullscreenState(enabled, refresh_rate))
    return false;

  m_fullscreen_state = enabled;
  return true;
}

}  // namespace DX11