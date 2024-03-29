
// pgn.h

#ifndef PGN_H
#define PGN_H

// includes

#include <cstdio>

#include "util.h"

// constants

const int PGN_STRING_SIZE = 256;
const int GAME_BUF_SIZE = 16000;

// types

struct pgn_t {

   FILE * file;

   int char_hack;
   int char_line;
   int char_column;
   bool char_unread;
   bool char_first;

   int token_type;
   char token_string[PGN_STRING_SIZE];
   int token_length;
   int token_line;
   int token_column;
   bool token_unread;
   bool token_first;

   char result[PGN_STRING_SIZE];
   char fen[PGN_STRING_SIZE];
   char event[PGN_STRING_SIZE];
  int white_elo;
  int black_elo;

   int move_line;
   int move_column;

   char game_string[GAME_BUF_SIZE];
  int game_string_len;
};

// functions

extern void pgn_open      (pgn_t * pgn, const char file_name[]);
extern void pgn_close     (pgn_t * pgn);

extern bool pgn_next_game (pgn_t * pgn);
extern bool pgn_next_move (pgn_t * pgn, char string[], int size);

#endif // !defined PGN_H

// end of pgn.h

