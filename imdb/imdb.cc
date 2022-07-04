#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "imdb.h"
#include <iostream>
#include <string>
#include <vector>
#include <stdio.h>
#include <string.h>

using namespace std;

const char *const imdb::kActorFileName = "actordata";
const char *const imdb::kMovieFileName = "moviedata";
imdb::imdb(const string& directory) {
  const string actorFileName = directory + "/" + kActorFileName;
  const string movieFileName = directory + "/" + kMovieFileName;  
  actorFile = acquireFileMap(actorFileName, actorInfo);
  movieFile = acquireFileMap(movieFileName, movieInfo);
}

bool imdb::good() const {
  return !( (actorInfo.fd == -1) || 
	    (movieInfo.fd == -1) ); 
}

imdb::~imdb() {
  releaseFileMap(actorInfo);
  releaseFileMap(movieInfo);
}

/**
 * Method: getCount
 * ----------------------------
 * Helper function that gets the count of credits/cast of a given player/movie.
 *
 */

short imdb::getCount(int& payload, const char* pos) const {
  if (payload % 2 == 1) {
      payload += 1;
  }
  short count = *(short *) (pos + payload);
    
  payload += 2;
  if (payload % 4 == 2) {
      payload += 2;
  }
  return count;
}

/**
 * Method: compareActorAtOffset
 * ----------------------------
 * Crawls over the image of actor data according to the assignment specification, 
 * finds the actor at the supplied offset, and compares it with the supplied 
 * player.
 *
 * Read imdb.h for more information.
 */

bool imdb::compareActorAtOffset(int offset, const string& player) const {
    std::string str;
    return strcmp((const char *)actorFile + offset, player.c_str()) < 0;
} 

/**
 * Method: getCredits
 * ------------------
 * Searches for an actor/actress's list of movie credits.  The list 
 * of credits is returned via the second argument, which you'll note
 * is a non-const vector<film> reference.  If the specified actor/actress
 * isn't in the database, then the films vector will be left empty.
 *
 * Read imdb.h for more information.
 */

bool imdb::getCredits(const string& player, vector<film>& films) const {
  const int *countp = (const int *) actorFile;
  const int *begin = (const int *) actorFile + 1;
  const int *end = begin + *countp;
   
  //Discover where the offset is or where its offset would need to be
  //inserted if everything were to remain sorted.
  const int *found = lower_bound(begin, end, player, [this](int offset, const string& player) {
      return compareActorAtOffset(offset, player);
  });
  const char *pos = (const char*) actorFile + *(const int *)found;
  
  //Check whether this player is at the given position.
  if (strcmp(pos, player.c_str()) == 0) {
      int payload = player.length() + 1;
      int count = getCount(payload, pos);
      const char *credits = pos + payload;
      
      //Get the credits of this player.
      for (int i = 0; i < count; i++) {
          const film thisMovie(movieFile, *((const int *)credits + i));
          films.push_back(thisMovie);
      }
      return true;
  }
  return false;
}

/**
 * Method: compareMovieAtOffset
 * ----------------------------
 * Crawls over the image of movie data according to the assignment specification, 
 * finds the movie at the supplied offset, and compares it with the supplied 
 * movie.
 *
 * Read imdb.h for more information.
 */

bool imdb::compareMovieAtOffset(int offset, const film& movie) const {
  const film thisMovie(movieFile, offset);
  return thisMovie < movie;
}

/**
 * Method: getCast
 * ---------------
 * Searches the receiving imdb for the specified film and returns the cast
 * by populating the specified vector<string> with the list of actors and actresses
 * who star in it.  If the movie doesn't exist in the database, the players vector
 * is cleared and its size left at 0.
 *
 * Read imdb.h for more information.
 */

bool imdb::getCast(const film& movie, vector<string>& players) const { 
  const int *countp = (const int *) movieFile;
  const int *begin = (const int *) movieFile + 1;
  const int *end = begin + *countp;

  //Discover where the offset is or where its offset would need to be
  //inserted if everything were to remain sorted.
  const int *found = lower_bound(begin, end, movie, [this](int offset, const film&movie) {
    return compareMovieAtOffset(offset, movie);
  });
  const char *pos = (const char*) movieFile + *found;
  const film thisMovie(movieFile, *found);

  //Check whether this movie is at the given position.
  if (thisMovie == movie) {
      int payload = movie.title.length() + 2;
      int count = getCount(payload, pos);
      const char *cast = pos + payload;

      //Get the cast of this movie.
      for (int i = 0; i < count; i++) {
          const int thisOffset = *(const int *)(cast + i * sizeof(int));
          std::string thisPlayer = (const char *)actorFile + thisOffset;
          players.push_back(thisPlayer);
      }
      return true;
  }
  return false; 
}

const void *imdb::acquireFileMap(const string& fileName, struct fileInfo& info) {
  struct stat stats;
  stat(fileName.c_str(), &stats);
  info.fileSize = stats.st_size;
  info.fd = open(fileName.c_str(), O_RDONLY);
  return info.fileMap = mmap(0, info.fileSize, PROT_READ, MAP_SHARED, info.fd, 0);
}

void imdb::releaseFileMap(struct fileInfo& info) {
  if (info.fileMap != NULL) munmap((char *) info.fileMap, info.fileSize);
  if (info.fd != -1) close(info.fd);
}
