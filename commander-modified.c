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



/* uncomment to display each row score (for debugging sort) */
/*#define DISPLAY_SCORE 1*/

GeanyPlugin      *geany_plugin;
GeanyData        *geany_data;
GeanyFunctions   *geany_functions;

PLUGIN_VERSION_CHECK(205)

PLUGIN_SET_TRANSLATABLE_INFO (
  LOCALEDIR, GETTEXT_PACKAGE,
  _("Commander Enhanced"),
  _("Provides a command panel for quick access to open docs and tags on docs"),
  "0.1",
  "Colomban Wendling <ban@herbesfolles.org>\nSagar Chalise<chalisesagar@gmail.com>"
)


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
  GtkTreeModel *filter;
  GtkTreePath  *last_path;
} plugin_data = {
  NULL, NULL, NULL,
  NULL, NULL,
  NULL
};

typedef enum {
  COL_TYPE_TAG = 1 << 0,
  COL_TYPE_DOC = 1 << 1,
  COL_TYPE_ANY = 0xffff
} ColType;

enum {
  COL_LABEL,
  COL_NAME,
  COL_TYPE,
  COL_TAGS,
  COL_DOCUMENT,
  COL_COUNT
};


#define PATH_SEPARATOR " \342\206\222 " /* right arrow */

#define SEPARATORS        " -_/\\\"'"
#define IS_SEPARATOR(c)   (strchr (SEPARATORS, (c)) != NULL)
#define next_separator(p) (strpbrk (p, SEPARATORS))

/* TODO: be more tolerant regarding unmatched character in the needle.
 * Right now, we implicitly accept unmatched characters at the end of the
 * needle but absolutely not at the start.  e.g. "xpy" won't match "python" at
 * all, though "pyx" will. */
//Taken from tag
// #define TAG_FREE(T)	g_slice_free(TMTag, (T))
// static void tm_tag_destroy(TMTag *tag)
// {
	// g_free(tag->name);
	// g_free(tag->arglist);
	// g_free(tag->scope);
	// g_free(tag->inheritance);
	// g_free(tag->var_type);
// }
// void tm_tag_unref(TMTag *tag)
// {
	// /* be NULL-proof because tm_tag_free() was NULL-proof and we indent to be a
	 // * drop-in replacment of it */
	// if (NULL != tag && g_atomic_int_dec_and_test(&tag->refcount))
	// {
		// tm_tag_destroy(tag);
		// TAG_FREE(tag);
	// }
