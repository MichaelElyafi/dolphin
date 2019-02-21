// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

// ---------------------------------------------------------------------------------------------
// GC graphics pipeline
// ---------------------------------------------------------------------------------------------
// 3d commands are issued through the fifo. The GPU draws to the 2MB EFB.
// The efb can be copied back into ram in two forms: as textures or as XFB.
// The XFB is the region in RAM that the VI chip scans out to the television.
// So, after all rendering to EFB is done, the image is copied into one of two XFBs in RAM.
// Next frame, that one is scanned out and the other one gets the copy. = double buffering.
// ---------------------------------------------------------------------------------------------

#include "VideoCommon/RenderBase.h"

#include <cinttypes>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>

#include "imgui.h"

#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/Event.h"
#include "Common/FileUtil.h"
#include "Common/Flag.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/Profiler.h"
#include "Common/StringUtil.h"
#include "Common/Thread.h"
#include "Common/Timer.h"

#include "Core/Analytics.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/FifoPlayer/FifoRecorder.h"
#include "Core/HW/SystemTimers.h"
#include "Core/HW/VideoInterface.h"
#include "Core/Host.h"
#include "Core/Movie.h"

#include "VideoCommon/AVIDump.h"
#include "VideoCommon/AbstractFramebuffer.h"
#include "VideoCommon/AbstractStagingTexture.h"
#include "VideoCommon/AbstractTexture.h"
#include "VideoCommon/BPMemory.h"
#include "VideoCommon/CPMemory.h"
#include "VideoCommon/CommandProcessor.h"
#include "VideoCommon/Debugger.h"
#include "VideoCommon/FramebufferManagerBase.h"
#include "VideoCommon/ImageWrite.h"
#include "VideoCommon/OnScreenDisplay.h"
#include "VideoCommon/PixelShaderManager.h"
#include "VideoCommon/PostProcessing.h"
#include "VideoCommon/ShaderCache.h"
#include "VideoCommon/ShaderGenCommon.h"
#include "VideoCommon/Statistics.h"
#include "VideoCommon/TextureCacheBase.h"
#include "VideoCommon/TextureDecoder.h"
#include "VideoCommon/VertexLoaderManager.h"
#include "VideoCommon/VertexManagerBase.h"
#include "VideoCommon/VertexShaderManager.h"
#include "VideoCommon/VideoConfig.h"
#include "VideoCommon/XFMemory.h"

// TODO: Move these out of here.
int frameCount;

std::unique_ptr<Renderer> g_renderer;

static float AspectToWidescreen(float aspect)
{
  return aspect * ((16.0f / 9.0f) / (4.0f / 3.0f));
}

Renderer::Renderer(int backbuffer_width, int backbuffer_height, float backbuffer_scale,
                   AbstractTextureFormat backbuffer_format)
    : m_backbuffer_width(backbuffer_width), m_backbuffer_height(backbuffer_height),
      m_backbuffer_scale(backbuffer_scale), m_backbuffer_format(backbuffer_format)
{
  UpdateActiveConfig();
  UpdateDrawRectangle();
  CalculateTargetSize();

  m_aspect_wide = SConfig::GetInstance().bWii && Config::Get(Config::SYSCONF_WIDESCREEN);
  m_last_refresh_rate = VideoInterface::GetTargetFractionalRefreshRate();
}

Renderer::~Renderer() = default;

bool Renderer::Initialize()
{
  return InitializeImGui();
}

void Renderer::Shutdown()
{
  // First stop any framedumping, which might need to dump the last xfb frame. This process
  // can require additional graphics sub-systems so it needs to be done first
  ShutdownFrameDumping();
  
  if (m_fullscreen_state)
    ChangeFullscreenState(false, 0.0f);
  
  ShutdownImGui();
}

void Renderer::RenderToXFB(u32 xfbAddr, const EFBRectangle& sourceRc, u32 fbStride, u32 fbHeight,
                           float Gamma)
{
  CheckFifoRecording();

  if (!fbStride || !fbHeight)
    return;
}

unsigned int Renderer::GetEFBScale() const
{
  return m_efb_scale;
}

int Renderer::EFBToScaledX(int x) const
{
  return x * static_cast<int>(m_efb_scale);
}

int Renderer::EFBToScaledY(int y) const
{
  return y * static_cast<int>(m_efb_scale);
}

float Renderer::EFBToScaledXf(float x) const
{
  return x * ((float)GetTargetWidth() / (float)EFB_WIDTH);
}

float Renderer::EFBToScaledYf(float y) const
{
  return y * ((float)GetTargetHeight() / (float)EFB_HEIGHT);
}

std::tuple<int, int> Renderer::CalculateTargetScale(int x, int y) const
{
  return std::make_tuple(x * static_cast<int>(m_efb_scale), y * static_cast<int>(m_efb_scale));
}

// return true if target size changed
bool Renderer::CalculateTargetSize()
{
  if (g_ActiveConfig.iEFBScale == EFB_SCALE_AUTO_INTEGRAL)
  {
    // Set a scale based on the window size
    int width = EFB_WIDTH * m_target_rectangle.GetWidth() / m_last_xfb_width;
    int height = EFB_HEIGHT * m_target_rectangle.GetHeight() / m_last_xfb_height;
    m_efb_scale = std::max((width - 1) / EFB_WIDTH + 1, (height - 1) / EFB_HEIGHT + 1);
  }
  else
  {
    m_efb_scale = g_ActiveConfig.iEFBScale;
  }

  const u32 max_size = g_ActiveConfig.backend_info.MaxTextureSize;
  if (max_size < EFB_WIDTH * m_efb_scale)
    m_efb_scale = max_size / EFB_WIDTH;

  int new_efb_width = 0;
  int new_efb_height = 0;
  std::tie(new_efb_width, new_efb_height) = CalculateTargetScale(EFB_WIDTH, EFB_HEIGHT);

  if (new_efb_width != m_target_width || new_efb_height != m_target_height)
  {
    m_target_width = new_efb_width;
    m_target_height = new_efb_height;
    PixelShaderManager::SetEfbScaleChanged(EFBToScaledXf(1), EFBToScaledYf(1));
    return true;
  }
  return false;
}

