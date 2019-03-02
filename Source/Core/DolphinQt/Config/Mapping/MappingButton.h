// Copyright 2017 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#pragma once

#include "Common/Flag.h"
#include "DolphinQt/QtUtils/ElidedButton.h"

class ControlReference;
class MappingWidget;
class QEvent;
class QMouseEvent;
class QTimer;

class MappingButton : public ElidedButton
{
  Q_OBJECT
public:
  MappingButton(MappingWidget* widget, ControlReference* ref, bool indicator);

  void Clear();
  void Update();
  void Detect();
  bool IsInput() const;

signals:
  void AdvancedPressed();

private:
  void mouseReleaseEvent(QMouseEvent* event) override;

  void Connect();

  MappingWidget* m_parent;
  ControlReference* m_reference;
  QTimer* m_timer;
};