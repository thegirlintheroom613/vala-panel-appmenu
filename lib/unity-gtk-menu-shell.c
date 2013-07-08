/*
 * Copyright 2012 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Ryan Lortie <desrt@desrt.ca>
 *          William Hua <william.hua@canonical.com>
 */

/**
 * SECTION:unity-gtk-menu-shell
 * @short_description: Menu shell proxy
 * @include: unity-gtk-parser.h
 *
 * A #UnityGtkMenuShell is a #GMenuModel that acts as a proxy for a
 * #GtkMenuShell. This can be used for purposes such as exporting menu
 * shells over DBus with g_dbus_connection_export_menu_model ().
 *
 * #UnityGtkMenuShell<!-- -->s are most useful when used with
 * #UnityGtkActionGroup<!-- -->s.
 */

#include "unity-gtk-menu-shell-private.h"
#include "unity-gtk-menu-section-private.h"
#include "unity-gtk-action-group-private.h"

G_DEFINE_QUARK (menu_shell, menu_shell);

G_DEFINE_TYPE (UnityGtkMenuShell,
               unity_gtk_menu_shell,
               G_TYPE_MENU_MODEL);

static gboolean unity_gtk_menu_shell_debug;

static gint
g_uintcmp (gconstpointer a,
           gconstpointer b,
           gpointer      user_data)
{
  return GPOINTER_TO_INT (a) - GPOINTER_TO_INT (b);
}

static guint
g_sequence_get_uint (GSequenceIter *iter)
{
  return GPOINTER_TO_UINT (g_sequence_get (iter));
}

static void
g_sequence_set_uint (GSequenceIter *iter,
                     guint          i)
{
  g_sequence_set (iter, GUINT_TO_POINTER (i));
}

static GSequenceIter *
g_sequence_insert_sorted_uint (GSequence *sequence,
                               guint      i)
{
  return g_sequence_insert_sorted (sequence, GUINT_TO_POINTER (i), g_uintcmp, NULL);
}

static GSequenceIter *
g_sequence_lookup_uint (GSequence *sequence,
                        guint      i)
{
  return g_sequence_lookup (sequence, GUINT_TO_POINTER (i), g_uintcmp, NULL);
}

static GSequenceIter *
g_sequence_search_uint (GSequence *sequence,
                        guint      i)
{
  return g_sequence_search (sequence, GUINT_TO_POINTER (i), g_uintcmp, NULL);
}

static GSequenceIter *
g_sequence_search_inf_uint (GSequence *sequence,
                            guint      i)
{
  GSequenceIter *iter = g_sequence_iter_prev (g_sequence_search_uint (sequence, i));

  return !g_sequence_iter_is_end (iter) && g_sequence_get_uint (iter) <= i ? iter : NULL;
}

static void
g_ptr_array_insert (GPtrArray *ptr_array,
                    gpointer   data,
                    gint       i)
{
  guint j;

  g_return_if_fail (ptr_array != NULL);
  g_warn_if_fail (-1 <= i && i <= ptr_array->len);

  if (i < 0 || i > ptr_array->len)
    i = ptr_array->len;

  g_ptr_array_add (ptr_array, NULL);

  for (j = ptr_array->len - 1; j > i; j--)
    ptr_array->pdata[j] = g_ptr_array_index (ptr_array, j - 1);

  ptr_array->pdata[j] = data;
}

static GPtrArray *
unity_gtk_menu_shell_get_items (UnityGtkMenuShell *shell)
{
  g_return_val_if_fail (UNITY_GTK_IS_MENU_SHELL (shell), NULL);

  if (shell->items == NULL)
    {
      GList *children;
      GList *iter;
      guint i;

      g_return_val_if_fail (shell->menu_shell != NULL, NULL);

      shell->items = g_ptr_array_new_with_free_func (g_object_unref);
      children = gtk_container_get_children (GTK_CONTAINER (shell->menu_shell));

      for (iter = children, i = 0; iter != NULL; i++)
        {
          g_ptr_array_add (shell->items, unity_gtk_menu_item_new (iter->data, shell, i));
          iter = g_list_next (iter);
        }

      g_list_free (children);
    }

  return shell->items;
}