// }
//Taken from tag
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

	//g_string_append_printf(buffer, " [%lu]", tag->line);

	return buffer->str;
}
void indicate_or_go_to_pos(GeanyEditor *editor, gchar *name, gint line, gboolean indicate){
    gint pos;
    struct Sci_TextToFind ttf;
    ttf.chrg.cpMin = sci_get_position_from_line(editor->sci, line-1);
    ttf.chrg.cpMax = sci_get_line_end_position(editor->sci, line);
    ttf.lpstrText = name;
    pos = sci_find_text(editor->sci, SCFIND_MATCHCASE | SCFIND_WHOLEWORD, &ttf);
    //sci_set_current_position(editor->sci, pos, TRUE);
    //ui_set_statusbar(TRUE, "%s pos: [%d] ttf: [%lu],[%lu],[%lu],[%lu]", ttf.lpstrText, pos, ttf.chrg.cpMin, ttf.chrg.cpMax, ttf.chrgText.cpMin, ttf.chrgText.cpMax);
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

static const gchar *get_tag_label(gint tag_type)
{
	gchar *tt_name;
	//const gchar *scope = tag->scope;
	static GString *buffer = NULL;	/* buffer will be small so we can keep it for reuse */
	//gboolean doc_is_utf8 = FALSE;

	switch(tag_type){
        case tm_tag_undef_t:
            tt_name = "undefined";
            break;
        case tm_tag_class_t:
            tt_name = "class";
            break;
        case tm_tag_enum_t:
            tt_name = "enum";
            break;
        case  tm_tag_enumerator_t:
            tt_name = "enumerator";
            break;
        case tm_tag_field_t:
            tt_name = "field";
            break;
        case tm_tag_function_t:
            tt_name = "function";
            break;
        case tm_tag_interface_t:
            tt_name = "interface";
            break;
        case tm_tag_member_t:
            tt_name = "member";
            break;
        case tm_tag_method_t:
            tt_name = "method";
            break;
        case tm_tag_namespace_t:
            tt_name = "namespace";
            break;
        case tm_tag_package_t:
            tt_name = "package";
            break;
        case tm_tag_prototype_t:
            tt_name = "prototype";
            break;
        case tm_tag_struct_t:
            tt_name = "struct";
            break;
        case tm_tag_typedef_t:
            tt_name = "typedef";
            break;
        case tm_tag_union_t:
            tt_name = "union";
            break;
        case tm_tag_variable_t:
            tt_name = "variable";
            break;
        case tm_tag_externvar_t:
            tt_name = "externvar";
            break;
        case tm_tag_macro_t:
            tt_name = "macro";
            break;
        case tm_tag_macro_with_arg_t:
            tt_name = "macro_w_args";
            break;
        case tm_tag_file_t:
            tt_name = "file";
            break;
        default:
            tt_name = "other";
    }

	if (! buffer)
		buffer = g_string_new(NULL);
	else
		g_string_truncate(buffer, 0);
    //tt_name = g_markup_printf_escaped();
    g_string_append(buffer, tt_name);
	//g_string_append_printf(buffer, "[%lu](%s)%s", tag->line, tt_name, tag->name);

	//g_string_append_printf(buffer, "(%s)->[%lu]", tag->line);

	return buffer->str;
}

// static inline gint
// get_score (const gchar *needle,
           // const gchar *haystack)
// {
  // if (! needle || ! haystack) {
    // return needle == NULL;
  // } else if (! *needle || ! *haystack) {
    // return *needle == 0;
  // }
  // if (IS_SEPARATOR (*haystack)) {
    // return get_score (needle + IS_SEPARATOR (*needle), haystack + 1);
  // }

  // if (IS_SEPARATOR (*needle)) {
    // return get_score (needle + 1, next_separator (haystack));
  // }

  // if (*needle == *haystack) {
    // gint a = get_score (needle + 1, haystack + 1) + 1 + IS_SEPARATOR (haystack[1]);
    // gint b = get_score (needle, next_separator (haystack));

    // return MAX (a, b);
  // } else {
    // return get_score (needle, next_separator (haystack));
  // }
// }
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
static const gchar *
path_basename (const gchar *path)
{
  const gchar *p1 = strrchr (path, '/');
  const gchar *p2 = g_strrstr (path, PATH_SEPARATOR);

  if (! p1 && ! p2) {
    return path;
  } else if (p1 > p2) {
    return p1;
  } else {
    return p2;
  }
}

// static gint
// key_score (const gchar *key_,
           // const gchar *text_, gint col_type)
// {
  // gchar  *text  = g_utf8_casefold (text_, -1);
  // gchar  *key   = g_utf8_casefold (key_, -1);
  // gint    score;
  // if(col_type == COL_TYPE_TAG){
    // score = (strcasestr(text, key) != NULL)?1:0;
  // }
  // else{
      // score = get_score (key, text) + get_score (key, path_basename (text)) / 2;
  // }

  // g_free (text);
  // g_free (key);

  // return score;
// }

static const gchar *
get_key (gint *type_)
{
  gint          type  = COL_TYPE_ANY;
  const gchar  *key   = gtk_entry_get_text (GTK_ENTRY (plugin_data.entry));

  if (g_str_has_prefix (key, "@")) {
    key += 1;
    type = COL_TYPE_DOC;
  } else if (g_str_has_prefix (key, "#")) {
    key += 1;
    type = COL_TYPE_TAG;
  }

  if (type_) {
    *type_ = type;
  }

  return key;
}

static void
tree_view_set_cursor_from_iter (GtkTreeView *view,
                                GtkTreeIter *iter)
{
  GtkTreePath *path;
    gint type;

  path = gtk_tree_model_get_path (gtk_tree_view_get_model (view), iter);
  gtk_tree_view_set_cursor (view, path, NULL, FALSE);
  GtkTreeModel *model = gtk_tree_view_get_model (view);
        gtk_tree_model_get(model, iter, COL_TYPE, &type, -1);
    if(type == COL_TYPE_TAG){
        TMTag *tag;
        gtk_tree_model_get(model, iter, COL_TAGS, &tag, -1);
        jump_to_symbol(tag->name, (gint)tag->line);

        //tm_tag_unref(tag);
        //g_free(tag);

    }
  gtk_tree_path_free (path);
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

      case GTK_MOVEMENT_PAGES:
        /* FIXME: move by page */
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
  gint type;
  gtk_tree_view_get_cursor (view, &path, &column);
  gtk_tree_model_get_iter (model, &iter, path);
    gtk_tree_model_get(model, &iter, COL_TYPE, &type, -1);
  if (path) {
    gtk_tree_view_row_activated (view, path, column);
    gtk_tree_path_free (path);
  }
    if(type == COL_TYPE_TAG){
          GeanyDocument *doc = document_get_current();
        TMTag *tag;
        gtk_tree_model_get(model, &iter, COL_TAGS, &tag, -1);
        editor_indicator_clear(doc->editor, GEANY_INDICATOR_SEARCH);
        keybindings_send_command(GEANY_KEY_GROUP_DOCUMENT, GEANY_KEYS_DOCUMENT_REMOVE_MARKERS);
        indicate_or_go_to_pos(doc->editor, tag->name, (gint)tag->line, FALSE);
        //tm_tag_unref(tag);
    }
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
         gchar *label = g_markup_printf_escaped ("<big>%s</big>\n"
                                             "<small><i>%s</i> at line %lu of <big>%s</big></small>",
                                             get_symbol_name(doc, tag),
                                             taglabel,
                                             tag->line,
                                            g_path_get_basename(DOC_FILENAME (doc)));
        //GdkPixbuf *icon;

        gtk_list_store_insert_with_values (store, NULL, -1,
                                       COL_LABEL, label,
                                       COL_NAME, tag->name,
                                       COL_TYPE, COL_TYPE_TAG,
                                       COL_DOCUMENT, doc,
                                       COL_TAGS, tag,
                                       -1);
        //printf("%s", label);
        g_free(label);
        //g_free(name);
    }
    //gtk_list_store_insert_before(store, plugin_data.tag_iter, NULL);
    }
}
/* utf8 */
gchar *get_project_base_path(void)
{
	GeanyProject *project = geany_data->app->project;

	if (project && !EMPTY(project->base_path))
	{
		if (g_path_is_absolute(project->base_path))
			return g_strdup(project->base_path);
		else
		{	/* build base_path out of project file name's dir and base_path */
			gchar *path;
			gchar *dir = g_path_get_dirname(project->file_name);

			if (utils_str_equal(project->base_path, "./"))
				return dir;

			path = g_build_filename(dir, project->base_path, NULL);
			g_free(dir);
			return path;
		}
	}
	return NULL;
}
// static void fill_project_files(GtkListStore *store, GeanyProject *project){
    // gchar *utf8_base_path = get_project_base_path();
