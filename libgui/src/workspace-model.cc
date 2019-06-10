/*

Copyright (C) 2013-2019 John W. Eaton
Copyright (C) 2011-2019 Jacob Dawid

This file is part of Octave.

Octave is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Octave is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Octave; see the file COPYING.  If not, see
<https://www.gnu.org/licenses/>.

*/

#if defined (HAVE_CONFIG_H)
#  include "config.h"
#endif

#include <iostream>

#include <QTreeWidget>
#include <QSettings>

#include "syminfo.h"
#include "utils.h"

#include "resource-manager.h"
#include "gui-preferences.h"
#include "workspace-model.h"

namespace octave
{
  workspace_model::workspace_model (QObject *p)
    : QAbstractTableModel (p)
  {
    m_columnNames.append (tr ("Name"));
    m_columnNames.append (tr ("Class"));
    m_columnNames.append (tr ("Dimension"));
    m_columnNames.append (tr ("Value"));
    m_columnNames.append (tr ("Attribute"));

    // Initialize the bachground and foreground colors of special
    // classes in the workspace view. The structure is
    // m_storage_class_colors(1,2,...,colors):        background colors
    // m_storage_class_colors(colors+1,...,2*colors): foreground colors
    int colors = resource_manager::storage_class_chars ().length ();
    for (int i = 0; i < 2*colors; i++)
      m_storage_class_colors.append (QColor (Qt::white));

  }

  QList<QColor>
  workspace_model::storage_class_default_colors (void)
  {
    QList<QColor> colors;

    if (colors.isEmpty ())
      {
        colors << QColor (190, 255, 255)
               << QColor (255, 255, 190)
               << QColor (255, 190, 255);
      }

    return colors;
  }

  QStringList
  workspace_model::storage_class_names (void)
  {
    QStringList names;

    if (names.isEmpty ())
      {
        names << QObject::tr ("argument")
              << QObject::tr ("global")
              << QObject::tr ("persistent");
      }

    return names;
  }

  int
  workspace_model::rowCount (const QModelIndex&) const
  {
    return m_symbols.size ();
  }

  int
  workspace_model::columnCount (const QModelIndex&) const
  {
    return m_columnNames.size ();
  }

  Qt::ItemFlags
  workspace_model::flags (const QModelIndex& idx) const
  {
    Qt::ItemFlags retval = Qt::NoItemFlags;

    if (idx.isValid ())
      {
        retval |= Qt::ItemIsEnabled;

        if (m_top_level && idx.column () == 0)
          retval |= Qt::ItemIsSelectable;
      }

    return retval;
  }

