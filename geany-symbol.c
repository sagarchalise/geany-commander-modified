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
 */
#include <tagmanager/tm_tagmanager.h>
#include <geanyplugin.h>


GeanyPlugin      *geany_plugin;
GeanyData        *geany_data;
GeanyFunctions   *geany_functions;

PLUGIN_VERSION_CHECK(205)

PLUGIN_SET_INFO ("Geany Symbol", "Provides a panel for quick access to tags in current document.", "0.1", "Sagar Chalise <chalisesagar@gmail.com>")


/* GTK compatibility functions/macros */

#if ! GTK_CHECK_VERSION (2, 18, 0)
# define gtk_widget_get_visible(w) \
  (GTK_WIDGET_VISIBLE (w))
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
  GtkTreeModel *sort;
  
  GtkTreePath  *last_path;
} plugin_data = {
  NULL, NULL, NULL,
  NULL, NULL,
  NULL
};


enum {
  COL_LABEL,
   COL_NAME,
  COL_LINE,
  //~ COL_TYPE,
  COL_COUNT
};
void gt_tag_print(TMTag *tag, FILE *fp)
{
	const char *type;
	if (!tag || !fp)
		return;
	if (tm_tag_file_t == tag->type)
	{
		fprintf(fp, "%s\n", tag->name);
		return;
	}
	type = tm_tag_type_name(tag);
	if (type)
		fprintf(fp, "Type: %s\n", type);
	if (tag->atts.entry.var_type)
		fprintf(fp, "Var Type: %s\n", tag->atts.entry.var_type);
	if (tag->atts.entry.scope)
		fprintf(fp, "Scope: %s\n", tag->atts.entry.scope);
	fprintf(fp, "Name: %s\n", tag->name);
	if (tag->atts.entry.arglist)
		fprintf(fp, "Arglist: %s", tag->atts.entry.arglist);
	if (tag->atts.entry.inheritance)
		fprintf(fp, "Inheritance : %s\n", tag->atts.entry.inheritance);
	if ((tag->atts.entry.file) && (tag->atts.entry.line > 0))
		fprintf(fp, "Line: %ld", tag->atts.entry.line);
	fprintf(fp, "\n");
}
void gt_tags_array_print(GPtrArray *tags, FILE *fp)
{
	guint i;
	TMTag *tag;
	if (!(tags && (tags->len > 0) && fp))
		return;
	for (i = 0; i < tags->len; ++i)
	{
		tag = TM_TAG(tags->pdata[i]);
		gt_tag_print(tag, fp);
	}
}
static inline gint
get_score (const gchar *needle,
           const gchar *haystack)
{
  if (needle == NULL || haystack == NULL ||
      *needle == '\0' || *haystack == '\0') {
    return 0;
  }

  return strcasestr(haystack, needle);
}

static gint
key_score (const gchar *key_,
           const gchar *text_)
{
  gchar  *text  = g_utf8_casefold (text_, -1);
  gchar  *key   = g_utf8_casefold (key_, -1);
  gint    score;
  
  score = get_score (key, text);
  
  g_free (text);
  g_free (key);
  
  return score;
}

static void jump_to_symbol(gchar *name, gboolean mark_all){
        GeanyDocument *doc = document_get_current();
        symbols_goto_tag(name, TRUE);
        editor_indicator_clear(doc->editor, GEANY_INDICATOR_SEARCH);
        if (mark_all)
            search_mark_all(doc, name, SCFIND_MATCHCASE | SCFIND_WHOLEWORD);
}
static gint
sort_func (GtkTreeModel  *model,
           GtkTreeIter   *a,
           GtkTreeIter   *b,
           gpointer       dummy)
{
  gint          scorea;
  gint          scoreb;
  gchar        *patha;
  gchar        *pathb;

  const gchar  *key   = gtk_entry_get_text (GTK_ENTRY (plugin_data.entry));
  
  
  gtk_tree_model_get (model, a, COL_NAME, &patha,  -1);
  gtk_tree_model_get (model, b, COL_NAME, &pathb, -1);
  
  scorea = key_score (key, patha);
  scoreb = key_score (key, pathb);
  
  g_free (patha);
  g_free (pathb);
  return scoreb - scorea;
}

static void
tree_view_set_cursor_from_iter (GtkTreeView *view,
                                GtkTreeIter *iter)
{
  GtkTreePath *path;
  
  path = gtk_tree_model_get_path (gtk_tree_view_get_model (view), iter);
  gtk_tree_view_set_cursor (view, path, NULL, FALSE);
  GtkTreeModel *model = gtk_tree_view_get_model (view);
    gchar *name;
        gtk_tree_model_get(model, iter, COL_NAME, &name, -1);
        jump_to_symbol(name, TRUE);
        g_free(name);
  gtk_tree_path_free (path);
}

