// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include "DolphinQt/Config/Mapping/MappingWindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/StringUtil.h"

#include "Core/Core.h"

#include "DolphinQt/Config/Mapping/GCKeyboardEmu.h"
#include "DolphinQt/Config/Mapping/GCMicrophone.h"
#include "DolphinQt/Config/Mapping/GCPadEmu.h"
#include "DolphinQt/Config/Mapping/Hotkey3D.h"
#include "DolphinQt/Config/Mapping/HotkeyControllerProfile.h"
#include "DolphinQt/Config/Mapping/HotkeyDebugging.h"
#include "DolphinQt/Config/Mapping/HotkeyGeneral.h"
#include "DolphinQt/Config/Mapping/HotkeyGraphics.h"
#include "DolphinQt/Config/Mapping/HotkeyStates.h"
#include "DolphinQt/Config/Mapping/HotkeyStatesOther.h"
#include "DolphinQt/Config/Mapping/HotkeyTAS.h"
#include "DolphinQt/Config/Mapping/HotkeyWii.h"
#include "DolphinQt/Config/Mapping/WiimoteEmuExtension.h"
#include "DolphinQt/Config/Mapping/WiimoteEmuGeneral.h"
#include "DolphinQt/Config/Mapping/WiimoteEmuMotionControl.h"
#include "DolphinQt/QtUtils/WrapInScrollArea.h"
#include "DolphinQt/Settings.h"

#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/ControllerInterface/Device.h"
#include "InputCommon/InputConfig.h"

constexpr const char* PROFILES_DIR = "Profiles/";

MappingWindow::MappingWindow(QWidget* parent, Type type, int port_num)
    : QDialog(parent), m_port(port_num)
{
  setWindowTitle(tr("Port %1").arg(port_num + 1));
  setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

  CreateDevicesLayout();
  CreateProfilesLayout();
  CreateResetLayout();
  CreateMainLayout();
  ConnectWidgets();
  SetMappingType(type);
}

void MappingWindow::CreateDevicesLayout()
{
  m_devices_layout = new QHBoxLayout();
  m_devices_box = new QGroupBox(tr("Device"));
  m_devices_combo = new QComboBox();
  m_devices_refresh = new QPushButton(tr("Refresh"));

  m_devices_refresh->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_devices_layout->addWidget(m_devices_combo);
  m_devices_layout->addWidget(m_devices_refresh);

  m_devices_box->setLayout(m_devices_layout);
}

void MappingWindow::CreateProfilesLayout()
{
  m_profiles_layout = new QHBoxLayout();
  m_profiles_box = new QGroupBox(tr("Profile"));
  m_profiles_combo = new QComboBox();
  m_profiles_load = new QPushButton(tr("Load"));
  m_profiles_save = new QPushButton(tr("Save"));
  m_profiles_delete = new QPushButton(tr("Delete"));

  auto* button_layout = new QHBoxLayout();

  m_profiles_box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_profiles_combo->setEditable(true);

  m_profiles_layout->addWidget(m_profiles_combo);
  button_layout->addWidget(m_profiles_load);
  button_layout->addWidget(m_profiles_save);
  button_layout->addWidget(m_profiles_delete);
  m_profiles_layout->addLayout(button_layout);

  m_profiles_box->setLayout(m_profiles_layout);
}

void MappingWindow::CreateResetLayout()
{
  m_reset_layout = new QHBoxLayout();
  m_reset_box = new QGroupBox(tr("Reset"));
  m_reset_clear = new QPushButton(tr("Clear"));
  m_reset_default = new QPushButton(tr("Default"));

  m_reset_box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  m_reset_layout->addWidget(m_reset_default);
  m_reset_layout->addWidget(m_reset_clear);

  m_reset_box->setLayout(m_reset_layout);
}

void MappingWindow::CreateMainLayout()
{
  m_main_layout = new QVBoxLayout();
  m_config_layout = new QHBoxLayout();
  m_iterative_input = new QCheckBox(tr("Iterative Input"));
  m_tab_widget = new QTabWidget();
  m_button_box = new QDialogButtonBox(QDialogButtonBox::Close);

  m_iterative_input->setToolTip(tr("Automatically progress one button after another during "
                                   "configuration. Useful for first-time setup."));

  m_config_layout->addWidget(m_devices_box);
  m_config_layout->addWidget(m_reset_box);
  m_config_layout->addWidget(m_profiles_box);

  m_main_layout->addLayout(m_config_layout);
  m_main_layout->addWidget(m_iterative_input);
  m_main_layout->addWidget(m_tab_widget);
  m_main_layout->addWidget(m_button_box);

  setLayout(m_main_layout);
}

