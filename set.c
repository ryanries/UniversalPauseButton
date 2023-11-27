#pragma once
#pragma warning(disable: 5045) // Spectre memory vulnerability warnings

#include "set.h"
#include <stdio.h>

void initializeSet(Set* set) {
  for (size_t i = 0; i < MAX_PROCESSES; i++) {
    set->elements[i] = 0;
  }
  set->size = 0;
}

bool addToSet(Set* set, u32 element) {
  // Check if the element is already in the set
  for (size_t i = 0; i < set->size; i++) {
    if (set->elements[i] == element) {
      return false;  // Element is already in the set
    }
  }

  // Add the element to the set
  if (set->size < MAX_PROCESSES) {
    set->elements[set->size++] = element;
    return true;  // Element added successfully
  }
  else {
    return false;  // Set is full
  }
}

bool removeFromSet(Set* set, u32 element) {
  for (size_t i = 0; i < MAX_PROCESSES; i++) {
    if (set->elements[i] == element) {
      set->elements[i] = 0;
      // Shift elements to the left to fill the gap
      for (size_t j = i; j < MAX_PROCESSES - 1; j++) {
        set->elements[j] = set->elements[j + 1];
      }
      set->size--;
      return true;  // Element removed successfully
    }
  }
  return false;  // Element is not in the set
}

bool isInSet(const Set* set, u32 element) {
  for (size_t i = 0; i < set->size; i++) {
    if (set->elements[i] == element) {
      return true;  // Element is in the set
    }
  }
  return false;  // Element is not in the set
}

void printSet(const Set* set) {
  printf("Set: ");
  for (size_t i = 0;  i < set->size; i++) {
    printf("%ld ", set->elements[i]);
  }
  printf("\n");
}

bool isSetEmpty(const Set* set)
{
  return set->size == 0;
}