static GPtrArray *
unity_gtk_menu_shell_get_sections (UnityGtkMenuShell *shell)
{
  g_return_val_if_fail (UNITY_GTK_IS_MENU_SHELL (shell), NULL);

  if (shell->sections == NULL)
    {
      GSequence *separator_indices = unity_gtk_menu_shell_get_separator_indices (shell);
      guint n = g_sequence_get_length (separator_indices);
      guint i;

      shell->sections = g_ptr_array_new_full (n + 1, g_object_unref);

      for (i = 0; i <= n; i++)
        g_ptr_array_add (shell->sections, unity_gtk_menu_section_new (shell, i));
    }

  return shell->sections;
}

static void
unity_gtk_menu_shell_show_item (UnityGtkMenuShell *shell,
                                UnityGtkMenuItem  *item)
{
  GSequence *visible_indices;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));
  g_warn_if_fail (item->parent_shell == shell);

  visible_indices = shell->visible_indices;

  if (visible_indices != NULL)
    {
      GSequence *separator_indices = shell->separator_indices;
      guint item_index = item->item_index;
      GSequenceIter *insert_iter = g_sequence_lookup_uint (visible_indices, item_index);
      gboolean already_visible = insert_iter != NULL;

      if (!already_visible)
        insert_iter = g_sequence_insert_sorted_uint (visible_indices, item_index);
      else
        g_warn_if_reached ();

      if (shell->action_group != NULL)
        {
          unity_gtk_action_group_connect_item (shell->action_group, item);

          if (item->child_shell != NULL)
            {
              if (item->child_shell_valid)
                unity_gtk_action_group_connect_shell (shell->action_group, item->child_shell);
              else
                g_warn_if_reached ();
            }
        }

      if (separator_indices != NULL)
        {
          GPtrArray *sections = shell->sections;
          GSequenceIter *separator_iter = g_sequence_search_inf_uint (separator_indices, item_index);
          guint section_index = separator_iter == NULL ? 0 : g_sequence_iter_get_position (separator_iter) + 1;
          gboolean separator_already_visible = separator_iter != NULL && g_sequence_get_uint (separator_iter) == item_index;

          if (!separator_already_visible)
            {
              if (unity_gtk_menu_item_is_separator (item))
                {
                  g_sequence_insert_sorted_uint (separator_indices, item_index);

                  if (sections != NULL)
                    {
                      UnityGtkMenuSection *section = g_ptr_array_index (sections, section_index);
                      GSequenceIter *section_iter = unity_gtk_menu_section_get_begin_iter (section);
                      guint position = g_sequence_iter_get_position (insert_iter) - g_sequence_iter_get_position (section_iter);
                      UnityGtkMenuSection *new_section = unity_gtk_menu_section_new (shell, section_index + 1);
                      guint removed = g_menu_model_get_n_items (G_MENU_MODEL (new_section));
                      guint i;

                      g_ptr_array_insert (sections, new_section, section_index + 1);

                      for (i = section_index + 2; i < sections->len; i++)
                        UNITY_GTK_MENU_SECTION (g_ptr_array_index (sections, i))->section_index = i;

                      if (removed)
                        g_menu_model_items_changed (G_MENU_MODEL (section), position, removed, 0);

                      g_menu_model_items_changed (G_MENU_MODEL (shell), section_index + 1, 0, 1);
                    }
                }
              else
                {
                  if (sections != NULL)
                    {
                      UnityGtkMenuSection *section = g_ptr_array_index (sections, section_index);
                      GSequenceIter *section_iter = unity_gtk_menu_section_get_begin_iter (section);
                      guint position = g_sequence_iter_get_position (insert_iter) - g_sequence_iter_get_position (section_iter);

                      g_menu_model_items_changed (G_MENU_MODEL (section), position, 0, 1);
                    }
                }
            }
          else
            g_warn_if_reached ();
        }
    }
}

