// Copyright 2010 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "Core/HW/WiimoteEmu/Extension/Nunchuk.h"

#include <array>
#include <cassert>
#include <cstring>

#include "Common/BitUtils.h"
#include "Common/Common.h"
#include "Common/CommonTypes.h"
#include "Common/MathUtil.h"
#include "Core/Config/WiimoteInputSettings.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"

#include "InputCommon/ControllerEmu/Control/Input.h"
#include "InputCommon/ControllerEmu/ControlGroup/AnalogStick.h"
#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/ControlGroup/Force.h"
#include "InputCommon/ControllerEmu/ControlGroup/Tilt.h"

namespace WiimoteEmu
{
constexpr std::array<u8, 6> nunchuk_id{{0x00, 0x00, 0xa4, 0x20, 0x00, 0x00}};

constexpr std::array<u8, 2> nunchuk_button_bitmasks{{
    Nunchuk::BUTTON_C,
    Nunchuk::BUTTON_Z,
}};

Nunchuk::Nunchuk() : EncryptedExtension(_trans("Nunchuk"))
{
  // buttons
  groups.emplace_back(m_buttons = new ControllerEmu::Buttons(_trans("Buttons")));
  m_buttons->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "C"));
  m_buttons->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "Z"));

  // stick
  constexpr auto gate_radius = ControlState(STICK_GATE_RADIUS) / STICK_RADIUS;
  groups.emplace_back(m_stick =
                          new ControllerEmu::OctagonAnalogStick(_trans("Stick"), gate_radius));

  // swing
  groups.emplace_back(m_swing = new ControllerEmu::Force(_trans("Swing")));
  groups.emplace_back(m_swing_slow = new ControllerEmu::Force("SwingSlow"));
  groups.emplace_back(m_swing_fast = new ControllerEmu::Force("SwingFast"));

  // tilt
  groups.emplace_back(m_tilt = new ControllerEmu::Tilt(_trans("Tilt")));

  // shake
  groups.emplace_back(m_shake = new ControllerEmu::Buttons(_trans("Shake")));
  // i18n: Refers to a 3D axis (used when mapping motion controls)
  m_shake->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::Translate, _trans("X")));
  // i18n: Refers to a 3D axis (used when mapping motion controls)
  m_shake->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::Translate, _trans("Y")));
  // i18n: Refers to a 3D axis (used when mapping motion controls)
  m_shake->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::Translate, _trans("Z")));

  groups.emplace_back(m_shake_soft = new ControllerEmu::Buttons("ShakeSoft"));
  m_shake_soft->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "X"));
  m_shake_soft->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "Y"));
  m_shake_soft->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "Z"));

  groups.emplace_back(m_shake_hard = new ControllerEmu::Buttons("ShakeHard"));
  m_shake_hard->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "X"));
  m_shake_hard->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "Y"));
  m_shake_hard->controls.emplace_back(new ControllerEmu::Input(ControllerEmu::DoNotTranslate, "Z"));
}