std::tuple<TargetRectangle, TargetRectangle>
Renderer::ConvertStereoRectangle(const TargetRectangle& rc) const
{
  // Resize target to half its original size
  TargetRectangle draw_rc = rc;
  if (g_ActiveConfig.stereo_mode == StereoMode::TAB)
  {
    // The height may be negative due to flipped rectangles
    int height = rc.bottom - rc.top;
    draw_rc.top += height / 4;
    draw_rc.bottom -= height / 4;
  }
  else
  {
    int width = rc.right - rc.left;
    draw_rc.left += width / 4;
    draw_rc.right -= width / 4;
  }

  // Create two target rectangle offset to the sides of the backbuffer
  TargetRectangle left_rc = draw_rc;
  TargetRectangle right_rc = draw_rc;
  if (g_ActiveConfig.stereo_mode == StereoMode::TAB)
  {
    left_rc.top -= m_backbuffer_height / 4;
    left_rc.bottom -= m_backbuffer_height / 4;
    right_rc.top += m_backbuffer_height / 4;
    right_rc.bottom += m_backbuffer_height / 4;
  }
  else
  {
    left_rc.left -= m_backbuffer_width / 4;
    left_rc.right -= m_backbuffer_width / 4;
    right_rc.left += m_backbuffer_width / 4;
    right_rc.right += m_backbuffer_width / 4;
  }

  return std::make_tuple(left_rc, right_rc);
}

void Renderer::SaveScreenshot(const std::string& filename, bool wait_for_completion)
{
  // We must not hold the lock while waiting for the screenshot to complete.
  {
    std::lock_guard<std::mutex> lk(m_screenshot_lock);
    m_screenshot_name = filename;
    m_screenshot_request.Set();
  }

  if (wait_for_completion)
  {
    // This is currently only used by Android, and it was using a wait time of 2 seconds.
    m_screenshot_completed.WaitFor(std::chrono::seconds(2));
  }
}

void Renderer::CheckForConfigChanges()
{
  const ShaderHostConfig old_shader_host_config = ShaderHostConfig::GetCurrent();
  const StereoMode old_stereo = g_ActiveConfig.stereo_mode;
  const u32 old_multisamples = g_ActiveConfig.iMultisamples;
  const int old_anisotropy = g_ActiveConfig.iMaxAnisotropy;
  const bool old_force_filtering = g_ActiveConfig.bForceFiltering;
  const bool old_vsync = g_ActiveConfig.bVSyncActive;
  const bool old_bbox = g_ActiveConfig.bBBoxEnable;

  UpdateActiveConfig();

  // Update texture cache settings with any changed options.
  g_texture_cache->OnConfigChanged(g_ActiveConfig);

  // Determine which (if any) settings have changed.
  ShaderHostConfig new_host_config = ShaderHostConfig::GetCurrent();
  u32 changed_bits = 0;
  if (old_shader_host_config.bits != new_host_config.bits)
    changed_bits |= CONFIG_CHANGE_BIT_HOST_CONFIG;
  if (old_stereo != g_ActiveConfig.stereo_mode)
    changed_bits |= CONFIG_CHANGE_BIT_STEREO_MODE;
  if (old_multisamples != g_ActiveConfig.iMultisamples)
    changed_bits |= CONFIG_CHANGE_BIT_MULTISAMPLES;
  if (old_anisotropy != g_ActiveConfig.iMaxAnisotropy)
    changed_bits |= CONFIG_CHANGE_BIT_ANISOTROPY;
  if (old_force_filtering != g_ActiveConfig.bForceFiltering)
    changed_bits |= CONFIG_CHANGE_BIT_FORCE_TEXTURE_FILTERING;
  if (old_vsync != g_ActiveConfig.bVSyncActive)
    changed_bits |= CONFIG_CHANGE_BIT_VSYNC;
  if (old_bbox != g_ActiveConfig.bBBoxEnable)
    changed_bits |= CONFIG_CHANGE_BIT_BBOX;
  if (CalculateTargetSize())
    changed_bits |= CONFIG_CHANGE_BIT_TARGET_SIZE;

  // No changes?
  if (changed_bits == 0)
    return;

  // Notify the backend of the changes, if any.
  OnConfigChanged(changed_bits);

  // Reload shaders if host config has changed.
  if (changed_bits & (CONFIG_CHANGE_BIT_HOST_CONFIG | CONFIG_CHANGE_BIT_MULTISAMPLES))
  {
    OSD::AddMessage("Video config changed, reloading shaders.", OSD::Duration::NORMAL);
    SetPipeline(nullptr);
    g_vertex_manager->InvalidatePipelineObject();
    g_shader_cache->SetHostConfig(new_host_config, g_ActiveConfig.iMultisamples);
  }
}

// Create On-Screen-Messages
void Renderer::DrawDebugText()
{
  const auto& config = SConfig::GetInstance();
  if (g_ActiveConfig.bShowFPS)
  {
    // Position in the top-right corner of the screen.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - (10.0f * m_backbuffer_scale),
                                   10.0f * m_backbuffer_scale),
                            ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(100.0f * m_backbuffer_scale, 90.0f * m_backbuffer_scale));

    if (ImGui::Begin("FPS", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav |
                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing))
    {
    const Core::PerformanceStatistics& pstats = Core::GetPerformanceStatistics();
	ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "FPS: %.2f", pstats.FPS);
	ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "VPS: %.2f", pstats.VPS);
	ImGui::TextColored(ImVec4(0.0f, 1.0f, 1.0f, 1.0f), "SPEED: %.2f", pstats.Speed);
    }
    ImGui::End();
  }

  const bool show_movie_window =
      config.m_ShowFrameCount | config.m_ShowLag | config.m_ShowInputDisplay | config.m_ShowRTC;
  if (show_movie_window)
  {
    // Position under the FPS display.
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - (10.0f * m_backbuffer_scale),
                                   50.0f * m_backbuffer_scale),
                            ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(150.0f * m_backbuffer_scale, 20.0f * m_backbuffer_scale),
        ImGui::GetIO().DisplaySize);
    if (ImGui::Begin("Movie", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
    {
      if (config.m_ShowFrameCount)
      {
        ImGui::Text("Frame: %" PRIu64, Movie::GetCurrentFrame());
      }
      if (Movie::IsPlayingInput())
      {
        ImGui::Text("Input: %" PRIu64 " / %" PRIu64, Movie::GetCurrentInputCount(),
                    Movie::GetTotalInputCount());
      }
      if (SConfig::GetInstance().m_ShowLag)
        ImGui::Text("Lag: %" PRIu64 "\n", Movie::GetCurrentLagCount());
      if (SConfig::GetInstance().m_ShowInputDisplay)
        ImGui::TextUnformatted(Movie::GetInputDisplay().c_str());
      if (SConfig::GetInstance().m_ShowRTC)
        ImGui::TextUnformatted(Movie::GetRTCDisplay().c_str());
    }
    ImGui::End();
  }

  if (g_ActiveConfig.bOverlayStats)
    Statistics::Display();

  if (g_ActiveConfig.bOverlayProjStats)
    Statistics::DisplayProj();
}