static void
unity_gtk_menu_shell_hide_item (UnityGtkMenuShell *shell,
                                UnityGtkMenuItem  *item)
{
  GSequence *visible_indices;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));
  g_warn_if_fail (item->parent_shell == shell);

  visible_indices = shell->visible_indices;

  if (visible_indices != NULL)
    {
      GSequence *separator_indices = shell->separator_indices;
      guint item_index = item->item_index;
      GSequenceIter *visible_iter = g_sequence_lookup_uint (visible_indices, item_index);

      if (shell->action_group != NULL)
        {
          if (item->child_shell != NULL)
            {
              if (item->child_shell_valid)
                unity_gtk_action_group_disconnect_shell (shell->action_group, item->child_shell);
              else
                g_warn_if_reached ();
            }

          unity_gtk_action_group_disconnect_item (shell->action_group, item);
        }

      if (separator_indices != NULL)
        {
          if (unity_gtk_menu_item_is_separator (item))
            {
              GSequenceIter *separator_iter = g_sequence_lookup_uint (separator_indices, item_index);

              if (separator_iter != NULL)
                {
                  GPtrArray *sections = shell->sections;
                  guint section_index = g_sequence_iter_get_position (separator_iter);

                  if (shell->sections != NULL)
                    {
                      UnityGtkMenuSection *section = g_ptr_array_index (sections, section_index);
                      UnityGtkMenuSection *next_section = g_ptr_array_index (sections, section_index + 1);
                      guint position = g_menu_model_get_n_items (G_MENU_MODEL (section));
                      guint added = g_menu_model_get_n_items (G_MENU_MODEL (next_section));
                      guint i;

                      g_sequence_remove (separator_iter);

                      if (visible_iter != NULL)
                        g_sequence_remove (visible_iter);
                      else
                        g_warn_if_reached ();

                      g_menu_model_items_changed (G_MENU_MODEL (shell), section_index + 1, 1, 0);

                      if (added)
                        g_menu_model_items_changed (G_MENU_MODEL (section), position, 0, added);

                      g_ptr_array_remove_index (sections, section_index + 1);

                      for (i = section_index + 1; i < sections->len; i++)
                        UNITY_GTK_MENU_SECTION (g_ptr_array_index (sections, i))->section_index = i;
                    }
                  else
                    {
                      g_sequence_remove (separator_iter);

                      if (visible_iter != NULL)
                        g_sequence_remove (visible_iter);
                      else
                        g_warn_if_reached ();
                    }
                }
              else
                {
                  g_warn_if_reached ();

                  if (visible_iter != NULL)
                    g_sequence_remove (visible_iter);
                  else
                    g_warn_if_reached ();
                }
            }
          else
            {
              if (visible_iter != NULL)
                {
                  GPtrArray *sections = shell->sections;
                  GSequenceIter *separator_iter = g_sequence_search_inf_uint (separator_indices, item_index);
                  guint section_index = separator_iter == NULL ? 0 : g_sequence_iter_get_position (separator_iter) + 1;

                  if (shell->sections != NULL)
                    {
                      UnityGtkMenuSection *section = g_ptr_array_index (sections, section_index);
                      GSequenceIter *section_iter = unity_gtk_menu_section_get_begin_iter (section);
                      guint position = g_sequence_iter_get_position (visible_iter) - g_sequence_iter_get_position (section_iter);

                      g_sequence_remove (visible_iter);
                      g_menu_model_items_changed (G_MENU_MODEL (section), position, 1, 0);
                    }
                }
              else
                g_warn_if_reached ();
            }
        }
      else
        {
          if (visible_iter != NULL)
            g_sequence_remove (visible_iter);
          else
            g_warn_if_reached ();
        }
    }
}

static void
unity_gtk_menu_shell_update_item (UnityGtkMenuShell *shell,
                                  UnityGtkMenuItem  *item)
{
  GSequence *visible_indices;
  GSequenceIter *visible_iter;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));
  g_warn_if_fail (item->parent_shell == shell);

  visible_indices = unity_gtk_menu_shell_get_visible_indices (shell);
  visible_iter = g_sequence_lookup_uint (visible_indices, item->item_index);

  if (visible_iter != NULL)
    {
      GSequence *separator_indices;
      GSequenceIter *separator_iter;
      guint section_index;
      GPtrArray *sections;
      UnityGtkMenuSection *section;
      GSequenceIter *section_iter;
      guint position;

      separator_indices = unity_gtk_menu_shell_get_separator_indices (shell);
      separator_iter = g_sequence_search_inf_uint (separator_indices, item->item_index);
      section_index = separator_iter == NULL ? 0 : g_sequence_iter_get_position (separator_iter) + 1;
      sections = unity_gtk_menu_shell_get_sections (shell);
      section = g_ptr_array_index (sections, section_index);
      section_iter = unity_gtk_menu_section_get_begin_iter (section);
      position = g_sequence_iter_get_position (visible_iter) - g_sequence_iter_get_position (section_iter);

      g_menu_model_items_changed (G_MENU_MODEL (section), position, 1, 1);
    }
}

