#ifndef BGA_CLIENT_H
#define BGA_CLIENT_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _BgaClientSession BgaClientSession;

typedef struct {
  const char *username;
  const char *password;
  gboolean remember_me;
} BgaCredentials;

typedef struct {
  long http_status;
  char *body;
} BgaHttpResponse;

typedef enum {
  BGA_LOGIN_RESULT_PARSE_ERROR = 0,
  BGA_LOGIN_RESULT_STATUS_ZERO,
  BGA_LOGIN_RESULT_SUCCESS_FALSE,
  BGA_LOGIN_RESULT_SUCCESS_TRUE
} BgaLoginResultKind;

typedef struct {
  BgaLoginResultKind kind;
  char *error;
  char *exception;
  char *message;
  char *user_id;
} BgaLoginResult;

typedef struct {
  char *table_id;
  char *start_at;
  char *player_one;
  char *player_two;
} BgaHistoryGameSummary;

gboolean bga_client_extract_request_token(const char *body, char **out_token, GError **error);
BgaClientSession *bga_client_session_new(GError **error);
void bga_client_session_free(BgaClientSession *session);
gboolean bga_client_session_fetch_homepage_and_request_token(BgaClientSession *session,
                                                             char **out_homepage_body,
                                                             char **out_token,
                                                             GError **error);
gboolean bga_client_session_login_with_password(BgaClientSession *session,
                                                const BgaCredentials *credentials,
                                                const char *request_token,
                                                BgaHttpResponse *out_response,
                                                GError **error);
gboolean bga_client_session_fetch_checkers_history(BgaClientSession *session,
                                                   const char *user_id,
                                                   BgaHttpResponse *out_response,
                                                   GError **error);
gboolean bga_client_fetch_request_token(char **out_token, GError **error);
gboolean bga_client_fetch_homepage_and_request_token(char **out_homepage_body,
                                                     char **out_token,
                                                     GError **error);
gboolean bga_client_login_with_password(const BgaCredentials *credentials,
                                        const char *request_token,
                                        BgaHttpResponse *out_response,
                                        GError **error);
gboolean bga_client_parse_login_response(const char *body, BgaLoginResult *out_result, GError **error);
gboolean bga_client_parse_checkers_history_games(const char *body, GPtrArray **out_games, GError **error);
void bga_http_response_clear(BgaHttpResponse *response);
void bga_login_result_clear(BgaLoginResult *result);
void bga_history_game_summary_free(BgaHistoryGameSummary *summary);

G_END_DECLS

#endif
