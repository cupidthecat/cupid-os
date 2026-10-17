#include "cupidbuild_host.h"
#include <stdio.h>
#include <string.h>
#define REQUIRE(c) do { if (!(c)) { fprintf(stderr,"check failed at %d\n",__LINE__); cupidbuild_host_observer_close(o); return 1; } } while(0)
int main(int argc, char **argv) {
  cupidbuild_host_observer_t *o = NULL;
  cupidbuild_host_file_observation_t rows[7];
  const char *good[] = {"first", "nested/second"};
  const char *mixed[] = {"missing", "first", "directory", "nested/second", "../outside", NULL, ""};
  uint64_t size = 99u;
  unsigned char resume;
  if(argc != 3) return 2;
  memset(rows,0xa5,sizeof(rows));
  REQUIRE(cupidbuild_host_observer_open(argv[1],&o));
  if(strcmp(argv[2],"valid")==0 || strcmp(argv[2],"drift")==0) {
    REQUIRE(cupidbuild_host_observer_files(o,good,2u,rows));
    REQUIRE(rows[0].observed==1 && rows[0].size==3u && rows[1].observed==1 && rows[1].size==5u);
    REQUIRE(cupidbuild_host_observer_require_unchanged(o));
    if(strcmp(argv[2],"drift")==0) {
      printf("ready\n"); fflush(stdout); REQUIRE(fread(&resume,1u,1u,stdin)==1u);
      REQUIRE(!cupidbuild_host_observer_require_unchanged(o));
    }
  } else if(strcmp(argv[2],"partial")==0) {
    REQUIRE(!cupidbuild_host_observer_files(o,mixed,7u,rows));
    REQUIRE(rows[1].observed==1 && rows[1].size==3u && rows[3].observed==1 && rows[3].size==5u);
    REQUIRE(rows[0].observed==0 && rows[0].size==0u && rows[2].observed==0 && rows[2].size==0u && rows[4].observed==0 && rows[4].size==0u && rows[5].observed==0 && rows[5].size==0u && rows[6].observed==0 && rows[6].size==0u);
    REQUIRE(rows[0].issue==CUPIDBUILD_OBSERVATION_MISSING &&
            rows[2].issue==CUPIDBUILD_OBSERVATION_KIND &&
            rows[4].issue==CUPIDBUILD_OBSERVATION_UNAVAILABLE &&
            rows[5].issue==CUPIDBUILD_OBSERVATION_UNAVAILABLE &&
            rows[6].issue==CUPIDBUILD_OBSERVATION_UNAVAILABLE);
    REQUIRE(rows[1].issue==CUPIDBUILD_OBSERVATION_OK && rows[3].issue==CUPIDBUILD_OBSERVATION_OK);
    REQUIRE(!cupidbuild_host_observer_require_unchanged(o));
    REQUIRE(!cupidbuild_host_observer_file(o,"first",0u,NULL,&size) && size==0u);
    memset(rows,0xa5,sizeof(rows));
    REQUIRE(!cupidbuild_host_observer_files(o,good,2u,rows));
    REQUIRE(rows[0].observed==0 && rows[0].size==0u && rows[1].observed==0 && rows[1].size==0u);
  } else if(strcmp(argv[2],"empty")==0) {
    REQUIRE(cupidbuild_host_observer_files(o,NULL,0u,NULL));
    REQUIRE(cupidbuild_host_observer_require_unchanged(o));
  } else if(strcmp(argv[2],"limit")==0) {
    cupidbuild_host_file_observation_t before;
    memcpy(&before,&rows[0],sizeof(before));
    REQUIRE(!cupidbuild_host_observer_files(o,good,4097u,rows));
    REQUIRE(memcmp(&before,&rows[0],sizeof(before))==0);
    REQUIRE(!cupidbuild_host_observer_require_unchanged(o));
  } else if(strcmp(argv[2],"null-paths")==0) {
    REQUIRE(!cupidbuild_host_observer_files(o,NULL,2u,rows));
    REQUIRE(rows[0].observed==0 && rows[0].size==0u && rows[1].observed==0 && rows[1].size==0u);
    REQUIRE(!cupidbuild_host_observer_require_unchanged(o));
  } else if(strcmp(argv[2],"null-results")==0) {
    REQUIRE(!cupidbuild_host_observer_files(o,good,2u,NULL));
    REQUIRE(!cupidbuild_host_observer_require_unchanged(o));
  } else if(strcmp(argv[2],"null-observer")==0) {
    REQUIRE(!cupidbuild_host_observer_files(NULL,good,2u,rows));
    REQUIRE(rows[0].observed==0 && rows[0].size==0u && rows[1].observed==0 && rows[1].size==0u);
    REQUIRE(cupidbuild_host_observer_require_unchanged(o));
  } else { cupidbuild_host_observer_close(o); return 2; }
  return cupidbuild_host_observer_close(o) ? 0 : 3;
}
