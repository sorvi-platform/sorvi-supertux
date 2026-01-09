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

#define SDL_MAIN_NEEDED
#include <SDL.h>

#include <config.h>
#include <memory>

#include "supertux/main.hpp"

static std::unique_ptr<Main> g_main;

extern const char SDL_SORVI_app_id[] = "org.sorvi.port.supertux";
extern const char SDL_SORVI_app_name[] = "supertux";
extern const char SDL_SORVI_app_version[] = "0.6.3";

int main(int argc, char** argv)
{
  g_main = std::make_unique<Main>();
  int ret = g_main->run(argc, argv);
  return ret;
}
