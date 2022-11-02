#pragma once

#include "Kr/KrCommon.h"

struct M_Arena;
struct R_Device;
struct R_Pipeline;
struct R_Texture;
struct R_Font;
struct R_Font_Config;

R_Pipeline *LoadPipeline(M_Arena *arena, R_Device *device, String content, String path);
void        ReleasePipeline(R_Pipeline *pipeline);
R_Texture * LoadTexture(M_Arena *arena, R_Device *device, const String content, const String path);
void        ReleaseTexture(R_Texture *texture);
R_Font *    LoadFont(M_Arena *arena, const R_Font_Config &config, float height);
void        FreeFontTexturePixels(R_Font *font);
bool        UploadFontTexture(R_Device *device, R_Font *font);
void        ReleaseFont(R_Font *font);
