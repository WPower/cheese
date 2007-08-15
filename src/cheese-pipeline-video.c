/*
 * Copyright (C) 2007 Copyright (C) 2007 daniel g. siegel <dgsiegel@gmail.com>
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

#include "cheese-config.h"

#include <gst/interfaces/xoverlay.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs.h>

#include "cheese.h"
#include "cheese-window.h"
#include "cheese-pipeline-video.h"
#include "cheese-effects-widget.h"
#include "cheese-fileutil.h"
#include "cheese-thumbnails.h"

G_DEFINE_TYPE(PipelineVideo, cheese_pipeline_video, G_TYPE_OBJECT)

static GObjectClass *parent_class = NULL;

typedef struct _PipelineVideoPrivate PipelineVideoPrivate;

struct _PipelineVideoPrivate
{
  gboolean lens_open;
  gchar *source_pipeline;
  gchar *used_effect;
  gchar *filename;

  GstElement *pipeline, *pipeline_rec;
  GstElement *ximagesink, *ximagesink_rec;
  GstElement *fakesink, *fakesink_rec;

  GstElement *source;
  GstElement *ffmpeg1, *ffmpeg2, *ffmpeg3;
  GstElement *ffmpeg1_rec, *ffmpeg2_rec, *ffmpeg3_rec;
  GstElement *tee, *tee_rec;
  GstElement *queuedisplay, *queuedisplay_rec, *queuemovie;
  GstElement *effect, *effect_rec;
  GstElement *audiosrc;
  GstElement *audioconvert;
  GstElement *vorbisenc;
  GstElement *filesink;
  GstElement *oggmux;
  GstElement *theoraenc;
  GstCaps *filter, *filter_rec;
};

#define PIPELINE_VIDEO_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PIPELINE_VIDEO_TYPE, PipelineVideoPrivate))

// private methods
static void cheese_pipeline_video_create_display(PipelineVideo *self);
static void cheese_pipeline_video_create_rec(PipelineVideo *self);

void
cheese_pipeline_video_finalize(GObject *object)
{
  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(object);
  gst_caps_unref(priv->filter);

  (*parent_class->finalize) (object);
  return;
}

PipelineVideo *
cheese_pipeline_video_new(void)
{
  PipelineVideo *self = g_object_new(PIPELINE_VIDEO_TYPE, NULL);

  return self;
}

void
cheese_pipeline_video_class_init(PipelineVideoClass *klass)
{
  GObjectClass *object_class;

  parent_class = g_type_class_peek_parent(klass);
  object_class = (GObjectClass*) klass;

  object_class->finalize = cheese_pipeline_video_finalize;
  g_type_class_add_private(klass, sizeof(PipelineVideoPrivate));

  G_OBJECT_CLASS(klass)->finalize = (GObjectFinalizeFunc) cheese_pipeline_video_finalize;
}

void
cheese_pipeline_video_init(PipelineVideo *self)
{
}

void
cheese_pipeline_video_set_play(PipelineVideo *self)
{
  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);
  if (priv->lens_open)
    gst_element_set_state(priv->pipeline_rec, GST_STATE_PLAYING);
  else
    gst_element_set_state(priv->pipeline, GST_STATE_PLAYING);
  return;
}

void
cheese_pipeline_video_set_stop(PipelineVideo *self)
{
  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);
  if (priv->lens_open)
    gst_element_set_state(priv->pipeline_rec, GST_STATE_NULL);
  else
    gst_element_set_state(priv->pipeline, GST_STATE_NULL);
  return;
}

void
cheese_pipeline_video_button_clicked(GtkWidget *widget, gpointer self)
{
  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);

  cheese_pipeline_video_set_stop(self);
  priv->lens_open = !priv->lens_open;
  if (priv->lens_open) {
    gtk_widget_set_sensitive(GTK_WIDGET(cheese_window.widgets.button_effects), FALSE);
    gtk_label_set_text_with_mnemonic(GTK_LABEL(cheese_window.widgets.label_take_photo), _("<b>_Stop recording</b>"));
    gtk_label_set_use_markup(GTK_LABEL(cheese_window.widgets.label_take_photo), TRUE);
    gtk_image_set_from_stock(GTK_IMAGE(cheese_window.widgets.image_take_photo), GTK_STOCK_MEDIA_STOP, GTK_ICON_SIZE_BUTTON);
  } else {
    gtk_widget_set_sensitive(GTK_WIDGET(cheese_window.widgets.button_effects), TRUE);
    gtk_label_set_text_with_mnemonic(GTK_LABEL(cheese_window.widgets.label_take_photo), _("<b>_Start recording</b>"));
    gtk_label_set_use_markup(GTK_LABEL(cheese_window.widgets.label_take_photo), TRUE);
    gtk_image_set_from_stock(GTK_IMAGE(cheese_window.widgets.image_take_photo), GTK_STOCK_MEDIA_RECORD, GTK_ICON_SIZE_BUTTON);

    g_print("Video saved: %s\n", priv->filename);
    cheese_thumbnails_append_item(priv->filename);
    priv->filename = cheese_fileutil_get_video_filename();

    g_object_set(priv->filesink, "location", priv->filename, NULL);

    // we have to create a new instance of oggmux, as
    // it is waiting for an EOS signal otherwise
    gst_element_unlink(priv->theoraenc, priv->oggmux);
    gst_element_unlink(priv->vorbisenc, priv->oggmux);
    gst_element_unlink(priv->oggmux, priv->filesink);

    gst_bin_remove(GST_BIN(priv->pipeline_rec), priv->oggmux);
    priv->oggmux = gst_element_factory_make("oggmux", "oggmux");
    gst_bin_add(GST_BIN(priv->pipeline_rec), priv->oggmux);

    gst_element_link(priv->theoraenc, priv->oggmux);
    gst_element_link(priv->vorbisenc, priv->oggmux);
    gst_element_link(priv->oggmux, priv->filesink);

  }

   // gst_element_set_state(priv->pipeline_rec, GST_STATE_READY);
  cheese_pipeline_video_set_play(self);
  return;
}

GstElement *
cheese_pipeline_video_get_ximagesink(PipelineVideo *self)
{
  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);
  return priv->ximagesink;
}

GstElement *
cheese_pipeline_video_get_fakesink(PipelineVideo *self)
{
  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);
  return priv->fakesink;
}

GstElement *
cheese_pipeline_video_get_pipeline(PipelineVideo *self)
{
  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);
  return priv->lens_open ? priv->pipeline_rec : priv->pipeline;
}

void
cheese_pipeline_video_change_effect(gchar *effect, gpointer self)
{
  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);

  if (effect != NULL) {
    cheese_pipeline_video_set_stop(PIPELINE_VIDEO(self));

    gst_element_unlink(priv->ffmpeg1, priv->effect);
    gst_element_unlink(priv->effect, priv->ffmpeg2);
    gst_bin_remove(GST_BIN(priv->pipeline), priv->effect);

    gst_element_unlink(priv->ffmpeg1_rec, priv->effect_rec);
    gst_element_unlink(priv->effect_rec, priv->ffmpeg2_rec);
    gst_bin_remove(GST_BIN(priv->pipeline_rec), priv->effect_rec);

    g_print("changing to effect: %s\n", effect);
    priv->effect = gst_parse_bin_from_description(effect, TRUE, NULL);
    priv->effect_rec = gst_parse_bin_from_description(effect, TRUE, NULL);

    gst_bin_add(GST_BIN(priv->pipeline_rec), priv->effect_rec);
    gst_bin_add(GST_BIN(priv->pipeline), priv->effect);

    gst_element_link(priv->ffmpeg1, priv->effect);
    gst_element_link(priv->effect, priv->ffmpeg2);

    gst_element_link(priv->ffmpeg1_rec, priv->effect_rec);
    gst_element_link(priv->effect_rec, priv->ffmpeg2_rec);

    cheese_pipeline_video_set_play(self);
    priv->used_effect = effect;
  }
}

void
cheese_pipeline_video_create(gchar *source_pipeline, PipelineVideo *self) {

  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);
  priv->source_pipeline = source_pipeline;
  cheese_pipeline_video_create_display(self);
  cheese_pipeline_video_create_rec(self);
}

static void
cheese_pipeline_video_create_display(PipelineVideo *self) {

  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);
  gboolean link_ok; 

  priv->pipeline = gst_pipeline_new("pipeline");
  priv->source = gst_parse_bin_from_description(priv->source_pipeline, TRUE, NULL);
  gst_bin_add(GST_BIN(priv->pipeline), priv->source);

  priv->ffmpeg1 = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace");
  gst_bin_add(GST_BIN(priv->pipeline), priv->ffmpeg1);

  priv->effect = gst_element_factory_make("identity", "effect");
  gst_bin_add(GST_BIN(priv->pipeline), priv->effect);

  priv->ffmpeg2 = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace2");
  gst_bin_add(GST_BIN(priv->pipeline), priv->ffmpeg2);

  priv->ffmpeg3 = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace3");
  gst_bin_add(GST_BIN(priv->pipeline), priv->ffmpeg3);

  priv->tee = gst_element_factory_make("tee", "tee");
  gst_bin_add(GST_BIN(priv->pipeline), priv->tee);

  priv->queuedisplay = gst_element_factory_make("queue", "queuedisplay");
  gst_bin_add(GST_BIN(priv->pipeline), priv->queuedisplay);

  priv->ximagesink = gst_element_factory_make("gconfvideosink", "gconfvideosink");
  gst_bin_add(GST_BIN(priv->pipeline), priv->ximagesink);

  gst_element_link(priv->source, priv->ffmpeg1);
  gst_element_link(priv->ffmpeg1, priv->effect);
  gst_element_link(priv->effect, priv->ffmpeg2);

  // theoraenc needs raw yuv data...
  priv->filter = gst_caps_new_simple("video/x-raw-yuv", NULL);
  link_ok = gst_element_link_filtered(priv->ffmpeg2, priv->tee, priv->filter);
  if (!link_ok) {
    g_warning("Failed to link elements!");
  }

  gst_element_link(priv->tee, priv->queuedisplay);
  gst_element_link(priv->queuedisplay, priv->ffmpeg3);

  gst_element_link(priv->ffmpeg3, priv->ximagesink);
}

static void
cheese_pipeline_video_create_rec(PipelineVideo *self) {

  PipelineVideoPrivate *priv = PIPELINE_VIDEO_GET_PRIVATE(self);
  gboolean link_ok;

  priv->pipeline_rec = gst_pipeline_new("pipeline");
  priv->source = gst_parse_bin_from_description(priv->source_pipeline, TRUE, NULL);
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->source);

  priv->ffmpeg1_rec = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace_rec");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->ffmpeg1_rec);

  priv->effect_rec = gst_element_factory_make("identity", "effect_rec");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->effect_rec);

  priv->ffmpeg2_rec = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace2_rec");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->ffmpeg2_rec);

  priv->ffmpeg3_rec = gst_element_factory_make("ffmpegcolorspace", "ffmpegcolorspace3_rec");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->ffmpeg3_rec);

  priv->tee_rec = gst_element_factory_make("tee", "tee_rec");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->tee_rec);

  priv->queuedisplay_rec = gst_element_factory_make("queue", "queuedisplay_rec");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->queuedisplay_rec);

  priv->ximagesink_rec = gst_element_factory_make("gconfvideosink", "gconfvideosink_rec");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->ximagesink_rec);

  /*
   * the pipeline looks like this:
   * v4l(2)src -> ffmpegcsp
   *                    '-> videoscale
   *                         '-> ffmpegcsp -> effects -> ffmpegcsp 
   *    -------------------------------------------------------'
   *    '--> tee (filtered) -> queue-> ffmpegcsp -> gconfvideosink
   *          |
   *       theoraenc
   *          |
   *       queuemovie -------,
   *                         |--------> mux -> filesink
   *                         |
   *                     vorbisenc
   *           audioconvert--^
   * gconfaudiosrc---^
   */

  gst_element_link(priv->source, priv->ffmpeg1_rec);
  gst_element_link(priv->ffmpeg1_rec, priv->effect_rec);
  gst_element_link(priv->effect_rec, priv->ffmpeg2_rec);
  //gst_element_link(priv->ffmpeg2, priv->tee);

  // theoraenc needs raw yuv data...
  priv->filter_rec = gst_caps_new_simple("video/x-raw-yuv", NULL);
  link_ok = gst_element_link_filtered(priv->ffmpeg2_rec, priv->tee_rec, priv->filter_rec);
  if (!link_ok) {
    g_warning("Failed to link elements!");
  }

  gst_element_link(priv->tee_rec, priv->queuedisplay_rec);
  gst_element_link(priv->queuedisplay_rec, priv->ffmpeg3_rec);

  gst_element_link(priv->ffmpeg3_rec, priv->ximagesink_rec);

  priv->audiosrc = gst_element_factory_make("gconfaudiosrc", "gconfaudiosrc");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->audiosrc);

  priv->audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->audioconvert);

  priv->vorbisenc = gst_element_factory_make("vorbisenc", "vorbisenc");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->vorbisenc);

  priv->oggmux = gst_element_factory_make("oggmux", "oggmux");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->oggmux);

  priv->filesink = gst_element_factory_make("filesink", "filesink");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->filesink);
  priv->filename = cheese_fileutil_get_video_filename();
  g_object_set(priv->filesink, "location", priv->filename, NULL);

  priv->theoraenc = gst_element_factory_make("theoraenc", "theoraenc");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->theoraenc);

  priv->queuemovie = gst_element_factory_make("queue", "queuemovie");
  gst_bin_add(GST_BIN(priv->pipeline_rec), priv->queuemovie);

  gst_element_link(priv->tee_rec, priv->queuemovie);
  gst_element_link(priv->queuemovie, priv->theoraenc);
  gst_element_link(priv->tee_rec, priv->theoraenc);

  gst_element_link(priv->theoraenc, priv->oggmux);

  priv->filter = gst_caps_new_simple("audio/x-raw-int",
      "channels", G_TYPE_INT, 2,
      "rate", G_TYPE_INT, 32000,
      "depth", G_TYPE_INT, 16, NULL);

  gst_element_link(priv->audiosrc, priv->audioconvert);
  //link_ok = gst_element_link_filtered(priv->audiosrc, priv->audioconvert, priv->filter);
  //link_ok = gst_element_link_filtered(priv->audioconvert, priv->vorbisenc, priv->filter);
  //if (!link_ok) {
  //  g_warning("Failed to link elements!");
  //}

  gst_element_link(priv->audioconvert, priv->vorbisenc);
  gst_element_link(priv->vorbisenc, priv->oggmux);
  gst_element_link(priv->oggmux, priv->filesink);
}