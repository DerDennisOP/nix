#ifndef NIX_API_EVAL_CACHE_H
#define NIX_API_EVAL_CACHE_H
/** @addtogroup libflake
 * @{
 */
/** @file
 * @brief Bindings to the Nix flake evaluation cache (`AttrCursor`)
 *
 * These bindings expose a cursor-driven, eval-cache-warm walk over a locked
 * flake's output attributes. When the flake source is immutable the walk is
 * served from the on-disk cache; otherwise it falls back to live evaluation.
 */

#include "nix_api_util.h"
#include "nix_api_expr.h"
#include "nix_api_flake.h"

#ifdef __cplusplus
extern "C" {
#endif
// cffi start

/**
 * @brief An open evaluation cache for a locked flake.
 * @see nix_eval_cache_open
 * @see nix_eval_cache_free
 */
typedef struct nix_eval_cache nix_eval_cache;

/**
 * @brief A cursor into the attribute tree of an evaluation cache.
 * @see nix_eval_cache_get_root
 * @see nix_attr_cursor_free
 */
typedef struct nix_attr_cursor nix_attr_cursor;

/**
 * @brief Open the evaluation cache for a locked flake.
 * @param[out] context Optional, stores error information
 * @param[in] state The eval state to evaluate the flake in
 * @param[in] locked_flake The locked flake to open the cache for
 * @return A new nix_eval_cache or NULL on failure. Free with nix_eval_cache_free.
 */
nix_eval_cache * nix_eval_cache_open(nix_c_context * context, EvalState * state, nix_locked_flake * locked_flake);

/**
 * @brief Release the resources associated with a nix_eval_cache. Does not fail.
 */
void nix_eval_cache_free(nix_eval_cache * cache);

/**
 * @brief Get a cursor pointing at the root of the cache's attribute tree.
 * @param[out] context Optional, stores error information
 * @param[in] cache The evaluation cache
 * @return A new nix_attr_cursor or NULL on failure. Free with nix_attr_cursor_free.
 */
nix_attr_cursor * nix_eval_cache_get_root(nix_c_context * context, nix_eval_cache * cache);

/**
 * @brief Commit pending writes and checkpoint the cache's SQLite WAL into the
 * main `.sqlite` file.
 *
 * Persists eval-cache entries written so far so a reader of the database file
 * (without its `-wal` sidecar) sees them, without closing the cache. Intended
 * for long-lived evaluators that keep the cache open across many evaluations.
 * @param[out] context Optional, stores error information
 * @param[in] cache The evaluation cache
 * @return NIX_OK on success.
 */
nix_err nix_eval_cache_commit(nix_c_context * context, nix_eval_cache * cache);

/**
 * @brief Descend into a child attribute by name.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The parent cursor
 * @param[in] name The attribute name to look up
 * @return A new nix_attr_cursor, or NULL if the attribute is missing or on
 * failure. Check the context to distinguish a missing attribute from an error.
 * Free the result with nix_attr_cursor_free.
 */
nix_attr_cursor * nix_attr_cursor_maybe_get_attr(nix_c_context * context, nix_attr_cursor * cursor, const char * name);

/**
 * @brief Release the resources associated with a nix_attr_cursor. Does not fail.
 */
void nix_attr_cursor_free(nix_attr_cursor * cursor);

/**
 * @brief Enumerate the attribute names of the attrset at the cursor.
 * @param[out] context Optional, stores error information
 * @param[in] state The eval state, used to resolve symbols to names
 * @param[in] cursor The cursor, which must point at an attrset
 * @param[in] callback Called once per attribute name
 * @param[in] user_data Optional, passed to the callback
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_attrs(
    nix_c_context * context,
    EvalState * state,
    nix_attr_cursor * cursor,
    void (*callback)(const char * name, void * user_data),
    void * user_data);

/**
 * @brief Determine whether the cursor points at a derivation.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The cursor
 * @param[out] out Set to whether the attrset is a derivation
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_is_derivation(nix_c_context * context, nix_attr_cursor * cursor, bool * out);

/**
 * @brief Force the derivation at the cursor and report its `.drv` store path.
 * @param[out] context Optional, stores error information
 * @param[in] state The eval state, whose store is used to print the path
 * @param[in] cursor The cursor, which must point at a derivation
 * @param[in] callback Called with the store path string
 * @param[in] user_data Optional, passed to the callback
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_drv_path(
    nix_c_context * context,
    EvalState * state,
    nix_attr_cursor * cursor,
    nix_get_string_callback callback,
    void * user_data);

/**
 * @brief Read the string value at the cursor.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The cursor, which must point at a string
 * @param[in] callback Called with the string value
 * @param[in] user_data Optional, passed to the callback
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_string(
    nix_c_context * context, nix_attr_cursor * cursor, nix_get_string_callback callback, void * user_data);

/**
 * @brief Read the boolean value at the cursor.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The cursor, which must point at a bool
 * @param[out] out Set to the boolean value
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_bool(nix_c_context * context, nix_attr_cursor * cursor, bool * out);

/**
 * @brief Read a list of strings at the cursor.
 * @param[out] context Optional, stores error information
 * @param[in] cursor The cursor, which must point at a list of strings
 * @param[in] callback Called once per list element
 * @param[in] user_data Optional, passed to the callback
 * @return NIX_OK on success.
 */
nix_err nix_attr_cursor_get_list_of_strings(
    nix_c_context * context,
    nix_attr_cursor * cursor,
    void (*callback)(const char * value, void * user_data),
    void * user_data);

// cffi end
#ifdef __cplusplus
} // extern "C"
#endif
/**
 * @}
 */
#endif // NIX_API_EVAL_CACHE_H
