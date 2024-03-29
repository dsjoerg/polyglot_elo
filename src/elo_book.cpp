
// elo_book.cpp

// includes

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#include "board.h"
#include "elo_book.h"
#include "move.h"
#include "move_do.h"
#include "move_legal.h"
#include "pgn.h"
#include "san.h"
#include "util.h"

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

// constants

static const int COUNT_MAX = 16384;

static const int NIL = -1;

// types

struct elopath_stats {
  int elo;
  int stdev_elo;
  int ply;
  int num_games;
  int elo_min;   // the smallest ELO of the games that exactly matched
  int elo_max;   // the largest ELO of the games that exactly matched
};

struct entry_t {
   uint64 key;
   uint16 move;
   uint16 n;
  uint16 terminal;
  uint16 gamenum;
  uint32 elo_sum;
  uint32 elo_min;
  uint32 elo_max;
  uint64 elo_sumsq;
};

struct book_t {
   int size;
   int alloc;
   uint32 mask;
   entry_t * entry;
   sint32 * hash;
};

// variables

static int MaxPly;
static int MinGame;
static double MinScore;
static bool RemoveWhite, RemoveBlack;
static bool Uniform;

static book_t Book[1];

// prototypes

static void   book_clear    ();
static void   book_insert   (const char file_name[], bool exact_match);
static void   eloize_games  (const char file_name[], bool exact_match);

static int    find_entry    (const board_t * board, int move, bool create);
static void   resize        ();
static void   halve_stats   (uint64 key);



// functions

// elo_book()

void elo_book(int argc, char * argv[]) {

   int i;
   const char * train_pgn_files[100];
   int num_train_files = 0;
   const char * input_pgn_files[100];
   int num_input_files = 0;
   const char * bin_file;
   bool exact_match = false;

   struct stat buf;
   struct dirent *dp;

   bin_file = NULL;
   my_string_set(&bin_file,"book.bin");

   // zero out these pointers because otherwise my_string_set() will
   // attempt to free() them
   for (i=0; i<100; i++) {
     train_pgn_files[i] = NULL;
     input_pgn_files[i] = NULL;
   }

   MaxPly = 1024;

   for (i = 1; i < argc; i++) {

      if (false) {

      } else if (my_string_equal(argv[i],"elo-book")) {

         // skip

      } else if (my_string_equal(argv[i],"-train-pgn")) {

         i++;
         if (argv[i] == NULL) my_fatal("elo_book(): missing argument\n");

         my_string_set(&train_pgn_files[num_train_files++],argv[i]);

      } else if (my_string_equal(argv[i],"-input-pgn")) {

         i++;
         if (argv[i] == NULL) my_fatal("elo_book(): missing argument\n");

         my_string_set(&input_pgn_files[num_input_files++],argv[i]);

      } else if (my_string_equal(argv[i],"-exact-match")) {

         i++;
         exact_match = true;

      } else if (my_string_equal(argv[i],"-max-ply")) {

         i++;
         if (argv[i] == NULL) my_fatal("book_make(): missing argument\n");

         MaxPly = atoi(argv[i]);
         ASSERT(MaxPly>=0);

      } else if (my_string_equal(argv[i],"-bin")) {

         i++;
         if (argv[i] == NULL) my_fatal("elo_book(): missing argument\n");

         my_string_set(&bin_file,argv[i]);

      } else {

         my_fatal("elo_book(): unknown option \"%s\"\n",argv[i]);
      }
   }

   book_clear();

   for (i=0; i<num_train_files; i++) {
     stat(train_pgn_files[i], &buf);
     if (buf.st_mode & S_IFDIR) {
       fprintf(stderr, "%s is a directory!\n", train_pgn_files[i]);
       chdir(train_pgn_files[i]);
       DIR *dirp = opendir(train_pgn_files[i]);
       while ((dp = readdir(dirp)) != NULL) {
         stat(dp->d_name, &buf);
         if (buf.st_mode & S_IFDIR) {
           fprintf(stderr, "dir contains %s, which is a directory\n", dp->d_name);
         } else {
           fprintf(stderr, "dir contains %s\n", dp->d_name);
           fprintf(stderr, "learning train games from %s ...\n", dp->d_name);
           book_insert(dp->d_name, exact_match);
         }
       }
       (void)closedir(dirp);
     } else {
       fprintf(stderr, "learning train games from %s ...\n", train_pgn_files[i]);
       book_insert(train_pgn_files[i], exact_match);
     }

   }

   for (i=0; i<num_input_files; i++) {
     stat(input_pgn_files[i], &buf);
     if (buf.st_mode & S_IFDIR) {
       fprintf(stderr, "%s is a directory!\n", input_pgn_files[i]);
       chdir(input_pgn_files[i]);
       DIR *dirp = opendir(input_pgn_files[i]);
       while ((dp = readdir(dirp)) != NULL) {
         stat(dp->d_name, &buf);
         if (buf.st_mode & S_IFDIR) {
           fprintf(stderr, "dir contains %s, which is a directory\n", dp->d_name);
         } else {
           fprintf(stderr, "dir contains %s\n", dp->d_name);
           fprintf(stderr, "eloizing games from %s ...\n", dp->d_name);
           eloize_games(dp->d_name, exact_match);
         }
       }
       (void)closedir(dirp);
     } else {
       fprintf(stderr, "eloizing games from %s...\n", input_pgn_files[i]);
       eloize_games(input_pgn_files[i], exact_match);
     }
   }

   fputs("all done!\n", stderr);
}

