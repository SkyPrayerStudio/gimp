/* GIMP - The GNU Image Manipulation Program
 *
 * gimpcagetool.c
 * Copyright (C) 2010 Michael Muré <batolettre@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

#include <string.h>

#include <gegl.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "libgimpbase/gimpbase.h"
#include "libgimpmath/gimpmath.h"
#include "libgimpwidgets/gimpwidgets.h"

#include "tools-types.h"

#include "base/tile-manager.h"

#include "core/gimp.h"
#include "core/gimpchannel.h"
#include "core/gimpdrawable-shadow.h"
#include "core/gimpimage.h"
#include "core/gimpimagemap.h"
#include "core/gimplayer.h"
#include "core/gimpprogress.h"
#include "core/gimpprojection.h"

#include "gegl/gimpcageconfig.h"

#include "widgets/gimphelp-ids.h"

#include "display/gimpdisplay.h"
#include "display/gimpdisplayshell.h"
#include "display/gimpdisplayshell-transform.h"

#include "gimpcagetool.h"
#include "gimpcageoptions.h"
#include "gimptoolcontrol.h"

#include "gimp-intl.h"


#define HANDLE_SIZE 25


static void       gimp_cage_tool_finalize           (GObject               *object);
static void       gimp_cage_tool_start              (GimpCageTool          *ct,
                                                     GimpDisplay           *display);
static void       gimp_cage_tool_halt               (GimpCageTool          *ct);
static void       gimp_cage_tool_button_press       (GimpTool              *tool,
                                                     const GimpCoords      *coords,
                                                     guint32                time,
                                                     GdkModifierType        state,
                                                     GimpButtonPressType    press_type,
                                                     GimpDisplay           *display);
static void       gimp_cage_tool_button_release     (GimpTool              *tool,
                                                     const GimpCoords      *coords,
                                                     guint32                time,
                                                     GdkModifierType        state,
                                                     GimpButtonReleaseType  release_type,
                                                     GimpDisplay           *display);
static gboolean   gimp_cage_tool_key_press          (GimpTool              *tool,
                                                     GdkEventKey           *kevent,
                                                     GimpDisplay           *display);
static void       gimp_cage_tool_motion             (GimpTool              *tool,
                                                     const GimpCoords      *coords,
                                                     guint32                time,
                                                     GdkModifierType        state,
                                                     GimpDisplay           *display);
static void       gimp_cage_tool_control            (GimpTool              *tool,
                                                     GimpToolAction         action,
                                                     GimpDisplay           *display);
static void       gimp_cage_tool_cursor_update      (GimpTool              *tool,
                                                     const GimpCoords      *coords,
                                                     GdkModifierType        state,
                                                     GimpDisplay           *display);
static void       gimp_cage_tool_oper_update        (GimpTool              *tool,
                                                     const GimpCoords      *coords,
                                                     GdkModifierType        state,
                                                     gboolean               proximity,
                                                     GimpDisplay           *display);

static void       gimp_cage_tool_draw               (GimpDrawTool          *draw_tool);

static gint       gimp_cage_tool_is_on_handle       (GimpCageConfig        *gcc,
                                                     GimpDrawTool          *draw_tool,
                                                     GimpDisplay           *display,
                                                     GimpCageMode           mode,
                                                     gdouble                x,
                                                     gdouble                y,
                                                     gint                   handle_size);

static void       gimp_cage_tool_switch_to_deform   (GimpCageTool          *ct);
static void       gimp_cage_tool_remove_last_handle (GimpCageTool          *ct);
static void       gimp_cage_tool_compute_coef       (GimpCageTool          *ct,
                                                     GimpDisplay           *display);
static void       gimp_cage_tool_process            (GimpCageTool          *ct,
                                                     GimpDisplay           *display);
static void       gimp_cage_tool_process_drawable   (GimpCageTool          *ct,
                                                     GimpDrawable          *drawable,
                                                     GimpProgress          *progress);
static void       gimp_cage_tool_prepare_preview    (GimpCageTool          *ct,
                                                     GimpDisplay           *display);
static gboolean   gimp_cage_tool_update_preview     (GimpTool              *tool);

static GeglNode * gimp_cage_tool_get_render_node    (GimpCageTool          *ct,
                                                     GeglNode              *parent);


G_DEFINE_TYPE (GimpCageTool, gimp_cage_tool, GIMP_TYPE_DRAW_TOOL)

#define parent_class gimp_cage_tool_parent_class


void
gimp_cage_tool_register (GimpToolRegisterCallback  callback,
                         gpointer                  data)
{
  (* callback) (GIMP_TYPE_CAGE_TOOL,
                GIMP_TYPE_CAGE_OPTIONS,
                gimp_cage_options_gui,
                0,
                "gimp-cage-tool",
                _("Cage Transform"),
                _("Cage Transform: Deform a selection with a cage"),
                N_("_Cage Transform"), "<shift>G",
                NULL, GIMP_HELP_TOOL_CAGE,
                GIMP_STOCK_TOOL_CAGE,
                data);
}

static void
gimp_cage_tool_class_init (GimpCageToolClass *klass)
{
  GObjectClass      *object_class    = G_OBJECT_CLASS (klass);
  GimpToolClass     *tool_class      = GIMP_TOOL_CLASS (klass);
  GimpDrawToolClass *draw_tool_class = GIMP_DRAW_TOOL_CLASS (klass);

  object_class->finalize     = gimp_cage_tool_finalize;

  tool_class->button_press   = gimp_cage_tool_button_press;
  tool_class->button_release = gimp_cage_tool_button_release;
  tool_class->key_press      = gimp_cage_tool_key_press;
  tool_class->motion         = gimp_cage_tool_motion;
  tool_class->control        = gimp_cage_tool_control;
  tool_class->cursor_update  = gimp_cage_tool_cursor_update;
  tool_class->oper_update    = gimp_cage_tool_oper_update;

  draw_tool_class->draw      = gimp_cage_tool_draw;
}

static void
gimp_cage_tool_init (GimpCageTool *self)
{
  self->config            = g_object_new (GIMP_TYPE_CAGE_CONFIG, NULL);
  self->cursor_position.x = 0;
  self->cursor_position.y = 0;
  self->handle_moved      = -1;
  self->cage_complete     = FALSE;

  self->coef              = NULL;
  self->image_map         = NULL;
  self->node_preview      = NULL;

  self->idle_id           = 0;
}

static void
gimp_cage_tool_finalize (GObject *object)
{
  GimpCageTool *ct = GIMP_CAGE_TOOL (object);

  gimp_cage_tool_halt (ct);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gimp_cage_tool_control (GimpTool       *tool,
                        GimpToolAction  action,
                        GimpDisplay    *display)
{
  switch (action)
    {
    case GIMP_TOOL_ACTION_PAUSE:
    case GIMP_TOOL_ACTION_RESUME:
      break;

    case GIMP_TOOL_ACTION_HALT:
      gimp_cage_tool_halt (GIMP_CAGE_TOOL (tool));
      break;
    }

  GIMP_TOOL_CLASS (parent_class)->control (tool, action, display);
}

static void
gimp_cage_tool_start (GimpCageTool *ct,
                      GimpDisplay  *display)
{
  GimpTool     *tool     = GIMP_TOOL (ct);
  GimpImage    *image    = gimp_display_get_image (display);
  GimpDrawable *drawable = gimp_image_get_active_drawable (image);
  gint          off_x;
  gint          off_y;

  gimp_tool_control_activate (tool->control);

  tool->display = display;

  if (ct->config)
    {
      g_object_unref (ct->config);
    }

  if (ct->image_map)
    {
      gimp_image_map_abort (ct->image_map);
      g_object_unref (ct->image_map);
    }

  ct->config            = g_object_new (GIMP_TYPE_CAGE_CONFIG, NULL);
  ct->cursor_position.x = -1000;
  ct->cursor_position.y = -1000;
  ct->handle_moved      = -1;
  ct->cage_complete     = FALSE;

  /* Setting up cage offset to convert the cage point coords to
   * drawable coords
   */
  gimp_item_get_offset (GIMP_ITEM (drawable), &off_x, &off_y);

  ct->config->offset_x = off_x;
  ct->config->offset_y = off_y;

  gimp_draw_tool_start (GIMP_DRAW_TOOL (ct), display);
}