static void
on_entry_text_notify (GObject    *object,
                      GParamSpec *pspec,
                      gpointer    dummy)
{
  GtkTreeIter   iter;
  GtkTreeView  *view  = GTK_TREE_VIEW (plugin_data.view);
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  
  /* we force re-sorting the whole model from how it was before, and the
   * back to the new filter.  this is somewhat hackish but since we don't
   * know the original sorting order, and GtkTreeSortable don't have a
   * resort() API anyway. */
  gtk_tree_model_sort_reset_default_sort_func (GTK_TREE_MODEL_SORT (model));
  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (model),
                                           sort_func, NULL, NULL);
  
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
  gtk_tree_view_get_cursor (view, &path, &column);
  gtk_tree_model_get_iter (model, &iter, path);
  gchar *name;
  gtk_tree_model_get(model, &iter, COL_NAME, &name, -1);
  editor_indicator_clear(doc->editor, GEANY_INDICATOR_SEARCH);
  sci_marker_delete_all(doc->editor->sci, 0);
  g_free(name);
  //jump_to_symbol(name, FALSE);
  if (path) {
    gtk_tree_view_row_activated (view, path, column);
    gtk_tree_path_free (path);
    //gtk_tree_iter_free(&iter);
    //g_free(name);
  }
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
        sci_marker_delete_all(old_doc->editor->sci, 0);
      gtk_widget_hide(widget);
      return TRUE;
    
    case GDK_KEY_Tab:
      /* avoid leaving the entry */
      return TRUE;
    
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
      tree_view_activate_focused_row (GTK_TREE_VIEW (plugin_data.view));
      gtk_widget_hide(widget);
      return TRUE;
    
    case GDK_KEY_Page_Up:
    case GDK_KEY_Page_Down:
      tree_view_move_focus (GTK_TREE_VIEW (plugin_data.view),
                            GTK_MOVEMENT_DISPLAY_LINES,
                            event->keyval == GDK_KEY_Page_Up ? -1 : 1);
      return TRUE;
    
    case GDK_KEY_Up:
    case GDK_KEY_Down: {
      tree_view_move_focus (GTK_TREE_VIEW (plugin_data.view),
                            GTK_MOVEMENT_DISPLAY_LINES,
                            event->keyval == GDK_KEY_Up ? -1 : 1);
      return TRUE;
    }
  }
  
  return FALSE;
}

static void
fill_store (GtkListStore *store)
{
  guint i;
  GeanyDocument *doc = document_get_current();
  //gt_symbol_print(sym, 0);
  //gt_tags_array_print(doc->tm_file->tags_array, stdout);
    
TMTag *tag;
    for (i = 0; i < doc->tm_file->tags_array->len; ++i)
	{
        const char *type;        
		tag = TM_TAG(doc->tm_file->tags_array->pdata[i]);
        type = tm_tag_type_name(tag);
        gchar *label = g_markup_printf_escaped ("<small><i>%s</i> %s</small>", type, tag->name);
        gtk_list_store_insert_with_values (store, NULL, -1,
                                       COL_LABEL, label,
                                       COL_NAME, tag->name,
                                       COL_LINE, tag->atts.entry.line,
                                       -1);
        g_free (label);
    }

}

static void
on_panel_show (GtkWidget *widget,
               gpointer   dummy)
{
  GtkTreeView *view = GTK_TREE_VIEW (plugin_data.view);
  gtk_widget_grab_focus (plugin_data.entry);
}

static void
on_panel_hide (GtkWidget *widget,
               gpointer   dummy)
{
  GtkTreeView  *view = GTK_TREE_VIEW (plugin_data.view);
  
  if (plugin_data.last_path) {
    gtk_tree_path_free (plugin_data.last_path);
    plugin_data.last_path = NULL;
  }
  gtk_tree_view_get_cursor (view, &plugin_data.last_path, NULL);
  
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
                                    "default-height", 200,
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
  //~ 
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
                                          G_TYPE_STRING,
                                          G_TYPE_ULONG);
                                          //G_TYPE_POINTER);
  fill_store(plugin_data.store);
  plugin_data.sort = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (plugin_data.store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (plugin_data.sort),
                                        COL_LINE,
                                        GTK_SORT_ASCENDING);
  
  scroll = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                         "hscrollbar-policy", GTK_POLICY_AUTOMATIC,
                         "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                         NULL);
  gtk_box_pack_start (GTK_BOX (box), scroll, TRUE, TRUE, 0);
  
  plugin_data.view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (plugin_data.sort));
  gtk_widget_set_can_focus (plugin_data.view, FALSE);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (plugin_data.view), FALSE);
  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  col = gtk_tree_view_column_new_with_attributes (NULL, cell,
                                                  "markup", COL_LABEL,
                                                  NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (plugin_data.view), col);
  //~ g_signal_connect (plugin_data.view, "row-activated",
                    //~ G_CALLBACK (on_view_row_activated), NULL);
  gtk_container_add (GTK_CONTAINER (scroll), plugin_data.view);
  
  gtk_widget_show_all (frame);
}

static void
on_kb_show_panel (guint key_id)
{
  GeanyDocument *doc = document_get_current();
  if(DOC_VALID(doc) && ((DOC_FILENAME(doc) != GEANY_STRING_UNTITLED)  && doc->has_tags)){
  create_panel();
  gtk_widget_show (plugin_data.panel);
  }
}

void
plugin_init (GeanyData *data)
{
  GeanyKeyGroup *group;
  
  group = plugin_set_key_group (geany_plugin, "Jump Symbols", KB_COUNT, NULL);
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