#pragma once

#include <stdbool.h>
#include "Globals.h"

// #define MAX_PROCESSES 8  // todo: centralize
typedef unsigned long u32;

typedef struct {
  u32 elements[MAX_PROCESSES];
  size_t size;
} Set;

void initializeSet(Set* set);
bool addToSet(Set* set, u32 element);
bool removeFromSet(Set* set, u32 element);
bool isInSet(const Set* set, u32 element);
void printSet(const Set* set);
bool isSetEmpty(const Set* set);