// book_clear()

static void book_clear() {

   int index;

   Book->alloc = 1;
   Book->mask = (Book->alloc * 2) - 1;

   Book->entry = (entry_t *) my_malloc(Book->alloc*sizeof(entry_t));
   Book->size = 0;

   Book->hash = (sint32 *) my_malloc((Book->alloc*2)*sizeof(sint32));
   for (index = 0; index < Book->alloc*2; index++) {
      Book->hash[index] = NIL;
   }
}

// book_insert()

static void book_insert(const char file_name[], bool exact_match) {

   int game_nb;
   pgn_t pgn[1];
   board_t board[1];
   int ply;
   char string[256];
   int move;
   int pos;
   int player_elo;
   int other_elo;

   ASSERT(file_name!=NULL);

   // init

   game_nb = 0;

   // scan loop

   pgn_open(pgn,file_name);

   while (pgn_next_game(pgn)) {

      board_start(board);
      ply = 0;
      player_elo = pgn->white_elo;
      other_elo = pgn->black_elo;

      while (pgn_next_move(pgn,string,256)) {

         if (ply < MaxPly) {

            move = move_from_san(string,board);

            if (move == MoveNone || !move_is_legal(move,board)) {
              fprintf(stderr, "book_insert(): illegal move \"%s\" at line %d, column %d\n",string,pgn->move_line,pgn->move_column);
               continue;
            }

            pos = find_entry(board,move,true);

            if (player_elo > -1) {
              Book->entry[pos].n++;
              Book->entry[pos].gamenum = atoi(pgn->event);
              Book->entry[pos].elo_sum += player_elo;
              Book->entry[pos].elo_min = MIN(Book->entry[pos].elo_min, player_elo);
              Book->entry[pos].elo_max = MAX(Book->entry[pos].elo_max, player_elo);
              Book->entry[pos].elo_sumsq += (player_elo * player_elo);
            }
            
            // swap player_elo and other_elo
            int tmp_elo = player_elo;
            player_elo = other_elo;
            other_elo = tmp_elo;

            if (Book->entry[pos].n >= COUNT_MAX) {
               halve_stats(board->key);
            }

            move_do(board,move);
            ply++;
         }
      }

      Book->entry[pos].terminal = 1;

      game_nb++;
      if (game_nb % 10000 == 0) fprintf(stderr,"%d games ...\n",game_nb);
   }

   pgn_close(pgn);

   fprintf(stderr, "%d game%s.\n",game_nb,(game_nb>1)?"s":"");
   fprintf(stderr, "%d entries.\n",Book->size);

   return;
}



// find_entry()

static int find_entry(const board_t * board, int move, bool create) {

   uint64 key;
   int index;
   int pos;

   ASSERT(board!=NULL);
   ASSERT(move_is_ok(move));

   ASSERT(move_is_legal(move,board));

   // init

   key = board->key;

   // search

   for (index = key & Book->mask; (pos=Book->hash[index]) != NIL; index = (index+1) & Book->mask) {

      ASSERT(pos>=0&&pos<Book->size);

      if (Book->entry[pos].key == key && Book->entry[pos].move == move) {
         return pos; // found
      }
   }

   if (!create) {
     return -1;
   }

   // not found

   ASSERT(Book->size<=Book->alloc);

   if (Book->size == Book->alloc) {

      // allocate more memory

      resize();

      for (index = key & Book->mask; Book->hash[index] != NIL; index = (index+1) & Book->mask)
         ;
   }

   // create a new entry

   ASSERT(Book->size<Book->alloc);
   pos = Book->size++;

   Book->entry[pos].key = key;
   Book->entry[pos].move = move;
   Book->entry[pos].n = 0;
   Book->entry[pos].terminal = 0;
   Book->entry[pos].elo_sum = 0;
   Book->entry[pos].elo_sumsq = 0;
   Book->entry[pos].elo_min = 4000;
   Book->entry[pos].elo_max = 0;

   // insert into the hash table

   ASSERT(index>=0&&index<Book->alloc*2);
   ASSERT(Book->hash[index]==NIL);
   Book->hash[index] = pos;

   ASSERT(pos>=0&&pos<Book->size);

   return pos;
}

// resize()

