/** @file
 * Implementacja rozszerzającej się w miarę potrzeb tablicy znaków oraz
 * funkcji z nią związanych.
 *
 * @author Jan Kociniak <jk394348@students.mimuw.edu.pl>
 * @date 01.06.2018
 */
#include "dynamic_string.h"

dyn_str dyn_str_init(void) {
    dyn_str new_dyn_str = malloc(sizeof(struct dynamic_string));

    if (new_dyn_str != NULL) {
        new_dyn_str->str = malloc(sizeof(char));

        if (new_dyn_str->str == NULL)
            return NULL;

        new_dyn_str->str[0] = '\0';
        new_dyn_str->size = 1;
        new_dyn_str->used = 1;
    }

    return new_dyn_str;
}

bool dyn_str_add(dyn_str str, char c) {
    if (str->used == str->size) {
        str->size *= 2;
        void *str_new = realloc(str->str, str->size);

        if (str_new == NULL)
            return false;

        str->str = str_new;
    }

    str->str[str->used - 1] = c;
    str->str[(str->used)++] = '\0';
    return true;
}

void dyn_str_reset(dyn_str str) {
    if (str != NULL) {
        free(str->str);
        str->str = malloc(sizeof(char));

        str->str[0] = '\0';
        str->size = 1;
        str->used = 1;
    }
}

void dyn_str_delete(dyn_str str) {
    if (str != NULL) {
        free(str->str);
        free(str);
    }
}