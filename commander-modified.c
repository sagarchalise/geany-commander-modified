/*
 *
 *  Copyright (C) 2012  Colomban Wendling <ban@herbesfolles.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
#include <gtk/gtk.h>
#include <glib.h>
 */
#ifdef HAVE_CONFIG_H
	#include "config.h"
#endif

#define _GNU_SOURCE
// #include <string.h>
#include <geanyplugin.h>
#include <gdk/gdkkeysyms.h>
#include <tagmanager/tm_tag.h>

#ifdef HAVE_LOCALE_H
	#include <locale.h>
#endif

GeanyPlugin      *geany_plugin;
GeanyData        *geany_data;

PLUGIN_VERSION_CHECK(211)

//PLUGIN_SET_INFO ("Symbol Browser", "Provides a panel for quick access to tags and symbols in current document.", "0.1", "Sagar Chalise <chalisesagar@gmail.com>")

PLUGIN_SET_TRANSLATABLE_INFO(LOCALEDIR,
	GETTEXT_PACKAGE,
	_("Symbol and Quick Open"),
	_("Provides a panel for quick access to tags and symbols in current document."),
	"0.1",
	"Sagar Chalise <chalisesagar@gmail.com>\n\
    https://github.com/sagarchalise/geany-symbol"
);
/* GTK compatibility functions/macros */

#if ! GTK_CHECK_VERSION (2, 18, 0)
# define gtk_widget_set_can_focus(w, v)               \
  G_STMT_START {                                      \
    GtkWidget *widget = (w);                          \
    if (v) {                                          \
      GTK_WIDGET_SET_FLAGS (widget, GTK_CAN_FOCUS);   \
    } else {                                          \
      GTK_WIDGET_UNSET_FLAGS (widget, GTK_CAN_FOCUS); \
    }                                                 \
  } G_STMT_END
#endif

#if ! GTK_CHECK_VERSION (2, 21, 8)
# define GDK_KEY_Down       GDK_Down
# define GDK_KEY_Escape     GDK_Escape
# define GDK_KEY_ISO_Enter  GDK_ISO_Enter
# define GDK_KEY_KP_Enter   GDK_KP_Enter
# define GDK_KEY_Page_Down  GDK_Page_Down
# define GDK_KEY_Page_Up    GDK_Page_Up
# define GDK_KEY_Return     GDK_Return
# define GDK_KEY_Tab        GDK_Tab
# define GDK_KEY_Up         GDK_Up
#endif


/* Plugin */

enum {
  KB_SHOW_PANEL,
  KB_COUNT
};

struct {
  GtkWidget    *panel;
  GtkWidget    *entry;
  GtkWidget    *view;
  GtkListStore *store;
  GtkTreeModel *filter;
} plugin_data = {
  NULL, NULL,
  NULL, NULL,
  NULL
};
enum {
  COL_LABEL,
  COL_LINE,
  COL_TAG,
  COL_COUNT
};

static const gchar *get_symbol_name(GeanyDocument *doc, const TMTag *tag)
{
	gchar *utf8_name;
	const gchar *scope = tag->scope;
	static GString *buffer = NULL;	/* buffer will be small so we can keep it for reuse */
	gboolean doc_is_utf8 = FALSE;

	/* encodings_convert_to_utf8_from_charset() fails with charset "None", so skip conversion
	 * for None at this point completely */
	if (utils_str_equal(doc->encoding, "UTF-8") ||
		utils_str_equal(doc->encoding, "None"))
		doc_is_utf8 = TRUE;
	else /* normally the tags will always be in UTF-8 since we parse from our buffer, but a
		  * plugin might have called tm_source_file_update(), so check to be sure */
		doc_is_utf8 = g_utf8_validate(tag->name, -1, NULL);

	if (! doc_is_utf8)
		utf8_name = encodings_convert_to_utf8_from_charset(tag->name,
			-1, doc->encoding, TRUE);
	else
		utf8_name = tag->name;

	if (utf8_name == NULL)
		return NULL;

	if (! buffer)
		buffer = g_string_new(NULL);
	else
		g_string_truncate(buffer, 0);

	g_string_append(buffer, utf8_name);

	if (! doc_is_utf8)
		g_free(utf8_name);

	g_string_append_printf(buffer, " [%lu]", tag->line);

	return buffer->str;
}
gboolean get_score(const gchar *key, const gchar *name){
    gchar  *haystack  = g_utf8_casefold (name, -1);
    gchar  *needle   = g_utf8_casefold (key, -1);
    gboolean score = TRUE;

    if (key == NULL || haystack == NULL ||
        *needle == '\0' || *haystack == '\0') {
        return score;
    }
    score = (strcasestr(haystack, needle) != NULL)?TRUE:FALSE;
    g_free (haystack);
    g_free (needle);
    return score;
}
void indicate_or_go_to_pos(GeanyEditor *editor, gchar *name, gint line, gboolean indicate){
    gint pos;
    struct Sci_TextToFind ttf;
    ttf.chrg.cpMin = sci_get_position_from_line(editor->sci, line-1);
    ttf.chrg.cpMax = sci_get_line_end_position(editor->sci, line);
    ttf.lpstrText = name;
    pos = sci_find_text(editor->sci, SCFIND_MATCHCASE | SCFIND_WHOLEWORD, &ttf);
    if(pos != -1){
	if(indicate){
	    editor_indicator_set_on_range(editor, GEANY_INDICATOR_SEARCH, ttf.chrgText.cpMin, ttf.chrgText.cpMax);
	}
	else{
	    editor_goto_pos(editor, pos, FALSE);
	}
    }
}