// }


static void
fill_store (GtkListStore *store)
{
  guint       i;

  // /* tag items */
  GeanyDocument *doc = document_get_current();

  store_populate_tag_items_for_current_doc (store);


  /* open files */
  foreach_document (i) {
    if(documents[i]->id != doc->id){

    gchar *basename = g_path_get_basename (DOC_FILENAME (documents[i]));
    gchar *label = g_markup_printf_escaped ("<big>%s</big>\n"
                                            "<small><i>%s</i></small>",
                                            basename,
                                            DOC_FILENAME (documents[i]));

    gtk_list_store_insert_with_values (store, NULL, -1,
                                       COL_LABEL, label,
                                       COL_NAME, basename,
                                       COL_TYPE, COL_TYPE_DOC,
                                       COL_DOCUMENT, documents[i],
                                       COL_TAGS, documents[i]->tm_file->tags_array,
                                       -1);
    g_free (basename);
    g_free (label);
    }
  }
  //TODO: Use the concept for project as well
  // GeanyProject *project = geany_data->app->project;
  // if(project){
      // store_project_files(store, project);
  // }
}

static gboolean
visible_func (GtkTreeModel *model,
              GtkTreeIter  *iter,
              gpointer      data)
{
  //atleast 2 chars  should  be  available for comparison
  guint16 key_length;
  gchar *name;
  gint          type;
  const gchar  *key = get_key (&type);
  gtk_tree_model_get (model, iter, COL_NAME, &name, -1);
  key_length = gtk_entry_get_text_length(GTK_ENTRY (plugin_data.entry));
  gboolean visible = TRUE;

  if(key_length > 1){
        visible = get_score(key, name);
    // if (type == COL_TYPE_DOC){
        // gchar *search_name;
      // GPtrArray *tags_array;
      // GeanyDocument *new_doc;
        // //TMTag *tag;
        // guint count;
      // gtk_tree_model_get (model, iter, COL_TAGS, &tags_array, -1);
      // search_name = strchr(key, '#');
      // if (search_name != NULL){
          // search_name += 1;
          // tm_tags_find(tags_array, search_name, TRUE, &count);
      // }
      // GeanyDocument *old_doc = document_get_current();
      // if (count){
          // gtk_tree_model_get (model, iter, COL_DOCUMENT, &new_doc, -1);
          // navqueue_goto_line(old_doc, new_doc, count);
      // }
    //}
  }
  g_free(name);
  return visible;
}

// static gint
// sort_func (GtkTreeModel  *model,
           // GtkTreeIter   *a,
           // GtkTreeIter   *b,
           // gpointer       dummy)
// {
  // gint          scorea;
  // gint          scoreb;
  // gchar        *patha;
  // gchar        *patha;
  // TMTag        *taga;
  // TMTag        *tagb;
  // gint          typea;
  // gint          typeb;
  // gint          type;
  // const gchar  *key = get_key (&type);

  // gtk_tree_model_get (model, a, COL_PATH, &patha, COL_TYPE, &typea, COL_TAGS, , -1);
  // gtk_tree_model_get (model, b, COL_PATH, &pathb, COL_TYPE, &typeb, -1);

  // scorea = key_score (key, patha, typea);
  // scoreb = key_score (key, pathb, typeb);

  // if (! (typea & type)) {
    // scorea -= 0xf000;
  // }
  // if (! (typeb & type)) {
    // scoreb -= 0xf000;
  // }

  // g_free (patha);
  // g_free (pathb);

  // return scoreb - scorea;