float Renderer::CalculateDrawAspectRatio() const
{
  if (g_ActiveConfig.aspect_mode == AspectMode::Stretch)
  {
    // If stretch is enabled, we prefer the aspect ratio of the window.
    return (static_cast<float>(m_backbuffer_width) / static_cast<float>(m_backbuffer_height));
  }

  // The rendering window aspect ratio as a proportion of the 4:3 or 16:9 ratio
  if (g_ActiveConfig.aspect_mode == AspectMode::AnalogWide ||
      (g_ActiveConfig.aspect_mode != AspectMode::Analog && m_aspect_wide))
  {
    return AspectToWidescreen(VideoInterface::GetAspectRatio());
  }
  else
  {
    return VideoInterface::GetAspectRatio();
  }
}

bool Renderer::IsHeadless() const
{
  return true;
}

void Renderer::ChangeSurface(void* new_surface_handle)
{
  std::lock_guard<std::mutex> lock(m_swap_mutex);
  m_new_surface_handle = new_surface_handle;
  m_surface_changed.Set();
}

void Renderer::ResizeSurface()
{
  std::lock_guard<std::mutex> lock(m_swap_mutex);
  m_surface_resized.Set();
}

std::tuple<float, float> Renderer::ScaleToDisplayAspectRatio(const int width,
                                                             const int height) const
{
  // Scale either the width or height depending the content aspect ratio.
  // This way we preserve as much resolution as possible when scaling.
  float scaled_width = static_cast<float>(width);
  float scaled_height = static_cast<float>(height);
  const float draw_aspect = CalculateDrawAspectRatio();
  if (scaled_width / scaled_height >= draw_aspect)
    scaled_height = scaled_width / draw_aspect;
  else
    scaled_width = scaled_height * draw_aspect;
  return std::make_tuple(scaled_width, scaled_height);
}

void Renderer::UpdateDrawRectangle()
{
  // The rendering window size
  const float win_width = static_cast<float>(m_backbuffer_width);
  const float win_height = static_cast<float>(m_backbuffer_height);

  // Update aspect ratio hack values
  // Won't take effect until next frame
  // Don't know if there is a better place for this code so there isn't a 1 frame delay
  if (g_ActiveConfig.bWidescreenHack)
  {
    float source_aspect = VideoInterface::GetAspectRatio();
    if (m_aspect_wide)
      source_aspect = AspectToWidescreen(source_aspect);
    float target_aspect = 0.0f;

    switch (g_ActiveConfig.aspect_mode)
    {
    case AspectMode::Stretch:
      target_aspect = win_width / win_height;
      break;
    case AspectMode::Analog:
      target_aspect = VideoInterface::GetAspectRatio();
      break;
    case AspectMode::AnalogWide:
      target_aspect = AspectToWidescreen(VideoInterface::GetAspectRatio());
      break;
    case AspectMode::Auto:
    default:
      target_aspect = source_aspect;
      break;
    }

    float adjust = source_aspect / target_aspect;
    if (adjust > 1)
    {
      // Vert+
      g_Config.fAspectRatioHackW = 1;
      g_Config.fAspectRatioHackH = 1 / adjust;
    }
    else
    {
      // Hor+
      g_Config.fAspectRatioHackW = adjust;
      g_Config.fAspectRatioHackH = 1;
    }
  }
  else
  {
    // Hack is disabled
    g_Config.fAspectRatioHackW = 1;
    g_Config.fAspectRatioHackH = 1;
  }

  float draw_width, draw_height, crop_width, crop_height;

  // get the picture aspect ratio
  draw_width = crop_width = CalculateDrawAspectRatio();
  draw_height = crop_height = 1;

  // crop the picture to a standard aspect ratio
  if (g_ActiveConfig.bCrop && g_ActiveConfig.aspect_mode != AspectMode::Stretch)
  {
    float expected_aspect = (g_ActiveConfig.aspect_mode == AspectMode::AnalogWide ||
                             (g_ActiveConfig.aspect_mode != AspectMode::Analog && m_aspect_wide)) ?
                                (16.0f / 9.0f) :
                                (4.0f / 3.0f);
    if (crop_width / crop_height >= expected_aspect)
    {
      // the picture is flatter than it should be
      crop_width = crop_height * expected_aspect;
    }
    else
    {
      // the picture is skinnier than it should be
      crop_height = crop_width / expected_aspect;
    }
  }

  // scale the picture to fit the rendering window
  if (win_width / win_height >= crop_width / crop_height)
  {
    // the window is flatter than the picture
    draw_width *= win_height / crop_height;
    crop_width *= win_height / crop_height;
    draw_height *= win_height / crop_height;
    crop_height = win_height;
  }
  else
  {
    // the window is skinnier than the picture
    draw_width *= win_width / crop_width;
    draw_height *= win_width / crop_width;
    crop_height *= win_width / crop_width;
    crop_width = win_width;
  }

  // ensure divisibility by 4 to make it compatible with all the video encoders
  draw_width = std::ceil(draw_width) - static_cast<int>(std::ceil(draw_width)) % 4;
  draw_height = std::ceil(draw_height) - static_cast<int>(std::ceil(draw_height)) % 4;

  m_target_rectangle.left = static_cast<int>(std::round(win_width / 2.0 - draw_width / 2.0));
  m_target_rectangle.top = static_cast<int>(std::round(win_height / 2.0 - draw_height / 2.0));
  m_target_rectangle.right = m_target_rectangle.left + static_cast<int>(draw_width);
  m_target_rectangle.bottom = m_target_rectangle.top + static_cast<int>(draw_height);
}

void Renderer::SetWindowSize(int width, int height)
{
  std::tie(width, height) = CalculateOutputDimensions(width, height);

  // Track the last values of width/height to avoid sending a window resize event every frame.
  if (width != m_last_window_request_width || height != m_last_window_request_height)
  {
    m_last_window_request_width = width;
    m_last_window_request_height = height;
    if (!m_fullscreen_state)
      Host_RequestRenderWindowSize(width, height);
  }
}

