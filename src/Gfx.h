#pragma once
#include <SDL.h>

namespace Gfx {
void fillRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int radius, SDL_Color c);
void strokeRoundRect(SDL_Renderer* r, const SDL_Rect& rc, int radius, int thickness, SDL_Color c);
// Texture drawn as a rounded rect; mod = per-vertex color/alpha (dim + fade)
void drawRoundImage(SDL_Renderer* r, SDL_Texture* tex, const SDL_Rect& rc, int radius, SDL_Color mod);
// 8-dot spinner centered at cx,cy; the bright dot rotates with time
void spinner(SDL_Renderer* r, int cx, int cy, int radius, int dot, Uint32 now, SDL_Color c);
}