static gboolean
gimp_cage_tool_key_press (GimpTool    *tool,
                          GdkEventKey *kevent,
                          GimpDisplay *display)
{
  GimpCageTool *ct = GIMP_CAGE_TOOL (tool);

  switch (kevent->keyval)
    {
    case GDK_BackSpace:
      gimp_cage_tool_remove_last_handle (ct);
      return TRUE;

    case GDK_Return:
    case GDK_KP_Enter:
    case GDK_ISO_Enter:
      if (ct->cage_complete)
        {
          gimp_image_map_abort (ct->image_map);
          g_object_unref (ct->image_map);
          ct->image_map = NULL;
          if (ct->idle_id)
            {
              g_source_remove (ct->idle_id);
              ct->idle_id = 0;
            }

          gimp_cage_tool_process (ct, display);
        }
      return TRUE;

    case GDK_Escape:
      gimp_cage_tool_halt (ct);
      return TRUE;

    default:
      break;
    }

  return FALSE;
}

static void
gimp_cage_tool_motion (GimpTool         *tool,
                       const GimpCoords *coords,
                       guint32           time,
                       GdkModifierType   state,
                       GimpDisplay      *display)
{
  GimpCageTool    *ct        = GIMP_CAGE_TOOL (tool);
  GimpDrawTool    *draw_tool = GIMP_DRAW_TOOL (tool);
  GimpCageOptions *options   = GIMP_CAGE_TOOL_GET_OPTIONS (ct);
  GimpCageConfig  *config    = ct->config;

  gimp_draw_tool_pause (draw_tool);

  if (ct->handle_moved >= 0)
    {
      gimp_cage_config_move_cage_point (config,
                                        options->cage_mode,
                                        ct->handle_moved,
                                        coords->x,
                                        coords->y);
    }

  gimp_draw_tool_resume (draw_tool);
}

