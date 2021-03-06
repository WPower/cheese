
#include "cheese-config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gst/gst.h>
#include "cheese-widget.h"

static gboolean
delete_callback (GtkWidget *window,
                 GdkEvent  *event,
                 gpointer   data)
{
  gtk_widget_destroy (window);
  gtk_main_quit ();
  return FALSE;
}

int
main (int argc, char **argv)
{
  GtkWidget *window;
  GtkWidget *camera;

  g_thread_init (NULL);
  gdk_threads_init ();
  gst_init (&argc, &argv);

  bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  gtk_clutter_init (&argc, &argv);

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_default_size (GTK_WINDOW (window), 400, 300);
  g_signal_connect (G_OBJECT (window), "delete-event",
                    G_CALLBACK (delete_callback), NULL);


  camera = cheese_widget_new ();
  gtk_container_add (GTK_CONTAINER (window), camera);

  gtk_widget_show_all (window);

  gtk_main ();

  return 0;
}
