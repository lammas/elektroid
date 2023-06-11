/*
 *   audio.c
 *   Copyright (C) 2019 David García Goñi <dagargo@gmail.com>
 *
 *   This file is part of Elektroid.
 *
 *   Elektroid is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Elektroid is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Elektroid. If not, see <http://www.gnu.org/licenses/>.
 */

#include <math.h>
#include "audio.h"

void audio_init_int (struct audio *);
void audio_destroy_int (struct audio *);
const gchar *audio_name ();
const gchar *audio_version ();

static inline gint16
audio_mix_channels (gint16 ** src, guint channels, gdouble gain)
{
  gdouble mix = 0;
  for (gint i = 0; i < channels; i++, (*src)++)
    {
      mix += **src;
    }
  return (gint16) (mix * gain);
}

static inline void
audio_copy_sample (gint16 * dst, gint16 * src)
{
#if defined(ELEKTROID_RTAUDIO)
  *dst = (gint16) (*src * audio->volume);
#else
  *dst = *src;
#endif
}

//Access must be synchronized
void
audio_write_to_output_buffer (struct audio *audio, void *buffer, gint frames)
{
  gint16 *dst, *src;
  guint32 len =
    audio->sel_len ? audio->sel_start + audio->sel_len : audio->frames;
  guint channels = (((struct sample_info *) audio->control.data)->channels);
  guint bytes_per_frame = channels * sizeof (gint16);
  gdouble gain = 1.0 / sqrt (channels);

  debug_print (2, "Writing %d frames...\n", frames);

  memset (buffer, 0, frames << AUDIO_CHANNELS);

  if ((audio->pos == len && !audio->loop) ||
      audio->status == AUDIO_STATUS_PREPARING_PLAYBACK ||
      audio->status == AUDIO_STATUS_STOPPING_PLAYBACK)
    {
      memset (buffer, 0, frames << AUDIO_CHANNELS);
      if (audio->status == AUDIO_STATUS_PREPARING_PLAYBACK)
	{
	  audio->status = AUDIO_STATUS_PLAYING;
	}
      else			//Stopping...
	{
	  audio->release_frames += frames;
	}
      return;
    }

  dst = buffer;

  src = (gint16 *) & audio->sample->data[audio->pos * bytes_per_frame];
  for (gint i = 0; i < frames; i++)
    {
      if (audio->pos == len)
	{
	  if (!audio->loop)
	    {
	      break;
	    }
	  debug_print (2, "Sample reset\n");
	  audio->pos = audio->sel_len ? audio->sel_start : 0;
	  src = (gint16 *) audio->sample->data;
	}

      if (audio->mono_mix)
	{
	  gint16 mix = audio_mix_channels (&src, channels, gain);
	  audio_copy_sample (dst, &mix);
	  dst++;
	  audio_copy_sample (dst, &mix);
	  dst++;
	}
      else
	{
	  audio_copy_sample (dst, src);
	  src++;
	  dst++;
	  audio_copy_sample (dst, src);
	  src++;
	  dst++;
	}

      audio->pos++;
    }
}

void
audio_init (struct audio *audio,
	    void (*volume_change_callback) (gpointer, gdouble), gpointer data)
{
  debug_print (1, "Initializing audio (%s %s)...\n", audio_name (),
	       audio_version ());
  audio->sample = g_byte_array_new ();
  audio->frames = 0;
  audio->loop = FALSE;
  audio->path[0] = 0;
  audio->status = AUDIO_STATUS_STOPPED;
  audio->volume_change_callback = volume_change_callback;
  audio->volume_change_callback_data = data;
  audio->control.data = g_malloc (sizeof (struct sample_info));
  audio->control.callback = NULL;
  audio->sel_len = 0;

  audio_init_int (audio);
}

void
audio_destroy (struct audio *audio)
{
  debug_print (1, "Destroying audio...\n");

  audio_stop_playback (audio);
  audio_reset_sample (audio);

  g_mutex_lock (&audio->control.mutex);

  audio_destroy_int (audio);

  g_free (audio->control.data);
  g_byte_array_free (audio->sample, TRUE);
  audio->sample = NULL;

  g_mutex_unlock (&audio->control.mutex);
}

void
audio_reset_sample (struct audio *audio)
{
  g_mutex_lock (&audio->control.mutex);
  debug_print (1, "Resetting sample...\n");
  g_byte_array_set_size (audio->sample, 0);
  audio->frames = 0;
  audio->pos = 0;
  audio->path[0] = 0;
  audio->release_frames = 0;
  audio->src = AUDIO_SRC_NONE;
  audio->status = AUDIO_STATUS_STOPPED;
  memset (audio->control.data, 0, sizeof (struct sample_info));
  g_mutex_unlock (&audio->control.mutex);
}

void
audio_prepare (struct audio *audio, enum audio_status status)
{
  g_mutex_lock (&audio->control.mutex);
  audio->pos = audio->sel_len ? audio->sel_start : 0;
  audio->release_frames = 0;
  audio->status = status;
  g_mutex_unlock (&audio->control.mutex);
}
