#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

#include <pthread.h>
#include <semaphore.h>

#include <map>
#include "city.h"
using namespace std;

//#define KVS_NUMBER 800000 // KVS delimiter between entry and data region
#define KVS_NUMBER 400

// int main(){
//   int fd;
//   char doc_name[] = "kvs";
//   if((fd = open(doc_name, O_WRONLY)) == -1){
//     if((fd = open(doc_name, O_CREAT | O_WRONLY | O_TRUNC)) == -1){
//       fprintf(stderr, "%s\n", "SET UP FAILED: cannot open or creat the KVS file");
//       exit(EXIT_FAILURE);
//     }
//   }
//   struct stat st;
//   if(stat("kvs", &st) < 0){
//     printf("ERROR\n");
//   }else{
//     printf("CL: %zu\n", st.st_size);
//   }
//   return 0;
// }

int main(){
  char s[] = "newfilenewfilenewfile";
  char d[] = "wtf";

  map<string, char *> m;
  m.insert(make_pair(s, d));
  map<string, char *>::iterator it;
  it = m.find(s);
  printf("%s: %s", it->first.c_str(), it->second);
  return 0;
}

// int main(){
//   char s[] = "newfilenewfilenewfile";
//   char d[] = "wtf";
//   int i = 0;
//   pair<string, int> block_pair = make_pair(s, i);
//   printf("%s: %d\n", block_pair.first.c_str(), block_pair.second);
//
//   map<pair<string, int>, char*> m;
//   m.insert(make_pair(block_pair, d));
//   map<pair<string, int>, char*>::iterator it;
//   for(it = m.begin(); it != m.end(); it++){
//     printf("%s %d:-->%s\n",it->first.first.c_str(), it->first.second, it->second);
//   }
//   return 0;
// }