std::tuple<int, int> Renderer::CalculateOutputDimensions(int width, int height)
{
  width = std::max(width, 1);
  height = std::max(height, 1);

  float scaled_width, scaled_height;
  std::tie(scaled_width, scaled_height) = ScaleToDisplayAspectRatio(width, height);

  if (g_ActiveConfig.bCrop)
  {
    // Force 4:3 or 16:9 by cropping the image.
    float current_aspect = scaled_width / scaled_height;
    float expected_aspect = (g_ActiveConfig.aspect_mode == AspectMode::AnalogWide ||
                             (g_ActiveConfig.aspect_mode != AspectMode::Analog && m_aspect_wide)) ?
                                (16.0f / 9.0f) :
                                (4.0f / 3.0f);
    if (current_aspect > expected_aspect)
    {
      // keep height, crop width
      scaled_width = scaled_height * expected_aspect;
    }
    else
    {
      // keep width, crop height
      scaled_height = scaled_width / expected_aspect;
    }
  }

  width = static_cast<int>(std::ceil(scaled_width));
  height = static_cast<int>(std::ceil(scaled_height));

  // UpdateDrawRectangle() makes sure that the rendered image is divisible by four for video
  // encoders, so do that here too to match it
  width -= width % 4;
  height -= height % 4;

  return std::make_tuple(width, height);
}

void Renderer::CheckFifoRecording()
{
  bool wasRecording = g_bRecordFifoData;
  g_bRecordFifoData = FifoRecorder::GetInstance().IsRecording();

  if (g_bRecordFifoData)
  {
    if (!wasRecording)
    {
      RecordVideoMemory();
    }

    FifoRecorder::GetInstance().EndFrame(CommandProcessor::fifo.CPBase,
                                         CommandProcessor::fifo.CPEnd);
  }
}

void Renderer::RecordVideoMemory()
{
  const u32* bpmem_ptr = reinterpret_cast<const u32*>(&bpmem);
  u32 cpmem[256] = {};
  // The FIFO recording format splits XF memory into xfmem and xfregs; follow
  // that split here.
  const u32* xfmem_ptr = reinterpret_cast<const u32*>(&xfmem);
  const u32* xfregs_ptr = reinterpret_cast<const u32*>(&xfmem) + FifoDataFile::XF_MEM_SIZE;
  u32 xfregs_size = sizeof(XFMemory) / 4 - FifoDataFile::XF_MEM_SIZE;

  FillCPMemoryArray(cpmem);

  FifoRecorder::GetInstance().SetVideoMemory(bpmem_ptr, cpmem, xfmem_ptr, xfregs_ptr, xfregs_size,
                                             texMem);
}

void Renderer::SetFullscreen(bool enable_fullscreen)
{
  if (enable_fullscreen == m_fullscreen_state)
    return;

  ChangeFullscreenState(enable_fullscreen,
                        g_ActiveConfig.bSyncRefreshRate ? m_last_refresh_rate : 0.0f);
}

bool Renderer::ChangeFullscreenState(bool enable, float target_refresh_rate)
{
  m_fullscreen_state = enable;
  Host_RequestFullscreen(enable, target_refresh_rate);
  return true;
}


static std::string GenerateImGuiVertexShader()
{
  const APIType api_type = g_ActiveConfig.backend_info.api_type;
  std::stringstream ss;

  // Uniform buffer contains the viewport size, and we transform in the vertex shader.
  if (api_type == APIType::D3D)
    ss << "cbuffer PSBlock : register(b0) {\n";
  else if (api_type == APIType::OpenGL)
    ss << "UBO_BINDING(std140, 1) uniform PSBlock {\n";
  else if (api_type == APIType::Vulkan)
    ss << "UBO_BINDING(std140, 1) uniform PSBlock {\n";
  ss << "float2 u_rcp_viewport_size_mul2;\n";
  ss << "};\n";

  if (api_type == APIType::D3D)
  {
    ss << "void main(in float2 rawpos : POSITION,\n"
       << "          in float2 rawtex0 : TEXCOORD,\n"
       << "          in float4 rawcolor0 : COLOR,\n"
       << "          out float2 frag_uv : TEXCOORD,\n"
       << "          out float4 frag_color : COLOR,\n"
       << "          out float4 out_pos : SV_Position)\n";
  }
  else
  {
    ss << "ATTRIBUTE_LOCATION(" << SHADER_POSITION_ATTRIB << ") in float2 rawpos;\n"
       << "ATTRIBUTE_LOCATION(" << SHADER_TEXTURE0_ATTRIB << ") in float2 rawtex0;\n"
       << "ATTRIBUTE_LOCATION(" << SHADER_COLOR0_ATTRIB << ") in float4 rawcolor0;\n"
       << "VARYING_LOCATION(0) out float2 frag_uv;\n"
       << "VARYING_LOCATION(1) out float4 frag_color;\n"
       << "void main()\n";
  }

  ss << "{\n"
     << "  frag_uv = rawtex0;\n"
     << "  frag_color = rawcolor0;\n";

  ss << "  " << (api_type == APIType::D3D ? "out_pos" : "gl_Position")
     << "= float4(rawpos.x * u_rcp_viewport_size_mul2.x - 1.0, 1.0 - rawpos.y * "
        "u_rcp_viewport_size_mul2.y, 0.0, 1.0);\n";

  // Clip-space is flipped in Vulkan
  if (api_type == APIType::Vulkan)
    ss << "  gl_Position.y = -gl_Position.y;\n";

  ss << "}\n";
  return ss.str();
}

static std::string GenerateImGuiPixelShader()
{
  const APIType api_type = g_ActiveConfig.backend_info.api_type;

  std::stringstream ss;
  if (api_type == APIType::D3D)
  {
    ss << "Texture2DArray tex0 : register(t0);\n"
       << "SamplerState samp0 : register(s0);\n"
       << "void main(in float2 frag_uv : TEXCOORD,\n"
       << "          in float4 frag_color : COLOR,\n"
       << "          out float4 ocol0 : SV_Target)\n";
  }
  else
  {
    ss << "SAMPLER_BINDING(0) uniform sampler2DArray samp0;\n"
       << "VARYING_LOCATION(0) in float2 frag_uv; \n"
       << "VARYING_LOCATION(1) in float4 frag_color;\n"
       << "FRAGMENT_OUTPUT_LOCATION(0) out float4 ocol0;\n"
       << "void main()\n";
  }

  ss << "{\n";

  if (api_type == APIType::D3D)
    ss << "  ocol0 = tex0.Sample(samp0, float3(frag_uv, 0.0)) * frag_color;\n";
  else
    ss << "  ocol0 = texture(samp0, float3(frag_uv, 0.0)) * frag_color;\n";

  ss << "}\n";

  return ss.str();
}

