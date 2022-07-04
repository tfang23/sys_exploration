#include <sstream>
#include <vector>
#include <list>
#include <set>
#include <unordered_set>
#include <string>
#include <iostream>
#include <iomanip>
#include <functional>
#include <map>
#include "imdb.h"
#include "imdb-utils.h"
#include "path.h"
using namespace std;

static const int kWrongArgumentCount = 1;
static const int kAdditionalArgumentIncorrect = 2;
static const int kDatabaseNotFound = 3;

/**
 * Method: BFS
 * ----------------------------
 * Helper function that implements the breadth-first search.
 *
 * @param db the database
 * @param source the source actor
 * @param target the target actor
 * @param maxLength maximum intermediate steps
 * @param pred_actor list of precedent actors
 * @param pred_film list of precedent films
 * @param dist list of distance of each actor from the source actor
 * @return true if the BFS finds a legal path, false if the BFS cannot fund a legal path.
 */

bool BFS(const imdb& db, string source, string target, int maxLength, map<string, string>& pred_actor, map<string, film>& pred_film, map<string, int>& dist) {  
    list<string> queue;
    set<string> visited_actors;
    set<film> visited_films;
    
    visited_actors.insert(source);
    queue.push_back(source);
    dist[source] = 0;

    //Evaluate movies one at a time.
    while (!queue.empty() && dist[queue.front()] < maxLength) {
        string u = queue.front();
        queue.pop_front();        
        vector<film> credits;
        db.getCredits(u, credits);

        //Find the costars in each film and check whether the target is there.
        for (int i = 0; i < credits.size(); i++) {
            const film& movie = credits[i];

            //Make sure the movie has not been evaluated before.
            if (visited_films.insert(movie).second == true) {
                vector<string> cast;
                db.getCast(movie, cast);
                
                for (int j = 0; j < cast.size(); j++) {
                    const string& costar = cast[j];

                    //Make sure the actor has not been evaluated before.
                    if (visited_actors.insert(costar).second == true) {
                        //Record the actor's distance from source as well as precedent actor and film.
                        pred_actor[costar] = u;
                        pred_film[costar] = movie;
                        dist[costar] = dist[u] + 1;
                                               
                        if (costar == target) {
                            return true;
                        }
                        
                        //Enqueue the actor for evaluation.
                        queue.push_back(costar);
                    }
                }
            }
        }
    }
    return false;
}

/**
 * Method: printShortestPath
 * ----------------------------
 * Helper function that prints the shortest path from source actor
 * to target actor within maximum length.
 *
 * @param db the database
 * @param source the source actor
 * @param target the target actor
 * @param maxLength maximum intermediate steps
 */

void printShortestPath(const imdb& db, string source, string target, int maxLength) {
    map<string, string> pred_actor;
    map<string, film> pred_film;
    map<string, int> dist;

    //Check whether the breadth-first search finds a legal path.
    if (BFS(db, source, target, maxLength, pred_actor, pred_film, dist) == false) {
        cout << "No path between those two people could be found." << endl;
        return;
    }
    int length = dist[target];
    path thisPath(target);
    string thisActor = target;
    film thisMovie;
    
    for (int i = 0; i < length; i++) {
        thisMovie = pred_film[thisActor];
        thisActor = pred_actor[thisActor];
        thisPath.addConnection(thisMovie, thisActor);
    }
    //Reverse the path to print in order.
    thisPath.reverse();
    cout << thisPath << endl;
} 

/**
 * Serves as the main entry point for the six-degrees executable.
 */
static const size_t kMaxDegreeOfSeparation = 6;
int main(int argc, char *argv[]) {
  size_t maxLength = kMaxDegreeOfSeparation;
  if (argc != 3 && argc != 4) {
    cout << "Usage: " << argv[0] << " <source-actor> <target-actor> [<max-path-length>]" << endl;
    return kWrongArgumentCount;
  }
  if (argc == 4) {
    try {
      maxLength = stoi(argv[3]);
    } catch (const exception& e) {
      cout << "Optional path length argument either malformed or too large a number." << endl;
      return kAdditionalArgumentIncorrect;
    }
    if (maxLength < 1 || maxLength > kMaxDegreeOfSeparation) {
      cout << "Optional path length argument must be non-negative and less than or equal to "
           << kMaxDegreeOfSeparation << "." << endl;
      return kAdditionalArgumentIncorrect;      
    }
  }
  
  imdb db(kIMDBDataDirectory);
  if (!db.good()) {
    cout << "Failed to properly initialize the imdb database." << endl;
    cout << "Please check to make sure the source files exist and that you have permission to read them." << endl;
    return kDatabaseNotFound;
  }
  
  string source = argv[1];
  string target = argv[2];
  if (source == target) {
    cout << "Ensure that source and target actors are different!" << endl;
  } else {
      printShortestPath(db, source, target, maxLength);
  }
  return 0;
}
