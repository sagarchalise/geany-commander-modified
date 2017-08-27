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
// gboolean cur_doc_tag = TRUE;

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
  GtkTreeModel *sort;
} plugin_data = {
  NULL, NULL,
  NULL, NULL,
  NULL, NULL
};
enum {
  COL_LABEL,
  COL_LINE,
  // COL_CUR_FILE,
  COL_TAG,
  COL_COUNT
};
gint get_score(const gchar *key, const gchar *name){
    gint score = 0; //no match

    if (key == NULL || name == NULL) {
        return score;
    }
    if(utils_str_equal(name, key)){
      score = 1;
    }
    else if (g_str_has_prefix(name, key)){
      score = 2;
    }
    else if (strstr(name, key) != NULL){
      score = 3;
    }
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
  GeanyDocument *cur_doc = document_get_current();
  if (tag->file->file_name != NULL && DOC_VALID(cur_doc)){
    if(utils_str_equal(tag->file->file_name, DOC_FILENAME(cur_doc))){
      jump_to_symbol(tag->name, (gint)tag->line);
      gtk_tree_path_free (path);
    }
  }
}
static gboolean
visible_func (GtkTreeModel *model,
              GtkTreeIter  *iter,
              gpointer      data)
{
  TMTag *tag;
  //atleast 2 chars  should  be  available for comparison
  guint16 key_length;
  GeanyDocument *cur_doc = document_get_current();
  gtk_tree_model_get (model, iter, COL_TAG, &tag, -1);
  const gchar  *key   = gtk_entry_get_text (GTK_ENTRY (plugin_data.entry));
  key_length = gtk_entry_get_text_length(GTK_ENTRY (plugin_data.entry));
  gboolean visible = TRUE;
  gint check = 1;
  gint score = -1;
  if (tag->file->file_name != NULL && DOC_VALID(cur_doc)){
      visible = utils_str_equal(tag->file->file_name, DOC_FILENAME(cur_doc));
  }
  if (g_str_has_prefix (key, "@")) {
    key += 1;
    check = 2;
    visible = !visible;
  }
  if(key_length > check && visible){
    score = get_score(key, tag->name);
  }
  return (score == 0)?FALSE:visible;
}

static gint
sort_func (GtkTreeModel  *model,
           GtkTreeIter   *a,
           GtkTreeIter   *b,
           gpointer       dummy)
{
  gint          scorea = 0;
  gint          scoreb = 0;
  TMTag *taga;
  TMTag *tagb;
  gtk_tree_model_get (model, a, COL_TAG, &taga, -1);
  gtk_tree_model_get (model, b, COL_TAG, &tagb, -1);
  const gchar  *key   = gtk_entry_get_text (GTK_ENTRY (plugin_data.entry));
  guint key_length = gtk_entry_get_text_length(GTK_ENTRY (plugin_data.entry));
  gint check = 1;
  if (g_str_has_prefix (key, "@")) {
    key += 1;
    check = 2;
  }
  if(key_length > check){
    scorea = get_score(key, taga->name);
    scoreb = get_score(key, tagb->name);
    msgwin_status_add("%d %d %s %s", scorea, scoreb, taga->name, tagb->name);
  }
  else{
    gtk_tree_model_get (model, a, COL_LINE, &scorea, -1);
    gtk_tree_model_get (model, b, COL_LINE, &scoreb, -1);
  }
  if(scorea == scoreb){
      return 0;
    }
    if(scorea > scoreb){
      return 1;
    }
    if(scorea < scoreb){
      return -1;
    }

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
  gtk_tree_model_sort_reset_default_sort_func (GTK_TREE_MODEL_SORT (model));
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
  if(utils_str_equal(tag->file->file_name, DOC_FILENAME(doc))){
    indicate_or_go_to_pos(doc->editor, tag->name, tag->line, FALSE);
  }
  else{
    GeanyDocument *new_doc = document_open_file(tag->file->file_name, FALSE, NULL, NULL);
    indicate_or_go_to_pos(new_doc->editor, tag->name, tag->line, FALSE);
  }
}
static void
on_entry_activate (GtkEntry  *entry,
                   gpointer   dummy)
{
  GtkTreeView  *view  = GTK_TREE_VIEW (plugin_data.view);
    tree_view_activate_focused_row (view);
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
		case tm_tag_macro_t: return "define";
		case tm_tag_macro_with_arg_t: return "macro";
		default: return "other";
	}
	return NULL;
}

