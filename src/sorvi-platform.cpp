//  SuperTux
//  Copyright (C) 2009 Ingo Ruhnke <grumbel@gmail.com>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

// Needed for sorvi, in future these will be metadata in the sorvi archive instead
extern const char SDL_SORVI_app_id[] = "org.sorvi.port.supertux";
extern const char SDL_SORVI_app_name[] = "supertux";
extern const char SDL_SORVI_app_version[] = "0.7.0";

// SDL3 -> SDL2-compat -> sorvi integration requires this set
// It also requires SDL2_iterate function to be defined
#define SDL_MAIN_NEEDED
#include <SDL.h>

#include <config.h>
#include <memory>

#include "supertux/main.hpp"
#include "supertux/screen_manager.hpp"

static std::unique_ptr<Main> g_main;

int main(int argc, char** argv)
{
  g_main = std::make_unique<Main>();
  int ret = g_main->run(argc, argv);
  // this version of main does not reset g_main
  return ret;
}

extern "C" {
  // called every frame by the sorvi platform
  int SDL2_iterate(void) {
    auto screen_manager = ScreenManager::current();
    if (screen_manager->get_screen_stack().empty()) return 1;
    screen_manager->loop_iter();
    return 0;
  }
}

// override ScreenManager::run so it doesn't busy loop
void ScreenManager::run()
{
  Integration::init_all();
  handle_screen_switch();
  SDL2_iterate();
}
