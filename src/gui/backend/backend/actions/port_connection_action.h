// SPDX-FileCopyrightText: © 2020-2021, 2024 Alexandros Theodotou <alex@zrythm.org>
// SPDX-License-Identifier: LicenseRef-ZrythmLicense

#ifndef __ACTION_PORT_CONNECTION_ACTION_H__
#define __ACTION_PORT_CONNECTION_ACTION_H__

#include "dsp/port_identifier.h"
#include "gui/backend/backend/actions/undoable_action.h"
#include "gui/dsp/port_connection.h"
#include "utils/icloneable.h"

namespace zrythm::gui::actions
{

class PortConnectionAction
    : public QObject,
      public UndoableAction,
      public ICloneable<PortConnectionAction>,
      public zrythm::utils::serialization::ISerializable<PortConnectionAction>
{
  Q_OBJECT
  QML_ELEMENT
  DEFINE_UNDOABLE_ACTION_QML_PROPERTIES (PortConnectionAction)

public:
  enum class Type
  {
    Connect,
    Disconnect,
    Enable,
    Disable,
    ChangeMultiplier,
  };

  using PortType = dsp::PortType;
  using PortUuid = dsp::PortIdentifier::PortUuid;

public:
  PortConnectionAction (QObject * parent = nullptr);

  PortConnectionAction (
    Type     type,
    PortUuid src_id,
    PortUuid dest_id,
    float    new_val);

  ~PortConnectionAction () override = default;

  QString to_string () const override;

  void init_after_cloning (
    const PortConnectionAction &other,
    ObjectCloneType             clone_type) override;

  DECLARE_DEFINE_FIELDS_METHOD ();

private:
  void init_loaded_impl () override { }
  void undo_impl () override;
  void perform_impl () override;

  void do_or_undo (bool do_it);

public:
  Type type_ = Type ();

  PortConnection * connection_ = nullptr;

  /**
   * Value before/after the change.
   *
   * To be swapped on undo/redo.
   */
  float val_ = 0.f;
};

class PortConnectionConnectAction final : public PortConnectionAction
{
public:
  PortConnectionConnectAction (PortUuid src_id, const PortUuid &dest_id)
      : PortConnectionAction (Type::Connect, src_id, dest_id, 0.f)
  {
  }
};

class PortConnectionDisconnectAction final : public PortConnectionAction
{
public:
  PortConnectionDisconnectAction (PortUuid src_id, const PortUuid &dest_id)
      : PortConnectionAction (Type::Disconnect, src_id, dest_id, 0.f)
  {
  }
};

class PortConnectionEnableAction final : public PortConnectionAction
{
public:
  PortConnectionEnableAction (PortUuid src_id, const PortUuid &dest_id)
      : PortConnectionAction (Type::Enable, src_id, dest_id, 0.f)
  {
  }
};

class PortConnectionDisableAction final : public PortConnectionAction
{
public:
  PortConnectionDisableAction (PortUuid src_id, const PortUuid &dest_id)
      : PortConnectionAction (Type::Disable, src_id, dest_id, 0.f)
  {
  }
};

class PortConnectionChangeMultiplierAction final : public PortConnectionAction
{
public:
  PortConnectionChangeMultiplierAction (
    PortUuid src_id,
    PortUuid dest_id,
    float    new_multiplier)
      : PortConnectionAction (Type::ChangeMultiplier, src_id, dest_id, new_multiplier)
  {
  }
};

}; // namespace zrythm::gui::actions

#endif