static void
gimp_cage_tool_oper_update (GimpTool         *tool,
                            const GimpCoords *coords,
                            GdkModifierType   state,
                            gboolean          proximity,
                            GimpDisplay      *display)
{
  GimpCageTool    *ct            = GIMP_CAGE_TOOL (tool);
  GimpDrawTool    *draw_tool     = GIMP_DRAW_TOOL (tool);
  GimpCageOptions *options       = GIMP_CAGE_TOOL_GET_OPTIONS (ct);
  GimpCageConfig  *config        = ct->config;
  gint             active_handle = -1;

  if (config)
    active_handle = gimp_cage_tool_is_on_handle (config,
                                                 draw_tool,
                                                 display,
                                                 options->cage_mode,
                                                 coords->x,
                                                 coords->y,
                                                 HANDLE_SIZE);

  if (! ct->cage_complete || (active_handle > -1))
    {
      gimp_draw_tool_pause (draw_tool);

      ct->cursor_position.x = coords->x;
      ct->cursor_position.y = coords->y;

      gimp_draw_tool_resume (draw_tool);
    }
}

static void
gimp_cage_tool_button_press (GimpTool            *tool,
                             const GimpCoords    *coords,
                             guint32              time,
                             GdkModifierType      state,
                             GimpButtonPressType  press_type,
                             GimpDisplay         *display)
{
  GimpCageTool    *ct      = GIMP_CAGE_TOOL (tool);
  GimpCageOptions *options = GIMP_CAGE_TOOL_GET_OPTIONS (ct);
  GimpCageConfig  *config  = ct->config;

  g_return_if_fail (GIMP_IS_CAGE_TOOL (ct));

  if (display != tool->display)
    {
      gimp_cage_tool_start (ct, display);
      config = ct->config;
    }

  gimp_draw_tool_pause (GIMP_DRAW_TOOL (ct));

  if (ct->handle_moved < 0)
    {
      ct->handle_moved = gimp_cage_tool_is_on_handle (config,
                                                      GIMP_DRAW_TOOL (ct),
                                                      display,
                                                      options->cage_mode,
                                                      coords->x,
                                                      coords->y,
                                                      HANDLE_SIZE);
      if (ct->handle_moved > 0 && ct->idle_id > 0)
        {
          g_source_remove(ct->idle_id);
          ct->idle_id = 0; /*Stop preview update for now*/
        }
    }

  if (ct->handle_moved < 0 && ! ct->cage_complete)
    {
      gimp_cage_config_add_cage_point (config, coords->x, coords->y);
    }

  gimp_draw_tool_resume (GIMP_DRAW_TOOL (ct));

  /* user is clicking on the first handle, we close the cage and
   * switch to deform mode
   */
  if (ct->handle_moved == 0 && config->n_cage_vertices > 2 && ! ct->coef)
    {
      ct->cage_complete = TRUE;
      gimp_cage_tool_switch_to_deform (ct);

      gimp_cage_config_reverse_cage_if_needed (config);
      gimp_cage_tool_compute_coef (ct, display);
      gimp_cage_tool_prepare_preview (ct, display);
    }
}

