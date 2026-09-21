#pragma once
#include <SDL.h>
#include <SDL_mixer.h>
#include <iostream>

namespace sound {

enum SoundType {
    SND_NONE = 0,
    SND_NAV,
    SND_CONFIRM,
    SND_BACK,
    SND_ACTION,
    SND_DELETE
};

inline Mix_Chunk* snd_nav = nullptr;
inline Mix_Chunk* snd_confirm = nullptr;
inline Mix_Chunk* snd_back = nullptr;
inline Mix_Chunk* snd_action = nullptr;

inline bool g_enabled = false;

inline void initAudio() {
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "SDL_mixer could not initialize! SDL_mixer Error: " << Mix_GetError() << std::endl;
        return;
    }
    
    Mix_AllocateChannels(16);

    snd_nav     = Mix_LoadWAV("romfs:/sfx/nav.wav");
    snd_confirm = Mix_LoadWAV("romfs:/sfx/confirm.wav");
    snd_back    = Mix_LoadWAV("romfs:/sfx/back.wav");
    snd_action  = Mix_LoadWAV("romfs:/sfx/action.wav");

    g_enabled = true;
}

inline void play(SoundType type) {
    if (!g_enabled) return;
    
    Mix_Chunk* chunk = nullptr;
    if (type == SND_NAV) chunk = snd_nav;
    else if (type == SND_CONFIRM) chunk = snd_confirm;
    else if (type == SND_BACK) chunk = snd_back;
    else if (type == SND_ACTION) chunk = snd_action;
    else if (type == SND_DELETE) chunk = snd_back; 
    
    if (chunk) {
        Mix_PlayChannel(-1, chunk, 0);
    }
}

inline void closeAudio() {
    if (snd_nav) Mix_FreeChunk(snd_nav);
    if (snd_confirm) Mix_FreeChunk(snd_confirm);
    if (snd_back) Mix_FreeChunk(snd_back);
    if (snd_action) Mix_FreeChunk(snd_action);
    
    Mix_CloseAudio();
}

} // namespace sound
