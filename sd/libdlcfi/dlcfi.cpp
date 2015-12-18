#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <unistd.h>
#include <dlfcn.h>
#include <link.h>

typedef struct _RangeMapElement {
  char *name;
  int64_t start;
  int64_t size;
  int64_t alignment;
} RangeMapElement_t;

typedef struct _RangeMap {
  int64_t nelements;
  RangeMapElement_t elements[1];
} RangeMap_t;

typedef struct _WhiteListElement {
  char *name;
  int64_t value;
} WhiteListElement_t;

typedef struct _WhiteList {
  int64_t nelements;
  WhiteListElement_t elements[1];
} WhiteList_t;

RangeMapElement_t *findRange(RangeMap_t *rMap, const char *className) {
  //printf("%lld entries in rMap:\n", (long long) rMap->nelements);
  for (int64_t i = 0; i < rMap->nelements; i++) {
    /*
    printf("%s start: %p size: %lld\n", rMap->elements[i].name,
      (intptr_t) rMap->elements[i].start,
      (intptr_t) rMap->elements[i].size
      );
    */

    if (!strcmp(className, rMap->elements[i].name)) {
    /*
      printf("Its a match!\n");
      fflush(stdout);
    */
      return &rMap->elements[i];
    }
  }
  return NULL;
}

bool vptr_safe(const void *vptr, const char *className) {
  Dl_info inf, inf1;
  RangeMap_t *rMap = NULL;
  WhiteList_t *wList = NULL;
  void *dlH;

  printf("Checking %p for %s\n", vptr, className);

  if (!dladdr(vptr, &inf)) {
    return false;
  }

  printf("Pointer lies in library %s loaded at %p", inf.dli_fname, inf.dli_fbase);
  if (inf.dli_sname) {
    printf(" in symbol %s at index %p\n", inf.dli_sname, ((intptr_t)vptr - (intptr_t)inf.dli_saddr)/8);
  } else {
    printf("\n");
  }

  dlH = dlopen(inf.dli_fname, RTLD_NOLOAD | RTLD_LOCAL | RTLD_LAZY);

  /*
  printf("dlH = %p error=%s\n", dlH, dlerror());
  */

  assert(dlH && "dlH should be not null");

  struct link_map *map;

  dlinfo(dlH, RTLD_DI_LINKMAP, &map);

//  printf("%s loaded at %p dynamic sec at %p\n", map->l_name, map->l_addr, map->l_ld);
  Elf64_Dyn *e = map->l_ld;

  while (e->d_tag != DT_NULL) {
//    printf("Tag: %d val: %p\n", e->d_tag, e->d_un.d_ptr);
    if (e->d_tag == 0x70000035) {
      rMap = (RangeMap_t*) (((intptr_t)inf.dli_fbase) + ((intptr_t)e->d_un.d_ptr));
    } else if (e->d_tag == 0x70000036) {
      wList = (WhiteList_t*) (((intptr_t)inf.dli_fbase) + ((intptr_t)e->d_un.d_ptr));
    }
    e++;
  }

  if (rMap) {
    RangeMapElement_t *range = findRange(rMap, className);
    if (range) {
      int64_t start = range->start;
      int64_t size = range->size;
      int64_t alignment = range->alignment;

      if (((int64_t)vptr) >= start && ((int64_t)vptr < (start + size * alignment)) &&
        ((int64_t)vptr) % alignment == 0) {
        /*
        printf("%p is in the range %p-%p for %s\n", (void*)vptr, (void*)start, (void*)(start+size), className);
        fflush(stdout);
        */
        return true;
      } else {
        /*
        printf("%p is not in the range %p-%p for %s\n", (void*)vptr, (void*)start, (void*)(start+size), className);
        fflush(stdout);
        */
        goto fail_l;
      }
    }
  } else {
    /*
    printf("Module %s was not compiled by our tool :(\n", inf.dli_fname);
    */
    return true;
  }

fail_l:
  if (wList) {
    printf("wList->nelements=%d\n", wList->nelements);
    for (int64_t i = 0; i < wList->nelements; i++) {
      printf("Class %s\n", wList->elements[i].name);
      if (!strcmp(className, wList->elements[i].name)) {
        if (wList->elements[i].value == (intptr_t)vptr) {
          // printf("%s found\n", className);
          return true;
        }
      }
    }
  }
  printf("%s not found\n", className);
  return false;
}