void
gimp_cage_tool_button_release (GimpTool              *tool,
                               const GimpCoords      *coords,
                               guint32                time,
                               GdkModifierType        state,
                               GimpButtonReleaseType  release_type,
                               GimpDisplay           *display)
{
  GimpCageTool *ct = GIMP_CAGE_TOOL (tool);

  if (ct->coef && ct->handle_moved > -1)
    {
      GimpDisplayShell *shell = gimp_display_get_shell (tool->display);
      GimpItem         *item  = GIMP_ITEM (tool->drawable);
      gint              x, y;
      gint              w, h;
      gint              off_x, off_y;
      GeglRectangle     visible;

      gimp_display_shell_untransform_viewport (shell, &x, &y, &w, &h);

      gimp_item_get_offset (item, &off_x, &off_y);

      gimp_rectangle_intersect (x, y, w, h,
                                off_x,
                                off_y,
                                gimp_item_get_width  (item),
                                gimp_item_get_height (item),
                                &visible.x,
                                &visible.y,
                                &visible.width,
                                &visible.height);

      visible.x -= off_x;
      visible.y -= off_y;

      gimp_draw_tool_pause (GIMP_DRAW_TOOL (ct));

      gimp_image_map_apply (ct->image_map, &visible);

      ct->idle_id = g_idle_add ((GSourceFunc) gimp_cage_tool_update_preview,
                                tool);

      gimp_draw_tool_resume (GIMP_DRAW_TOOL (ct));
    }

  ct->handle_moved = -1;
}

static void
gimp_cage_tool_cursor_update (GimpTool         *tool,
                              const GimpCoords *coords,
                              GdkModifierType   state,
                              GimpDisplay      *display)
{
  GimpCageTool    *ct      = GIMP_CAGE_TOOL (tool);
  GimpCageOptions *options = GIMP_CAGE_TOOL_GET_OPTIONS (ct);

  if (tool->display == NULL)
    {
      GIMP_TOOL_CLASS (parent_class)->cursor_update (tool, coords, state,
                                                     display);
    }
  else
    {
      GimpCursorModifier modifier;

      if (options->cage_mode == GIMP_CAGE_MODE_CAGE_CHANGE)
        {
          modifier = GIMP_CURSOR_MODIFIER_ANCHOR;
        }
      else
        {
          modifier = GIMP_CURSOR_MODIFIER_MOVE;
        }

      gimp_tool_set_cursor (tool, display,
                            gimp_tool_control_get_cursor (tool->control),
                            gimp_tool_control_get_tool_cursor (tool->control),
                            modifier);
    }
}

