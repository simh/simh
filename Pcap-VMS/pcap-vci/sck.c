#include <in.
#include <socket.
#include <stdlib.
#include <stdio.

#pragma __required_pointer_size __sa
#pragma __required_pointer_size sho
extern void *decc$malloc(size_t size
#define short_malloc decc$mall
#pragma __required_pointer_size __resto

main

  int                s = -1, rc = 
  unsigned int       
  struct sockaddr_in s
  struct sockaddr_in *alloc_s

  sa.sin_family = AF_INE

  s = socket(sa.sin_family, SOCK_STREAM, 0
  printf("socket returned is %d\n", s

  rc = bind(s, (struct sockaddr *)&sa, sizeof(sa)
  printf("bind rc is %d\n", rc
  rc = listen(s, 2
  printf("listen rc is %d\n", rc

  l = sizeof(sa
  rc = getsockname(s, (struct sockaddr *)&sa, &l
  printf("getsockname (stack call) rc is %d\n", rc


  alloc_sa = (struct sockaddr_in *)short_malloc(sizeof(sa)
  rc = getsockname(s, (struct sockaddr *)alloc_sa, &l
  printf("getsockname (alloc call) rc is %d\n", rc


