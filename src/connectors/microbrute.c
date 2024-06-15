/*
 *   microbrute.c
 *   Copyright (C) 2022 David García Goñi <dagargo@gmail.com>
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

#include "microbrute.h"
#include "common.h"

#define MICROBRUTE_MAX_SEQ_STR_LEN 256

#define MICROBRUTE_MAX_SEQS 8

#define MICROBRUTE_SEQ_REQ_COUNTER_POS 6
#define MICROBRUTE_SEQ_REQ_ID_POS 9
#define MICROBRUTE_SEQ_REQ_OFFSET_POS 10
#define MICROBRUTE_SEQ_RPLY_LEN_POS 11
#define MICROBRUTE_SEQ_RPLY_DATA_POS 12
#define MICROBRUTE_SEQ_TXT_POS 2

#define MICROBRUTE_SYSEX_RX_CHANNEL 0x5
#define MICROBRUTE_SYSEX_TX_CHANNEL 0x7
#define MICROBRUTE_SYSEX_NOTE_PRIORITY 0xB
#define MICROBRUTE_SYSEX_ENVELOPE_LEGATO 0xD
#define MICROBRUTE_SYSEX_LFO_KEY_RETRIGGER 0xF
#define MICROBRUTE_SYSEX_VEL_RESPONSE 0x11
#define MICROBRUTE_SYSEX_STEP_ON 0x2A
#define MICROBRUTE_SYSEX_BEND_RANGE 0x2C
#define MICROBRUTE_SYSEX_PLAY_ON 0x2E
#define MICROBRUTE_SYSEX_NEXT_SEQUENCE 0x32
#define MICROBRUTE_SYSEX_RETRIGGERING 0x34
#define MICROBRUTE_SYSEX_GATE_LENGTH 0x36
#define MICROBRUTE_SYSEX_STEP_LENGTH 0x38
#define MICROBRUTE_SYSEX_SYNC 0x3C

#define MICROBRUTE_SYSEX_CALIB_PB_CENTER 0x21
#define MICROBRUTE_SYSEX_CALIB_BOTH_BOTTOM 0x22
#define MICROBRUTE_SYSEX_CALIB_BOTH_TOP 0x23
#define MICROBRUTE_SYSEX_CALIB_END 0x24

#define MICROBRUTE_CTL_RX_CHANNEL 102
#define MICROBRUTE_CTL_TX_CHANNEL 103
#define MICROBRUTE_CTL_NOTE_PRIORITY 111
#define MICROBRUTE_CTL_ENVELOPE_LEGATO 109
#define MICROBRUTE_CTL_LFO_KEY_RETRIGGER 110
#define MICROBRUTE_CTL_VEL_RESPONSE 112
#define MICROBRUTE_CTL_STEP_ON 114
//Setting the bend range is performed with a RPN
#define MICROBRUTE_CTL_PLAY_ON 105
#define MICROBRUTE_CTL_NEXT_SEQUENCE 106
#define MICROBRUTE_CTL_RETRIGGERING 104
#define MICROBRUTE_CTL_GATE_LENGTH 113
#define MICROBRUTE_CTL_STEP_LENGTH 107
#define MICROBRUTE_CTL_SYNC 108

#define MICROBRUTE_NOP 0xff

static const guint8 ARTURIA_ID[] = { 0x0, 0x20, 0x6b };
static const guint8 FAMILY_ID[] = { 0x4, 0x0 };
static const guint8 MODEL_ID[] = { 0x2, 0x1 };

static const guint8 MICROBRUTE_SEQ_REQ[] =
  { 0xf0, 0x0, 0x20, 0x6b, 0x5, 0x1, 0x0, 0x03, 0x3B, 0x0, 0x0, 0x20, 0xf7 };

static const guint8 MICROBRUTE_SEQ_MSG[] =
  { 0xf0, 0x0, 0x20, 0x6b, 0x05, 0x01, 0x0, 0x23, 0x3a, 0x0, 0x0, 0x20,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
  0x0, 0x0, 0xf7
};

static const guint8 MICROBRUTE_GET_PARAM_MSG[] =
  { 0xf0, 0x0, 0x20, 0x6b, 0x5, 0x1, 0, 0, 0, 0xf7 };

static const guint8 MICROBRUTE_SET_PARAM_MSG[] =
  { 0xf0, 0x0, 0x20, 0x6b, 0x5, 0x1, 0, 0, 0, 0, 0xf7 };

enum microbrute_fs
{
  FS_MICROBRUTE_SEQUENCE
};

struct microbrute_int_param
{
  guint8 sysex;
  guint8 ctl;
    guint8 (*value_map) (guint8);
};

static guint8
microbrute_map_plus_one (guint8 value)
{
  return value + 1;
}

static guint8
microbrute_map_proportional_3 (guint8 value)
{
  return value * 42;
}

static guint8
microbrute_map_proportional_2 (guint8 value)
{
  return value * 64;
}

static guint8
microbrute_map_step_length (guint8 value)
{
  switch (value)
    {
    case 4:
      return 0;
    case 8:
      return 30;
    case 16:
      return 60;
    case 32:
      return 90;
    default:
      return 0;
    }
}

static guint8
microbrute_map_special (guint8 value)
{
  switch (value)
    {
    case 0:
      return 0;
    case 1:
      return 43;
    case 2:
      return 87;
    default:
      return 0;
    }
}

static const struct microbrute_int_param MICROBRUTE_PARAMS[] = {
  {MICROBRUTE_SYSEX_NOTE_PRIORITY, MICROBRUTE_CTL_NOTE_PRIORITY,
   microbrute_map_special},
  {MICROBRUTE_SYSEX_VEL_RESPONSE, MICROBRUTE_CTL_VEL_RESPONSE,
   microbrute_map_special},
  {MICROBRUTE_SYSEX_LFO_KEY_RETRIGGER, MICROBRUTE_CTL_LFO_KEY_RETRIGGER,
   microbrute_map_proportional_2},
  {MICROBRUTE_SYSEX_ENVELOPE_LEGATO, MICROBRUTE_CTL_ENVELOPE_LEGATO,
   microbrute_map_proportional_2},
  {MICROBRUTE_SYSEX_BEND_RANGE, MICROBRUTE_NOP, NULL},	//This uses a NRPN instead of a controller
  {MICROBRUTE_SYSEX_GATE_LENGTH, MICROBRUTE_CTL_GATE_LENGTH,
   microbrute_map_proportional_3},
  {MICROBRUTE_SYSEX_SYNC, MICROBRUTE_CTL_SYNC, microbrute_map_special},
  {MICROBRUTE_SYSEX_TX_CHANNEL, MICROBRUTE_CTL_TX_CHANNEL,
   microbrute_map_plus_one},
  {MICROBRUTE_SYSEX_RX_CHANNEL, MICROBRUTE_CTL_RX_CHANNEL,
   microbrute_map_plus_one},
  {MICROBRUTE_SYSEX_RETRIGGERING, MICROBRUTE_CTL_RETRIGGERING,
   microbrute_map_special},
  {MICROBRUTE_SYSEX_PLAY_ON, MICROBRUTE_CTL_PLAY_ON,
   microbrute_map_proportional_2},
  {MICROBRUTE_SYSEX_NEXT_SEQUENCE, MICROBRUTE_CTL_NEXT_SEQUENCE,
   microbrute_map_special},
  {MICROBRUTE_SYSEX_STEP_ON, MICROBRUTE_CTL_STEP_ON,
   microbrute_map_proportional_2},
  {MICROBRUTE_SYSEX_STEP_LENGTH, MICROBRUTE_CTL_STEP_LENGTH,
   microbrute_map_step_length},
  {MICROBRUTE_SYSEX_CALIB_PB_CENTER, MICROBRUTE_NOP, NULL},
  {MICROBRUTE_SYSEX_CALIB_BOTH_BOTTOM, MICROBRUTE_NOP, NULL},
  {MICROBRUTE_SYSEX_CALIB_BOTH_TOP, MICROBRUTE_NOP, NULL},
  {MICROBRUTE_SYSEX_CALIB_END, MICROBRUTE_NOP, NULL}
};

static guint8
microbrute_get_counter (struct backend *backend)
{
  guint8 *seq = backend->data;
  guint8 value = *seq;
  (*seq)++;
  if (*seq == 0x80)
    {
      *seq = 0;
    }
  return value;
}

static gchar *
microbrute_get_download_path (struct backend *backend,
			      const struct fs_operations *ops,
			      const gchar *dst_dir, const gchar *src_path,
			      struct idata *sequence)
{
  guint id;
  common_slot_get_id_name_from_path (src_path, &id, NULL);
  return common_get_download_path_with_params (backend, ops, dst_dir,
					       id, 1, NULL);
}

static gint
microbrute_read_dir (struct backend *backend, struct item_iterator *iter,
		     const gchar *dir, GSList *extensions)
{
  struct common_simple_read_dir_data *data;

  if (strcmp (dir, "/"))
    {
      return -ENOTDIR;
    }

  data = g_malloc (sizeof (struct common_simple_read_dir_data));
  data->next = 1;
  data->max = MICROBRUTE_MAX_SEQS + 1;

  init_item_iterator (iter, dir, data, common_simple_next_dentry, g_free);

  return 0;
}

static GByteArray *
microbrute_get_sequence_request_msg (struct backend *backend, guint8 id,
				     guint8 offset)
{
  guint8 counter = microbrute_get_counter (backend);
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (MICROBRUTE_SEQ_REQ));
  g_byte_array_append (tx_msg, MICROBRUTE_SEQ_REQ,
		       sizeof (MICROBRUTE_SEQ_REQ));
  tx_msg->data[MICROBRUTE_SEQ_REQ_COUNTER_POS] = counter;
  tx_msg->data[MICROBRUTE_SEQ_REQ_ID_POS] = id;
  tx_msg->data[MICROBRUTE_SEQ_REQ_OFFSET_POS] = offset;
  return tx_msg;
}

static gint
microbrute_download_seq_data (struct backend *backend, guint seqnum,
			      guint offset, GByteArray *sequence)
{
  GByteArray *tx_msg, *rx_msg;
  gchar aux[LABEL_MAX];
  guint8 *step;

  if (!offset)
    {
      snprintf (aux, LABEL_MAX, "%1d:", seqnum + 1);
      g_byte_array_append (sequence, (guint8 *) aux, strlen (aux));
    }

  tx_msg = microbrute_get_sequence_request_msg (backend, seqnum, offset);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  step = &rx_msg->data[MICROBRUTE_SEQ_RPLY_DATA_POS];
  while (*step && *step != 0xf7)
    {
      if (*step == 0x7f)
	{
	  g_byte_array_append (sequence, (guint8 *) " x", 2);
	}
      else
	{
	  snprintf (aux, LABEL_MAX, " %02d", *step);
	  g_byte_array_append (sequence, (guint8 *) aux, strlen (aux));
	}
      step++;
    }

  free_msg (rx_msg);

  return 0;
}

static gint
microbrute_download (struct backend *backend, const gchar *src_path,
		     struct idata *sequence, struct job_control *control)
{
  gint err;
  guint seqnum;
  gboolean active;
  GByteArray *data;

  err = common_slot_get_id_name_from_path (src_path, &seqnum, NULL);
  if (err)
    {
      return err;
    }

  seqnum--;
  if (seqnum >= MICROBRUTE_MAX_SEQS)
    {
      return -EINVAL;
    }

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  data = g_byte_array_new ();
  err = microbrute_download_seq_data (backend, seqnum, 0, data);
  if (err)
    {
      goto err;
    }

  set_job_control_progress (control, 0.5);

  err = microbrute_download_seq_data (backend, seqnum, 0x20, data);
  if (err)
    {
      goto err;
    }

  g_mutex_lock (&control->mutex);
  active = control->active;
  g_mutex_unlock (&control->mutex);
  if (!active)
    {
      err = -ECANCELED;
      goto err;
    }

  set_job_control_progress (control, 1.0);
  sequence->content = data;

  return 0;

err:
  g_byte_array_free (data, TRUE);
  return err;
}

static GByteArray *
microbrute_set_sequence_request_msg (struct backend *backend, guint8 id,
				     guint8 offset)
{
  guint8 counter = microbrute_get_counter (backend);
  GByteArray *tx_msg = g_byte_array_sized_new (sizeof (MICROBRUTE_SEQ_MSG));
  g_byte_array_append (tx_msg, MICROBRUTE_SEQ_MSG,
		       sizeof (MICROBRUTE_SEQ_MSG));
  tx_msg->data[MICROBRUTE_SEQ_REQ_COUNTER_POS] = counter;
  tx_msg->data[MICROBRUTE_SEQ_REQ_ID_POS] = id;
  tx_msg->data[MICROBRUTE_SEQ_REQ_OFFSET_POS] = offset;
  return tx_msg;
}

static gint
microbrute_send_seq_msg (struct backend *backend, guint8 seqnum,
			 guint8 offset, gchar **tokens, gint *pos, gint total)
{
  struct sysex_transfer transfer;
  guint8 steps = 0;
  gchar *token = *tokens;
  gint err;
  guint8 *step;

  transfer.raw = microbrute_set_sequence_request_msg (backend, seqnum,
						      offset);

  step = &transfer.raw->data[MICROBRUTE_SEQ_RPLY_DATA_POS];
  while (steps < 32 && *pos < total)
    {
      if (*token < 0x20)
	{
	  error_print ("Invalid character\n");
	  token++;
	  (*pos)++;
	  continue;
	}
      else if (*token == ' ')
	{
	  token++;
	  (*pos)++;
	  continue;
	}
      else if (token[0] == '0' && token[1] != ' ')
	{
	  token++;
	  (*pos)++;
	  continue;
	}
      else if (*token == 'x' || *token == 'X')
	{
	  *step = 0x7f;
	  token++;
	  (*pos)++;
	  debug_print (2, "Note: -\n");
	}
      else
	{
	  gchar *rem;
	  glong note = strtol (token, &rem, 10);
	  *step = note >= 0x7f ? 0x7f : note;
	  *step = *step < 12 ? 0x7f : *step;
	  if (*step == 0 && token == rem)
	    {
	      error_print ("Error while reading note\n");
	      token++;
	      (*pos)++;
	      continue;
	    }
	  token = rem;
	  *pos += (*step >= 100) ? 3 : (*step >= 10) ? 2 : 1;
	  debug_print (2, "Note: 0x%02x (%d)\n", *step, *step);
	}
      steps++;
      step++;
    }
  transfer.raw->data[MICROBRUTE_SEQ_RPLY_LEN_POS] = steps;

  //This doesn't need synchronized access as the caller provices this already.
  err = backend_tx_sysex (backend, &transfer);
  free_msg (transfer.raw);

  *tokens = token;

  return err < 0 ? err : steps;
}

static gint
microbrute_upload (struct backend *backend, const gchar *path,
		   struct idata *sequence, struct job_control *control)
{
  GByteArray *input = sequence->content;
  gchar *token = (gchar *) & input->data[MICROBRUTE_SEQ_TXT_POS];
  gint pos = MICROBRUTE_SEQ_TXT_POS;
  guint seqnum;
  gint steps, err;

  err = common_slot_get_id_name_from_path (path, &seqnum, NULL);
  if (err)
    {
      return err;
    }

  seqnum--;
  if (seqnum >= MICROBRUTE_MAX_SEQS)
    {
      return -EINVAL;
    }

  g_mutex_lock (&backend->mutex);

  control->parts = 1;
  control->part = 0;
  set_job_control_progress (control, 0.0);

  steps = microbrute_send_seq_msg (backend, seqnum, 0, &token, &pos,
				   input->len);
  if (steps < 0)
    {
      goto end;
    }
  else if (pos < input->len)
    {
      set_job_control_progress (control, 0.5);
      steps = microbrute_send_seq_msg (backend, seqnum, 0x20, &token, &pos,
				       input->len);
      if (steps < 0)
	{
	  goto end;
	}
    }

  set_job_control_progress (control, 1.0);

end:
  g_mutex_unlock (&backend->mutex);
  return steps < 0 ? steps : 0;
}

static const struct fs_operations FS_MICROBRUTE_OPERATIONS = {
  .id = FS_MICROBRUTE_SEQUENCE,
  .options = FS_OPTION_SINGLE_OP | FS_OPTION_ID_AS_FILENAME |
    FS_OPTION_SLOT_STORAGE | FS_OPTION_SORT_BY_NAME,
  .name = "sequence",
  .gui_name = "Sequences",
  .gui_icon = BE_FILE_ICON_SEQ,
  .ext = "mbseq",
  .readdir = microbrute_read_dir,
  .print_item = common_print_item,
  .download = microbrute_download,
  .upload = microbrute_upload,
  .load = file_load,
  .save = save_file,
  .get_upload_path = common_slot_get_upload_path,
  .get_download_path = microbrute_get_download_path
};

gint
microbrute_handshake (struct backend *backend)
{
  guint8 *seq;

  if (memcmp (backend->midi_info.company, ARTURIA_ID, sizeof (ARTURIA_ID)) ||
      memcmp (backend->midi_info.family, FAMILY_ID, sizeof (FAMILY_ID)) ||
      memcmp (backend->midi_info.model, MODEL_ID, sizeof (MODEL_ID)))
    {
      return -ENODEV;
    }

  seq = g_malloc (sizeof (guint8));
  *seq = 0;

  backend_fill_fs_ops (backend, &FS_MICROBRUTE_OPERATIONS, NULL);
  backend->destroy_data = backend_destroy_data;
  backend->data = seq;

  snprintf (backend->name, LABEL_MAX, "Arturia MicroBrute");

  return 0;
}

static GByteArray *
microbrute_get_parameter_msg (struct backend *backend, guint8 param)
{
  GByteArray *tx_msg;
  guint8 counter = microbrute_get_counter (backend);

  tx_msg = g_byte_array_sized_new (sizeof (MICROBRUTE_GET_PARAM_MSG));
  g_byte_array_append (tx_msg, MICROBRUTE_GET_PARAM_MSG,
		       sizeof (MICROBRUTE_GET_PARAM_MSG));
  tx_msg->data[6] = counter;
  tx_msg->data[8] = param;
  return tx_msg;
}

gint
microbrute_get_parameter (struct backend *backend,
			  enum microbrute_param param, guint8 *value)
{
  GByteArray *tx_msg, *rx_msg;
  guint8 *seq = backend->data;
  guint8 counter = *seq;
  guint8 op = MICROBRUTE_PARAMS[param].sysex;

  tx_msg = microbrute_get_parameter_msg (backend, op + 1);
  rx_msg = backend_tx_and_rx_sysex (backend, tx_msg, -1);
  if (!rx_msg)
    {
      return -EIO;
    }

  if (rx_msg->data[6] != counter)
    {
      error_print ("Bad sequence number byte\n");
      return -EIO;
    }
  if (rx_msg->data[7] != 1)
    {
      error_print ("Bad client byte\n");
      return -EIO;
    }
  if (rx_msg->data[8] != op)
    {
      error_print ("Bad parameter byte\n");
      return -EIO;
    }

  *value = rx_msg->data[9];
  free_msg (rx_msg);
  return 0;
}

static GByteArray *
microbrute_set_parameter_msg (struct backend *backend, guint8 param,
			      guint8 value)
{
  guint8 counter = microbrute_get_counter (backend);
  GByteArray *tx_msg =
    g_byte_array_sized_new (sizeof (MICROBRUTE_SET_PARAM_MSG));
  g_byte_array_append (tx_msg, MICROBRUTE_SET_PARAM_MSG,
		       sizeof (MICROBRUTE_SET_PARAM_MSG));
  tx_msg->data[6] = counter;
  tx_msg->data[7] = 1;
  tx_msg->data[8] = MICROBRUTE_PARAMS[param].sysex;
  tx_msg->data[9] = value;
  return tx_msg;
}

gint
microbrute_set_parameter (struct backend *backend,
			  enum microbrute_param param, guint8 value,
			  guint8 channel, gboolean sysex)
{
  gint err;
  if (sysex)
    {
      struct sysex_transfer transfer;
      transfer.raw = microbrute_set_parameter_msg (backend, param, value);
      err = backend_tx_sysex (backend, &transfer);
      free_msg (transfer.raw);
    }
  else
    {
      if (MICROBRUTE_PARAMS[param].ctl == MICROBRUTE_NOP ||
	  !MICROBRUTE_PARAMS[param].value_map)
	{
	  error_print ("Bad parameter\n");
	  return -EINVAL;
	}
      if (param == MICROBRUTE_BEND_RANGE)
	{
	  err = backend_send_rpn (backend, channel, 0, 0, value, 0);
	}
      else
	{
	  guint8 v = MICROBRUTE_PARAMS[param].value_map (value);
	  err = backend_send_controller (backend, channel,
					 MICROBRUTE_PARAMS[param].ctl, v);
	}
    }

  return err;
}
