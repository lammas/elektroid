/*
 *   progress.c
 *   Copyright (C) 2023 David García Goñi <dagargo@gmail.com>
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

#include <glib/gi18n.h>
#include "progress.h"

#define MIN_TIME_UNTIL_DIALOG_RESPONSE 1e6

struct progress progress;

struct progress_progress_thread_data
{
  GThreadFunc f;
  gpointer data;
};

static gpointer
progress_join_thread ()
{
  gpointer output = NULL;

  debug_print (1, "Stopping SysEx thread...\n");
  if (progress.thread)
    {
      output = g_thread_join (progress.thread);
    }
  progress.thread = NULL;

  return output;
}

static void
progress_stop_running_sysex (GtkDialog *dialog, gint response_id,
			     gpointer data)
{
  if (response_id == GTK_RESPONSE_CANCEL)
    {
      gtk_label_set_text (GTK_LABEL (progress.label), _("Cancelling..."));
    }

  debug_print (1, "Stopping SysEx transfer...\n");
  g_mutex_lock (&progress.sysex_transfer.mutex);
  progress.sysex_transfer.active = FALSE;
  g_mutex_unlock (&progress.sysex_transfer.mutex);
}

void
progress_stop_thread ()
{
  progress_stop_running_sysex (NULL, 0, NULL);
  progress_join_thread ();
}

void
progress_set_fraction (gdouble fraction)
{
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (progress.bar), fraction);
}

gboolean
progress_is_active ()
{
  gboolean active;

  g_mutex_lock (&progress.sysex_transfer.mutex);
  active = progress.sysex_transfer.active;
  g_mutex_unlock (&progress.sysex_transfer.mutex);

  return active;
}

gboolean
progress_pulse (gpointer data)
{
  gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progress.bar));

  return progress_is_active ();
}

gboolean
progress_update (gpointer data)
{
  gchar *text;
  enum sysex_transfer_status status;

  g_mutex_lock (&progress.sysex_transfer.mutex);
  status = progress.sysex_transfer.status;
  g_mutex_unlock (&progress.sysex_transfer.mutex);

  switch (status)
    {
    case WAITING:
      text = _("Waiting...");
      break;
    case SENDING:
      text = _("Sending...");
      break;
    case RECEIVING:
      text = _("Receiving...");
      break;
    default:
      text = "";
    }
  gtk_label_set_text (GTK_LABEL (progress.label), text);

  return progress_pulse (NULL);
}

void
progress_init (GtkBuilder *builder)
{
  progress.dialog =
    GTK_DIALOG (gtk_builder_get_object (builder, "progress_dialog"));
  progress.bar =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_bar"));
  progress.label =
    GTK_WIDGET (gtk_builder_get_object (builder, "progress_label"));

  g_signal_connect (progress.dialog, "response",
		    G_CALLBACK (progress_stop_running_sysex), NULL);
}

static gboolean
elektroid_new_progress_thread_gsourcefunc (gpointer user_data)
{
  struct progress_progress_thread_data *data = user_data;
  debug_print (1, "Creating SysEx thread...\n");
  progress.thread = g_thread_new ("progress thread", data->f, data->data);
  g_free (data);
  return FALSE;
}

//Using this before a call to gtk_dialog_run ensures that the threads starts after the dialog is being run.
gpointer
progress_run (GThreadFunc f, gpointer user_data, const gchar *name,
	      const gchar *text, gint *res)
{
  gpointer v;
  gint dres;
  struct progress_progress_thread_data *data =
    g_malloc (sizeof (struct progress_progress_thread_data));
  data->f = f;
  data->data = user_data;
  g_idle_add (elektroid_new_progress_thread_gsourcefunc, data);

  progress.start = g_get_monotonic_time ();
  gtk_window_set_title (GTK_WINDOW (progress.dialog), name);
  gtk_label_set_text (GTK_LABEL (progress.label), text);

  dres = gtk_dialog_run (progress.dialog);
  if (res)
    {
      *res = dres;
    }
  //Without these lines below, the progress.label is not updated.
  //This happens because when the dialog is closed the gtk main thread is blocked
  //when joining the thread which ultimately causes pending widget updates to
  //not be performed.
  usleep (100000);
  while (gtk_events_pending ())
    {
      gtk_main_iteration ();
    }
  v = progress_join_thread ();
  gtk_widget_hide (GTK_WIDGET (progress.dialog));
  return v;
}

/**
 * This function guarantees that the time since start is at least the timeout.
 * This is needed when controlling a dialog from a thread because the dialog needs to be showed before the response is sent from the thread.
 */
static void
progress_usleep_since (gint64 timeout, gint64 start)
{
  gint64 diff = g_get_monotonic_time () - start;
  if (diff < timeout)
    {
      usleep (timeout - diff);
    }
}

void
progress_response (gint response)
{
  progress_usleep_since (MIN_TIME_UNTIL_DIALOG_RESPONSE, progress.start);
  gtk_dialog_response (GTK_DIALOG (progress.dialog), response);
}