static void jump_to_symbol(gchar *name, gint line){
    GeanyDocument *doc = document_get_current();
    editor_indicator_clear(doc->editor, GEANY_INDICATOR_SEARCH);
    keybindings_send_command(GEANY_KEY_GROUP_DOCUMENT, GEANY_KEYS_DOCUMENT_REMOVE_MARKERS);
    if(!sci_is_marker_set_at_line(doc->editor->sci, line-1, 0))
	sci_set_marker_at_line(doc->editor->sci, line-1, 0);
    sci_goto_line(doc->editor->sci, line-1, TRUE);
    indicate_or_go_to_pos(doc->editor, name, line, TRUE);
}
static void
tree_view_set_cursor_from_iter (GtkTreeView *view,
                                GtkTreeIter *iter)
{
  GtkTreePath *path;
  path = gtk_tree_model_get_path (gtk_tree_view_get_model (view), iter);
  gtk_tree_view_set_cursor (view, path, NULL, FALSE);
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  TMTag *tag;
  gtk_tree_model_get(model, iter, COL_TAG, &tag, -1);
  jump_to_symbol(tag->name, (gint)tag->line);
  gtk_tree_path_free (path);
}
static gboolean
visible_func (GtkTreeModel *model,
              GtkTreeIter  *iter,
              gpointer      data)
{
  TMTag *tag;
  //atleast 2 chars  should  be  available for comparison
  guint16 key_length;
  gtk_tree_model_get (model, iter, COL_TAG, &tag, -1);
  const gchar  *key   = gtk_entry_get_text (GTK_ENTRY (plugin_data.entry));
  key_length = gtk_entry_get_text_length(GTK_ENTRY (plugin_data.entry));
  gboolean visible = TRUE;
  if(key_length > 1){
        visible = get_score(key, tag->name);
    }
  return visible;
}


static void
on_entry_text_notify (GObject    *object,
                      GParamSpec *pspec,
                      gpointer    dummy)
{
  GtkTreeIter   iter;
  GtkTreeView  *view  = GTK_TREE_VIEW (plugin_data.view);
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(model));
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(model), visible_func, NULL, NULL);
   if (gtk_tree_model_get_iter_first (model, &iter)) {
     tree_view_set_cursor_from_iter (view, &iter);
   }
}

static void
tree_view_move_focus (GtkTreeView    *view,
                      GtkMovementStep step,
                      gint            amount)
{
  GtkTreeIter   iter;
  GtkTreePath  *path;
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  gboolean      valid = FALSE;
  gtk_tree_view_get_cursor (view, &path, NULL);
  if (! path) {
    valid = gtk_tree_model_get_iter_first (model, &iter);
  } else {
    switch (step) {
      case GTK_MOVEMENT_BUFFER_ENDS:
        valid = gtk_tree_model_get_iter_first (model, &iter);
        if (valid && amount > 0) {
          GtkTreeIter prev;

          do {
            prev = iter;
          } while (gtk_tree_model_iter_next (model, &iter));
          iter = prev;
        }
        break;

      case GTK_MOVEMENT_DISPLAY_LINES:
        gtk_tree_model_get_iter (model, &iter, path);
        if (amount > 0) {
          while ((valid = gtk_tree_model_iter_next (model, &iter)) &&
                 --amount > 0)
            ;
        } else if (amount < 0) {
          while ((valid = gtk_tree_path_prev (path)) && --amount > 0)
            ;

          if (valid) {
            gtk_tree_model_get_iter (model, &iter, path);
          }
        }
        break;

      default:
        g_assert_not_reached ();
    }
    gtk_tree_path_free (path);
  }

  if (valid) {
    tree_view_set_cursor_from_iter (view, &iter);
  } else {
    gtk_widget_error_bell (GTK_WIDGET (view));
  }
}