static void resize() {

   int size;
   int pos;
   int index;

   ASSERT(Book->size==Book->alloc);

   Book->alloc *= 2;
   Book->mask = (Book->alloc * 2) - 1;

   size = 0;
   size += Book->alloc * sizeof(entry_t);
   size += (Book->alloc*2) * sizeof(sint32);

   if (size >= 1048576) fprintf(stderr,"allocating %gMB ...\n",double(size)/1048576.0);

   // resize arrays

   Book->entry = (entry_t *) my_realloc(Book->entry,Book->alloc*sizeof(entry_t));
   Book->hash = (sint32 *) my_realloc(Book->hash,(Book->alloc*2)*sizeof(sint32));

   // rebuild hash table

   for (index = 0; index < Book->alloc*2; index++) {
      Book->hash[index] = NIL;
   }

   for (pos = 0; pos < Book->size; pos++) {

      for (index = Book->entry[pos].key & Book->mask; Book->hash[index] != NIL; index = (index+1) & Book->mask)
         ;

      ASSERT(index>=0&&index<Book->alloc*2);
      Book->hash[index] = pos;
   }
}

// halve_stats()

static void halve_stats(uint64 key) {

   int index;
   int pos;

   // search

   for (index = key & Book->mask; (pos=Book->hash[index]) != NIL; index = (index+1) & Book->mask) {

      ASSERT(pos>=0&&pos<Book->size);

      if (Book->entry[pos].key == key) {
         Book->entry[pos].n = (Book->entry[pos].n + 1) / 2;
         Book->entry[pos].elo_sum = (Book->entry[pos].elo_sum + 1) / 2;
         Book->entry[pos].elo_sumsq = (Book->entry[pos].elo_sumsq + 1) / 2;
      }
   }
}

static void eloize_games(const char file_name[], bool exact_match) {
   int game_nb;
   pgn_t pgn[1];
   board_t board[1];
   int ply;
   char string[256];
   int move;
   int pos;

   struct elopath_stats final_stats;      // ELO stats for the last move # that had enough matches
   struct elopath_stats penultimate_stats; // ELO stats for the move before that
   
   int elo_min;   // looking at each move #, the smallest of the average ELOs
   int elo_max;   // looking at each move #, the largest of the average ELOs
   bool still_in_book;

   ASSERT(file_name!=NULL);

   // init

   game_nb = 0;

   // scan loop

   pgn_open(pgn,file_name);

   while (pgn_next_game(pgn)) {
      board_start(board);
      ply = 0;

      final_stats.elo = -1;
      final_stats.ply = -1;
      final_stats.stdev_elo = -1;
      final_stats.num_games = -1;
      final_stats.elo_min = -1;
      final_stats.elo_max = -1;

      penultimate_stats.elo = -1;
      penultimate_stats.ply = -1;
      penultimate_stats.stdev_elo = -1;
      penultimate_stats.num_games = -1;
      penultimate_stats.elo_min = -1;
      penultimate_stats.elo_max = -1;

      elo_min = 3000;
      elo_max = 0;

      still_in_book = true;
      while (pgn_next_move(pgn,string,256)) {
         if (ply < MaxPly) {
            move = move_from_san(string,board);

            if (move == MoveNone || !move_is_legal(move,board)) {
              fprintf(stderr,"book_filter(): illegal move \"%s\" at line %d, column %d\n",string,pgn->move_line,pgn->move_column);
               continue;
            }
            pos = find_entry(board,move,false);
            if (pos == -1 || (exact_match && Book->entry[pos].n == 0)) {
              still_in_book = false;
            }
            if (pos > -1 && ((exact_match && Book->entry[pos].n > 0)  || Book->entry[pos].n > 10)) {
              int avg_elo = Book->entry[pos].elo_sum / Book->entry[pos].n;
              int stdev_elo = sqrt((Book->entry[pos].elo_sumsq / Book->entry[pos].n) - (avg_elo * avg_elo));

              penultimate_stats = final_stats;

              final_stats.ply = ply;
              final_stats.elo = avg_elo;
              final_stats.stdev_elo = stdev_elo;
              final_stats.num_games = Book->entry[pos].n;
              final_stats.elo_min = Book->entry[pos].elo_min;
              final_stats.elo_max = Book->entry[pos].elo_max;
              
              elo_min = MIN(avg_elo, elo_min);
              elo_max = MAX(avg_elo, elo_max);
              //              printf("ply %3i. %5i games, ELO %i += %i.\n", ply, Book->entry[pos].n, avg_elo, stdev_elo);
            }
            move_do(board,move);
            ply++;
         }
      }
      game_nb++;
      if (game_nb % 10000 == 0) fprintf(stderr,"%d games ... (mode %i)\n",game_nb,exact_match);
      if (exact_match) {
        if (still_in_book && (Book->entry[pos].terminal == 1) && (Book->entry[pos].n == 1)) {
          printf("%s,%i\n", pgn->event, Book->entry[pos].gamenum);
        }
      } else {
        printf("%s,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i,%i\n", pgn->event, final_stats.elo, final_stats.ply, final_stats.num_games, final_stats.stdev_elo, elo_min, elo_max, final_stats.elo_min, final_stats.elo_max, penultimate_stats.elo, penultimate_stats.ply, penultimate_stats.num_games, penultimate_stats.stdev_elo, penultimate_stats.elo_min, penultimate_stats.elo_max);
      }
   }
   pgn_close(pgn);
   fprintf(stderr, "ALL DONE.  %d games ...\n",game_nb);
}


// end of elo_book.cpp

