#include "homeworlds_move_report.h"

#include "homeworlds_game.h"
#include "homeworlds_position_text.h"

#include <stdarg.h>

typedef gboolean (*HomeworldsMoveReportWriteTextFunc)(const char *text, gpointer user_data);

typedef struct {
  HomeworldsMoveReportWriteTextFunc write_text;
  gpointer user_data;
  gsize all_move_count;
} HomeworldsMoveReportWriter;

static gboolean homeworlds_move_report_count_word(gsize count, const char **out_word) {
  g_return_val_if_fail(out_word != NULL, FALSE);

  *out_word = count == 1 ? "move" : "moves";
  return TRUE;
}

static gboolean homeworlds_move_report_write_text(HomeworldsMoveReportWriter *writer, const char *text) {
  g_return_val_if_fail(writer != NULL, FALSE);
  g_return_val_if_fail(writer->write_text != NULL, FALSE);
  g_return_val_if_fail(text != NULL, FALSE);

  return writer->write_text(text, writer->user_data);
}

static gboolean homeworlds_move_report_write_printf(HomeworldsMoveReportWriter *writer, const char *format, ...) {
  va_list args;
  g_autofree char *text = NULL;

  g_return_val_if_fail(writer != NULL, FALSE);
  g_return_val_if_fail(format != NULL, FALSE);

  va_start(args, format);
  text = g_strdup_vprintf(format, args);
  va_end(args);
  g_return_val_if_fail(text != NULL, FALSE);

  return homeworlds_move_report_write_text(writer, text);
}

static gboolean homeworlds_move_report_write_string_text(const char *text, gpointer user_data) {
  GString *string = user_data;

  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(string != NULL, FALSE);

  g_string_append(string, text);
  return TRUE;
}

static gboolean homeworlds_move_report_write_file_text(const char *text, gpointer user_data) {
  FILE *file = user_data;

  g_return_val_if_fail(text != NULL, FALSE);
  g_return_val_if_fail(file != NULL, FALSE);

  return fputs(text, file) >= 0;
}

static gboolean homeworlds_move_report_write_played_moves(HomeworldsMoveReportWriter *writer,
                                                          const GArray *played_moves) {
  g_return_val_if_fail(writer != NULL, FALSE);

  if (!homeworlds_move_report_write_text(writer, "moves:\n")) {
    return FALSE;
  }
  if (played_moves == NULL || played_moves->len == 0) {
    return homeworlds_move_report_write_text(writer, "<none>\n\n");
  }

  for (guint i = 0; i < played_moves->len; ++i) {
    const HomeworldsMove *move = &g_array_index(played_moves, HomeworldsMove, i);
    char notation[128] = {0};

    if (!homeworlds_move_format(move, notation, sizeof(notation))) {
      if (!homeworlds_move_report_write_printf(writer, "%u. <unformattable move>\n", i + 1)) {
        return FALSE;
      }
      continue;
    }
    if (!homeworlds_move_report_write_printf(writer, "%u. %s\n", i + 1, notation)) {
      return FALSE;
    }
  }

  return homeworlds_move_report_write_text(writer, "\n");
}

static gboolean homeworlds_move_report_write_position(HomeworldsMoveReportWriter *writer,
                                                      const HomeworldsPosition *position) {
  g_autofree char *ascii = NULL;

  g_return_val_if_fail(writer != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);

  ascii = homeworlds_position_format_ascii(position);
  if (ascii == NULL) {
    g_debug("Failed to format Homeworlds position for move report");
    return FALSE;
  }

  if (!homeworlds_move_report_write_printf(writer, "position:\n%s", ascii)) {
    return FALSE;
  }
  if (!g_str_has_suffix(ascii, "\n") && !homeworlds_move_report_write_text(writer, "\n")) {
    return FALSE;
  }
  return homeworlds_move_report_write_text(writer, "\n");
}

static gboolean homeworlds_move_report_write_streamed_move(gconstpointer move_data, gpointer user_data) {
  const HomeworldsMove *move = move_data;
  HomeworldsMoveReportWriter *writer = user_data;
  char notation[128] = {0};

  g_return_val_if_fail(move != NULL, FALSE);
  g_return_val_if_fail(writer != NULL, FALSE);

  writer->all_move_count++;
  if (!homeworlds_move_format(move, notation, sizeof(notation))) {
    g_debug("Failed to format streamed Homeworlds move for move report");
    return homeworlds_move_report_write_printf(writer,
                                               "%" G_GSIZE_FORMAT ". <unformattable move>\n",
                                               writer->all_move_count);
  }

  return homeworlds_move_report_write_printf(writer,
                                             "%" G_GSIZE_FORMAT ". %s\n",
                                             writer->all_move_count,
                                             notation);
}

static gboolean homeworlds_move_report_write_all_moves(HomeworldsMoveReportWriter *writer,
                                                       const HomeworldsPosition *position,
                                                       gboolean include_count) {
  gboolean streamed = FALSE;

  g_return_val_if_fail(writer != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);

  writer->all_move_count = 0;
  if (!homeworlds_move_report_write_text(writer, "all_moves:\n")) {
    return FALSE;
  }

  streamed = homeworlds_position_stream_all_moves(position, homeworlds_move_report_write_streamed_move, writer);
  if (writer->all_move_count == 0 && !homeworlds_move_report_write_text(writer, "<none>\n")) {
    return FALSE;
  }
  if (!homeworlds_move_report_write_printf(writer,
                                           "\nall_moves_streamed: %" G_GSIZE_FORMAT "\n",
                                           writer->all_move_count)) {
    return FALSE;
  }
  if (!streamed && !homeworlds_move_report_write_text(writer, "all_moves_stream_error: true\n")) {
    return FALSE;
  }
  if (include_count) {
    const char *word = NULL;

    if (!homeworlds_move_report_count_word(writer->all_move_count, &word)) {
      return FALSE;
    }
    if (!homeworlds_move_report_write_printf(writer,
                                             "all_moves_count: %" G_GSIZE_FORMAT " %s\n",
                                             writer->all_move_count,
                                             word)) {
      return FALSE;
    }
  }

  return streamed;
}

char *homeworlds_move_report_format(const HomeworldsPosition *position) {
  GString *text = NULL;
  HomeworldsMoveReportWriter writer = {0};

  g_return_val_if_fail(position != NULL, NULL);

  if (position->phase == HOMEWORLDS_PHASE_FINISHED) {
    return g_strdup("No moves.");
  }

  text = g_string_new(NULL);
  g_return_val_if_fail(text != NULL, NULL);
  writer.write_text = homeworlds_move_report_write_string_text;
  writer.user_data = text;
  if (!homeworlds_move_report_write_all_moves(&writer, position, TRUE)) {
    g_debug("Failed to write Homeworlds move report");
  }

  return g_string_free(text, FALSE);
}

gboolean homeworlds_move_report_write(FILE *file,
                                      const HomeworldsPosition *position,
                                      const GArray *played_moves,
                                      gsize *out_all_move_count) {
  HomeworldsMoveReportWriter writer = {0};
  gboolean ok = FALSE;

  g_return_val_if_fail(file != NULL, FALSE);
  g_return_val_if_fail(position != NULL, FALSE);

  writer.write_text = homeworlds_move_report_write_file_text;
  writer.user_data = file;

  ok = homeworlds_move_report_write_played_moves(&writer, played_moves) &&
       homeworlds_move_report_write_position(&writer, position) &&
       homeworlds_move_report_write_all_moves(&writer, position, FALSE);

  if (out_all_move_count != NULL) {
    *out_all_move_count = writer.all_move_count;
  }
  return ok;
}