static void
unity_gtk_menu_shell_handle_item_visible (UnityGtkMenuShell *shell,
                                          UnityGtkMenuItem  *item)
{
  GSequence *visible_indices;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));
  g_warn_if_fail (item->parent_shell == shell);

  visible_indices = shell->visible_indices;

  if (visible_indices != NULL)
    {
      GSequenceIter *visible_iter = g_sequence_lookup_uint (visible_indices, item->item_index);
      gboolean was_visible = visible_iter != NULL;
      gboolean is_visible = unity_gtk_menu_item_is_visible (item);

      if (!was_visible && is_visible)
        unity_gtk_menu_shell_show_item (shell, item);
      else if (was_visible && !is_visible)
        unity_gtk_menu_shell_hide_item (shell, item);
    }
}

static void
unity_gtk_menu_shell_handle_item_sensitive (UnityGtkMenuShell *shell,
                                            UnityGtkMenuItem  *item)
{
  GActionGroup *action_group;
  UnityGtkAction *action;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));
  g_warn_if_fail (item->parent_shell == shell);

  action_group = G_ACTION_GROUP (shell->action_group);
  action = item->action;

  if (action_group != NULL && action != NULL)
    {
      gboolean enabled = unity_gtk_menu_item_is_sensitive (item);

      g_action_group_action_enabled_changed (action_group, action->name, enabled);
    }
}

static void
unity_gtk_menu_shell_handle_item_label (UnityGtkMenuShell *shell,
                                        UnityGtkMenuItem  *item)
{
  unity_gtk_menu_shell_update_item (shell, item);
}

static void
unity_gtk_menu_shell_handle_item_accel_path (UnityGtkMenuShell *shell,
                                             UnityGtkMenuItem  *item)
{
  unity_gtk_menu_shell_update_item (shell, item);
}

static void
unity_gtk_menu_shell_handle_item_active (UnityGtkMenuShell *shell,
                                         UnityGtkMenuItem  *item)
{
  GActionGroup *action_group;
  UnityGtkAction *action;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));
  g_warn_if_fail (item->parent_shell == shell);

  action_group = G_ACTION_GROUP (shell->action_group);
  action = item->action;

  if (action_group != NULL && action != NULL)
    {
      if (action->items_by_name != NULL)
        {
          const gchar *name = NULL;
          GHashTableIter iter;
          gpointer key;
          gpointer value;

          g_hash_table_iter_init (&iter, action->items_by_name);
          while (name == NULL && g_hash_table_iter_next (&iter, &key, &value))
            if (unity_gtk_menu_item_is_active (value))
              name = key;

          if (name != NULL)
            {
              GVariant *state = g_variant_new_string (name);

              g_action_group_action_state_changed (action_group, action->name, state);
            }
          else
            g_action_group_action_state_changed (action_group, action->name, NULL);
        }
      else if (unity_gtk_menu_item_is_check (item))
        {
          gboolean active = unity_gtk_menu_item_is_active (item);
          GVariant *state = g_variant_new_boolean (active);

          g_action_group_action_state_changed (action_group, action->name, state);
        }
    }
}

static void
unity_gtk_menu_shell_handle_item_parent (UnityGtkMenuShell *shell,
                                         UnityGtkMenuItem  *item)
{
  GtkMenuItem *menu_item;
  GtkWidget *parent;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));
  g_warn_if_fail (item->parent_shell == shell);

  menu_item = item->menu_item;
  parent = gtk_widget_get_parent (GTK_WIDGET (menu_item));

  if (parent == NULL)
    {
      GPtrArray *items = shell->items;

      if (unity_gtk_menu_item_is_visible (item))
        unity_gtk_menu_shell_hide_item (shell, item);

      if (items != NULL)
        {
          GSequence *visible_indices = shell->visible_indices;
          GSequence *separator_indices = shell->separator_indices;
          guint item_index = item->item_index;
          guint i;

          g_ptr_array_remove_index (items, item_index);

          for (i = item_index; i < items->len; i++)
            UNITY_GTK_MENU_ITEM (g_ptr_array_index (items, i))->item_index = i;

          if (visible_indices != NULL)
            {
              GSequenceIter *iter = g_sequence_search_uint (visible_indices, item_index);

              while (!g_sequence_iter_is_end (iter))
                {
                  g_sequence_set_uint (iter, g_sequence_get_uint (iter) - 1);
                  iter = g_sequence_iter_next (iter);
                }
            }

          if (separator_indices != NULL)
            {
              GSequenceIter *iter = g_sequence_search_uint (separator_indices, item_index);

              while (!g_sequence_iter_is_end (iter))
                {
                  g_sequence_set_uint (iter, g_sequence_get_uint (iter) - 1);
                  iter = g_sequence_iter_next (iter);
                }
            }
        }
    }
}