static void
tree_view_activate_focused_row (GtkTreeView *view)
{
  GtkTreePath        *path;
  GtkTreeViewColumn  *column;
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  GtkTreeIter   iter;
  GeanyDocument *doc = document_get_current();
  TMTag *tag;
  gtk_tree_view_get_cursor (view, &path, &column);
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get(model, &iter, COL_TAG, &tag, -1);
  editor_indicator_clear(doc->editor, GEANY_INDICATOR_SEARCH);
  keybindings_send_command(GEANY_KEY_GROUP_DOCUMENT, GEANY_KEYS_DOCUMENT_REMOVE_MARKERS);
  if (path) {
    gtk_tree_view_row_activated (view, path, column);
    gtk_tree_path_free (path);
  }
  indicate_or_go_to_pos(doc->editor, tag->name, tag->line, FALSE);
}
static void
on_entry_activate (GtkEntry  *entry,
                   gpointer   dummy)
{
  tree_view_activate_focused_row (GTK_TREE_VIEW (plugin_data.view));
}

static gboolean
on_panel_key_press_event (GtkWidget    *widget,
                          GdkEventKey  *event,
                          gpointer      dummy)
{
  GeanyDocument *old_doc = document_get_current();
  switch (event->keyval) {
    case GDK_KEY_Escape:
        editor_indicator_clear(old_doc->editor, GEANY_INDICATOR_SEARCH);
        keybindings_send_command(GEANY_KEY_GROUP_DOCUMENT, GEANY_KEYS_DOCUMENT_REMOVE_MARKERS);
        gtk_widget_hide(widget);
      return TRUE;

    case GDK_KEY_Tab:
        /* avoid leaving the entry */
        return TRUE;

    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
       keybindings_send_command(GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR);
      tree_view_activate_focused_row (GTK_TREE_VIEW (plugin_data.view));
      gtk_widget_hide(widget);
      return TRUE;

    case GDK_KEY_Page_Up:
    case GDK_KEY_Page_Down:
       keybindings_send_command(GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR);
      tree_view_move_focus (GTK_TREE_VIEW (plugin_data.view),
                            GTK_MOVEMENT_DISPLAY_LINES,
                            event->keyval == GDK_KEY_Page_Up ? -1 : 1);
      return TRUE;

    case GDK_KEY_Up:
    case GDK_KEY_Down: {
       keybindings_send_command(GEANY_KEY_GROUP_FOCUS, GEANY_KEYS_FOCUS_EDITOR);
      tree_view_move_focus (GTK_TREE_VIEW (plugin_data.view),
                            GTK_MOVEMENT_DISPLAY_LINES,
                            event->keyval == GDK_KEY_Up ? -1 : 1);
      return TRUE;
    }
  }

  return FALSE;
}

static const gchar *get_tag_label(gint tag_type)
{
  switch(tag_type)
	{
		case tm_tag_class_t: return "class";
		case tm_tag_enum_t: return "enum";
		case tm_tag_enumerator_t: return "enumval";
		case tm_tag_field_t: return "field";
		case tm_tag_function_t: return "function";
		case tm_tag_interface_t: return "interface";
		case tm_tag_member_t: return "member";
		case tm_tag_method_t: return "method";
		case tm_tag_namespace_t: return "namespace";
		case tm_tag_package_t: return "package";
		case tm_tag_prototype_t: return "prototype";
		case tm_tag_struct_t: return "struct";
		case tm_tag_typedef_t: return "typedef";
		case tm_tag_union_t: return "union";
		case tm_tag_variable_t: return "variable";
		case tm_tag_externvar_t: return "extern";
		case tm_tag_macro_t: return "define";
		case tm_tag_macro_with_arg_t: return "macro";
		default: return "other";
	}
	return NULL;
}


static void
store_populate_tag_items_for_current_doc (GtkListStore  *store)
{
    GeanyDocument *doc = document_get_current();
    
    if(DOC_VALID(doc) && doc->tm_file && doc->tm_file->tags_array->len > 0){
    const gchar *taglabel;
    gint i;
  TMTag *tag;
    for (i = 0; i < doc->tm_file->tags_array->len; ++i)
	{
        tag = TM_TAG(doc->tm_file->tags_array->pdata[i]);
        taglabel = get_tag_label(tag->type);
         gchar *label = g_markup_printf_escaped ("<small>(<i>%s</i>)</small> %s\n<small>%s</small>",
                                             taglabel,
                                             get_symbol_name(doc, tag),
                                             DOC_FILENAME(doc));
        gtk_list_store_insert_with_values (store, NULL, -1,
                                       COL_LABEL, label,
                                       COL_LINE, tag->line,
                                       COL_TAG, tag,
                                       -1);
        g_free(label);
    }
    }
}

