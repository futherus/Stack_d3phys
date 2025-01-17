/**
 * @file
 * @brief  Stack
 * @author d3phys
 * @date   07.10.2021
 */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "include/stack.h"
#include "include/log.h"
#include "include/hash.h"
#include "include/config.h"

#ifdef UNPROTECT
#undef HASH_PROTECT
#undef CANARY_PROTECT
#endif /* UNPROTECT */

static const int FILL_BYTE = 'u';
static inline const item_t get_poison(const int byte);
static item_t POISON = get_poison(FILL_BYTE);

static const size_t INIT_CAP       = 8;
static const size_t CAP_FACTOR     = 2;
static const size_t CAP_MAX        = ~(SIZE_MAX >> 1);

static inline int expandable(const stack_t *const stk);
static inline int shrinkable(const stack_t *const stk);

#ifdef CANARY_PROTECT
static inline canary_t *left_canary(const void *const items, const size_t capacity);
static inline canary_t *right_canary(const void *const items, const size_t capacity);
#endif /* CANARY_PROTECT */

#ifdef HASH_PROTECT
static hash_t hash_stack(stack_t *const stk, int seed = SEED);
#endif /* HASH_PROTECT */

static inline const char *const indicate_err(int condition);
static inline void set_error(int *const error, int value);

static item_t *realloc_stack(const stack_t *const stk, const size_t capacity);

static int verify_stack(stack_t *const stk);
static int verify_empty_stack(const stack_t *const stk);

/**
 * @brief Calculates stack hash
 *
 * @param stk  Stack
 * @param seed Hash algorithm seed
 *
 * Calculates hash using murmur2 hash algorithm. 
 * Uses stack's location in memory and check every byte. 
 */
#ifdef HASH_PROTECT
static hash_t hash_stack(stack_t *const stk, int seed)
{
        assert(stk);

        hash_t hash = stk->hash;
        stk->hash = 0;

        hash_t stk_hash  = murmur_hash(stk, sizeof(stack_t), seed);
        hash_t data_hash = murmur_hash(stk->items, stk->capacity * sizeof(item_t), seed);

        stk->hash = hash;
        return stk_hash ^ data_hash;
}
#endif /* HASH_PROTECT */

/**
 * @brief Reallocates stack memory 
 *
 * @param stk Stack to reallocate
 * @param stk Stack's new capacity
 *
 * It is ANSI realloc() function wrapper. 
 * It allocates additional memory and repositions canaries 
 * if canary protection defined.
 */
static item_t *realloc_stack(const stack_t *const stk, const size_t capacity)
{
        assert(stk);
        size_t cap = capacity * sizeof(item_t);

#ifdef CANARY_PROTECT
        cap += sizeof(void *) - cap % sizeof(void *) + 2 * sizeof(canary_t);
#endif

        item_t *items = stk->items;
$       (items  = (item_t *)realloc(items, cap);)

        if (!items) {
                log("Invalid stack reallocation: %s\n", strerror(errno));
                return nullptr;
        }

$       (memset(items + stk->size, FILL_BYTE, cap - stk->size * sizeof(item_t));)

#ifdef CANARY_PROTECT
        *right_canary(items, capacity) = CANARY ^ (size_t)items;
        *left_canary (items, capacity) = CANARY ^ (size_t)items;
#endif

        return items;
}

stack_t *const construct_stack(stack_t *const stk, int *const error)
{
        assert(stk);
        item_t *items = nullptr;
        int err = 0;

#ifndef UNPROTECT
$       (err = verify_empty_stack(stk);)
#endif  /* UNPROTECT */

        if (err) {
                log("Can't construct (stack is not empty)\n");
                goto finally;
        }

        items = realloc_stack(stk, INIT_CAP);
        if (!items) {
                log("Invalid stack memory allocation\n");
                err = STK_BAD_ALLOC;
                goto finally;
        }

        stk->capacity = INIT_CAP;
        stk->items    = (item_t *)items;
        stk->size     = 0;

#ifdef CANARY_PROTECT
        stk->left_canary  = CANARY;
        stk->right_canary = CANARY;
#endif /* CANARY_PROTECT */

#ifdef HASH_PROTECT
$       (stk->hash = hash_stack(stk);)
#endif /* HASH_PROTECT */

#ifndef UNPROTECT
$       (err = verify_stack(stk);)
#endif /* UNPROTECT */

finally:
        if (err) {
                set_error(error, err);
                log_dump(stk);
                return nullptr;
        }

        return stk;
}

