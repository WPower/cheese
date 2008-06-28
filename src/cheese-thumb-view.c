/*
 * Copyright (C) 2007,2008 daniel g. siegel <dgsiegel@gmail.com>
 * Copyright (C) 2007,2008 Jaap Haitsma <jaap@haitsma.org>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#ifdef HAVE_CONFIG_H
#include "cheese-config.h"
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <libgnomeui/libgnomeui.h>

#include "cheese-fileutil.h"
#include "eog-thumbnail.h"

#include "cheese-thumb-view.h"


#define CHEESE_THUMB_VIEW_GET_PRIVATE(o) \
	(G_TYPE_INSTANCE_GET_PRIVATE ((o), CHEESE_TYPE_THUMB_VIEW, CheeseThumbViewPrivate))

G_DEFINE_TYPE (CheeseThumbView, cheese_thumb_view, GTK_TYPE_ICON_VIEW);


typedef struct
{
  GtkListStore *store;
  CheeseFileUtil *fileutil;
  GFileMonitor *photo_file_monitor;
  GFileMonitor *video_file_monitor;
} CheeseThumbViewPrivate;

enum
{
  THUMBNAIL_PIXBUF_COLUMN,
  THUMBNAIL_URL_COLUMN
};

/* Drag 'n Drop */
enum 
{
  TARGET_PLAIN,
  TARGET_PLAIN_UTF8,
  TARGET_URILIST,
};

static GtkTargetEntry target_table[] = 
{
  { "text/uri-list", 0, TARGET_URILIST },
};

static void
cheese_thumb_view_append_item (CheeseThumbView *thumb_view, GFile *file)
{
  CheeseThumbViewPrivate* priv = CHEESE_THUMB_VIEW_GET_PRIVATE (thumb_view);
  GtkTreeIter iter;
  GdkPixbuf *pixbuf = NULL;
  GnomeThumbnailFactory *factory;
  GFileInfo *info;
  char *thumb_loc;
  GtkTreePath *path;
  GTimeVal mtime;
  const char *mime_type;
  char *uri;
  char *filename;

  info = g_file_query_info (file, "standard::content-type,time::modified", 0, NULL, NULL);

  if (!info)
  {
    g_warning ("Invalid filename\n");
    return;
  }
  g_file_info_get_modification_time (info, &mtime);
  mime_type = g_file_info_get_content_type (info);

  factory = gnome_thumbnail_factory_new (GNOME_THUMBNAIL_SIZE_NORMAL);
  uri = g_file_get_uri (file);
  filename = g_file_get_path (file);

  thumb_loc = gnome_thumbnail_factory_lookup (factory, uri, mtime.tv_sec);

  if (!thumb_loc)
  {
    pixbuf = gnome_thumbnail_factory_generate_thumbnail (factory, uri, mime_type);
    if (!pixbuf)
    {
      g_warning ("could not load %s (%s)\n", filename, mime_type);
      return;
    }
    gnome_thumbnail_factory_save_thumbnail (factory, pixbuf, uri, mtime.tv_sec);
  }
  else
  {
    pixbuf = gdk_pixbuf_new_from_file (thumb_loc, NULL);
    if (!pixbuf)
    {
      g_warning ("could not load %s (%s)\n", filename, mime_type);
      return;
    }
  }
  g_object_unref(info);
  g_object_unref (factory);
  g_free (thumb_loc);
  g_free (uri);

  eog_thumbnail_add_frame (&pixbuf);

  gtk_list_store_append (priv->store, &iter);
  gtk_list_store_set (priv->store, &iter, THUMBNAIL_PIXBUF_COLUMN,
                      pixbuf, THUMBNAIL_URL_COLUMN, filename, -1);
  g_free (filename);
  path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->store), &iter);
  gtk_icon_view_scroll_to_path (GTK_ICON_VIEW (thumb_view), path,
                                TRUE, 1.0, 0.5);

  g_object_unref (pixbuf);
}

static void
cheese_thumb_view_remove_item (CheeseThumbView *thumb_view, GFile *file)
{
  CheeseThumbViewPrivate* priv = CHEESE_THUMB_VIEW_GET_PRIVATE (thumb_view);
  char *path;
  GtkTreeIter iter;
  char *filename;

  filename = g_file_get_path (file);

  gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->store), &iter);
  /* check if the selected item is the first, else go through the store */
  gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &iter, THUMBNAIL_URL_COLUMN, &path, -1);
  if (g_ascii_strcasecmp (path, filename)) 
  {
    while (gtk_tree_model_iter_next (GTK_TREE_MODEL (priv->store), &iter))
    {
      gtk_tree_model_get (GTK_TREE_MODEL (priv->store), &iter, THUMBNAIL_URL_COLUMN, &path, -1);
      if (!g_ascii_strcasecmp (path, filename))
        break;
    }
  }
  g_free (path);
  g_free (filename);
  gboolean valid = gtk_list_store_remove (priv->store, &iter);
  if (valid)
  {
    GtkTreePath *tree_path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->store), &iter);
    gtk_icon_view_select_path (GTK_ICON_VIEW (thumb_view), tree_path);
    gtk_tree_path_free(tree_path);
  }
}