  QVariant
  workspace_model::headerData (int section, Qt::Orientation orientation,
                               int role) const
  {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole)
      return m_columnNames[section];
    else
      return QVariant ();
  }

  QVariant
  workspace_model::data (const QModelIndex& idx, int role) const
  {
    QVariant retval;

    if (idx.isValid ())
      {
        if ((role == Qt::BackgroundColorRole || role == Qt::ForegroundRole)
            && m_enable_colors)
          {
            QString class_chars = resource_manager::storage_class_chars ();
            int actual_class
              = class_chars.indexOf (m_scopes[idx.row ()].toLatin1 ());
            if (actual_class >= 0)
              {
                // Valid class: Get backgorund (normal indexes) or foreground
                // color (indexes with offset)
                if (role == Qt::ForegroundRole)
                  actual_class += class_chars.length ();

                return QVariant (m_storage_class_colors.at (actual_class));
              }
            else
              return retval;
          }

        if (role == Qt::DisplayRole
            || (idx.column () == 0 && role == Qt::EditRole)
            || (idx.column () == 0 && role == Qt::ToolTipRole))
          {
            switch (idx.column ())
              {
              case 0:
                if (role == Qt::ToolTipRole)
                  retval
                    = QVariant (tr ("Right click to copy, rename, or display"));
                else
                  retval = QVariant (m_symbols[idx.row ()]);
                break;

              case 1:
                retval = QVariant (m_class_names[idx.row ()]);
                break;

              case 2:
                retval = QVariant (m_dimensions[idx.row ()]);
                break;

              case 3:
                retval = QVariant (m_values[idx.row ()]);
                break;

              case 4:
                {
                  QString sclass;

                  QString class_chars = resource_manager::storage_class_chars ();

                  int actual_class
                    = class_chars.indexOf (m_scopes[idx.row ()].toLatin1 ());

                  if (actual_class >= 0)
                    {
                      QStringList class_names
                        = resource_manager::storage_class_names ();

                      sclass = class_names.at (actual_class);
                    }

                  if (m_complex_flags[idx.row ()])
                    {
                      if (sclass.isEmpty ())
                        sclass = tr ("complex");
                      else
                        sclass += ", " + tr ("complex");
                    }

                  retval = QVariant (sclass);
                }
                break;
              }
          }
      }

    return retval;
  }

  bool
  workspace_model::setData (const QModelIndex& idx, const QVariant& value,
                            int role)
  {
    bool retval = false;

    if (idx.column () == 0 && role == Qt::EditRole)
      {
        QString qold_name = m_symbols[idx.row ()];

        QString qnew_name = value.toString ();

        std::string new_name = qnew_name.toStdString ();

        if (valid_identifier (new_name))
          {
            emit rename_variable (qold_name, qnew_name);

            retval = true;
          }
      }

    return retval;
  }

  void
  workspace_model::set_workspace (bool top_level, bool /* debug */,
                                  const symbol_info_list& syminfo)
  {
    clear_data ();

    m_top_level = top_level;
    m_syminfo_list = syminfo;

    update_table ();
  }

  void
  workspace_model::clear_workspace (void)
  {
    clear_data ();
    update_table ();
  }

  void
  workspace_model::notice_settings (const QSettings *settings)
  {
    QList<QColor> default_colors =
      resource_manager::storage_class_default_colors ();
    QString class_chars = resource_manager::storage_class_chars ();

    m_enable_colors =
        settings->value (ws_enable_colors.key, ws_enable_colors.key).toBool ();

    for (int i = 0; i < class_chars.length (); i++)
      {
        QVariant default_var = default_colors.at (i);
        QColor setting_color = settings->value ("workspaceview/color_"
                                                + class_chars.mid (i,1),
                                                default_var).value<QColor> ();

        QPalette p (setting_color);
        m_storage_class_colors.replace (i,setting_color);

        QColor fg_color = p.color (QPalette::WindowText);
        m_storage_class_colors.replace (i + class_chars.length (), fg_color);

      }
  }

  void
  workspace_model::clear_data (void)
  {
    m_top_level = false;
    m_syminfo_list = symbol_info_list ();
    m_scopes = QString ();
    m_symbols = QStringList ();
    m_class_names = QStringList ();
    m_dimensions = QStringList ();
    m_values = QStringList ();
    m_complex_flags = QIntList ();
  }

  void
  workspace_model::update_table (void)
  {
    beginResetModel ();

    for (const auto& syminfo : m_syminfo_list)
      {
        std::string nm = syminfo.name ();

        octave_value val = syminfo.value ();

        // FIXME: fix size for objects, see kluge in ov.cc
        Matrix sz = val.size ();
        dim_vector dv = dim_vector::alloc (sz.numel ());
        for (octave_idx_type i = 0; i < dv.ndims (); i++)
          dv(i) = sz(i);

        char storage = ' ';
        if (syminfo.is_formal ())
          storage = 'a';
        else if (syminfo.is_global ())
          storage = 'g';
        else if (syminfo.is_persistent ())
          storage = 'p';

        std::ostringstream buf;
        val.short_disp (buf);
        std::string short_disp_str = buf.str ();

        m_scopes.append (storage);
        m_symbols.append (QString::fromStdString (nm));
        m_class_names.append (QString::fromStdString (val.class_name ()));
        m_dimensions.append (QString::fromStdString (dv.str ()));
        m_values.append (QString::fromStdString (short_disp_str));
        m_complex_flags.append (val.iscomplex ());
      }

    endResetModel ();

    emit model_changed ();
  }
}