void push_stack(stack_t *const stk, const item_t item, int *const error) 
{
        assert(stk);
        int err = 0;

#ifndef UNPROTECT
$       (err = verify_stack(stk);)
#endif /* UNPROTECT */

        if (err) {
                log("Can't push to invalid stack\n");
                goto finally;
        }

        if (expandable(stk)) {
                size_t capacity = stk->capacity;
                capacity *= CAP_FACTOR;

$               (void *items = realloc_stack(stk, capacity);)
                if (!items) {
                        log("Invalid stack expanding: %s\n", strerror(errno));
                        err = STK_BAD_ALLOC;
                        goto finally;
                }

                stk->capacity = capacity;
                stk->items    = (item_t *)items;
        }

        stk->items[stk->size++] = item;

#ifdef HASH_PROTECT
$       (stk->hash = hash_stack(stk);)
#endif /* HASH_PROTECT */

#ifndef UNPROTECT
$       (err = verify_stack(stk);)
#endif /* UNPROTECT */

finally:
        if (err) {
                set_error(error, err);
                log_dump(stk);
        }
}

item_t pop_stack(stack_t *const stk, int *const error)
{
        assert(stk);
        int err = 0;

        size_t item = POISON;

#ifndef UNPROTECT
$       (err = verify_stack(stk);)
#endif /* UNPROTECT */

        if (err) {
                log("Can't pop item from invalid stack\n");
                goto finally;
        }

        if (stk->size == 0) {
                log("Can't pop from an empty stack\n");
                err = STK_EMPTY_POP;
                goto finally;
        }

        if (shrinkable(stk)) {
                size_t capacity = stk->capacity;
                capacity /= CAP_FACTOR;

$               (void *items = realloc_stack(stk, capacity);)
                if (!items) {
                        log("Invalid stack shrinking: %s\n", strerror(errno));
                        err = STK_BAD_ALLOC;
                        goto finally;
                }

                stk->items = (item_t *)items;
                stk->capacity = capacity;
        }

        item = stk->items[--stk->size];
        stk->items[stk->size] = POISON;

#ifdef HASH_PROTECT
$       (stk->hash = hash_stack(stk);)
#endif /* HASH_PROTECT */

#ifndef UNPROTECT
$       (err = verify_stack(stk);)
#endif /* UNPROTECT */

finally:
        if (err) {
                set_error(error, err);
                log_dump(stk);
        }

        return item;
}

stack_t *const destruct_stack(stack_t *const stk) 
{
        assert(stk);

        if (stk->items)
                free(stk->items);
        stk->items        = nullptr;

        stk->capacity     = 0;
        stk->size         = 0;

#ifdef HASH_PROTECT
        stk->hash         = 0;
#endif /* HASH_PROTECT */

#ifdef CANARY_PROTECT
        stk->left_canary  = 0;
        stk->right_canary = 0;
#endif /* CANARY_PROTECT */

        return stk;
}

static int verify_empty_stack(const stack_t *const stk)
{
        assert(stk);
        int vrf = 0x00000000;

        if (stk->items) 
                vrf |= INVALID_ITEMS;

        if (stk->capacity)
                vrf |= INVALID_CAPACITY;

        if (stk->size) 
                vrf |= INVALID_SIZE;

#ifdef HASH_PROTECT
        if (stk->hash)
                vrf |= INVALID_HASH;
#endif /* HASH_PROTECT */

#ifdef CANARY_PROTECT
        if (stk->left_canary)
                vrf |= INVALID_STK_LCNRY;
        if (stk->right_canary)
                vrf |= INVALID_STK_RCNRY;
#endif /* CANARY_PROTECT */

        return vrf;
}

static int verify_stack(stack_t *const stk)
{
        assert(stk);
        int vrf = 0x00000000;

        if (stk->capacity > CAP_MAX || stk->capacity < INIT_CAP)
                vrf |= INVALID_CAPACITY;

        if (stk->size > stk->capacity) 
                vrf |= INVALID_SIZE;

#ifdef CANARY_PROTECT
        canary_t cnry = CANARY ^ (canary_t)stk->items;

        if (stk->items != nullptr) {

                if (*left_canary (stk->items, stk->capacity) != cnry)
                        vrf |= INVALID_DATA_LCNRY;

                if (*right_canary(stk->items, stk->capacity) != cnry)
                        vrf |= INVALID_DATA_RCNRY;
        }

        if (stk->left_canary  != CANARY)
                vrf |= INVALID_STK_LCNRY;

        if (stk->right_canary != CANARY)
                vrf |= INVALID_STK_RCNRY;
#endif /* CANARY_PROTECT */

#ifdef HASH_PROTECT
        if (stk->items != nullptr) {
                if (stk->hash != hash_stack(stk))
                        vrf |= INVALID_HASH;
        }
#endif /* HASH_PROTECT */

        return vrf;
}

static inline const item_t get_poison(const int byte) 
{
        item_t poison = 0;
        memset((void *)&poison, byte, sizeof(item_t));
        return poison;
}