static void
cheese_thumb_view_monitor_cb (GFileMonitor      *file_monitor,
                              GFile             *file,
                              GFile             *other_file,
                              GFileMonitorEvent  event_type,
                              CheeseThumbView   *thumb_view)
{
  switch (event_type)
  {
    case G_FILE_MONITOR_EVENT_DELETED:
      cheese_thumb_view_remove_item (thumb_view, file);
      break;
    case G_FILE_MONITOR_EVENT_CREATED:
      cheese_thumb_view_append_item (thumb_view, file);
      break;
    default:
      break;
  }
}


static void
cheese_thumb_view_on_drag_data_get_cb (GtkIconView      *thumb_view,
                                       GdkDragContext   *drag_context,
                                       GtkSelectionData *data,
                                       guint             info,
                                       guint             time,
                                       gpointer          user_data)
{
  GList *list;
  GtkTreePath *tree_path = NULL;
  GtkTreeIter iter;
  GtkTreeModel *model;
  char *str;
  char *uris = NULL;
  char *tmp_str;
  gint i;

  list = gtk_icon_view_get_selected_items (thumb_view);
  model = gtk_icon_view_get_model (thumb_view);

  for (i = 0; i < g_list_length (list); i++)
  {
    tree_path = g_list_nth_data (list, i);
    gtk_tree_model_get_iter (model, &iter, tree_path);
    gtk_tree_model_get (model, &iter, 1, &str, -1);

    /* we always store local paths in the model, but DnD
     * needs URIs, so we must add file:// to the path.
     */

    /* build the "text/uri-list" string */
    if (uris) 
    {
      tmp_str = g_strconcat (uris, "file://", str, "\r\n", NULL);
      g_free (uris);
    } 
    else 
    { 
      tmp_str = g_strconcat ("file://", str, "\r\n", NULL);
    }
    uris = tmp_str;

    g_free (str);
  }
  gtk_selection_data_set (data, data->target, 8,
                          (guchar*) uris, strlen (uris));
  g_free (uris);
  g_list_free (list);
}


static char *
cheese_thumb_view_get_url_from_path (CheeseThumbView *thumb_view, GtkTreePath *path)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  char *file;

  model = gtk_icon_view_get_model (GTK_ICON_VIEW (thumb_view));
  gtk_tree_model_get_iter (model, &iter, path);

  gtk_tree_model_get (model, &iter, THUMBNAIL_URL_COLUMN, &file, -1);
			    
  return file;
}

char *
cheese_thumb_view_get_selected_image (CheeseThumbView *thumb_view)
{
  GList *list;
  char *filename = NULL;

  list = gtk_icon_view_get_selected_items (GTK_ICON_VIEW (thumb_view));
  if (list)
  {
    filename = cheese_thumb_view_get_url_from_path (thumb_view, (GtkTreePath *) list->data);
    g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
    g_list_free (list);
  }

  return filename;
}

GList *
cheese_thumb_view_get_selected_images_list (CheeseThumbView *thumb_view)
{
  GList *l, *item;
  GList *list = NULL;

  GtkTreePath *path;

  l = gtk_icon_view_get_selected_items (GTK_ICON_VIEW (thumb_view));

  for (item = l; item != NULL; item = item->next) 
  {
    path = (GtkTreePath *) item->data;
    list = g_list_prepend (list, cheese_thumb_view_get_url_from_path (thumb_view, path));
    gtk_tree_path_free (path);
  }

  g_list_free (l);
  list = g_list_reverse (list);

  return list;
}

static void
cheese_thumb_view_fill (CheeseThumbView *thumb_view)
{
  CheeseThumbViewPrivate* priv = CHEESE_THUMB_VIEW_GET_PRIVATE (thumb_view);
  GDir *dir_videos, *dir_photos;
  char *path_videos, *path_photos;
  const char *name;
  char *filename;
  GFile *file;

  gtk_list_store_clear (priv->store);

  path_videos = cheese_fileutil_get_video_path (priv->fileutil);
  path_photos = cheese_fileutil_get_photo_path (priv->fileutil);
  
  dir_videos = g_dir_open (path_videos, 0, NULL);
  dir_photos = g_dir_open (path_photos, 0, NULL);
  
  if (!dir_videos && !dir_photos)
    return;

  //read videos from the vid directory
  while ((name = g_dir_read_name (dir_videos)))
  {
    if (!(g_str_has_suffix (name, VIDEO_NAME_SUFFIX)))
      continue;
    
    filename = g_build_filename (path_videos, name, NULL);
    file = g_file_new_for_path (filename);
    cheese_thumb_view_append_item (thumb_view, file);
    g_free (filename);
    g_object_unref (file);
  }
  g_dir_close (dir_videos);
  
  //read photos from the photo directory
  while ((name = g_dir_read_name (dir_photos)))
  {
    if (!(g_str_has_suffix (name, PHOTO_NAME_SUFFIX)))
      continue;

    filename = g_build_filename (path_photos, name, NULL);
    file = g_file_new_for_path (filename);
    cheese_thumb_view_append_item (thumb_view, file);
    g_free (filename);
    g_object_unref (file);
  }
  g_dir_close (dir_photos);
}