// }

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
    gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(model));
    gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(model), visible_func, NULL, NULL);

  if (gtk_tree_model_get_iter_first (model, &iter)) {
    tree_view_set_cursor_from_iter (view, &iter);
  }
}

static void
on_entry_activate (GtkEntry  *entry,
                   gpointer   dummy)
{
  tree_view_activate_focused_row (GTK_TREE_VIEW (plugin_data.view));
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
on_panel_show (GtkWidget *widget,
               gpointer   dummy)
{
  GtkTreePath *path;
  GtkTreeView *view = GTK_TREE_VIEW (plugin_data.view);

  fill_store (plugin_data.store);

  gtk_widget_grab_focus (plugin_data.entry);

  if (plugin_data.last_path) {
    gtk_tree_view_set_cursor (view, plugin_data.last_path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell (view, plugin_data.last_path, NULL,
                                  TRUE, 0.5, 0.5);
  }
  /* make sure the cursor is set (e.g. if plugin_data.last_path wasn't valid) */
  gtk_tree_view_get_cursor (view, &path, NULL);
  if (path) {
    gtk_tree_path_free (path);
  } else {
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter_first (gtk_tree_view_get_model (view), &iter)) {
      tree_view_set_cursor_from_iter (GTK_TREE_VIEW (plugin_data.view), &iter);
    }
  }
}

static void
on_view_row_activated (GtkTreeView       *view,
                       GtkTreePath       *path,
                       GtkTreeViewColumn *column,
                       gpointer           dummy)
{
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  GtkTreeIter   iter;

  if (gtk_tree_model_get_iter (model, &iter, path)) {
    gint type;

    gtk_tree_model_get (model, &iter, COL_TYPE, &type, -1);

    GeanyDocument  *doc;
    switch (type) {
      case COL_TYPE_DOC: {
        gint            page;

        gtk_tree_model_get (model, &iter, COL_DOCUMENT, &doc, -1);
        page = document_get_notebook_page (doc);
        gtk_notebook_set_current_page (GTK_NOTEBOOK (geany_data->main_widgets->notebook),
                                       page);
        break;
      }
      // case COL_TYPE_TAG: {
        // TMTag  *tag;
        // gint            page;

        // gtk_tree_model_get (model, &iter, COL_TAGS, &tag, -1);
        // page = document_get_notebook_page (doc);
        // gtk_notebook_set_current_page (GTK_NOTEBOOK (geany_data->main_widgets->notebook),
                                       // page);
        // break;
      // }

    }
    gtk_widget_hide (plugin_data.panel);
  }
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
                                    "default-width", 500,
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
                                          G_TYPE_INT,
                                          G_TYPE_POINTER,
                                          G_TYPE_POINTER);
    fill_store(plugin_data.store);
  plugin_data.filter = gtk_tree_model_filter_new (GTK_TREE_MODEL (plugin_data.store), NULL);
  GtkTreeModel *sort = gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(plugin_data.filter));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (sort),
                                        COL_TYPE,
                                        GTK_SORT_ASCENDING);

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
  g_signal_connect (plugin_data.view, "row-activated",
                    G_CALLBACK (on_view_row_activated), NULL);
  gtk_container_add (GTK_CONTAINER (scroll), plugin_data.view);

  gtk_widget_show_all (frame);
}

static void
on_kb_show_panel (guint key_id)
{
  gtk_widget_show (plugin_data.panel);
}

static gboolean
on_plugin_idle_init (gpointer dummy)
{
  create_panel ();

  return FALSE;
}

void
plugin_init (GeanyData *data)
{
  GeanyKeyGroup *group;

  group = plugin_set_key_group (geany_plugin, "commander-enhanced", KB_COUNT, NULL);
  keybindings_set_item (group, KB_SHOW_PANEL, on_kb_show_panel,
                        0, 0, "show_panel", _("Show Command Panel"), NULL);

  /* delay for other plugins to have a chance to load before, so we will
   * include their items */
  plugin_idle_add (geany_plugin, on_plugin_idle_init, NULL);
}

void
plugin_cleanup (void)
{
  if (plugin_data.panel) {
    gtk_widget_destroy (plugin_data.panel);
  }
  if (plugin_data.last_path) {
    gtk_tree_path_free (plugin_data.last_path);
  }
}

// void
// plugin_help (void)
// {
  // utils_open_browser (DOCDIR "/" PLUGIN "/README");
// }