static inline int expandable(const stack_t *const stk)
{
        assert(stk);
        return stk->capacity == stk->size;
}

static inline int shrinkable(const stack_t *const stk) 
{
        assert(stk);
        return stk->capacity / (CAP_FACTOR * CAP_FACTOR) + 1 >= stk->size && 
               stk->capacity > INIT_CAP;
}

static inline void set_error(int *const error, int value) 
{
        if (error)
                *error = value;
}

#ifdef CANARY_PROTECT
static inline canary_t *left_canary(const void *const items, const size_t capacity)
{
        assert(items);
        return (canary_t *)((char *)items - sizeof(canary_t));
}
#endif /* CANARY_PROTECT */

#ifdef CANARY_PROTECT
static inline canary_t *right_canary(const void *const items, const size_t capacity)  
{
        assert(items);
        return (canary_t *)((char *)items + 
                            (sizeof(item_t) * capacity) + sizeof(void *) -
                            (sizeof(item_t) * capacity) % sizeof(void *));
}
#endif /* CANARY_PROTECT */

static inline const char *const indicate_err(int condition)
{
        if (condition)
                return "<font color=\"red\"><b>error</b></font>"; 
        else
                return "<font color=\"green\"><b>ok</b></font>";
}

/**
 * @brief Prints weird stack dump
 *
 * @param stk Stack to dump
 *
 * It is scary! Stay away from him...
 */
void dump_stack(stack_t *const stk)
{
        assert(stk);

        if (stk->items == nullptr) {

                int vrf = verify_empty_stack(stk);

                log_buf("----------------------------------------------\n");
                log_buf(" Empty stack: %s\n",                    indicate_err(vrf));
                log_buf(" Size:     %10ld %s\n", stk->size,     indicate_err(vrf & INVALID_SIZE));
                log_buf(" Capacity: %10ld %s\n", stk->capacity, indicate_err(vrf & INVALID_CAPACITY));
                log_buf(" Address start: nullptr\n");
                log_buf("----------------------------------------------\n");

        } else {

                int vrf = verify_stack(stk);

                log_buf("----------------------------------------------\n");
                log_buf(" Stack: %s\n",                  indicate_err(vrf));
                log_buf(" Size:     %15ld %s\n",      stk->size, indicate_err(vrf & INVALID_SIZE));
                log_buf(" Capacity: %15ld %s\n",  stk->capacity, indicate_err(vrf & INVALID_CAPACITY));
                log_buf(" Address start: 0x%lx\n", (size_t)stk->items);
                log_buf(" Address   end: 0x%lx\n", (size_t)stk->items + 
                                        sizeof(item_t) * stk->capacity);
                log_buf("----------------------------------------------\n");

#ifdef HASH_PROTECT 
                log_buf(" Hash       (hex): %8x %s\n", hash_stack(stk), 
                                                        indicate_err(vrf & INVALID_HASH));
                log_buf(" Saved hash (hex): %8x\n", stk->hash);
                log_buf("----------------------------------------------\n");
#endif  /* HASH_PROTECT */

#ifdef CANARY_PROTECT
                log_buf(" Left  stack canary(hex) = %lx %s\n", stk->left_canary,
                                        indicate_err(vrf & INVALID_STK_LCNRY));

                log_buf(" Right stack canary(hex) = %lx %s\n", stk->right_canary,
                                        indicate_err(vrf & INVALID_STK_RCNRY));
                log_buf("----------------------------------------------\n");

                log_buf(" Left  data canary(hex) =  %lx %s\n Address: 0x%lx\n", 
                               *left_canary(stk->items, stk->capacity), 
                                indicate_err(vrf & INVALID_DATA_LCNRY),
                        (size_t)left_canary(stk->items, stk->capacity));

                log_buf("\n");

                log_buf(" Right data canary(hex) =  %lx %s\n Address: 0x%lx\n", 
                               *right_canary(stk->items, stk->capacity), 
                                indicate_err(vrf & INVALID_DATA_RCNRY),
                        (size_t)right_canary(stk->items, stk->capacity));
                log_buf("----------------------------------------------\n");
#endif  /* CANARY_PROTECT */

                for (size_t i = 0; i < stk->capacity; i++) {
                        if (stk->items[i] == POISON) {
                                log_buf("| 0x%.4lX stack[%7ld] = %18s |\n", 
                                        sizeof(*stk->items) * i, i, "poison");
                        } else {
                                log_buf("| 0x%.4lX stack[%7ld] = %18d |\n", 
                                        sizeof(*stk->items) * i, i, stk->items[i]);
                        }
                }

                log_buf("----------------------------------------------\n");

        }

        log_buf("\n\n\n");
        log_flush();
}