static void
gimp_cage_tool_draw (GimpDrawTool *draw_tool)
{
  GimpCageTool    *ct        = GIMP_CAGE_TOOL (draw_tool);
  GimpCageOptions *options   = GIMP_CAGE_TOOL_GET_OPTIONS (ct);
  GimpCageConfig  *config    = ct->config;
  gint             i         = 0;
  gint             on_handle = -1;
  GimpVector2     *vertices;
  gint             n_vertices;

  if (config->n_cage_vertices <= 0)
    return;

  if (options->cage_mode == GIMP_CAGE_MODE_CAGE_CHANGE)
    vertices = config->cage_vertices;
  else
    vertices = config->cage_vertices_d;

  n_vertices = config->n_cage_vertices;

  /*gimp_draw_tool_add_lines (draw_tool,
                             vertices,
                             config->n_cage_vertices,
                             FALSE);*/

  if (! ct->cage_complete && ct->cursor_position.x != -1000)
    {
      gimp_draw_tool_add_line (draw_tool,
                               vertices[n_vertices - 1].x + ct->config->offset_x,
                               vertices[n_vertices - 1].y + ct->config->offset_y,
                               ct->cursor_position.x,
                               ct->cursor_position.y);
    }
  else
    {
      gimp_draw_tool_add_line (draw_tool,
                               vertices[n_vertices - 1].x + ct->config->offset_x,
                               vertices[n_vertices - 1].y + ct->config->offset_y,
                               vertices[0].x + ct->config->offset_x,
                               vertices[0].y + ct->config->offset_y);
    }

  on_handle = gimp_cage_tool_is_on_handle (config,
                                           draw_tool,
                                           draw_tool->display,
                                           options->cage_mode,
                                           ct->cursor_position.x,
                                           ct->cursor_position.y,
                                           HANDLE_SIZE);

  for (i = 0; i < n_vertices; i++)
    {
      GimpHandleType handle = GIMP_HANDLE_CIRCLE;

      if (i == on_handle)
        handle = GIMP_HANDLE_FILLED_CIRCLE;

      if (i > 0)
        gimp_draw_tool_add_line (draw_tool,
                                 vertices[i - 1].x + ct->config->offset_x,
                                 vertices[i - 1].y + ct->config->offset_y,
                                 vertices[i].x + ct->config->offset_x,
                                 vertices[i].y + ct->config->offset_y);

      gimp_draw_tool_add_handle (draw_tool,
                                 handle,
                                 vertices[i].x + ct->config->offset_x,
                                 vertices[i].y + ct->config->offset_y,
                                 HANDLE_SIZE, HANDLE_SIZE,
                                 GIMP_HANDLE_ANCHOR_CENTER);
    }
}

static gint
gimp_cage_tool_is_on_handle (GimpCageConfig *gcc,
                             GimpDrawTool   *draw_tool,
                             GimpDisplay    *display,
                             GimpCageMode    mode,
                             gdouble         x,
                             gdouble         y,
                             gint            handle_size)
{
  gint    i;
  gdouble vert_x;
  gdouble vert_y;
  gdouble dist = G_MAXDOUBLE;

  g_return_val_if_fail (GIMP_IS_CAGE_CONFIG (gcc), -1);

  if (gcc->n_cage_vertices == 0)
    return -1;

  for (i = 0; i < gcc->n_cage_vertices; i++)
    {
      if (mode == GIMP_CAGE_MODE_CAGE_CHANGE)
        {
          vert_x = gcc->cage_vertices[i].x + gcc->offset_x;
          vert_y = gcc->cage_vertices[i].y + gcc->offset_y;
        }
      else
        {
          vert_x = gcc->cage_vertices_d[i].x + gcc->offset_x;
          vert_y = gcc->cage_vertices_d[i].y + gcc->offset_y;
        }

      dist = gimp_draw_tool_calc_distance_square (GIMP_DRAW_TOOL (draw_tool),
                                                  display,
                                                  x, y,
                                                  vert_x, vert_y);

      if (dist <= (handle_size * handle_size))
        return i;
    }

  return -1;
}

static void
gimp_cage_tool_remove_last_handle (GimpCageTool *ct)
{
  GimpCageConfig *config = ct->config;

  gimp_draw_tool_pause (GIMP_DRAW_TOOL (ct));

  gimp_cage_config_remove_last_cage_point (config);

  gimp_draw_tool_resume (GIMP_DRAW_TOOL (ct));
}

static void
gimp_cage_tool_switch_to_deform (GimpCageTool *ct)
{
  GimpCageOptions *options = GIMP_CAGE_TOOL_GET_OPTIONS (ct);

  g_object_set (options, "cage-mode", GIMP_CAGE_MODE_DEFORM, NULL);
}