bool Renderer::InitializeImGui()
{
  if (!ImGui::CreateContext())
  {
    PanicAlert("Creating ImGui context failed");
    return false;
  }

  // Don't create an ini file. TODO: Do we want this in the future?
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::GetIO().DisplayFramebufferScale.x = m_backbuffer_scale;
  ImGui::GetIO().DisplayFramebufferScale.y = m_backbuffer_scale;
  ImGui::GetIO().FontGlobalScale = m_backbuffer_scale;
  ImGui::GetStyle().ScaleAllSizes(m_backbuffer_scale);

  PortableVertexDeclaration vdecl = {};
  vdecl.position = {VAR_FLOAT, 2, offsetof(ImDrawVert, pos), true, false};
  vdecl.texcoords[0] = {VAR_FLOAT, 2, offsetof(ImDrawVert, uv), true, false};
  vdecl.colors[0] = {VAR_UNSIGNED_BYTE, 4, offsetof(ImDrawVert, col), true, false};
  vdecl.stride = sizeof(ImDrawVert);
  m_imgui_vertex_format = g_vertex_manager->CreateNativeVertexFormat(vdecl);
  if (!m_imgui_vertex_format)
  {
    PanicAlert("Failed to create imgui vertex format");
    return false;
  }

  const std::string vertex_shader_source = GenerateImGuiVertexShader();
  const std::string pixel_shader_source = GenerateImGuiPixelShader();
  std::unique_ptr<AbstractShader> vertex_shader = CreateShaderFromSource(
      ShaderStage::Vertex, vertex_shader_source.c_str(), vertex_shader_source.size());
  std::unique_ptr<AbstractShader> pixel_shader = CreateShaderFromSource(
      ShaderStage::Pixel, pixel_shader_source.c_str(), pixel_shader_source.size());
  if (!vertex_shader || !pixel_shader)
  {
    PanicAlert("Failed to compile imgui shaders");
    return false;
  }

  AbstractPipelineConfig pconfig = {};
  pconfig.vertex_format = m_imgui_vertex_format.get();
  pconfig.vertex_shader = vertex_shader.get();
  pconfig.pixel_shader = pixel_shader.get();
  pconfig.rasterization_state.hex = RenderState::GetNoCullRasterizationState().hex;
  pconfig.rasterization_state.primitive = PrimitiveType::Triangles;
  pconfig.depth_state.hex = RenderState::GetNoDepthTestingDepthStencilState().hex;
  pconfig.blending_state.hex = RenderState::GetNoBlendingBlendState().hex;
  pconfig.blending_state.blendenable = true;
  pconfig.blending_state.srcfactor = BlendMode::SRCALPHA;
  pconfig.blending_state.dstfactor = BlendMode::INVSRCALPHA;
  pconfig.blending_state.srcfactoralpha = BlendMode::ZERO;
  pconfig.blending_state.dstfactoralpha = BlendMode::ONE;
  pconfig.framebuffer_state.color_texture_format = m_backbuffer_format;
  pconfig.framebuffer_state.depth_texture_format = AbstractTextureFormat::Undefined;
  pconfig.framebuffer_state.samples = 1;
  pconfig.framebuffer_state.per_sample_shading = false;
  pconfig.usage = AbstractPipelineUsage::Utility;
  m_imgui_pipeline = g_renderer->CreatePipeline(pconfig);
  if (!m_imgui_pipeline)
  {
    PanicAlert("Failed to create imgui pipeline");
    return false;
  }

  // Font texture(s).
  {
    ImGuiIO& io = ImGui::GetIO();
    u8* font_tex_pixels;
    int font_tex_width, font_tex_height;
    io.Fonts->GetTexDataAsRGBA32(&font_tex_pixels, &font_tex_width, &font_tex_height);

    TextureConfig font_tex_config(font_tex_width, font_tex_height, 1, 1, 1,
                                  AbstractTextureFormat::RGBA8, false);
    std::unique_ptr<AbstractTexture> font_tex = CreateTexture(font_tex_config);
    if (!font_tex)
    {
      PanicAlert("Failed to create imgui texture");
      return false;
    }
    font_tex->Load(0, font_tex_width, font_tex_height, font_tex_width, font_tex_pixels,
                   sizeof(u32) * font_tex_width * font_tex_height);

    io.Fonts->TexID = font_tex.get();

    m_imgui_textures.push_back(std::move(font_tex));
  }

  m_imgui_last_frame_time = Common::Timer::GetTimeUs();
  BeginImGuiFrame();
  return true;
}

void Renderer::ShutdownImGui()
{
  ImGui::EndFrame();
  ImGui::DestroyContext();
  m_imgui_pipeline.reset();
  m_imgui_vertex_format.reset();
  m_imgui_textures.clear();
}

void Renderer::BeginImGuiFrame()
{
  std::unique_lock<std::mutex> imgui_lock(m_imgui_mutex);

  const u64 current_time_us = Common::Timer::GetTimeUs();
  const u64 time_diff_us = current_time_us - m_imgui_last_frame_time;
  const float time_diff_secs = static_cast<float>(time_diff_us / 1000000.0);
  m_imgui_last_frame_time = current_time_us;

  // Update I/O with window dimensions.
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize =
      ImVec2(static_cast<float>(m_backbuffer_width), static_cast<float>(m_backbuffer_height));
  io.DeltaTime = time_diff_secs;

  ImGui::NewFrame();
}