static void
cheese_thumb_view_finalize (GObject *object)
{
  CheeseThumbView *thumb_view;

  thumb_view = CHEESE_THUMB_VIEW (object);
  CheeseThumbViewPrivate *priv = CHEESE_THUMB_VIEW_GET_PRIVATE (thumb_view);  

  g_object_unref (priv->store);
  g_object_unref (priv->fileutil);
  g_file_monitor_cancel (priv->photo_file_monitor);
  g_file_monitor_cancel (priv->video_file_monitor);

  G_OBJECT_CLASS (cheese_thumb_view_parent_class)->finalize (object);
}

static void
cheese_thumb_view_class_init (CheeseThumbViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  object_class->finalize = cheese_thumb_view_finalize;

  g_type_class_add_private (klass, sizeof (CheeseThumbViewPrivate));
}


static void
cheese_thumb_view_init (CheeseThumbView *thumb_view)
{
  CheeseThumbViewPrivate* priv = CHEESE_THUMB_VIEW_GET_PRIVATE (thumb_view);
  char *path_videos = NULL, *path_photos = NULL;
  
  GFile *file;  
  const int THUMB_VIEW_HEIGHT = 120;

  eog_thumbnail_init ();

  priv->store = gtk_list_store_new (2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
  
  priv->fileutil = cheese_fileutil_new ();

  gtk_icon_view_set_model (GTK_ICON_VIEW (thumb_view), GTK_TREE_MODEL (priv->store));

  gtk_widget_set_size_request (GTK_WIDGET (thumb_view), -1, THUMB_VIEW_HEIGHT);

  path_videos = cheese_fileutil_get_video_path (priv->fileutil);
  path_photos = cheese_fileutil_get_photo_path (priv->fileutil);
  
  g_mkdir_with_parents (path_videos, 0775);
  g_mkdir_with_parents (path_photos, 0775);

  //connect signal to video path
  file = g_file_new_for_path (path_videos);
  priv->video_file_monitor = g_file_monitor_directory (file, 0, NULL, NULL);
  g_signal_connect (priv->video_file_monitor, "changed", G_CALLBACK (cheese_thumb_view_monitor_cb), thumb_view);
  
  //if both paths are the same, make only one file monitor and point twice to the file monitor (photo_file_monitor = video_file_monitor)
  if (strcmp (path_videos, path_photos) != 0) {
    //connect signal to photo path
    file = g_file_new_for_path (path_photos);
    priv->photo_file_monitor = g_file_monitor_directory (file, 0, NULL, NULL);
    g_signal_connect (priv->photo_file_monitor, "changed", G_CALLBACK (cheese_thumb_view_monitor_cb), thumb_view);
  }
  else
  {
    priv->photo_file_monitor = priv->video_file_monitor;
  }
  
  gtk_icon_view_set_pixbuf_column (GTK_ICON_VIEW (thumb_view), 0);
#ifdef HILDON
  gtk_icon_view_set_columns (GTK_ICON_VIEW (thumb_view), -1);
#else
  gtk_icon_view_set_columns (GTK_ICON_VIEW (thumb_view), G_MAXINT);
#endif

  gtk_icon_view_enable_model_drag_source (GTK_ICON_VIEW (thumb_view), GDK_BUTTON1_MASK,
                                          target_table, G_N_ELEMENTS (target_table),
                                          GDK_ACTION_COPY);
  g_signal_connect (G_OBJECT (thumb_view), "drag-data-get",
                    G_CALLBACK (cheese_thumb_view_on_drag_data_get_cb), NULL);

  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE (priv->store),
                                       THUMBNAIL_URL_COLUMN, GTK_SORT_ASCENDING);

  cheese_thumb_view_fill (thumb_view);
}

GtkWidget * 
cheese_thumb_view_new ()
{
  CheeseThumbView *thumb_view;

  thumb_view = g_object_new (CHEESE_TYPE_THUMB_VIEW, NULL);  
  return GTK_WIDGET (thumb_view);
}