static void
gimp_cage_tool_compute_coef (GimpCageTool *ct,
                             GimpDisplay  *display)
{
  GimpCageConfig *config   = ct->config;
  Babl           *format;
  GeglNode       *gegl;
  GeglNode       *input;
  GeglNode       *output;
  GeglProcessor  *processor;
  GimpProgress   *progress;
  GeglBuffer     *buffer;
  gdouble         value;

  if (ct->coef)
    {
      gegl_buffer_destroy (ct->coef);
      ct->coef = NULL;
    }

  format = babl_format_n (babl_type ("float"),
                          config->n_cage_vertices * 2);

  progress = gimp_progress_start (GIMP_PROGRESS (display),
                                  _("Coefficient computation"),
                                  FALSE);

  gegl = gegl_node_new ();

  input = gegl_node_new_child (gegl,
                               "operation", "gimp:cage-coef-calc",
                               "config",    ct->config,
                               NULL);

  output = gegl_node_new_child (gegl,
                                "operation", "gegl:buffer-sink",
                                "buffer",    &buffer,
                                "format",    format,
                                NULL);

  gegl_node_connect_to (input, "output",
                        output, "input");

  processor = gegl_node_new_processor (output, NULL);

  while (gegl_processor_work (processor, &value))
    {
      gimp_progress_set_value (progress, value);
    }

  gimp_progress_end (progress);

  gegl_processor_destroy (processor);

  ct->coef = buffer;
  g_object_unref (gegl);
}

static GeglNode *
gimp_cage_tool_get_render_node (GimpCageTool *ct,
                                GeglNode     *parent)
{
  GimpCageOptions *options  = GIMP_CAGE_TOOL_GET_OPTIONS (ct);
  GeglNode        *coef, *cage, *render; /* Render nodes */
  GeglNode        *input, *output; /* Proxy nodes*/
  GeglNode        *node; /* wraper to be returned */

  node = gegl_node_new_child (parent, NULL, NULL);

  input  = gegl_node_get_input_proxy  (node, "input");
  output = gegl_node_get_output_proxy (node, "output");

  coef = gegl_node_new_child (ct->node_preview,
                              "operation", "gegl:buffer-source",
                              "buffer",    ct->coef,
                              NULL);

  cage = gegl_node_new_child (parent,
                              "operation",        "gimp:cage-transform",
                              "config",           ct->config,
                              "fill_plain_color", options->fill_plain_color,
                              NULL);

  render = gegl_node_new_child (parent,
                                "operation", "gegl:map-absolute",
                                NULL);

  gegl_node_connect_to (input, "output",
                        cage, "input");

  gegl_node_connect_to (coef, "output",
                        cage, "aux");

  gegl_node_connect_to (input, "output",
                        render, "input");

  gegl_node_connect_to (cage, "output",
                        render, "aux");

  gegl_node_connect_to (render, "output",
                        output, "input");
  return node;
}

static void
gimp_cage_tool_prepare_preview (GimpCageTool *ct,
                                GimpDisplay  *display)
{
  GimpImage    *image    = gimp_display_get_image (display);
  GimpDrawable *drawable = gimp_image_get_active_drawable (image);
  GeglNode     *node;

  if (ct->node_preview)
    {
      g_object_unref (ct->node_preview);
      ct->node_preview = NULL;
    }

  ct->node_preview  = gegl_node_new ();

  node = gimp_cage_tool_get_render_node (ct, ct->node_preview);

  ct->image_map = gimp_image_map_new (drawable,
                                      _("Cage transform"),
                                      node,
                                      NULL,
                                      NULL);
}

static gboolean
gimp_cage_tool_update_preview (GimpTool *tool)
{
  GimpCageTool *ct    = GIMP_CAGE_TOOL (tool);
  GimpImage    *image = gimp_display_get_image (tool->display);

  if (! ct->image_map)
    {
      /*Destroyed, bailing out*/
      ct->idle_id = 0;
      return FALSE;
    }

  if (! gimp_image_map_is_busy (ct->image_map))
    {
      ct->idle_id = 0;
      gimp_projection_flush_now (gimp_image_get_projection (image));
      gimp_display_flush_now (tool->display);
      return FALSE;
    }

  return TRUE;
}