static void
unity_gtk_menu_shell_handle_item_submenu (UnityGtkMenuShell *shell,
                                          UnityGtkMenuItem  *item)
{
  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));
  g_warn_if_fail (item->parent_shell == shell);

  if (item->child_shell_valid)
    {
      GtkMenuShell *old_submenu = item->child_shell != NULL ? item->child_shell->menu_shell : NULL;
      GtkMenuShell *new_submenu = item->menu_item != NULL ? GTK_MENU_SHELL (gtk_menu_item_get_submenu (item->menu_item)) : NULL;

      if (new_submenu != old_submenu)
        {
          UnityGtkMenuShell *child_shell = item->child_shell;
          GSequence *visible_indices = unity_gtk_menu_shell_get_visible_indices (shell);
          GSequence *separator_indices = unity_gtk_menu_shell_get_separator_indices (shell);
          GSequenceIter *separator_iter = g_sequence_search_inf_uint (separator_indices, item->item_index);
          guint section_index = separator_iter == NULL ? 0 : g_sequence_iter_get_position (separator_iter) + 1;
          GPtrArray *sections = unity_gtk_menu_shell_get_sections (shell);
          UnityGtkMenuSection *section = g_ptr_array_index (sections, section_index);
          GSequenceIter *section_iter = unity_gtk_menu_section_get_begin_iter (section);
          GSequenceIter *visible_iter = g_sequence_lookup_uint (visible_indices, item->item_index);
          guint position = g_sequence_iter_get_position (visible_iter) - g_sequence_iter_get_position (section_iter);

          if (child_shell != NULL)
            {
              item->child_shell = NULL;
              g_object_unref (child_shell);
            }

          item->child_shell_valid = FALSE;

          g_menu_model_items_changed (G_MENU_MODEL (section), position, 1, 1);
        }
    }
}

static void
unity_gtk_menu_shell_handle_shell_insert (GtkMenuShell *menu_shell,
                                          GtkWidget    *child,
                                          gint          position,
                                          gpointer      user_data)
{
  UnityGtkMenuShell *shell;
  GPtrArray *items;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (user_data));

  if (unity_gtk_menu_shell_is_debug ())
    g_print ("%s ((%s *) %p, (%s *) %p \"%s\", %d, (%s *) %p)\n", G_STRFUNC, G_OBJECT_TYPE_NAME (menu_shell), menu_shell, G_OBJECT_TYPE_NAME (child), child, gtk_menu_item_get_label (GTK_MENU_ITEM (child)), position, G_OBJECT_TYPE_NAME (user_data), user_data);

  shell = UNITY_GTK_MENU_SHELL (user_data);
  items = shell->items;

  if (items != NULL)
    {
      UnityGtkMenuItem *item;
      GtkMenuItem *menu_item;
      GSequence *visible_indices;
      GSequence *separator_indices;
      guint i;

      if (position < 0)
        position = items->len;

      menu_item = GTK_MENU_ITEM (child);
      item = unity_gtk_menu_item_new (menu_item, shell, position);
      g_ptr_array_insert (items, item, position);

      for (i = position + 1; i < items->len; i++)
        UNITY_GTK_MENU_ITEM (g_ptr_array_index (items, i))->item_index = i;

      visible_indices = shell->visible_indices;
      separator_indices = shell->separator_indices;

      if (visible_indices != NULL)
        {
          GSequenceIter *iter = g_sequence_search_uint (visible_indices, position - 1);

          while (!g_sequence_iter_is_end (iter))
            {
              g_sequence_set_uint (iter, g_sequence_get_uint (iter) + 1);
              iter = g_sequence_iter_next (iter);
            }
        }

      if (separator_indices != NULL)
        {
          GSequenceIter *iter = g_sequence_search_uint (separator_indices, position - 1);

          while (!g_sequence_iter_is_end (iter))
            {
              g_sequence_set_uint (iter, g_sequence_get_uint (iter) + 1);
              iter = g_sequence_iter_next (iter);
            }
        }

      if (unity_gtk_menu_item_is_visible (item))
        unity_gtk_menu_shell_show_item (shell, item);
    }
}