void Nunchuk::Update()
{
  DataFormat nc_data = {};

  // stick
  const ControllerEmu::AnalogStick::StateData stick_state = m_stick->GetState();
  nc_data.jx = u8(STICK_CENTER + stick_state.x * STICK_RADIUS);
  nc_data.jy = u8(STICK_CENTER + stick_state.y * STICK_RADIUS);

  // Some terribly coded games check whether to move with a check like
  //
  //     if (x != 0 && y != 0)
  //         do_movement(x, y);
  //
  // With keyboard controls, these games break if you simply hit
  // of the axes. Adjust this if you're hitting one of the axes so that
  // we slightly tweak the other axis.
  if (nc_data.jx != STICK_CENTER || nc_data.jy != STICK_CENTER)
  {
    if (nc_data.jx == STICK_CENTER)
      ++nc_data.jx;
    if (nc_data.jy == STICK_CENTER)
      ++nc_data.jy;
  }

  NormalizedAccelData accel;

  // tilt
  EmulateTilt(&accel, m_tilt);

  // swing
  EmulateSwing(&accel, m_swing, Config::Get(Config::NUNCHUK_INPUT_SWING_INTENSITY_MEDIUM));
  EmulateSwing(&accel, m_swing_slow, Config::Get(Config::NUNCHUK_INPUT_SWING_INTENSITY_SLOW));
  EmulateSwing(&accel, m_swing_fast, Config::Get(Config::NUNCHUK_INPUT_SWING_INTENSITY_FAST));

  // shake
  EmulateShake(&accel, m_shake, Config::Get(Config::NUNCHUK_INPUT_SHAKE_INTENSITY_MEDIUM),
               m_shake_step.data());
  EmulateShake(&accel, m_shake_soft, Config::Get(Config::NUNCHUK_INPUT_SHAKE_INTENSITY_SOFT),
               m_shake_soft_step.data());
  EmulateShake(&accel, m_shake_hard, Config::Get(Config::NUNCHUK_INPUT_SHAKE_INTENSITY_HARD),
               m_shake_hard_step.data());

  // buttons
  m_buttons->GetState(&nc_data.bt.hex, nunchuk_button_bitmasks.data());

  // flip the button bits :/
  nc_data.bt.hex ^= 0x03;

  // Calibration values are 8-bit but we want 10-bit precision, so << 2.
  auto acc = DenormalizeAccelData(accel, ACCEL_ZERO_G << 2, ACCEL_ONE_G << 2);

  nc_data.ax = (acc.x >> 2) & 0xFF;
  nc_data.ay = (acc.y >> 2) & 0xFF;
  nc_data.az = (acc.z >> 2) & 0xFF;
  nc_data.bt.acc_x_lsb = acc.x & 0x3;
  nc_data.bt.acc_y_lsb = acc.y & 0x3;
  nc_data.bt.acc_z_lsb = acc.z & 0x3;

  Common::BitCastPtr<DataFormat>(&m_reg.controller_data) = nc_data;
}

bool Nunchuk::IsButtonPressed() const
{
  u8 buttons = 0;
  m_buttons->GetState(&buttons, nunchuk_button_bitmasks.data());
  return buttons != 0;
}

void Nunchuk::Reset()
{
  m_reg = {};
  m_reg.identifier = nunchuk_id;

  // Build calibration data:
  m_reg.calibration = {{
      // Accel Zero X,Y,Z:
      ACCEL_ZERO_G,
      ACCEL_ZERO_G,
      ACCEL_ZERO_G,
      // Possibly LSBs of zero values:
      0x00,
      // Accel 1G X,Y,Z:
      ACCEL_ONE_G,
      ACCEL_ONE_G,
      ACCEL_ONE_G,
      // Possibly LSBs of 1G values:
      0x00,
      // Stick X max,min,center:
      STICK_CENTER + STICK_RADIUS,
      STICK_CENTER - STICK_RADIUS,
      STICK_CENTER,
      // Stick Y max,min,center:
      STICK_CENTER + STICK_RADIUS,
      STICK_CENTER - STICK_RADIUS,
      STICK_CENTER,
      // 2 checksum bytes calculated below:
      0x00,
      0x00,
  }};

  UpdateCalibrationDataChecksum(m_reg.calibration, CALIBRATION_CHECKSUM_BYTES);
}

ControllerEmu::ControlGroup* Nunchuk::GetGroup(NunchukGroup group)
{
  switch (group)
  {
  case NunchukGroup::Buttons:
    return m_buttons;
  case NunchukGroup::Stick:
    return m_stick;
  case NunchukGroup::Tilt:
    return m_tilt;
  case NunchukGroup::Swing:
    return m_swing;
  case NunchukGroup::Shake:
    return m_shake;
  default:
    assert(false);
    return nullptr;
  }
}

void Nunchuk::LoadDefaults(const ControllerInterface& ciface)
{
  // Stick
  m_stick->SetControlExpression(0, "W");  // up
  m_stick->SetControlExpression(1, "S");  // down
  m_stick->SetControlExpression(2, "A");  // left
  m_stick->SetControlExpression(3, "D");  // right

  // Because our defaults use keyboard input, set calibration shape to a square.
  m_stick->SetCalibrationFromGate(ControllerEmu::SquareStickGate(1.0));

// Buttons
#ifdef _WIN32
  m_buttons->SetControlExpression(0, "LCONTROL");  // C
  m_buttons->SetControlExpression(1, "LSHIFT");    // Z
#elif __APPLE__
  m_buttons->SetControlExpression(0, "Left Control");  // C
  m_buttons->SetControlExpression(1, "Left Shift");    // Z
#else
  m_buttons->SetControlExpression(0, "Control_L");  // C
  m_buttons->SetControlExpression(1, "Shift_L");    // Z
#endif
}
}  // namespace WiimoteEmu