static void
gimp_cage_tool_process (GimpCageTool *ct,
                        GimpDisplay  *display)
{
  GimpImage    *image    = gimp_display_get_image (display);
  GimpLayer    *layer    = gimp_image_get_active_layer (image);
  GimpDrawable *drawable = GIMP_DRAWABLE (layer);
  GimpDrawable *mask     = GIMP_DRAWABLE (gimp_layer_get_mask (layer));

  g_return_if_fail (ct->coef);

  if (GIMP_IS_DRAWABLE (drawable))
    {
      GimpProgress *progress;

      progress = gimp_progress_start (GIMP_PROGRESS (display),
                                      _("Rendering cage transform"),
                                      FALSE);

      gimp_cage_tool_process_drawable (ct, drawable, progress);

      gimp_progress_end (progress);

      if (mask)
        {
          progress = gimp_progress_start (GIMP_PROGRESS (display),
                                          _("Rendering mask cage transform"),
                                          FALSE);
          gimp_cage_tool_process_drawable (ct, mask, progress);

          gimp_progress_end (progress);
        }

      gimp_image_flush (image);

      gimp_cage_tool_halt (ct);
    }
}

static void
gimp_cage_tool_process_drawable (GimpCageTool *ct,
                                 GimpDrawable *drawable,
                                 GimpProgress *progress)
{
  TileManager   *new_tiles = NULL;
  TileManager   *old_tiles = NULL;
  GeglRectangle  rect;
  GeglProcessor *processor;
  gdouble        value;

  if (GIMP_IS_DRAWABLE (drawable))
    {
      GeglNode *gegl = gegl_node_new ();

      /* reverse transform */
      GeglNode *node, *input, *output;

      old_tiles = gimp_drawable_get_tiles (drawable);

      input  = gegl_node_new_child (gegl,
                                    "operation",    "gimp:tilemanager-source",
                                    "tile-manager", old_tiles,
                                    "linear",       TRUE,
                                    NULL);

      node = gimp_cage_tool_get_render_node (ct, gegl);

      new_tiles = gimp_drawable_get_shadow_tiles (drawable);
      output = gegl_node_new_child (gegl,
                                    "operation",    "gimp:tilemanager-sink",
                                    "tile-manager", new_tiles,
                                    "linear",       TRUE,
                                    NULL);

      gegl_node_connect_to (input, "output",
                            node, "input");

      gegl_node_connect_to (node, "output",
                            output, "input");

    /* Debug code sample for debugging coef calculation.*/

    /*
    debug = gegl_node_new_child (gegl,
                                  "operation", "gegl:debugit",
                                  NULL);

    gegl_node_connect_to (coef, "output",
                          debug, "input");

    gegl_node_connect_to (debug, "output",
                          output, "input");*/


      processor = gegl_node_new_processor (output, NULL);

      while (gegl_processor_work (processor, &value))
        {
          gimp_progress_set_value (progress, value);
        }

      rect.x      = 0;
      rect.y      = 0;
      rect.width  = tile_manager_width  (new_tiles);
      rect.height = tile_manager_height (new_tiles);

      gimp_drawable_merge_shadow_tiles (drawable, TRUE, _("Cage transform"));
      gimp_drawable_free_shadow_tiles (drawable);

      gimp_drawable_update (drawable, rect.x, rect.y, rect.width, rect.height);

      g_object_unref (gegl);

      gegl_processor_destroy (processor);
    }
}

static void
gimp_cage_tool_halt (GimpCageTool *ct)
{
  GimpTool     *tool      = GIMP_TOOL (ct);
  GimpDrawTool *draw_tool = GIMP_DRAW_TOOL (ct);

  if (gimp_draw_tool_is_active (draw_tool))
    gimp_draw_tool_stop (draw_tool);

  if (gimp_tool_control_is_active (tool->control))
    gimp_tool_control_halt (tool->control);

  if (ct->config)
    {
      g_object_unref (ct->config);
      ct->config = NULL;
    }

  if (ct->coef)
    {
      gegl_buffer_destroy (ct->coef);
      ct->coef = NULL;
    }

  if (ct->image_map)
    {
      if (ct->idle_id > 0)
        {
          g_source_remove (ct->idle_id);
          ct->idle_id = 0;
        }

      gimp_tool_control_set_preserve (tool->control, TRUE);

      gimp_image_map_abort (ct->image_map);
      g_object_unref (ct->image_map);
      ct->image_map = NULL;

      gimp_tool_control_set_preserve (tool->control, FALSE);

      gimp_image_flush (gimp_display_get_image (tool->display));
    }

  if (ct->node_preview)
    {
      g_object_unref (ct->node_preview);
      ct->node_preview = NULL;
    }

  tool->display  = NULL;
}