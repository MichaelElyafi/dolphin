// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include <d3d11.h>
#include <string>
#include "VideoBackends/D3D/D3DState.h"
#include "VideoCommon/RenderBase.h"

namespace DX11
{
class DXTexture;
class DXFramebuffer;

class Renderer : public ::Renderer
{
public:
  Renderer(int backbuffer_width, int backbuffer_height, float backbuffer_scale);
  ~Renderer() override;

  StateCache& GetStateCache() { return m_state_cache; }

  bool IsHeadless() const override;

  std::unique_ptr<AbstractTexture> CreateTexture(const TextureConfig& config) override;
  std::unique_ptr<AbstractStagingTexture>
  CreateStagingTexture(StagingTextureType type, const TextureConfig& config) override;
  std::unique_ptr<AbstractShader> CreateShaderFromSource(ShaderStage stage, const char* source,
                                                         size_t length) override;
  std::unique_ptr<AbstractShader> CreateShaderFromBinary(ShaderStage stage, const void* data,
                                                         size_t length) override;
  std::unique_ptr<NativeVertexFormat>
  CreateNativeVertexFormat(const PortableVertexDeclaration& vtx_decl) override;
  std::unique_ptr<AbstractPipeline> CreatePipeline(const AbstractPipelineConfig& config) override;
  std::unique_ptr<AbstractFramebuffer>
  CreateFramebuffer(AbstractTexture* color_attachment, AbstractTexture* depth_attachment) override;

  void SetPipeline(const AbstractPipeline* pipeline) override;
  void SetFramebuffer(AbstractFramebuffer* framebuffer) override;
  void SetAndDiscardFramebuffer(AbstractFramebuffer* framebuffer) override;
  void SetAndClearFramebuffer(AbstractFramebuffer* framebuffer, const ClearColor& color_value = {},
                              float depth_value = 0.0f) override;
  void SetScissorRect(const MathUtil::Rectangle<int>& rc) override;
  void SetTexture(u32 index, const AbstractTexture* texture) override;
  void SetSamplerState(u32 index, const SamplerState& state) override;
  void SetComputeImageTexture(AbstractTexture* texture, bool read, bool write) override;
  void UnbindTexture(const AbstractTexture* texture) override;
  void SetViewport(float x, float y, float width, float height, float near_depth,
                   float far_depth) override;
  void Draw(u32 base_vertex, u32 num_vertices) override;
  void DrawIndexed(u32 base_index, u32 num_indices, u32 base_vertex) override;
  void DispatchComputeShader(const AbstractShader* shader, u32 groups_x, u32 groups_y,
                             u32 groups_z) override;
  void BindBackbuffer(const ClearColor& clear_color = {}) override;
  void PresentBackbuffer() override;

  u16 BBoxRead(int index) override;
  void BBoxWrite(int index, u16 value) override;

protected:
  bool ChangeFullscreenState(bool enabled, float refresh_rate) override;
  void RenderXFBToScreen(const AbstractTexture* texture, const EFBRectangle& rc) override;
  void OnConfigChanged(u32 bits) override;

private:
  void Create3DVisionTexture(int width, int height);
  void CheckForSurfaceChange();
  void CheckForSurfaceResize();
  void UpdateBackbufferSize();

  StateCache m_state_cache;

  std::unique_ptr<DXTexture> m_3d_vision_texture;
  std::unique_ptr<DXFramebuffer> m_3d_vision_framebuffer;

  bool m_last_fullscreen_state = false;
};
}  // namespace DX11