static void unity_gtk_menu_shell_clear_menu_shell (UnityGtkMenuShell *shell);

static void
unity_gtk_menu_shell_set_menu_shell (UnityGtkMenuShell *shell,
                                     GtkMenuShell      *menu_shell)
{
  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));

  if (menu_shell != shell->menu_shell)
    {
      GPtrArray *items = shell->items;
      GPtrArray *sections = shell->sections;
      GSequence *visible_indices = shell->visible_indices;
      GSequence *separator_indices = shell->separator_indices;

      if (shell->action_group != NULL)
        unity_gtk_action_group_disconnect_shell (shell->action_group, shell);

      if (shell->menu_shell_insert_handler_id)
        {
          g_assert (shell->menu_shell != NULL);
          g_signal_handler_disconnect (shell->menu_shell, shell->menu_shell_insert_handler_id);
          shell->menu_shell_insert_handler_id = 0;
        }

      if (separator_indices != NULL)
        {
          shell->separator_indices = NULL;
          g_sequence_free (separator_indices);
        }

      if (visible_indices != NULL)
        {
          shell->visible_indices = NULL;
          g_sequence_free (visible_indices);
        }

      if (sections != NULL)
        {
          shell->sections = NULL;
          g_ptr_array_unref (sections);
        }

      if (items != NULL)
        {
          shell->items = NULL;
          g_ptr_array_unref (items);
        }

      if (shell->menu_shell != NULL)
        g_object_steal_qdata (G_OBJECT (shell->menu_shell), menu_shell_quark ());

      shell->menu_shell = menu_shell;

      if (menu_shell != NULL)
        {
          g_object_set_qdata_full (G_OBJECT (menu_shell), menu_shell_quark (), shell, (GDestroyNotify) unity_gtk_menu_shell_clear_menu_shell);

          shell->menu_shell_insert_handler_id = g_signal_connect (menu_shell, "insert", G_CALLBACK (unity_gtk_menu_shell_handle_shell_insert), shell);
        }
    }
}

static void
unity_gtk_menu_shell_clear_menu_shell (UnityGtkMenuShell *shell)
{
  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));

  shell->menu_shell_insert_handler_id = 0;

  unity_gtk_menu_shell_set_menu_shell (shell, NULL);
}

static void
unity_gtk_menu_shell_dispose (GObject *object)
{
  UnityGtkMenuShell *shell;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (object));

  shell = UNITY_GTK_MENU_SHELL (object);

  unity_gtk_menu_shell_set_menu_shell (shell, NULL);

  G_OBJECT_CLASS (unity_gtk_menu_shell_parent_class)->dispose (object);
}

static gboolean
unity_gtk_menu_shell_is_mutable (GMenuModel *model)
{
  g_return_val_if_fail (UNITY_GTK_IS_MENU_SHELL (model), TRUE);

  return TRUE;
}

static gint
unity_gtk_menu_shell_get_n_items (GMenuModel *model)
{
  g_return_val_if_fail (UNITY_GTK_IS_MENU_SHELL (model), 0);

  return unity_gtk_menu_shell_get_sections (UNITY_GTK_MENU_SHELL (model))->len;
}

static void
unity_gtk_menu_shell_get_item_attributes (GMenuModel  *model,
                                          gint         item_index,
                                          GHashTable **attributes)
{
  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (model));
  g_return_if_fail (0 <= item_index && item_index < g_menu_model_get_n_items (model));
  g_return_if_fail (attributes != NULL);

  *attributes = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_variant_unref);
}

static void
unity_gtk_menu_shell_get_item_links (GMenuModel  *model,
                                     gint         item_index,
                                     GHashTable **links)
{
  UnityGtkMenuShell *shell;
  GPtrArray *sections;
  UnityGtkMenuSection *section;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (model));
  g_return_if_fail (0 <= item_index && item_index < g_menu_model_get_n_items (model));
  g_return_if_fail (links != NULL);

  shell = UNITY_GTK_MENU_SHELL (model);
  sections = unity_gtk_menu_shell_get_sections (shell);
  section = g_ptr_array_index (sections, item_index);

  *links = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
  g_hash_table_insert (*links, G_MENU_LINK_SECTION, g_object_ref (section));
}