void Renderer::RenderImGui()
{
  ImGui::Render();

  ImDrawData* draw_data = ImGui::GetDrawData();
  if (!draw_data)
    return;

  SetViewport(0.0f, 0.0f, static_cast<float>(m_backbuffer_width),
              static_cast<float>(m_backbuffer_height), 0.0f, 1.0f);

  // Uniform buffer for draws.
  struct ImGuiUbo
  {
    float u_rcp_viewport_size_mul2[2];
    float padding[2];
  };
  ImGuiUbo ubo = {{1.0f / m_backbuffer_width * 2.0f, 1.0f / m_backbuffer_height * 2.0f}};

  // Set up common state for drawing.
  SetPipeline(m_imgui_pipeline.get());
  SetSamplerState(0, RenderState::GetPointSamplerState());
  g_vertex_manager->UploadUtilityUniforms(&ubo, sizeof(ubo));

  for (int i = 0; i < draw_data->CmdListsCount; i++)
  {
    const ImDrawList* cmdlist = draw_data->CmdLists[i];
    if (cmdlist->VtxBuffer.empty() || cmdlist->IdxBuffer.empty())
      return;

    u32 base_vertex, base_index;
    g_vertex_manager->UploadUtilityVertices(cmdlist->VtxBuffer.Data, sizeof(ImDrawVert),
                                            cmdlist->VtxBuffer.Size, cmdlist->IdxBuffer.Data,
                                            cmdlist->IdxBuffer.Size, &base_vertex, &base_index);

    for (const ImDrawCmd& cmd : cmdlist->CmdBuffer)
    {
      if (cmd.UserCallback)
      {
        cmd.UserCallback(cmdlist, &cmd);
        continue;
      }

      SetScissorRect(MathUtil::Rectangle<int>(
          static_cast<int>(cmd.ClipRect.x), static_cast<int>(cmd.ClipRect.y),
          static_cast<int>(cmd.ClipRect.z), static_cast<int>(cmd.ClipRect.w)));
      SetTexture(0, reinterpret_cast<const AbstractTexture*>(cmd.TextureId));
      DrawIndexed(base_index, cmd.ElemCount, base_vertex);
      base_index += cmd.ElemCount;
    }
  }
}

std::unique_lock<std::mutex> Renderer::GetImGuiLock()
{
  return std::unique_lock<std::mutex>(m_imgui_mutex);
}

void Renderer::BeginUIFrame()
{
  ResetAPIState();
  BindBackbuffer({0.0f, 0.0f, 0.0f, 1.0f});
}

void Renderer::EndUIFrame()
{
  {
    auto lock = GetImGuiLock();
    RenderImGui();
  }

  {
    std::lock_guard<std::mutex> guard(m_swap_mutex);
    PresentBackbuffer();
  }

  BeginImGuiFrame();
  RestoreAPIState();
}

void Renderer::Swap(u32 xfbAddr, u32 fbWidth, u32 fbStride, u32 fbHeight, const EFBRectangle& rc,
                    u64 ticks)
{
  const AspectMode suggested = g_ActiveConfig.suggested_aspect_mode;
  if (suggested == AspectMode::Analog || suggested == AspectMode::AnalogWide)
  {
    m_aspect_wide = suggested == AspectMode::AnalogWide;
  }
  else if (SConfig::GetInstance().bWii)
  {
    m_aspect_wide = Config::Get(Config::SYSCONF_WIDESCREEN);
  }
  else
  {
    // Heuristic to detect if a GameCube game is in 16:9 anamorphic widescreen mode.

    size_t flush_count_4_3, flush_count_anamorphic;
    std::tie(flush_count_4_3, flush_count_anamorphic) =
        g_vertex_manager->ResetFlushAspectRatioCount();
    size_t flush_total = flush_count_4_3 + flush_count_anamorphic;

    // Modify the threshold based on which aspect ratio we're already using: if
    // the game's in 4:3, it probably won't switch to anamorphic, and vice-versa.
    if (m_aspect_wide)
      m_aspect_wide = !(flush_count_4_3 > 0.75 * flush_total);
    else
      m_aspect_wide = flush_count_anamorphic > 0.75 * flush_total;
  }

  // Ensure the last frame was written to the dump.
  // This is required even if frame dumping has stopped, since the frame dump is one frame
  // behind the renderer.
  FlushFrameDump();
  
  // If the refresh rate has changed, update the host.
  const float current_refresh_rate = VideoInterface::GetTargetFractionalRefreshRate();
  if (m_last_refresh_rate != current_refresh_rate)
  {
    m_last_refresh_rate = current_refresh_rate;
    if (IsFullscreen() && g_ActiveConfig.bSyncRefreshRate)
      ChangeFullscreenState(true, current_refresh_rate);
  }


  if (xfbAddr && fbWidth && fbStride && fbHeight)
  {
    constexpr int force_safe_texture_cache_hash = 0;
    // Get the current XFB from texture cache
    auto* xfb_entry = g_texture_cache->GetXFBTexture(
        xfbAddr, fbStride, fbHeight, TextureFormat::XFB, force_safe_texture_cache_hash);

    if (xfb_entry && xfb_entry->id != m_last_xfb_id)
    {
      const TextureConfig& texture_config = xfb_entry->texture->GetConfig();
      m_last_xfb_texture = xfb_entry->texture.get();
      m_last_xfb_id = xfb_entry->id;
      m_last_xfb_ticks = ticks;

      auto xfb_rect = texture_config.GetRect();

      // It's possible that the returned XFB texture is native resolution
      // even when we're rendering at higher than native resolution
      // if the XFB was was loaded entirely from console memory.
      // If so, adjust the rectangle by native resolution instead of scaled resolution.
      const u32 native_stride_width_difference = fbStride - fbWidth;
      if (texture_config.width == xfb_entry->native_width)
        xfb_rect.right -= native_stride_width_difference;
      else
        xfb_rect.right -= EFBToScaledX(native_stride_width_difference);

      m_last_xfb_region = xfb_rect;

      // Since we use the common pipelines here and draw vertices if a batch is currently being
      // built by the vertex loader, we end up trampling over its pointer, as we share the buffer
      // with the loader, and it has not been unmapped yet. Force a pipeline flush to avoid this.
      g_vertex_manager->Flush();

      // Render the XFB to the screen.
      ResetAPIState();
      BindBackbuffer({0.0f, 0.0f, 0.0f, 1.0f});
      UpdateDrawRectangle();
      RenderXFBToScreen(xfb_entry->texture.get(), xfb_rect);

      // Hold the imgui lock while we're presenting.
      // It's only to prevent races on inputs anyway, at this point.
      {
        auto lock = GetImGuiLock();

        DrawDebugText();
        OSD::DrawMessages();

        RenderImGui();
      }

      // Present to the window system.
      {
        std::lock_guard<std::mutex> guard(m_swap_mutex);
        PresentBackbuffer();
      }

      // Update the window size based on the frame that was just rendered.
      // Due to depending on guest state, we need to call this every frame.
      SetWindowSize(texture_config.width, texture_config.height);

      DolphinAnalytics::PerformanceSample perf_sample;
      perf_sample.speed_ratio = SystemTimers::GetEstimatedEmulationPerformance();
      perf_sample.num_prims = stats.thisFrame.numPrims + stats.thisFrame.numDLPrims;
      perf_sample.num_draw_calls = stats.thisFrame.numDrawCalls;
      DolphinAnalytics::Instance()->ReportPerformanceInfo(std::move(perf_sample));

      if (IsFrameDumping())
        DumpCurrentFrame();

      frameCount++;
      GFX_DEBUGGER_PAUSE_AT(NEXT_FRAME, true);

      // Begin new frame
      stats.ResetFrame();
      g_shader_cache->RetrieveAsyncShaders();
      BeginImGuiFrame();

      // We invalidate the pipeline object at the start of the frame.
      // This is for the rare case where only a single pipeline configuration is used,
      // and hybrid ubershaders have compiled the specialized shader, but without any
      // state changes the specialized shader will not take over.
      g_vertex_manager->InvalidatePipelineObject();

      // Flush any outstanding EFB copies to RAM, in case the game is running at an uncapped frame
      // rate and not waiting for vblank. Otherwise, we'd end up with a huge list of pending copies.
      g_texture_cache->FlushEFBCopies();

      // Remove stale EFB/XFB copies.
      g_texture_cache->Cleanup(frameCount);

      // Handle any config changes, this gets propogated to the backend.
      CheckForConfigChanges();
      g_Config.iSaveTargetId = 0;

      RestoreAPIState();

      Core::Callback_VideoCopiedToXFB(true);
    }
    else
    {
      Flush();
    }

    // Update our last xfb values
    m_last_xfb_width = (fbStride < 1 || fbStride > MAX_XFB_WIDTH) ? MAX_XFB_WIDTH : fbStride;
    m_last_xfb_height = (fbHeight < 1 || fbHeight > MAX_XFB_HEIGHT) ? MAX_XFB_HEIGHT : fbHeight;
  }
  else
  {
    Flush();
  }
}

