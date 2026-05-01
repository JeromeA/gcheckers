#include "app_main.h"
#include "game_app_profile.h"

int main(int argc, char **argv) {
  return ggame_app_main_run(argc, argv, ggame_app_profile_get_by_kind(GGAME_APP_KIND_HOMEWORLDS));
}