static void
unity_gtk_menu_shell_class_init (UnityGtkMenuShellClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GMenuModelClass *menu_model_class = G_MENU_MODEL_CLASS (klass);

  object_class->dispose = unity_gtk_menu_shell_dispose;
  menu_model_class->is_mutable = unity_gtk_menu_shell_is_mutable;
  menu_model_class->get_n_items = unity_gtk_menu_shell_get_n_items;
  menu_model_class->get_item_attributes = unity_gtk_menu_shell_get_item_attributes;
  menu_model_class->get_item_links = unity_gtk_menu_shell_get_item_links;
}

static void
unity_gtk_menu_shell_init (UnityGtkMenuShell *self)
{
}

/**
 * unity_gtk_menu_shell_new:
 * @menu_shell: a #GtkMenuShell to watch.
 *
 * Creates a new #UnityGtkMenuShell based on the contents of the given
 * @menu_shell. Any subsequent changes to @menu_shell are reflected in
 * the returned #UnityGtkMenuShell.
 *
 * Returns: a new #UnityGtkMenuShell based on @menu_shell.
 */
UnityGtkMenuShell *
unity_gtk_menu_shell_new (GtkMenuShell *menu_shell)
{
  UnityGtkMenuShell *shell = g_object_new (UNITY_GTK_TYPE_MENU_SHELL, NULL);

  unity_gtk_menu_shell_set_menu_shell (shell, menu_shell);

  return shell;
}

UnityGtkMenuItem *
unity_gtk_menu_shell_get_item (UnityGtkMenuShell *shell,
                               guint              index)
{
  GPtrArray *items;

  g_return_val_if_fail (UNITY_GTK_IS_MENU_SHELL (shell), NULL);

  items = unity_gtk_menu_shell_get_items (shell);

  g_return_val_if_fail (0 <= index && index < items->len, NULL);

  return g_ptr_array_index (items, index);
}

GSequence *
unity_gtk_menu_shell_get_visible_indices (UnityGtkMenuShell *shell)
{
  g_return_val_if_fail (UNITY_GTK_IS_MENU_SHELL (shell), NULL);

  if (shell->visible_indices == NULL)
    {
      GPtrArray *items = unity_gtk_menu_shell_get_items (shell);
      guint i;

      shell->visible_indices = g_sequence_new (NULL);

      for (i = 0; i < items->len; i++)
        {
          UnityGtkMenuItem *item = g_ptr_array_index (items, i);

          if (unity_gtk_menu_item_is_visible (item))
            g_sequence_append (shell->visible_indices, GUINT_TO_POINTER (i));
        }

      if (shell->action_group != NULL)
        unity_gtk_action_group_connect_shell (shell->action_group, shell);
    }

  return shell->visible_indices;
}

GSequence *
unity_gtk_menu_shell_get_separator_indices (UnityGtkMenuShell *shell)
{
  g_return_val_if_fail (UNITY_GTK_IS_MENU_SHELL (shell), NULL);

  unity_gtk_menu_shell_get_visible_indices (shell);

  if (shell->separator_indices == NULL)
    {
      GPtrArray *items = unity_gtk_menu_shell_get_items (shell);
      guint i;

      shell->separator_indices = g_sequence_new (NULL);

      for (i = 0; i < items->len; i++)
        {
          UnityGtkMenuItem *item = g_ptr_array_index (items, i);

          if (unity_gtk_menu_item_is_visible (item) && unity_gtk_menu_item_is_separator (item))
            g_sequence_append (shell->separator_indices, GUINT_TO_POINTER (i));
        }
    }

  return shell->separator_indices;
}

