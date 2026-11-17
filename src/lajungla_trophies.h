// Original-game trophy HLE and optional game-only SDK bridge. Author: matymeatboy.
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Call before guest boot. Catalog is privately extracted catalog.bin; state_directory
// is separate from mounted game content, normally <save_root>/trophies.
int lajungla_trophies_configure(const char* catalog, const char* state_directory);
void lajungla_trophies_register(void); // after ppu_hle_register_all()
void lajungla_trophies_frame(void);    // once per presented frame (SDK maintenance)
void lajungla_trophies_save_loaded(void); // every successful save load, not save writes
void lajungla_trophies_casual(const char* reason); // cheats/debug/save states
void lajungla_trophies_shutdown(void);
typedef void (*lajungla_trophy_notify_fn)(uint32_t id, const char* name, const char* description);
void lajungla_trophies_set_notify(lajungla_trophy_notify_fn notify);
#ifdef __cplusplus
}
#endif