/* utf8 */
gchar *get_project_rel_path(const gchar *utf8_parent, const gchar *utf8_descendant)
{
	GFile *gf_parent, *gf_descendant;
	gchar *locale_parent, *locale_descendant;
	gchar *locale_ret, *utf8_ret;
  GString *proj = g_string_new("Project -> ");

	locale_parent = utils_get_locale_from_utf8(utf8_parent);
	locale_descendant = utils_get_locale_from_utf8(utf8_descendant);
	gf_parent = g_file_new_for_path(locale_parent);
	gf_descendant = g_file_new_for_path(locale_descendant);

	locale_ret = g_file_get_relative_path(gf_parent, gf_descendant);
	g_string_append(proj, locale_ret);
  utf8_ret = utils_get_utf8_from_locale(proj->str);

	g_object_unref(gf_parent);
	g_object_unref(gf_descendant);
	g_free(locale_parent);
	g_free(locale_descendant);
	g_free(locale_ret);
  g_string_free(proj, TRUE);

	return utf8_ret;
}

static void
store_populate_tag_items_for_current_doc (GtkListStore  *store)
{
    GPtrArray *tags_array = geany_data->app->tm_workspace->tags_array;
    GeanyProject *project = geany_data->app->project;
    gchar *proj_path;
    if(project != NULL){
      proj_path = project->base_path;
    }
    if(tags_array->len > 0){
      GeanyDocument *doc = document_get_current();
    const gchar *taglabel;
    gint i;
  TMTag *tag;
    for (i = 0; i < tags_array->len; ++i)
	{
        tag = TM_TAG(tags_array->pdata[i]);
        if(!tag->file){
          continue;
        }
        if(tag->type == tm_tag_externvar_t || tag->type == tm_tag_prototype_t)
        continue;
        
        taglabel = get_tag_label(tag->type);
        if(taglabel == NULL){
          continue;
        }
        GeanyFiletype *ft = filetypes_detect_from_file(tag->file->file_name);
        switch(ft->id){
          case GEANY_FILETYPES_CMAKE:
          case GEANY_FILETYPES_CONF:
          case GEANY_FILETYPES_CSS:
          case GEANY_FILETYPES_DIFF:
          case GEANY_FILETYPES_HTML:
          case GEANY_FILETYPES_LATEX:
          case GEANY_FILETYPES_XML:
          case GEANY_FILETYPES_PO:
          case GEANY_FILETYPES_YAML:
            continue;
        }
      gchar *tag_path = (proj_path == NULL)?tag->file->short_name:get_project_rel_path(proj_path, tag->file->file_name);
         gchar *label = g_markup_printf_escaped ("<small>(<i>%s</i>)</small> %s [%ld]\n<small>%s</small>",
                                             taglabel,
                                             tag->name, tag->line,
                                             tag_path);
        gtk_list_store_insert_with_values (store, NULL, -1,
                                       COL_LABEL, label,
                                       COL_LINE, tag->line,
                                       // COL_CUR_FILE, utils_str_equal(tag->file->file_name, DOC_FILENAME(doc)),
                                       COL_TAG, tag,
                                       -1);
        g_free(label);
        g_free(tag_path);
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
                                    "default-width", 300,
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
                                          // G_TYPE_BOOLEAN,
                                          G_TYPE_POINTER);
    fill_store(plugin_data.store);
  plugin_data.sort = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(plugin_data.store));
  // gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (plugin_data.sort), COL_LINE, GTK_SORT_ASCENDING);
    gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (plugin_data.sort),
                                           sort_func, NULL, NULL);
  plugin_data.filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (plugin_data.sort), NULL);
  gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(plugin_data.filter), visible_func, NULL, NULL);

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
  
  GPtrArray *tags_array = geany_data->app->tm_workspace->tags_array;
  if(tags_array->len > 0){
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