static void
fill_store (GtkListStore *store)
{
	store_populate_tag_items_for_current_doc(store);
}
static void
on_panel_show (GtkWidget *widget,
               gpointer   dummy)
{
  gtk_widget_grab_focus (plugin_data.entry);
}

static void
on_panel_hide (GtkWidget *widget,
               gpointer   dummy)
{
  GtkTreeView  *view = GTK_TREE_VIEW (plugin_data.view);
  gtk_list_store_clear (plugin_data.store);
}

static void
create_panel (void)
{
  GtkWidget          *frame;
  GtkWidget          *box;
  GtkWidget          *scroll;
  GtkTreeViewColumn  *col;
  GtkCellRenderer    *cell;

  plugin_data.panel = g_object_new (GTK_TYPE_WINDOW,
                                    "decorated", FALSE,
                                    "default-width", 250,
                                    "default-height", 100,
                                    "transient-for", geany_data->main_widgets->window,
                                    "window-position", GTK_WIN_POS_CENTER_ON_PARENT,
                                    "type-hint", GDK_WINDOW_TYPE_HINT_DIALOG,
                                    "skip-taskbar-hint", TRUE,
                                    "skip-pager-hint", TRUE,
                                    NULL);
  g_signal_connect (plugin_data.panel, "focus-out-event",
                    G_CALLBACK (gtk_widget_hide), NULL);
  g_signal_connect (plugin_data.panel, "show",
                    G_CALLBACK (on_panel_show), NULL);
  g_signal_connect (plugin_data.panel, "hide",
                    G_CALLBACK (on_panel_hide), NULL);
  g_signal_connect (plugin_data.panel, "key-press-event",
                    G_CALLBACK (on_panel_key_press_event), NULL);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (plugin_data.panel), frame);

  box = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), box);

  plugin_data.entry = gtk_entry_new ();
  g_signal_connect (plugin_data.entry, "notify::text",
                    G_CALLBACK (on_entry_text_notify), NULL);
  g_signal_connect (plugin_data.entry, "activate",
                    G_CALLBACK (on_entry_activate), NULL);
  gtk_box_pack_start (GTK_BOX (box), plugin_data.entry, FALSE, TRUE, 0);

    plugin_data.store = gtk_list_store_new (COL_COUNT,
                                          G_TYPE_STRING,
                                          G_TYPE_INT,
                                          G_TYPE_POINTER);
    fill_store(plugin_data.store);
  plugin_data.filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (plugin_data.store), NULL);
  GtkTreeModel *sort = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(plugin_data.filter));
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sort), COL_LINE, GTK_SORT_ASCENDING);
  scroll = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                         "hscrollbar-policy", GTK_POLICY_AUTOMATIC,
                         "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                         NULL);
  gtk_box_pack_start (GTK_BOX (box), scroll, TRUE, TRUE, 0);

  plugin_data.view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (plugin_data.filter));
  gtk_widget_set_can_focus (plugin_data.view, FALSE);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (plugin_data.view), FALSE);
  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  col = gtk_tree_view_column_new_with_attributes (NULL, cell,
                                                  "markup", COL_LABEL,
                                                  NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (plugin_data.view), col);

  gtk_container_add (GTK_CONTAINER (scroll), plugin_data.view);

  gtk_widget_show_all (frame);
}


static void
on_kb_show_panel (guint key_id)
{
  GeanyDocument *doc = document_get_current();
  if(DOC_VALID(doc) && doc->tm_file && doc->tm_file->tags_array->len > 0){
    create_panel();
    gtk_widget_show (plugin_data.panel);
  }
}

void
plugin_init (GeanyData *data)
{
  GeanyKeyGroup *group;

  group = plugin_set_key_group (geany_plugin, _("Jump Symbols"), KB_COUNT, NULL);
  keybindings_set_item (group, KB_SHOW_PANEL, on_kb_show_panel,
                        0, 0, "show_panel", _("Show Symbol List"), NULL);
}

void
plugin_cleanup (void)
{
  if (plugin_data.panel) {
    gtk_widget_destroy (plugin_data.panel);
  }
}
