#ifndef REASONS_REPL_H
#define REASONS_REPL_H

#include "reasons/runtime.h"
#include "reasons/eval.h"
#include "reasons/debugger.h"
#include <stdbool.h>

/* Opaque REPL state structure */
typedef struct REPLState REPLState;

/* ======== PUBLIC API ======== */

/**
 * Creates a new REPL instance
 * 
 * @param env Runtime environment to use
 * @param eval_ctx Evaluation context to use
 * @return New REPL instance or NULL on failure
 */
REPLState* repl_create(runtime_env_t *env, eval_context_t *eval_ctx);

/**
 * Destroys a REPL instance and releases all resources
 * 
 * @param repl REPL instance to destroy
 */
void repl_destroy(REPLState *repl);

/**
 * Runs the REPL main loop
 * 
 * @param repl REPL instance to run
 */
void repl_run(REPLState *repl);

/**
 * Sets verbose output mode
 * 
 * @param repl REPL instance
 * @param verbose True to enable verbose output
 */
void repl_set_verbose(REPLState *repl, bool verbose);

/**
 * Gets verbose output mode status
 * 
 * @param repl REPL instance
 * @return True if verbose mode is enabled
 */
bool repl_is_verbose(REPLState *repl);

/**
 * Enters debugger mode
 * 
 * @param repl REPL instance
 */
void repl_enter_debug_mode(REPLState *repl);

/**
 * Exits debugger mode
 * 
 * @param repl REPL instance
 */
void repl_exit_debug_mode(REPLState *repl);

/**
 * Checks if debugger mode is active
 * 
 * @param repl REPL instance
 * @return True if in debugger mode
 */
bool repl_in_debug_mode(REPLState *repl);

/**
 * Gets debugger instance associated with REPL
 * 
 * @param repl REPL instance
 * @return Debugger state instance
 */
DebuggerState* repl_get_debugger(REPLState *repl);

/**
 * Gets runtime environment associated with REPL
 * 
 * @param repl REPL instance
 * @return Runtime environment
 */
runtime_env_t* repl_get_env(REPLState *repl);

/**
 * Gets evaluation context associated with REPL
 * 
 * @param repl REPL instance
 * @return Evaluation context
 */
eval_context_t* repl_get_eval_context(REPLState *repl);

/**
 * Loads and executes a script file
 * 
 * @param repl REPL instance
 * @param filename Path to script file
 */
void repl_load_script(REPLState *repl, const char *filename);

/**
 * Prints REPL help information
 */
void repl_print_help(void);

/**
 * Gets currently loaded script filename
 * 
 * @param repl REPL instance
 * @return Current script filename or NULL
 */
const char* repl_get_current_script(REPLState *repl);

/**
 * Gets total lines processed in REPL session
 * 
 * @param repl REPL instance
 * @return Line count
 */
unsigned repl_get_line_count(REPLState *repl);

/**
 * Clears last error message
 * 
 * @param repl REPL instance
 */
void repl_clear_error(REPLState *repl);

/**
 * Gets last error message
 * 
 * @param repl REPL instance
 * @return Last error message or NULL
 */
const char* repl_get_last_error(REPLState *repl);

#endif /* REASONS_REPL_H */