bool Renderer::IsFrameDumping()
{
  if (m_screenshot_request.IsSet())
    return true;

  if (SConfig::GetInstance().m_DumpFrames)
    return true;

  return false;
}

void Renderer::DumpCurrentFrame()
{
  // Scale/render to frame dump texture.
  RenderFrameDump();

  // Queue a readback for the next frame.
  QueueFrameDumpReadback();
}

void Renderer::RenderFrameDump()
{
  int target_width, target_height;
  if (!g_ActiveConfig.bInternalResolutionFrameDumps && !IsHeadless())
  {
    auto target_rect = GetTargetRectangle();
    target_width = target_rect.GetWidth();
    target_height = target_rect.GetHeight();
  }
  else
  {
    std::tie(target_width, target_height) = CalculateOutputDimensions(
        m_last_xfb_texture->GetConfig().width, m_last_xfb_texture->GetConfig().height);
  }

  // Ensure framebuffer exists (we lazily allocate it in case frame dumping isn't used).
  // Or, resize texture if it isn't large enough to accommodate the current frame.
  if (!m_frame_dump_render_texture ||
      m_frame_dump_render_texture->GetConfig().width != static_cast<u32>(target_width) ||
      m_frame_dump_render_texture->GetConfig().height != static_cast<u32>(target_height))
  {
    // Recreate texture objects. Release before creating so we don't temporarily use twice the RAM.
    TextureConfig config(target_width, target_height, 1, 1, 1, AbstractTextureFormat::RGBA8, true);
    m_frame_dump_render_texture.reset();
    m_frame_dump_render_texture = CreateTexture(config);
    ASSERT(m_frame_dump_render_texture);
  }

  // Scaling is likely to occur here, but if possible, do a bit-for-bit copy.
  if (m_last_xfb_region.GetWidth() != target_width ||
      m_last_xfb_region.GetHeight() != target_height)
  {
    m_frame_dump_render_texture->ScaleRectangleFromTexture(
        m_last_xfb_texture, m_last_xfb_region, EFBRectangle{0, 0, target_width, target_height});
  }
  else
  {
    m_frame_dump_render_texture->CopyRectangleFromTexture(
        m_last_xfb_texture, m_last_xfb_region, 0, 0,
        EFBRectangle{0, 0, target_width, target_height}, 0, 0);
  }
}

void Renderer::QueueFrameDumpReadback()
{
  // Index 0 was just sent to AVI dump. Swap with the second texture.
  if (m_frame_dump_readback_textures[0])
    std::swap(m_frame_dump_readback_textures[0], m_frame_dump_readback_textures[1]);

  std::unique_ptr<AbstractStagingTexture>& rbtex = m_frame_dump_readback_textures[0];
  if (!rbtex || rbtex->GetConfig() != m_frame_dump_render_texture->GetConfig())
  {
    rbtex = CreateStagingTexture(StagingTextureType::Readback,
                                 m_frame_dump_render_texture->GetConfig());
  }

  m_last_frame_state = AVIDump::FetchState(m_last_xfb_ticks);
  m_last_frame_exported = true;
  rbtex->CopyFromTexture(m_frame_dump_render_texture.get(), 0, 0);
}

void Renderer::FlushFrameDump()
{
  if (!m_last_frame_exported)
    return;

  // Ensure the previously-queued frame was encoded.
  FinishFrameData();

  // Queue encoding of the last frame dumped.
  std::unique_ptr<AbstractStagingTexture>& rbtex = m_frame_dump_readback_textures[0];
  rbtex->Flush();
  if (rbtex->Map())
  {
    DumpFrameData(reinterpret_cast<u8*>(rbtex->GetMappedPointer()), rbtex->GetConfig().width,
                  rbtex->GetConfig().height, static_cast<int>(rbtex->GetMappedStride()),
                  m_last_frame_state);
    rbtex->Unmap();
  }

  m_last_frame_exported = false;

  // Shutdown frame dumping if it is no longer active.
  if (!IsFrameDumping())
    ShutdownFrameDumping();
}

