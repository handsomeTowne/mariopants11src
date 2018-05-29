#pragma once

// files.h
//   for loading and saving files

#include "editor.h" // for SongData

namespace files
{

// returns true if loaded correctly
bool load_file(const char* filename, Song* song);
bool save_file(const char* filename, const Song* song);
bool save_multi_file(const char* filename, const Song* song);

// returns description of last error
const char* get_file_error();

// returns true if a file extension is matched
bool match_extension(const char* filename, const char* extension);

// file type is resolved by extension
// loading allows:
//   .sho
//   .zst
// saving allows:
//   .sho
//   .zst (if file exists, will insert data, otherwise will create from scratch)
//   .wav

} // namespace files

// end of file