void MappingWindow::ConnectWidgets()
{
  connect(&Settings::Instance(), &Settings::DevicesChanged, this,
          &MappingWindow::OnGlobalDevicesChanged);
  connect(m_button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_devices_refresh, &QPushButton::clicked, this, &MappingWindow::RefreshDevices);
  connect(m_devices_combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
          this, &MappingWindow::OnDeviceChanged);
  connect(m_reset_clear, &QPushButton::clicked, this, &MappingWindow::OnClearFieldsPressed);
  connect(m_reset_default, &QPushButton::clicked, this, &MappingWindow::OnDefaultFieldsPressed);
  connect(m_profiles_save, &QPushButton::clicked, this, &MappingWindow::OnSaveProfilePressed);
  connect(m_profiles_load, &QPushButton::clicked, this, &MappingWindow::OnLoadProfilePressed);
  connect(m_profiles_delete, &QPushButton::clicked, this, &MappingWindow::OnDeleteProfilePressed);
  // We currently use the "Close" button as an "Accept" button so we must save on reject.
  connect(this, &QDialog::rejected, [this] { emit Save(); });
}

void MappingWindow::OnDeleteProfilePressed()
{
  const QString profile_name = m_profiles_combo->currentText();
  const QString profile_path = m_profiles_combo->currentData().toString();

  if (!File::Exists(profile_path.toStdString()))
  {
    QMessageBox error(this);
    error.setIcon(QMessageBox::Critical);
    error.setWindowTitle(tr("Error"));
    error.setText(tr("The profile '%1' does not exist").arg(profile_name));
    error.exec();
    return;
  }

  QMessageBox confirm(this);

  confirm.setIcon(QMessageBox::Warning);
  confirm.setWindowTitle(tr("Confirm"));
  confirm.setText(tr("Are you sure that you want to delete '%1'?").arg(profile_name));
  confirm.setInformativeText(tr("This cannot be undone!"));
  confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);

  if (confirm.exec() != QMessageBox::Yes)
  {
    return;
  }

  m_profiles_combo->removeItem(m_profiles_combo->currentIndex());

  File::Delete(profile_path.toStdString());

  QMessageBox result(this);
  result.setIcon(QMessageBox::Information);
  result.setWindowTitle(tr("Success"));
  result.setText(tr("Successfully deleted '%1'.").arg(profile_name));
}

void MappingWindow::OnLoadProfilePressed()
{
  const QString profile_path = m_profiles_combo->currentData().toString();

  if (m_profiles_combo->currentIndex() == 0)
    return;

  IniFile ini;
  ini.Load(profile_path.toStdString());

  m_controller->LoadConfig(ini.GetOrCreateSection("Profile"));
  m_controller->UpdateReferences(g_controller_interface);

  emit Update();

  RefreshDevices();
}

void MappingWindow::OnSaveProfilePressed()
{
  const QString profile_name = m_profiles_combo->currentText();
  const std::string profile_path = File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR +
                                   m_config->GetProfileName() + "/" + profile_name.toStdString() +
                                   ".ini";

  if (profile_name.isEmpty())
    return;

  File::CreateFullPath(profile_path);

  IniFile ini;

  m_controller->SaveConfig(ini.GetOrCreateSection("Profile"));
  ini.Save(profile_path);

  if (m_profiles_combo->currentIndex() == 0 || m_profiles_combo->findText(profile_name) == -1)
  {
    m_profiles_combo->addItem(profile_name, QString::fromStdString(profile_path));
    m_profiles_combo->setCurrentIndex(m_profiles_combo->count() - 1);
  }
}

void MappingWindow::OnDeviceChanged(int index)
{
  if (IsMappingAllDevices())
    return;

  const auto device = m_devices_combo->currentText().toStdString();
  m_controller->SetDefaultDevice(device);
}

bool MappingWindow::IsMappingAllDevices() const
{
  return m_devices_combo->currentIndex() == m_devices_combo->count() - 1;
}

void MappingWindow::RefreshDevices()
{
  Core::RunAsCPUThread([&] { g_controller_interface.RefreshDevices(); });
}

void MappingWindow::OnGlobalDevicesChanged()
{
  m_devices_combo->clear();

  Core::RunAsCPUThread([&] {
    m_controller->UpdateReferences(g_controller_interface);

    const auto default_device = m_controller->GetDefaultDevice().ToString();

    if (!default_device.empty())
      m_devices_combo->addItem(QString::fromStdString(default_device));

    for (const auto& name : g_controller_interface.GetAllDeviceStrings())
    {
      if (name != default_device)
        m_devices_combo->addItem(QString::fromStdString(name));
    }

    m_devices_combo->addItem(tr("All devices"));

    m_devices_combo->setCurrentIndex(0);
  });
}

