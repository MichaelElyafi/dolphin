// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/WiimoteEmu/Extension/Drums.h"

#include <array>
#include <cassert>
#include <cstring>

#include "Common/BitUtils.h"
#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"

#include "InputCommon/ControllerEmu/Control/Input.h"
#include "InputCommon/ControllerEmu/ControlGroup/AnalogStick.h"
#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"

namespace WiimoteEmu
{
constexpr std::array<u8, 6> drums_id{{0x01, 0x00, 0xa4, 0x20, 0x01, 0x03}};

constexpr std::array<u16, 6> drum_pad_bitmasks{{
    Drums::PAD_RED,
    Drums::PAD_YELLOW,
    Drums::PAD_BLUE,
    Drums::PAD_GREEN,
    Drums::PAD_ORANGE,
    Drums::PAD_BASS,
}};

constexpr std::array<const char*, 6> drum_pad_names{{
    _trans("Red"),
    _trans("Yellow"),
    _trans("Blue"),
    _trans("Green"),
    _trans("Orange"),
    _trans("Bass"),
}};

constexpr std::array<u16, 2> drum_button_bitmasks{{
    Drums::BUTTON_MINUS,
    Drums::BUTTON_PLUS,
}};

Drums::Drums() : EncryptedExtension(_trans("Drums"))
{
  // pads
  groups.emplace_back(m_pads = new ControllerEmu::Buttons(_trans("Pads")));
  for (auto& drum_pad_name : drum_pad_names)
  {
    m_pads->controls.emplace_back(
        new ControllerEmu::Input(ControllerEmu::Translate, drum_pad_name));
  }

  // stick
  constexpr auto gate_radius = ControlState(STICK_GATE_RADIUS) / STICK_RADIUS;
  groups.emplace_back(m_stick =
                          new ControllerEmu::OctagonAnalogStick(_trans("Stick"), gate_radius));

  // buttons
  groups.emplace_back(m_buttons = new ControllerEmu::Buttons(_trans("Buttons")));
  m_buttons->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "-"));
  m_buttons->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "+"));
}

void Drums::Update()
{
  DataFormat drum_data = {};

  // stick
  {
    const ControllerEmu::AnalogStick::StateData stick_state = m_stick->GetState();

    drum_data.sx = static_cast<u8>((stick_state.x * STICK_RADIUS) + STICK_CENTER);
    drum_data.sy = static_cast<u8>((stick_state.y * STICK_RADIUS) + STICK_CENTER);
  }

  // TODO: Implement these:
  drum_data.which = 0x1F;
  drum_data.none = 1;
  drum_data.hhp = 1;
  drum_data.velocity = 0xf;
  drum_data.softness = 7;

  // buttons
  m_buttons->GetState(&drum_data.bt, drum_button_bitmasks.data());

  // pads
  m_pads->GetState(&drum_data.bt, drum_pad_bitmasks.data());

  // flip button bits
  drum_data.bt ^= 0xFFFF;

  Common::BitCastPtr<DataFormat>(&m_reg.controller_data) = drum_data;
}

bool Drums::IsButtonPressed() const
{
  u16 buttons = 0;
  m_buttons->GetState(&buttons, drum_button_bitmasks.data());
  m_pads->GetState(&buttons, drum_pad_bitmasks.data());
  return buttons != 0;
}

void Drums::Reset()
{
  m_reg = {};
  m_reg.identifier = drums_id;

  // TODO: Is there calibration data?
}

ControllerEmu::ControlGroup* Drums::GetGroup(DrumsGroup group)
{
  switch (group)
  {
  case DrumsGroup::Buttons:
    return m_buttons;
  case DrumsGroup::Pads:
    return m_pads;
  case DrumsGroup::Stick:
    return m_stick;
  default:
    assert(false);
    return nullptr;
  }
}
}  // namespace WiimoteEmu