void Renderer::ShutdownFrameDumping()
{
  // Ensure the last queued readback has been sent to the encoder.
  FlushFrameDump();

  if (!m_frame_dump_thread_running.IsSet())
    return;

  // Ensure previous frame has been encoded.
  FinishFrameData();

  // Wake thread up, and wait for it to exit.
  m_frame_dump_thread_running.Clear();
  m_frame_dump_start.Set();
  if (m_frame_dump_thread.joinable())
    m_frame_dump_thread.join();
  m_frame_dump_render_texture.reset();
  for (auto& tex : m_frame_dump_readback_textures)
    tex.reset();
}

void Renderer::DumpFrameData(const u8* data, int w, int h, int stride, const AVIDump::Frame& state)
{
  m_frame_dump_config = FrameDumpConfig{data, w, h, stride, state};

  if (!m_frame_dump_thread_running.IsSet())
  {
    if (m_frame_dump_thread.joinable())
      m_frame_dump_thread.join();
    m_frame_dump_thread_running.Set();
    m_frame_dump_thread = std::thread(&Renderer::RunFrameDumps, this);
  }

  // Wake worker thread up.
  m_frame_dump_start.Set();
  m_frame_dump_frame_running = true;
}

void Renderer::FinishFrameData()
{
  if (!m_frame_dump_frame_running)
    return;

  m_frame_dump_done.Wait();
  m_frame_dump_frame_running = false;
}

void Renderer::RunFrameDumps()
{
  Common::SetCurrentThreadName("FrameDumping");
  bool dump_to_avi = !g_ActiveConfig.bDumpFramesAsImages;
  bool frame_dump_started = false;

// If Dolphin was compiled without libav, we only support dumping to images.
#if !defined(HAVE_FFMPEG)
  if (dump_to_avi)
  {
    WARN_LOG(VIDEO, "AVI frame dump requested, but Dolphin was compiled without libav. "
                    "Frame dump will be saved as images instead.");
    dump_to_avi = false;
  }
#endif

  while (true)
  {
    m_frame_dump_start.Wait();
    if (!m_frame_dump_thread_running.IsSet())
      break;

    auto config = m_frame_dump_config;

    // Save screenshot
    if (m_screenshot_request.TestAndClear())
    {
      std::lock_guard<std::mutex> lk(m_screenshot_lock);

      if (TextureToPng(config.data, config.stride, m_screenshot_name, config.width, config.height,
                       false))
        OSD::AddMessage("Screenshot saved to " + m_screenshot_name);

      // Reset settings
      m_screenshot_name.clear();
      m_screenshot_completed.Set();
    }

    if (SConfig::GetInstance().m_DumpFrames)
    {
      if (!frame_dump_started)
      {
        if (dump_to_avi)
          frame_dump_started = StartFrameDumpToAVI(config);
        else
          frame_dump_started = StartFrameDumpToImage(config);

        // Stop frame dumping if we fail to start.
        if (!frame_dump_started)
          SConfig::GetInstance().m_DumpFrames = false;
      }

      // If we failed to start frame dumping, don't write a frame.
      if (frame_dump_started)
      {
        if (dump_to_avi)
          DumpFrameToAVI(config);
        else
          DumpFrameToImage(config);
      }
    }

    m_frame_dump_done.Set();
  }

  if (frame_dump_started)
  {
    // No additional cleanup is needed when dumping to images.
    if (dump_to_avi)
      StopFrameDumpToAVI();
  }
}

#if defined(HAVE_FFMPEG)

bool Renderer::StartFrameDumpToAVI(const FrameDumpConfig& config)
{
  return AVIDump::Start(config.width, config.height);
}

void Renderer::DumpFrameToAVI(const FrameDumpConfig& config)
{
  AVIDump::AddFrame(config.data, config.width, config.height, config.stride, config.state);
}

void Renderer::StopFrameDumpToAVI()
{
  AVIDump::Stop();
}

#else

bool Renderer::StartFrameDumpToAVI(const FrameDumpConfig& config)
{
  return false;
}

void Renderer::DumpFrameToAVI(const FrameDumpConfig& config)
{
}

void Renderer::StopFrameDumpToAVI()
{
}

#endif  // defined(HAVE_FFMPEG)

std::string Renderer::GetFrameDumpNextImageFileName() const
{
  return StringFromFormat("%sframedump_%u.png", File::GetUserPath(D_DUMPFRAMES_IDX).c_str(),
                          m_frame_dump_image_counter);
}

bool Renderer::StartFrameDumpToImage(const FrameDumpConfig& config)
{
  m_frame_dump_image_counter = 1;
  if (!SConfig::GetInstance().m_DumpFramesSilent)
  {
    // Only check for the presence of the first image to confirm overwriting.
    // A previous run will always have at least one image, and it's safe to assume that if the user
    // has allowed the first image to be overwritten, this will apply any remaining images as well.
    std::string filename = GetFrameDumpNextImageFileName();
    if (File::Exists(filename))
    {
      if (!AskYesNoT("Frame dump image(s) '%s' already exists. Overwrite?", filename.c_str()))
        return false;
    }
  }

  return true;
}

void Renderer::DumpFrameToImage(const FrameDumpConfig& config)
{
  std::string filename = GetFrameDumpNextImageFileName();
  TextureToPng(config.data, config.stride, filename, config.width, config.height, false);
  m_frame_dump_image_counter++;
}

bool Renderer::UseVertexDepthRange() const
{
  // We can't compute the depth range in the vertex shader if we don't support depth clamp.
  if (!g_ActiveConfig.backend_info.bSupportsDepthClamp)
    return false;

  // We need a full depth range if a ztexture is used.
  if (bpmem.ztex2.type != ZTEXTURE_DISABLE && !bpmem.zcontrol.early_ztest)
    return true;

  // If an inverted depth range is unsupported, we also need to check if the range is inverted.
  if (!g_ActiveConfig.backend_info.bSupportsReversedDepthRange && xfmem.viewport.zRange < 0.0f)
    return true;

  // If an oversized depth range or a ztexture is used, we need to calculate the depth range
  // in the vertex shader.
  return fabs(xfmem.viewport.zRange) > 16777215.0f || fabs(xfmem.viewport.farZ) > 16777215.0f;
}

std::unique_ptr<VideoCommon::AsyncShaderCompiler> Renderer::CreateAsyncShaderCompiler()
{
  return std::make_unique<VideoCommon::AsyncShaderCompiler>();
}