void MappingWindow::SetMappingType(MappingWindow::Type type)
{
  MappingWidget* widget;

  switch (type)
  {
  case Type::MAPPING_GC_KEYBOARD:
    widget = new GCKeyboardEmu(this);
    AddWidget(tr("GameCube Keyboard"), widget);
    setWindowTitle(tr("GameCube Keyboard at Port %1").arg(GetPort() + 1));
    break;
  case Type::MAPPING_GC_BONGOS:
  case Type::MAPPING_GC_STEERINGWHEEL:
  case Type::MAPPING_GC_DANCEMAT:
  case Type::MAPPING_GCPAD:
    widget = new GCPadEmu(this);
    setWindowTitle(tr("GameCube Controller at Port %1").arg(GetPort() + 1));
    AddWidget(tr("GameCube Controller"), widget);
    break;
  case Type::MAPPING_GC_MICROPHONE:
    widget = new GCMicrophone(this);
    setWindowTitle(tr("GameCube Microphone Slot %1")
                       .arg(GetPort() == 0 ? QStringLiteral("A") : QStringLiteral("B")));
    AddWidget(tr("Microphone"), widget);
    break;
  case Type::MAPPING_WIIMOTE_EMU:
  {
    auto* extension = new WiimoteEmuExtension(this);
    widget = new WiimoteEmuGeneral(this, extension);
    setWindowTitle(tr("Wii Remote %1").arg(GetPort() + 1));
    AddWidget(tr("General and Options"), widget);
    // i18n: IR stands for infrared and refers to the pointer functionality of Wii Remotes
    AddWidget(tr("Motion Controls and IR"), new WiimoteEmuMotionControl(this));
    AddWidget(tr("Extension"), extension);
    break;
  }
  case Type::MAPPING_HOTKEYS:
  {
    widget = new HotkeyGeneral(this);
    AddWidget(tr("General"), widget);
    // i18n: TAS is short for tool-assisted speedrun. Read http://tasvideos.org/ for details.
    // Frame advance is an example of a typical TAS tool.
    AddWidget(tr("TAS Tools"), new HotkeyTAS(this));

    AddWidget(tr("Debugging"), new HotkeyDebugging(this));

    AddWidget(tr("Wii and Wii Remote"), new HotkeyWii(this));
    AddWidget(tr("Controller Profile"), new HotkeyControllerProfile(this));
    AddWidget(tr("Graphics"), new HotkeyGraphics(this));
    // i18n: Stereoscopic 3D
    AddWidget(tr("3D"), new Hotkey3D(this));
    AddWidget(tr("Save and Load State"), new HotkeyStates(this));
    AddWidget(tr("Other State Management"), new HotkeyStatesOther(this));
    setWindowTitle(tr("Hotkey Settings"));
    break;
  }
  default:
    return;
  }

  widget->LoadSettings();

  m_config = widget->GetConfig();

  m_controller = m_config->GetController(GetPort());
  m_profiles_combo->addItem(QStringLiteral(""));

  const std::string profiles_path =
      File::GetUserPath(D_CONFIG_IDX) + PROFILES_DIR + m_config->GetProfileName();
  for (const auto& filename : Common::DoFileSearch({profiles_path}, {".ini"}))
  {
    std::string basename;
    SplitPath(filename, nullptr, &basename, nullptr);
    m_profiles_combo->addItem(QString::fromStdString(basename), QString::fromStdString(filename));
  }

  RefreshDevices();
}

void MappingWindow::AddWidget(const QString& name, QWidget* widget)
{
  m_tab_widget->addTab(GetWrappedWidget(widget, this, 150, 205), name);
}

int MappingWindow::GetPort() const
{
  return m_port;
}

ControllerEmu::EmulatedController* MappingWindow::GetController() const
{
  return m_controller;
}

std::shared_ptr<ciface::Core::Device> MappingWindow::GetDevice() const
{
  return g_controller_interface.FindDevice(GetController()->GetDefaultDevice());
}

void MappingWindow::OnDefaultFieldsPressed()
{
  m_controller->LoadDefaults(g_controller_interface);
  m_controller->UpdateReferences(g_controller_interface);
  emit Update();
  emit Save();
}

void MappingWindow::OnClearFieldsPressed()
{
  // Loading an empty inifile section clears everything.
  IniFile::Section sec;
  m_controller->LoadConfig(&sec);
  m_controller->UpdateReferences(g_controller_interface);
  emit Update();
  emit Save();
}

bool MappingWindow::IsIterativeInput() const
{
  return m_iterative_input->isChecked();
}