void
unity_gtk_menu_shell_handle_item_notify (UnityGtkMenuShell *shell,
                                         UnityGtkMenuItem  *item,
                                         GParamSpec        *pspec)
{
  static const gchar *visible_name;
  static const gchar *sensitive_name;
  static const gchar *label_name;
  static const gchar *accel_path_name;
  static const gchar *active_name;
  static const gchar *parent_name;
  static const gchar *submenu_name;

  const gchar *pspec_name;

  g_return_if_fail (UNITY_GTK_IS_MENU_SHELL (shell));
  g_return_if_fail (UNITY_GTK_IS_MENU_ITEM (item));

  if (visible_name == NULL)
    visible_name = g_intern_static_string ("visible");
  if (sensitive_name == NULL)
    sensitive_name = g_intern_static_string ("sensitive");
  if (label_name == NULL)
    label_name = g_intern_static_string ("label");
  if (accel_path_name == NULL)
    accel_path_name = g_intern_static_string ("accel-path");
  if (active_name == NULL)
    active_name = g_intern_static_string ("active");
  if (parent_name == NULL)
    parent_name = g_intern_static_string ("parent");
  if (submenu_name == NULL)
    submenu_name = g_intern_static_string ("submenu");

  pspec_name = g_param_spec_get_name (pspec);

  if (unity_gtk_menu_shell_is_debug ())
    g_print ("%s ((%s *) %p, (%s *) %p { \"%s\" }, %s)\n", G_STRFUNC, G_OBJECT_TYPE_NAME (shell), shell, G_OBJECT_TYPE_NAME (item), item, unity_gtk_menu_item_get_label (item), pspec_name);

  if (pspec_name == visible_name)
    unity_gtk_menu_shell_handle_item_visible (shell, item);
  else if (pspec_name == sensitive_name)
    unity_gtk_menu_shell_handle_item_sensitive (shell, item);
  else if (pspec_name == label_name)
    unity_gtk_menu_shell_handle_item_label (shell, item);
  else if (pspec_name == accel_path_name)
    unity_gtk_menu_shell_handle_item_accel_path (shell, item);
  else if (pspec_name == active_name)
    unity_gtk_menu_shell_handle_item_active (shell, item);
  else if (pspec_name == parent_name)
    unity_gtk_menu_shell_handle_item_parent (shell, item);
  else if (pspec_name == submenu_name)
    unity_gtk_menu_shell_handle_item_submenu (shell, item);
}

void
unity_gtk_menu_shell_print (UnityGtkMenuShell *shell,
                            guint              indent)
{
  gchar *space;

  g_return_if_fail (shell == NULL || UNITY_GTK_IS_MENU_SHELL (shell));

  space = g_strnfill (indent, ' ');

  if (shell != NULL)
    {
      g_print ("%s(%s *) %p\n", space, G_OBJECT_CLASS_NAME (G_OBJECT_GET_CLASS (shell)), shell);

      if (shell->menu_shell != NULL)
        g_print ("%s  %lu (%s *) %p\n", space, shell->menu_shell_insert_handler_id, G_OBJECT_CLASS_NAME (G_OBJECT_GET_CLASS (shell->menu_shell)), shell->menu_shell);
      else if (shell->menu_shell_insert_handler_id)
        g_print ("%s  %lu\n", space, shell->menu_shell_insert_handler_id);

      if (shell->items != NULL)
        {
          guint i;

          for (i = 0; i < shell->items->len; i++)
            unity_gtk_menu_item_print (g_ptr_array_index (shell->items, i), indent + 2);
        }

      if (shell->sections != NULL)
        {
          guint i;

          for (i = 0; i < shell->sections->len; i++)
            unity_gtk_menu_section_print (g_ptr_array_index (shell->sections, i), indent + 2);
        }

      if (shell->visible_indices != NULL)
        {
          GSequenceIter *iter = g_sequence_get_begin_iter (shell->visible_indices);

          g_print ("%s ", space);

          while (!g_sequence_iter_is_end (iter))
            {
              g_print (" %u", g_sequence_get_uint (iter));
              iter = g_sequence_iter_next (iter);
            }

          g_print ("\n");
        }

      if (shell->separator_indices != NULL)
        {
          GSequenceIter *iter = g_sequence_get_begin_iter (shell->separator_indices);

          g_print ("%s ", space);

          while (!g_sequence_iter_is_end (iter))
            {
              g_print (" %u", g_sequence_get_uint (iter));
              iter = g_sequence_iter_next (iter);
            }

          g_print ("\n");
        }

      if (shell->action_group != NULL)
        g_print ("%s  (%s *) %p\n", space, G_OBJECT_CLASS_NAME (G_OBJECT_GET_CLASS (shell->action_group)), shell->action_group);
    }
  else
    g_print ("%sNULL\n", space);

  g_free (space);
}

gboolean
unity_gtk_menu_shell_is_debug (void)
{
  return unity_gtk_menu_shell_debug;
}

/**
 * unity_gtk_menu_shell_set_debug:
 * @debug: #TRUE to enable debugging output
 *
 * Sets if menu shell changes should be logged using g_print ().
 */
void
unity_gtk_menu_shell_set_debug (gboolean debug)
{
  unity_gtk_menu_shell_debug = debug;
}
