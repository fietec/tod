/*
  clags.h - A simple declarative command line arguments parser for C

  Version: 1.1.0

  MIT License

  Copyright (c) 2026 Constantijn de Meer

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

/*
  Clags Syntax Reference
  ======================

  Positional arguments
  --------------------

  <arg>        required positional argument
  [arg]        optional positional argument

  <arg..>      positional list argument (one or more values)
  [arg..]      optional positional list argument


  List Termination
  ----------------

  If a list terminator is configured (e.g. "::"):

  <list1..> :: <list2..> :: [arg]

  Without a terminator, a positional list must be last.


  Options
  -------

  Options may appear anywhere while option parsing is enabled.

  Long options:

  --opt VALUE
  --opt=VALUE

  Short options:

  -o VALUE  // separate argument
  -oValue   // attached argument


  Option Lists
  ------------

  If an option is configured as a list, each occurrence appends one value.

  --file a --file b --file c
  -f a -f b -f c

  Comma-separated values are NOT supported.


  Flags
  -----

  Flags do not take values.

  Short flag:

  -h

  Long flag:

  --help

  Combined short flags:

  -abc    ==  -a -b -c

  Note: If a short option that takes a value appears in a combined group,
        the rest of the string is treated as its argument:

        -wovalue    // -w is a flag, -o consumes 'value'


  Subcommands
  -----------

  Subcommands are positional arguments with their own config.

  program <subcmd> [SUBCMD_ARGS]

  Parsing continues in the selected subcommand's config.


  Special Tokens
  --------------

  Option parsing toggle (`--`):

  `--` disables option/flag parsing from this point onward.

  If the config enables toggling, `--` can re-enable parsing.
  This is an add-on feature of clags, not POSIX.

  Ignored Arguments (`ignore_prefix`):

  Arguments prefixed with the configured ignore prefix are ignored.

  For example with the ignore prefix defined as "!":

  !ignored  // argument is ignored


  Value Types
  -----------

  This table describes the supported value types and their expected syntax.

  Value Type    | Syntax / Examples           | Notes
  --------------------------------------------------------------------------
  string        | "hello", world              | Any sequence of characters. Stored as `char*`.
  bool          | true, false, yes, N         | Case-insensitive. Stored as `bool`.

  int8/uint8/   | 0, -128, 127, 0x1F, 0b1010  | Signed and unsigned integers of various sizes.
  int32/uint32/                               | Accepts decimal, hex (0x), octal (0), and binary (0b) notation.
  int64/uint64                                | Values must fit in the type’s bounds. Stored as the respective `int<size>_t`.

  double        | 3.14, -0.001, 2e10          | Floating-point value. Stored as `double`.
  choice        | "red", "green", "blue"      | Must match one of the configured choices. Case-sensitive by default, can be disabled. Stored as `clags_choice_t*`.
  path          | /home/user/docs, C:\Windows | Any filesystem path. Stored as `char*`.
  file          | /etc/passwd, myfile.txt     | Must be a regular file. Stored as `char*`.
  dir           | /tmp, C:\Users              | Must be a directory. Stored as `char*`.
  size          | 1024, 10KiB, 2MB            | Supports decimal and binary suffixes. Case-insensitive. Stored as `clags_fsize_t`.
  time_s        | 10s, 5m, 2h                 | Time in seconds; supports s/m/h/d suffixes. Case-insensitive. Stored as `clags_time_t`.
  time_ns       | 100ns, 1us, 5ms, 2s         | Time in nanoseconds; supports ns/us/ms/s/m/h/d suffixes. Case-insensitive. Stored as `clags_time_t`.
  custom        | depends on user function    | Validation done via a custom function pointer.
*/

#ifndef CLAGS_H
#define CLAGS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <float.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <sys/stat.h>

#ifdef _WIN32
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFREG) != 0)
#endif // S_ISREG
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFDIR) != 0)
#endif // S_ISDIR
#define stat _stat
#define strcasecmp _stricmp
#endif // _WIN32

// memory allocation functions; can be overridden
#ifndef CLAGS_FREE
#define CLAGS_FREE free       // default free function
#endif // CLAGS_FREE

#ifndef CLAGS_CALLOC
#define CLAGS_CALLOC calloc   // default calloc function
#endif // CLAGS_CALLOC

#ifndef CLAGS_REALLOC
#define CLAGS_REALLOC realloc // default realloc function
#endif // CLAGS_REALLOC

// the initial capacity of lists
#ifndef CLAGS_LIST_INIT_CAPACITY
#define CLAGS_LIST_INIT_CAPACITY 8
#endif // CLAGS_LIST_INIT_CAPACITY

// the character column at which ':' appears in `clags_usage` output
// you can adjust this value to control the alignment of argument descriptions
#ifndef CLAGS_USAGE_ALIGNMENT
#define CLAGS_USAGE_ALIGNMENT 36
#endif // CLAGS_USAGE_ALIGNMENT

#define CLAGS__USAGE_PRINTF_ALIGNMENT -(CLAGS_USAGE_ALIGNMENT-4)
#define CLAGS__USAGE_TEMP_BUFFER_SIZE (CLAGS_USAGE_ALIGNMENT-3)

// macro for enabling printf-like format checks in `clags_sb_appendf`
#if defined(__GNUC__) || defined(__clang__)
#    ifdef __MINGW_PRINTF_FORMAT
#        define CLAGS__PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) __attribute__ ((format (__MINGW_PRINTF_FORMAT, STRING_INDEX, FIRST_TO_CHECK)))
#    else
#        define CLAGS__PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK) __attribute__ ((format (printf, STRING_INDEX, FIRST_TO_CHECK)))
#    endif // __MINGW_PRINTF_FORMAT
#else
#    define CLAGS__PRINTF_FORMAT(STRING_INDEX, FIRST_TO_CHECK)
#endif

typedef enum{
    Clags_Info,
    Clags_Warning,
    Clags_Error,
    Clags_ConfigWarning,
    Clags_ConfigError,
    Clags_NoLogs,        // disable all logs
} clags_log_level_t;

typedef struct clags_config_t clags_config_t;
typedef bool (*clags_custom_verify_func_t)(clags_config_t *config, const char *arg_name, const char *arg, void *variable);       // the function type for custom verifiers
typedef bool (clags_verify_func_t)(clags_config_t *config, const char *arg_name, const char *arg, void *variable, void *verify);
typedef clags_verify_func_t *clags_verify_func_ptr_t;
typedef void (*clags_log_handler_t)(clags_log_level_t level, const char *format, va_list args);                                  // the function type of custom log handlers
typedef void (*clags_callback_func_t)(clags_config_t *config);                                                                   // the function type of callback functions
typedef uint64_t clags_fsize_t;
typedef uint64_t clags_time_t;

// all available value verifiers
clags_verify_func_t clags__verify_string;
clags_verify_func_t clags__verify_custom;
clags_verify_func_t clags__verify_subcmd;
clags_verify_func_t clags__verify_bool;
clags_verify_func_t clags__verify_int8;
clags_verify_func_t clags__verify_uint8;
clags_verify_func_t clags__verify_int32;
clags_verify_func_t clags__verify_uint32;
clags_verify_func_t clags__verify_int64;
clags_verify_func_t clags__verify_uint64;
clags_verify_func_t clags__verify_double;
clags_verify_func_t clags__verify_choice;
clags_verify_func_t clags__verify_path;
clags_verify_func_t clags__verify_file;
clags_verify_func_t clags__verify_dir;
clags_verify_func_t clags__verify_size;
clags_verify_func_t clags__verify_time_s;
clags_verify_func_t clags__verify_time_ns;

// the defintion of all supported value types. Format: (enum value, verification function, type name)
// if an argument’s `value_type` is not explicitly set, `Clags_String` is used by default.
#define clags__types                                                                                                                                        \
    X(Clags_String, clags__verify_string,  "string"  ) /* string value; variable type: char*                                                             */ \
    X(Clags_Custom, clags__verify_custom,  "custom"  ) /* custom verification function; variable type: depends on verify function                        */ \
    X(Clags_Bool,   clags__verify_bool,    "bool"    ) /* boolean value; variable type: bool                                                             */ \
    X(Clags_Int8,   clags__verify_int8,    "int8"    ) /* signed 8-bit integer; variable type: int8_t                                                    */ \
    X(Clags_UInt8,  clags__verify_uint8,   "uint8"   ) /* unsigned 8-bit integer; variable type: uint8_t                                                 */ \
    X(Clags_Int32,  clags__verify_int32,   "int32"   ) /* signed 32-bit integer; variable type: int32_t                                                  */ \
    X(Clags_UInt32, clags__verify_uint32,  "uint32"  ) /* unsigned 32-bit integer; variable type: uint32_t                                               */ \
    X(Clags_Int64,  clags__verify_int64,   "int64"   ) /* signed 64-bit integer; variable type: int64_t                                                  */ \
    X(Clags_UInt64, clags__verify_uint64,  "uint64"  ) /* unsigned 64-bit integer; variable type: uint64_t                                               */ \
    X(Clags_Double, clags__verify_double,  "double"  ) /* floating-point value; variable type: double                                                    */ \
    X(Clags_Choice, clags__verify_choice,  "choice"  ) /* selects one value from a set of choices; variable type: clags_choice_t*                        */ \
    X(Clags_Path,   clags__verify_path,    "path"    ) /* valid filesystem path; variable type: char*                                                    */ \
    X(Clags_File,   clags__verify_file,    "file"    ) /* path to a regular file; variable type: char*                                                   */ \
    X(Clags_Dir,    clags__verify_dir,     "dir"     ) /* path to a directory; variable type: char*                                                      */ \
    X(Clags_Size,   clags__verify_size,    "size"    ) /* size in bytes (supports suffixes like KiB/MB); variable type: clags_fsize_t                    */ \
    X(Clags_TimeS,  clags__verify_time_s,  "time_s"  ) /* time duration in seconds (supports suffixes s/m/h/d); variable type: clags_time_t              */ \
    X(Clags_TimeNS, clags__verify_time_ns, "time_ns" ) /* time duration in nanoseconds (supports suffixes ns/us/ms/s/m/h/d); variable type: clags_time_t */ \
    X(Clags_Subcmd, clags__verify_subcmd,  "subcmd"  ) /* subcommand; variable type: clags_subcmd_t*                                                     */ \

// the definition of all error types and their respective descriptions
#define clags__errors                                                                           \
    X(Clags_Error_Ok,               "no error"                                                ) \
    X(Clags_Error_InvalidConfig,    "configuration is invalid"                                ) \
    X(Clags_Error_InvalidValue,     "argument value does not match expected type or criteria" ) \
    X(Clags_Error_InvalidOption,    "unrecognized option or flag syntax"                      ) \
    X(Clags_Error_TooManyArguments, "too many positional arguments provided"                  ) \
    X(Clags_Error_TooFewArguments,  "required positional arguments missing"                   ) \

// an auto-generated enum of all supported value types
#define X(type, func, name) type,
typedef enum{
    clags__types
} clags_value_type_t;
#undef X

// an auto-generated enum of all error types
#define X(type, desc) type,
typedef enum{
    clags__errors
} clags_error_t;
#undef X

// all available flag types
// if a flag's `type` is not explicitly set, `Clags_BoolFlag` is used by default
typedef enum {
    Clags_BoolFlag = 0,   // standard boolean flag; set to `true` when the flag occurs; variable type: bool
    Clags_ConfigFlag,     // stores a pointer to the config in which the flag was set; variable type: clags_config_t*
    Clags_CountFlag,      // tracks how many times the flag was encountered; variable type: size_t
    Clags_CallbackFlag,   // invokes a user-provided callback function each time the flag occurs; variable type: clags_flag_callback_func_t
} clags_flag_type_t;

// the definition of clags's string builder
typedef struct {
    char *items;
    size_t count;
    size_t capacity;
} clags_sb_t;

// the definition of a "generic" list
typedef struct{
    void *items;
    size_t item_size;  // set by the appropiate `clags_list_<type>` macro
    size_t count;
    size_t capacity;
} clags_list_t;

// the definition of a choice
typedef struct{
    const char *value;
    const char *description;
} clags_choice_t;

// a wrapper for choice definitions, construct with `clags_choices`
typedef struct{
    clags_choice_t *items;
    size_t count;
    bool print_no_details; // do not print the full choice descriptions in `clags_usage`, if possible
    bool case_insensitive; // match choices regardless of case
} clags_choices_t;

// the definition of a subcommand
typedef struct{
    const char *name;        // the name and identifier of a subcommand
    const char *description; // help text describing the subcommand
    clags_config_t *config;  // the config that should be used to parse the subcommand's arguments
} clags_subcmd_t;

// a wrapper for subcommand definitions, construct with `clags_subcmd`
typedef struct{
    clags_subcmd_t *items;
    size_t count;
} clags_subcmds_t;

// the definition of a positional argument, construct with `clags_positional`
typedef struct{
    void *variable;                        // pointer to store the parsed value at; type must match `value_type`, or `clags_list_t` of that type if `is_list` is set
    const char *arg_name;                  // name shown in usage for the positional argument
    const char *description;               // help text describing the positional argument
    // options
    clags_value_type_t value_type;         // type of the positional value. See `clags__types` for a list of all types
    bool is_list;                          // if true, this positional consumes multiple values into a `clags_list_t` of the `value_type`
    bool optional;                         // if true, the positional argument is optional, meaning it may be omitted
    union{                                 // only one of these should be set
        clags_custom_verify_func_t verify; // a custom verification function pointer, only if `value_type` == `Clags_Custom`
        clags_choices_t *choices;          // pointer to the choice wrapper, only if `value_type` == `Clags_Choice`
        clags_subcmds_t *subcmds;          // pointer to subcommand definitions, only if `value_type` == `Clags_Subcmd`
        void *_data;                       // internal, do not touch
    };
} clags_positional_t;

// the definition of an option argument, construct with `clags_option`
typedef struct{
    char short_flag;                       // single-character flag (e.g. 'o' for -o), '\0' if none
    const char *long_flag;                 // full-length flag (e.g. "output" for --output), NULL if none
    void *variable;                        // pointer to store the parsed value at; type must match `value_type`, or `clags_list_t` of that type if `is_list` is set
    const char *arg_name;                  // name shown in usage for the option's value (e.g. "FILE")
    const char *description;               // help text describing the option
    // options
    clags_value_type_t value_type;         // type of the option's value. See `clags__types` for a list of all types
    bool is_list;                          // if true, each occurrence appends one value to a clags_list_t
    union{                                 // only one of these should be set
        clags_custom_verify_func_t verify; // a custom verification function pointer, only if `value_type` == `Clags_Custom`
        clags_choices_t *choices;          // pointer to the choice wrapper, only if `value_type` == `Clags_Choice`
        void *_data;                       // internal, do not touch
    };
} clags_option_t;

// the definition of a flag argument, construct with `clags_flag`
typedef struct{
    char short_flag;                       // single-character flag (e.g. 'h' for -h), '\0' if none
    const char *long_flag;                 // full-length flag (e.g. "help" for --help), NULL if none
    void *variable;                        // pointer to store the flag value; type depends on `.type`
    const char *description;               // help text describing the flag
    // options
    bool exit;                             // true if parsing should exit immediately when the flag occurs
    clags_flag_type_t type;                // behavior of the flag; see `clags_flag_type_t` (BoolFlag, CountFlag, ConfigFlag, CallbackFlag)
} clags_flag_t;

// entirely internal
typedef struct{
    clags_positional_t *positional;
    size_t positional_count;
    size_t required_count;
    clags_option_t *option;
    size_t option_count;
    clags_flag_t *flags;
    size_t flag_count;
} clags_args_t;

typedef enum{
    Clags_Positional,
    Clags_Option,
    Clags_Flag,
} clags_arg_type_t;

// a wrapper for all arg types
// automatically construct with `clags_positional`, `clags_option` and `clags_flag` macros
// a `clags_arg_t` array can then be passed to `clags_config` to define a (sub)command's arguments
typedef struct{
    clags_arg_type_t type;
    union{
        clags_positional_t pos;
        clags_option_t opt;
        clags_flag_t flag;
    };
} clags_arg_t;

// the available config options
typedef struct{
    const char *ignore_prefix;        // a custom prefix that instructs the parser to ignore rguments starting with this prefix
    clags_list_t *ignored_args;       // append all ignored arguments to this list without the `ignore_prefix`
    const char *list_terminator;      // a custom list terminator that tells the parser that following positional arguments do no longer belong to the current list
    bool print_no_notes;              // do not print the `Notes` section in the usage
    bool allow_option_parsing_toggle; // allow "--" to be used to toggle option and flag parsing, one-time disabling is always enabled
    bool duplicate_strings;           // duplicate all strings instead of setting variables to the content of argv, free the allocated memory via `clags_config_free_allocs`
    clags_log_handler_t log_handler;  // a custom log handler
    clags_log_level_t min_log_level;  // the minimal log level for which to print logs
    const char *description;          // a description of the current (sub)command
} clags_options_t;

// a config for a single (sub)command
// construct with the `clags_config` macro
struct clags_config_t{
    clags_arg_t *args;                // the array of argument definitions
    size_t args_count;                // the amount of argument definitions
    clags_options_t options;          // additional settings

    // internal, set automatically
    const char *name;                 // the program name or the name of the current subcommand
    clags_config_t *parent;           // pointer to the parent (sub)command's config
    bool invalid;                     // argument definitions are invalid
    clags_list_t allocs;              // all duplicated strings allocated in this config's context, only if `options.duplicate_strings` is enabled
    clags_error_t error;              // the last error detected while parsing this config
};

// helper macros
#define clags_arr_len(arr) ((arr)==NULL?0:(sizeof(arr)/sizeof(arr[0])))
#define clags_return_defer(value) do{result = (value); goto defer;}while(0)
#define clags_assert(expr, msg) do{if(!(expr)){fprintf(stderr, "%s:%d in %s: [FATAL] Assertion failed [%s] : %s\n", __FILE__, __LINE__, __func__, #expr, (msg)); fflush(stderr); abort();}}while(0)
#define clags_unreachable(msg) do{fprintf(stderr, "%s:%d in %s: [FATAL] Unreachable: %s\n", __FILE__, __LINE__, __func__, (msg)); fflush(stderr); abort();}while(0)

/* Custom Variable Types */

// predefined list constructors for different types
// returns an empty `clags_list_t` with item_size set appropriately
// use `clags_custom_list(size)` to define a list of custom element size
#define clags__sized_list(size) (clags_list_t) {.items=NULL, .count=0, .capacity=0, .item_size=(size)}
#define clags_list()            clags__sized_list(sizeof(char*))
#define clags_string_list()     clags__sized_list(sizeof(char*))
#define clags_path_list()       clags__sized_list(sizeof(char*))
#define clags_file_list()       clags__sized_list(sizeof(char*))
#define clags_dir_list()        clags__sized_list(sizeof(char*))
#define clags_custom_list(size) clags__sized_list(size)
#define clags_bool_list()       clags__sized_list(sizeof(bool))
#define clags_int8_list()       clags__sized_list(sizeof(int8_t))
#define clags_uint8_list()      clags__sized_list(sizeof(uint8_t))
#define clags_int32_list()      clags__sized_list(sizeof(int32_t))
#define clags_uint32_list()     clags__sized_list(sizeof(uint32_t))
#define clags_int64_list()      clags__sized_list(sizeof(int64_t))
#define clags_uint64_list()     clags__sized_list(sizeof(uint64_t))
#define clags_double_list()     clags__sized_list(sizeof(double))
#define clags_size_list()       clags__sized_list(sizeof(clags_fsize_t))
#define clags_time_list()       clags__sized_list(sizeof(clags_time_t))
#define clags_choice_list()     clags__sized_list(sizeof(clags_choice_t*))

// macros for easy value extraction from lists
// `value_type` must match the type stored within the list
#define clags_list_element(list, value_type, index) ((value_type*)(list).items)[index]

// wrapper for a `clags_choice_t` array
// use designated initializers in the variadic arguments to set additional options.
// see `clags_choices_t` for all available fields
#define clags_choices(arr, ...) (clags_choices_t){.items=(arr), .count=clags_arr_len(arr), __VA_ARGS__}
// macro for getting the pointer to the index-th choice
#define clags_choice_value(choices, index) (&(choices)[index])

// wrapper for a `clags_subcmd_t` array
#define clags_subcmds(subcmds) (clags_subcmds_t){.items=(subcmds), .count=clags_arr_len(subcmds)}

/* Argument Constructors */

/*
  Define a positional argument.

  Parameters:
    var  : pointer to the variable that receives the parsed value, must be of a type matching the value type,
           or `clags_list_t` of that type if `is_list` is set, default: char*
    name : argument name shown in usage
    desc : description shown in help output

  Additional behavior (value type, lists, optionality, choices, custom verification, subcommands, etc.)
  is configured via designated initializers in the variadic arguments.
  See `clags_positional_t` for all available fields.
*/
#define clags_positional(var, name, desc, ...) (clags_arg_t){.type=Clags_Positional, .pos=(clags_positional_t){.variable=(var), .arg_name=(name), .description=(desc), __VA_ARGS__}}

/*
  Define an option argument.

  Parameters:
    sflag : short option character (e.g. 'o' for -o), or '\0' if unused
    lflag : long option name (e.g. "output"), or NULL if unused
    var   : pointer to the variable that receives the parsed value, must be of a type matching the value type,
            or `clags_list_t` of that type if `is_list` is set, default: char*
    name  : value name shown in usage (e.g. "FILE")
    desc  : description shown in help output

  Additional behavior (value type, lists, choices, custom verification, etc.)
  is configured via designated initializers in the variadic arguments.
  See `clags_option_t` for all available fields.
*/
#define clags_option(sflag, lflag, var, name, desc, ...) (clags_arg_t){.type=Clags_Option, .opt=(clags_option_t){.short_flag=(sflag), .long_flag=(lflag), .variable=(var), .arg_name=(name), .description=(desc), __VA_ARGS__}}

/*
  Define a flag argument.

  Parameters:
    sflag : short flag character (e.g. 'h' for -h), or '\0' if unused
    lflag : long flag name (e.g. "help"), or NULL if unused
    var   : pointer to the variable that receives the flag value; type depends on `.type`, default: bool
    desc  : description shown in help output

  Additional behavior (exit-on-set, count flags, config pointer storage, callbacks, etc.)
  is configured via designated initializers in the variadic arguments.
  See `clags_flag_t` for all available fields.
*/
#define clags_flag(sflag, lflag, var, desc, ...) (clags_arg_t) {.type=Clags_Flag, .flag=(clags_flag_t){.short_flag=(sflag), .long_flag=(lflag), .variable=(var), .description=(desc), __VA_ARGS__}}

// simple helpers for the common help flags
#define clags_flag_help(val)        clags_flag('h', "help", val, "print this help dialog", .exit=true)
#define clags_flag_help_config(val) clags_flag('h', "help", val, "print this help dialog", .exit=true, .type=Clags_ConfigFlag)

/* Config Constructors */

/*
  Construct a `clags_config_t` from an array of arguments.

  Parameters:
    arguments : array of `clags_arg_t` defining the positionals, options, and flags
    ...       : optional designated initializers for `clags_options_t` fields
                (e.g. .ignore_prefix="!", .list_terminator="::", .duplicate_strings=true, etc.),
                see `clags_options_t` for all available fields.
*/
#define clags_config(arguments, ...) (clags_config_t){.args=(arguments), .args_count=clags_arr_len(arguments), .allocs=(clags_list_t){.item_size=sizeof(char*)}, .options=(clags_options_t){__VA_ARGS__}}

/*
  Construct a `clags_config_t` from an array of arguments and a pre-defined options struct.
  Useful when sharing options over mutiple configs.

  Parameters:
    arguments : array of `clags_arg_t` defining positionals, options, and flags
    opts      : a fully initialized `clags_options_t` struct with custom config options
*/
#define clags_config_with_options(arguments, opts) (clags_config_t){.args=(arguments), .args_count=clags_arr_len(arguments), .allocs=(clags_list_t){.item_size=sizeof(char*)}, .options=(opts)}

/* Core Functions */

/*
  Parse arguments based on the provided config.

  Arguments:
    - argc          : the number of arguments
    - argv          : the array of arguments
    - config        : pointer to a config with argument definitions and other options

  Returns:
    clags_config_t* : pointer to the failed config. If parsing fails, the `.error` field
                      will be set to indicate the type of the encountered error.
*/
clags_config_t* clags_parse(int argc, char **argv, clags_config_t *config);

/*
  Print a detailed usage based on the provided config.

  Arguments:
    - program_name  : the name of the program
    - config        : pointer to a config with argument definitions and other options
*/
void clags_usage(const char *program_name, clags_config_t *config);

/*
  Get the index of a selected subcommand in the provided subcommand array.

  Arguments:
    - subcmds       : pointer to a clags_subcmds_t containing the subcommand array
    - subcmd        : pointer to the selected clags_subcmd_t to find

  Returns:
    int             : the index of the selected subcommand in the array,
                      or -1 if the subcommand was not found or either argument is NULL
*/
int clags_subcmd_index(clags_subcmds_t *subcmds, clags_subcmd_t *subcmd);

/*
  Get the index of a selected choice in the provided choice array.

  Arguments:
    - choices       : pointer to clags_choices_t containing the choice array
    - choice        : pointer to the selected clags_choice_t to find

  Returns:
    int             : the index of the selected choice in the array,
                      or -1 if the choice was not found or either argument is NULL
*/
int clags_choice_index(clags_choices_t *choices, clags_choice_t *choice);

/*
  Duplicate a string if string duplication is enabled in the config,
  otherwise return the original string.

  Arguments:
    - config  : pointer to the clags configuration
    - string  : the string to duplicate

  Returns:
    char*     : pointer to the duplicated string if duplication is enabled,
                otherwise the original string. Memory allocated for duplicates
                is tracked internally within the config and will be freed when the config is cleaned up.
*/
char* clags_config_duplicate_string(clags_config_t *config, const char *string);

/*
  Free all memory allocated for strings duplicated during parsing.
  This only applies if `.duplicate_strings` was enabled in the config.

  Arguments:
    - config        : pointer to the clags_config_t whose duplicated strings should be freed
*/
void clags_config_free_allocs(clags_config_t *config);

/*
  Free all lists and allocated strings of a config.
  The function does not propagate to child configs.

  Arguments:
    - config        : pointer to the config of which to free all strings and lists
*/
void clags_config_free(clags_config_t *config);

/*
  Free all memory associated with a `clags_list_t` instance.

  Arguments:
    - list          : a pointer to the list to free
*/
void clags_list_free(clags_list_t *list);

/*
  Return a description of the provided error type.

  Arguments:
    - error         : the error type

  Returns:
    const char*     : a string description of the provided error type
*/
const char* clags_error_description(clags_error_t error);

/* Logging */

/*
  Use a config's log handler to log a formatted message.

  Arguments:
    - config        : the config for which to log using its options
    - level         : the log level of the message, if less than the configs minimal log level, nothing will be logged
    - format        : the printf-style format of the message
    - ...           : the variadic format arguments
*/
void clags_log(clags_config_t *config, clags_log_level_t level, const char *format, ...) CLAGS__PRINTF_FORMAT(3, 4);
// log the content of a string builder instead of a formatted message
void clags_log_sb(clags_config_t *config, clags_log_level_t level, clags_sb_t *sb);

/* String Builder Functionality */
void clags_sb_appendf(clags_sb_t *sb, const char *format, ...);
void clags_sb_append_null(clags_sb_t *sb);
void clags_sb_free(clags_sb_t *sb);

#endif // CLAGS_H

/*
  The start of the implementation section.
  Define `CLAGS_IMPLEMENTATION` to access, otherwise this file will act as a regular header.
*/
#ifdef CLAGS_IMPLEMENTATION

#define X(type, func, name) [type] = func,
static clags_verify_func_ptr_t clags__verify_funcs[] = {
    clags__types
};
#undef X

#define X(type, func, name) [type] = name,
static const char *clags__type_names[] = {
    clags__types
};
#undef X

static inline char* clags__strdup(const char *string)
{
    if (!string) return NULL;
    size_t length = strlen(string);
    char *new_string = CLAGS_CALLOC(length+1, sizeof(char));
    clags_assert(new_string != NULL, "Out of memory!");
    return strcpy(new_string, string);
}

static inline char* clags__strchrnull(const char *string, char c)
{
    if (!string) return NULL;
    char *s = (char*) string;
    while (*s != '\0'){
        if (*s == c) return s;
        s++;
    }
    return s;
}

static inline void clags__sb_reserve(clags_sb_t *sb, size_t capacity)
{
    if (sb->capacity >= capacity) return;
    if (sb->capacity == 0) sb->capacity = CLAGS_LIST_INIT_CAPACITY;
    while (capacity > sb->capacity){
        sb->capacity *= 2;
    }
    sb->items = CLAGS_REALLOC(sb->items, sb->capacity*sizeof(*sb->items));
    clags_assert(sb->items != NULL, "Out of memory!");
}

void clags_sb_appendf(clags_sb_t *sb, const char *format, ...)
{
    va_list args, args_copy;

    va_start(args, format);
    va_copy(args_copy, args);

    int n = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);

    clags__sb_reserve(sb, sb->count + n + 1);
    char *start = sb->items + sb->count;

    vsnprintf(start, n+1, format, args);
    va_end(args);

    sb->count += n;
}

void clags_sb_append_null(clags_sb_t *sb)
{
    clags__sb_reserve(sb, sb->count+1);
    sb->items[sb->count++] = '\0';
}

void clags_sb_free(clags_sb_t *sb)
{
    if (!sb) return;
    CLAGS_FREE(sb->items);
    sb->items = NULL;
    sb->count = sb->capacity = 0;
}

void clags__default_log_handler(clags_log_level_t level, const char *format, va_list args)
{
    switch(level){
        case Clags_Info:{
            fprintf(stdout, "[INFO] ");
            vfprintf(stdout, format, args);
            fprintf(stdout, "\n");
            return;
        }break;
        case Clags_Warning:{
            fprintf(stderr, "[WARNING] ");
        }break;
        case Clags_Error:{
            fprintf(stderr, "[ERROR] ");
        }break;
        case Clags_ConfigWarning:{
            fprintf(stderr, "[CONFIG_WARNING] ");
        }break;
        case Clags_ConfigError:{
            fprintf(stderr, "[CONFIG_ERROR] ");
        }break;
        case Clags_NoLogs: return;
        default:{
            clags_unreachable("Invalid clags_log_level_t!");
        }
    }
    vfprintf(stderr, format, args);
    fprintf(stderr, "\n");
}

void clags_log(clags_config_t *config, clags_log_level_t level, const char *format, ...)
{
    if (config && config->options.min_log_level > level) return;
    va_list args;
    va_start(args, format);
    clags_log_handler_t handler = (config && config->options.log_handler)? config->options.log_handler : clags__default_log_handler;
    handler(level, format, args);
    va_end(args);
}

void clags_log_sb(clags_config_t *config, clags_log_level_t level, clags_sb_t *sb)
{
    clags_log(config, level, "%s", sb->items);
}

char* clags_config_duplicate_string(clags_config_t *config, const char *string)
{
    if (config == NULL) return (char*) string;
    char *duplicate;
    if (config->options.duplicate_strings){
        duplicate = clags__strdup(string);
        clags_assert(duplicate != NULL, "Out of memory!");

        clags_list_t *allocs = &config->allocs;
        if (allocs->item_size == 0) allocs->item_size = sizeof(char*);
        if (allocs->count >= allocs->capacity){
            size_t new_capacity = allocs->capacity ? allocs->capacity*2 : CLAGS_LIST_INIT_CAPACITY;
            allocs->items = CLAGS_REALLOC(allocs->items, allocs->item_size*new_capacity);
            clags_assert(allocs->items != NULL, "Out of memory!");
            allocs->capacity = new_capacity;
        }
        ((char**) allocs->items)[allocs->count++] = duplicate;
    } else{
        duplicate = (char*) string;
    }
    return duplicate;
}

bool clags__verify_string(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    (void) arg_name;
    if (pvalue) *(char**)pvalue = clags_config_duplicate_string(config, arg);
    return true;
}

bool clags__verify_bool(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    if (strcasecmp(arg, "true") == 0 || strcasecmp(arg, "yes") == 0 || strcasecmp(arg, "y") == 0){
        if (pvalue) *(bool*)pvalue = true;
        return true;
    } else if (strcasecmp(arg, "false") == 0 || strcasecmp(arg, "no") == 0 || strcasecmp(arg, "n") == 0){
        if (pvalue) *(bool*)pvalue = false;
        return true;
    }
    clags_log(config, Clags_Error, "Invalid boolean value for argument '%s': '%s'!", arg_name, arg);
    return false;
}

bool clags__verify_int8(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;
    long value = strtol(arg, &endptr, 0);

    if (*endptr != '\0') {
        clags_log(config, Clags_Error, "Invalid int8 value for argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    if (errno == ERANGE || value < INT8_MIN || value > INT8_MAX) {
        clags_log(config, Clags_Error, "int8 value out of range (%"PRId8" to %"PRId8") for argument '%s': '%s'!", INT8_MIN, INT8_MAX, arg_name, arg);
        return false;
    }

    if (pvalue) *(int8_t*)pvalue = (int8_t)value;
    return true;
}

bool clags__verify_uint8(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;
    unsigned long value = strtoul(arg, &endptr, 0);

    if (*endptr != '\0') {
        clags_log(config, Clags_Error, "Invalid uint8 value for argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    if (errno == ERANGE || value > UINT8_MAX || *arg == '-') {
        clags_log(config, Clags_Error, "uint8 value out of range (0 to %"PRIu8") for argument '%s': '%s'!", UINT8_MAX, arg_name, arg);
        return false;
    }

    if (pvalue) *(uint8_t*)pvalue = (uint8_t)value;
    return true;
}

bool clags__verify_int32(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;
    long value = strtol(arg, &endptr, 0);

    if (*endptr != '\0') {
        clags_log(config, Clags_Error, "Invalid int32 value for argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    if (errno == ERANGE || value < INT32_MIN || value > INT32_MAX) {
        clags_log(config, Clags_Error, "int32 value out of range (%"PRId32" to %"PRId32") for argument '%s': '%s'!", INT32_MIN, INT32_MAX, arg_name, arg);
        return false;
    }

    if (pvalue) *(int32_t*)pvalue = (int32_t)value;
    return true;
}

bool clags__verify_uint32(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;
    unsigned long value = strtoul(arg, &endptr, 0);

    if (*endptr != '\0') {
        clags_log(config, Clags_Error, "Invalid uint32 value for argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    if (errno == ERANGE || value > UINT32_MAX || *arg == '-') {
        clags_log(config, Clags_Error, "uint32 value out of range (0 to %"PRIu32") for argument '%s': '%s'!", UINT32_MAX, arg_name, arg);
        return false;
    }

    if (pvalue) *(uint32_t*)pvalue = (uint32_t)value;
    return true;
}

bool clags__verify_int64(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;
    long long value = strtoll(arg, &endptr, 0);

    if (*endptr != '\0') {
        clags_log(config, Clags_Error, "Invalid int64 value for argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    if (errno == ERANGE || value < INT64_MIN || value > INT64_MAX) {
        clags_log(config, Clags_Error, "int64 value out of range (%"PRId64" to %"PRId64") for argument '%s': '%s'!", INT64_MIN, INT64_MAX, arg_name, arg);
        return false;
    }

    if (pvalue) *(int64_t*)pvalue = (int64_t)value;
    return true;
}

bool clags__verify_uint64(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;
    unsigned long long value = strtoull(arg, &endptr, 0);

    if (*endptr != '\0') {
        clags_log(config, Clags_Error, "Invalid uint64 value for argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    if (errno == ERANGE || value > UINT64_MAX || *arg == '-') {
        clags_log(config, Clags_Error, "uint64 value out of range (0 to %"PRIu64") for argument '%s': '%s'!", UINT64_MAX, arg_name, arg);
        return false;
    }

    if (pvalue) *(uint64_t*)pvalue = (uint64_t)value;
    return true;
}

bool clags__verify_double(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;
    double value = strtod(arg, &endptr);

    if (*endptr != '\0') {
        clags_log(config, Clags_Error, "Invalid double value for argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    if (errno == ERANGE || value > DBL_MAX || value < -DBL_MAX) {
        clags_log(config, Clags_Error, "double value out of range (%lf to %lf) for argument '%s': '%s'!", DBL_MAX, -DBL_MAX, arg_name, arg);
        return false;
    }

    if (pvalue) *(double*)pvalue = value;
    return true;
}

bool clags__verify_choice(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    if (pvalue == NULL) return false;
    clags_choice_t  **pchoice = (clags_choice_t**) pvalue;
    clags_choices_t  *choices = (clags_choices_t*) data;
    for (size_t i=0; i<choices->count; ++i){
        clags_choice_t *choice = choices->items + i;
        if ((choices->case_insensitive && strcasecmp(choice->value, arg) == 0) || (!choices->case_insensitive && strcmp(choice->value, arg) == 0)){
            if (pchoice) *pchoice = choice;
            return true;
        }
    }
    clags_log(config, Clags_Error, "Invalid choice for argument '%s': '%s'!", arg_name, arg);
    return false;
}

bool clags__verify_path(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    struct stat attr;
    if (stat(arg, &attr) == -1){
        clags_log(config, Clags_Error, "Invalid path for argument '%s': '%s' : %s!", arg_name, arg, strerror(errno));
        return false;
    }
    if (pvalue) *(char**)pvalue = clags_config_duplicate_string(config, arg);
    return true;
}

bool clags__verify_file(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    struct stat attr;
    if (stat(arg, &attr) == -1){
        clags_log(config, Clags_Error, "Invalid path for argument '%s': '%s' : %s!", arg_name, arg, strerror(errno));
        return false;
    }
    if (!S_ISREG(attr.st_mode)){
        clags_log(config, Clags_Error, "Path for arguments '%s' is not a file: '%s'!", arg_name, arg);
        return false;
    }
    if (pvalue) *(char**)pvalue = clags_config_duplicate_string(config, arg);
    return true;
}

bool clags__verify_dir(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    struct stat attr;
    if (stat(arg, &attr) == -1){
        clags_log(config, Clags_Error, "Invalid path for argument '%s': '%s' : %s!", arg_name, arg, strerror(errno));
        return false;
    }
    if (!S_ISDIR(attr.st_mode)){
        clags_log(config, Clags_Error, "Path for arguments '%s' is not a dir: '%s'!", arg_name, arg);
        return false;
    }
    if (pvalue) *(char**)pvalue = clags_config_duplicate_string(config, arg);
    return true;
}

bool clags__verify_size(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;
    unsigned long long value = strtoull(arg, &endptr, 10);

    if (endptr == arg){
        clags_log(config, Clags_Error, "No leading number in size argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    clags_fsize_t factor;
    if (*endptr == '\0' || strcmp(endptr, "B") == 0) factor = 1;
    else if (strcasecmp(endptr, "KiB") == 0)             factor = 1ULL << 10;
    else if (strcasecmp(endptr, "KB")  == 0)             factor = 1000;
    else if (strcasecmp(endptr, "MiB") == 0)             factor = 1ULL << 20;
    else if (strcasecmp(endptr, "MB")  == 0)             factor = 1000000;
    else if (strcasecmp(endptr, "GiB") == 0)             factor = 1ULL << 30;
    else if (strcasecmp(endptr, "GB")  == 0)             factor = 1000000000;
    else if (strcasecmp(endptr, "TiB") == 0)             factor = 1ULL << 40;
    else if (strcasecmp(endptr, "TB")  == 0)             factor = 1000000000000;
    else {
        clags_log(config, Clags_Error, "Invalid size unit for argument '%s': '%s'!", arg_name, endptr);
        return false;
    }

    if (errno == ERANGE || value > UINT64_MAX/factor || *arg == '-') {
        clags_log(config, Clags_Error, "clags_fsize_t value out of range (0 to %"PRIu64") for argument '%s': '%s'!", UINT64_MAX, arg_name, arg);
        return false;
    }
    if (pvalue) *(clags_fsize_t*)pvalue = (clags_fsize_t)value * factor;
    return true;
}

bool clags__verify_time_s(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;

    double value = strtod(arg, &endptr);
    if (endptr == arg){
        clags_log(config, Clags_Error, "No leading number in time argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    clags_time_t factor;
    if (*endptr == '\0' || strcasecmp(endptr, "s") == 0)  factor =       1;
    else if (strcasecmp(endptr, "m")  == 0)               factor =      60;
    else if (strcasecmp(endptr, "h")  == 0)               factor =    3600;
    else if (strcasecmp(endptr, "d")  == 0)               factor = 24*3600;
    else {
        clags_log(config, Clags_Error, "Invalid time unit for argument '%s': '%s'!", arg_name, endptr);
        return false;
    }
    if (errno == ERANGE || value > UINT64_MAX/factor || value < 0){
        clags_log(config, Clags_Error, "clags_time_t value out of range (0s to %"PRIu64"s) for argument '%s': '%s'!", UINT64_MAX, arg_name, arg);
        return false;
    }
    if (pvalue) *(clags_time_t*)pvalue = (clags_time_t)(value * factor);
    return true;
}

bool clags__verify_time_ns(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    (void) data;
    char *endptr;
    errno = 0;

    double value = strtod(arg, &endptr);
    if (endptr == arg){
        clags_log(config, Clags_Error, "No leading number in time argument '%s': '%s'!", arg_name, arg);
        return false;
    }
    clags_time_t factor;
    if (*endptr == '\0' || strcasecmp(endptr, "ns") == 0)      factor = 1;
    else if (strcasecmp(endptr, "us") == 0)                    factor = 1000ULL;
    else if (strcasecmp(endptr, "ms") == 0)                    factor = 1000000ULL;
    else if (strcasecmp(endptr, "s") == 0)                     factor = 1000000000ULL;
    else if (strcasecmp(endptr, "m") == 0)                     factor = 60ULL * 1000000000ULL;
    else if (strcasecmp(endptr, "h") == 0)                     factor = 3600ULL * 1000000000ULL;
    else if (strcasecmp(endptr, "d") == 0)                     factor = 24ULL * 3600ULL * 1000000000ULL;
    else {
        clags_log(config, Clags_Error, "Invalid time unit for argument '%s': '%s'!", arg_name, endptr);
        return false;
    }
    if (errno == ERANGE || value > (double) UINT64_MAX/factor || value < 0){
        clags_log(config, Clags_Error, "clags_time_t value out of range (0ns to %"PRIu64"ns) for argument '%s': '%s'!", UINT64_MAX, arg_name, arg);
        return false;
    }
    if (pvalue) *(clags_time_t*)pvalue = (clags_time_t)(value * factor + 0.5);
    return true;
}

bool clags__verify_subcmd(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    clags_subcmds_t *subcmds = (clags_subcmds_t*) data;
    for (size_t i=0; i<subcmds->count; ++i){
        clags_subcmd_t subcmd = subcmds->items[i];
        if (strcmp(subcmd.name, arg) == 0){
            if (pvalue) *(clags_subcmd_t**) pvalue = &subcmds->items[i];
            clags_config_t *child_config = subcmd.config;
            if (child_config){
                child_config->parent = config;
            }
            return true;
        }
    }
    clags_log(config, Clags_Error, "unknown subcommand '%s' for argument '%s'!", arg, arg_name);
    return false;
}

bool clags__verify_custom(clags_config_t *config, const char *arg_name, const char *arg, void *pvalue, void *data)
{
    clags_custom_verify_func_t value_func = (clags_custom_verify_func_t) data;
    if (!value_func(config, arg_name, (char*)arg, pvalue)) {
        clags_log(config, Clags_Error, "Value for argument '%s' does not match custom criteria: '%s'!", arg_name, arg);
        return false;
    }
    return true;
}

static inline bool clags__append_to_list(clags_config_t *config, clags_value_type_t value_type, const char *arg_name, const char *arg, void *variable, void *data)
{
    clags_list_t *list = (clags_list_t*) variable;
    size_t item_size = list->item_size;
    if (list->count >= list->capacity){
        size_t new_capacity = list->capacity==0? CLAGS_LIST_INIT_CAPACITY:list->capacity*2;
        list->items = CLAGS_REALLOC(list->items, new_capacity*item_size);
        clags_assert(list->items != NULL, "Out of memory!");
        list->capacity = new_capacity;
    }
    char *ptr = (char*) list->items;
    if (clags__verify_funcs[value_type](config, arg_name, arg, ptr+item_size*list->count, data)){
        list->count++;
        return true;
    }
    return false;
}

static inline bool clags__set_arg(clags_config_t *config, clags_value_type_t value_type, const char *arg_name, const char *arg, void *variable, void *data, bool is_list)
{
    bool result;
    if (is_list){
        result = clags__append_to_list(config, value_type, arg_name, arg, variable, data);
    } else{
        result = clags__verify_funcs[value_type](config, arg_name, arg, variable, data);
    }
    if (!result) config->error = Clags_Error_InvalidValue;
    return result;
}

static inline void clags__set_flag(clags_config_t *config, clags_flag_t *flag)
{
    if (!flag->variable) return;
    switch (flag->type){
        case Clags_BoolFlag:{
            *(bool*) flag->variable = true;
        }break;
        case Clags_ConfigFlag:{
            *(clags_config_t**) flag->variable = config;
        }break;
        case Clags_CountFlag:{
            *(size_t*) flag->variable += 1;
        }break;
        case Clags_CallbackFlag:{
            ((clags_callback_func_t)flag->variable)(config);
        }break;
        default:{
            clags_unreachable("Invalid clags_flag_type_t");
        }
    }
}

bool clags__validate_positional(clags_config_t *config, clags_positional_t pos)
{
    switch (pos.value_type){
        case Clags_Subcmd:{
            if (pos.subcmds == NULL){
                clags_log(config, Clags_ConfigError, "incomplete subcommand definition for argument '%s'! Define `.subcmds` for subcommand verification!", pos.arg_name);
                return false;
            }
        } break;
        case Clags_Choice:{
            if (pos.choices == NULL){
                clags_log(config, Clags_ConfigError, "incomplete choice definition for argument '%s'! Define `.choices` for choice verification!", pos.arg_name);
                return false;
            }
        } break;
        case Clags_Custom:{
            if (pos.verify == NULL){
                clags_log(config, Clags_ConfigError, "incomplete custom verifier definition for argument '%s'! Define `.verify` for custom verification!", pos.arg_name);
                return false;
            }
        } break;
        default: break;
    }
    return true;
}

bool clags__validate_option(clags_config_t *config, clags_option_t opt)
{
    char buf[3] = {'-', '\0', '\0'};
    const char *name = opt.long_flag ? opt.long_flag
                      : (opt.short_flag ? (buf[1] = opt.short_flag, buf) : "(unnamed)");
    if (opt.short_flag == '\0' && opt.long_flag == NULL){
        clags_log(config, Clags_ConfigWarning, "option argument is unreachable. Define at least one of `short_flag` and `long_flag`.");
    }
    if (opt.long_flag && strncmp(opt.long_flag, "--", 2) == 0){
        clags_log(config, Clags_ConfigWarning,
                  "option long flag '%s' should not start with '--'. "
                  "The parser automatically handles leading '--' for long flags, "
                  "so including it in the config may cause incorrect parsing.",
                  opt.long_flag);
    }
    switch (opt.value_type){
        case Clags_Subcmd:{
            clags_log(config, Clags_ConfigError, "option argument '%s' may not be a subcommand!", name);
            return false;
        } break;
        case Clags_Choice:{
            if (opt.choices == NULL){
                clags_log(config, Clags_ConfigError, "incomplete choice definition for argument '%s'! Define `.choices` for choice verification!", name);
                return false;
            }
        } break;
        case Clags_Custom:{
            if (opt.verify == NULL){
                clags_log(config, Clags_ConfigError, "incomplete custom verifier definition for argument '%s'! Define `.verify` for custom verification!", name);
                return false;
            }
        } break;
        default: break;
    }
    return true;
}

bool clags__validate_flag(clags_config_t *config, clags_flag_t flag)
{
    if (flag.short_flag == '\0' && flag.long_flag == NULL){
        clags_log(config, Clags_ConfigWarning, "flag argument is unreachable. Define at least one of `short_flag` and `long_flag`.");
    }
    if (flag.long_flag && strncmp(flag.long_flag, "--", 2) == 0){
        clags_log(config, Clags_ConfigWarning,
                  "long flag '%s' should not start with '--'. "
                  "The parser automatically handles leading '--' for long flags, "
                  "so including it in the config may cause incorrect parsing.",
                  flag.long_flag);
    }
    switch (flag.type){
        case Clags_BoolFlag:
        case Clags_ConfigFlag:
        case Clags_CountFlag:
        case Clags_CallbackFlag:break;
        default:{
            clags_log(config, Clags_ConfigError, "invalid flag type: %d!", flag.type);
            return false;
        }
    }
    return true;
}

bool clags__validate_config(clags_config_t *config)
{
    bool result = true;
    // validate options
    if (config->options.list_terminator && strcmp(config->options.list_terminator, "--") == 0){
        clags_log(config, Clags_ConfigError,"'.list_terminator' may not be '--' because '--' is reserved for toggling option and flag parsing!");
        clags_return_defer(false);
    }
    if (config->options.ignore_prefix && strcmp(config->options.ignore_prefix, "--") == 0){
        clags_log(config, Clags_ConfigError, "'.ignore_prefix' may not be '--' since this conflicts with the long option and flag prefix!");
        clags_return_defer(false);
    }

    // validate args

    bool last_was_list = false;
    bool subcmd_found = false;
    bool optional_found = false;
    const char *last_pos_name = NULL;
    for (size_t i=0; i<config->args_count; ++i){
        switch (config->args[i].type){
            case Clags_Positional:{
                clags_positional_t pos = config->args[i].pos;
                if (!clags__validate_positional(config, pos)) clags_return_defer(false);
                if (optional_found && !pos.optional){
                    clags_log(config, Clags_ConfigError, "invalid positional argument order: required argument '%s' appears after optional argument '%s'", pos.arg_name, last_pos_name);
                    clags_return_defer(false);
                }
                optional_found = pos.optional;
                if (pos.value_type == Clags_Subcmd){
                    subcmd_found = true;
                    if (last_pos_name != NULL){
                        clags_log(config, Clags_ConfigError, "subcommand '%s' must be the only positional argument in its config!", pos.arg_name);
                        clags_return_defer(false);
                    }
                } else if (subcmd_found){
                    clags_log(config, Clags_ConfigError, "trailing positional argument after subcommand: '%s'!", pos.arg_name);
                    clags_return_defer(false);
                }
                if (last_was_list && config->options.list_terminator == NULL){
                    clags_sb_t sb = {0};
                    clags_sb_appendf(&sb, "positional argument '%s' is unreachable after list '%s'! Define '.list_terminator' in 'clags_config' to separate them", pos.arg_name, last_pos_name);
                    if (!pos.is_list){
                        clags_sb_appendf(&sb, " or make '%s' option", pos.arg_name);
                    }
                    clags_sb_appendf(&sb, ".");
                    clags_log_sb(config, Clags_ConfigError, &sb);
                    clags_sb_free(&sb);
                    clags_return_defer(false);
                }
                last_was_list = pos.is_list;
                last_pos_name = pos.arg_name;
            } break;
            case Clags_Option:{
                last_was_list = false;
                if (!clags__validate_option(config, config->args[i].opt)) return false;
            } break;
            case Clags_Flag:{
                last_was_list = false;
                if (!clags__validate_flag(config, config->args[i].flag)) return false;
            } break;
        }
    }
defer:
    if (!result) config->error = Clags_Error_InvalidConfig;
    return result;
}

void clags__sort_args(clags_args_t *args, clags_config_t *config)
{
    for (size_t i=0; i<config->args_count; ++i){
        switch(config->args[i].type){
            case Clags_Positional:{
                clags_positional_t pos = config->args[i].pos;
                args->positional[args->positional_count++] = pos;
                if (!pos.optional) args->required_count++;
            } break;
            case Clags_Option:{
                args->option[args->option_count++] = config->args[i].opt;
            } break;
            case Clags_Flag:{
                args->flags[args->flag_count++] = config->args[i].flag;
            } break;
            default: {
                clags_unreachable("Invalid clags_arg_type_t");
            }
        }
    }
}

void clags__choice_usage(clags_choices_t *choices, bool is_list)
{
    if (!choices->print_no_details || choices->count >= 6){
        printf(" (%s%s)\n        Choices%s:\n", clags__type_names[Clags_Choice], is_list?"[]":"", choices->case_insensitive? " (case-insensitive)" : "");
        for (size_t j=0; j<choices->count; ++j){
            clags_choice_t choice = choices->items[j];
            printf("          - %*s : %s\n", CLAGS__USAGE_PRINTF_ALIGNMENT+8, choice.value, choice.description);
        }
    } else{
        printf(" (%s%s:", clags__type_names[Clags_Choice], is_list?"[]":"");
        for (size_t j=0; j<choices->count; ++j){
            printf("%s%s", j>0?" | ":" ", choices->items[j].value);
        }
        printf(")");
    }
}

void clags__subcmd_usage(clags_subcmds_t *subcmds)
{
    printf(" (%s)\n      Subcommands:\n", clags__type_names[Clags_Subcmd]);
    for (size_t i=0; i<subcmds->count; ++i){
        clags_subcmd_t subcmd = subcmds->items[i];
        printf("        - %*s : %s\n", CLAGS__USAGE_PRINTF_ALIGNMENT+6, subcmd.name, subcmd.description);
    }
}

void clags__type_usage(clags_value_type_t type, void *data, bool is_list)
{
    switch (type){
        case Clags_Choice:{
            clags__choice_usage((clags_choices_t *)data, is_list);
        }break;
        case Clags_Subcmd:{
            clags__subcmd_usage((clags_subcmds_t*) data);
        }break;
        case Clags_String:{
            if (is_list) printf(" ([])");
        }break;
        default:{
            printf(" (%s%s)", clags__type_names[type], is_list?"[]":"");
        }
    }
    printf("\n");
}

void clags__subcommand_path_usage(const char *program_name, clags_config_t *config)
{
    if (config->parent){
        clags__subcommand_path_usage(program_name, config->parent);
        printf(" %s", config->name);
    } else{
        printf("Usage: %s", program_name);
    }
}

clags_config_t* clags_parse(int argc, char **argv, clags_config_t *config)
{
    if (config == NULL || config->args == NULL || config->invalid) return NULL;
    // validate the configuration, exit and mark config as invalid on fatal error
    if (!clags__validate_config(config)){
        config->invalid = true;
        return config;
    }

    config->name = clags_config_duplicate_string(config, argv[0]);
    config->error = Clags_Error_Ok;

    clags_config_t *result = NULL;

    // sort arguments by type
    clags_positional_t *positional = CLAGS_CALLOC(config->args_count, sizeof(*positional));
    clags_option_t     *option     = CLAGS_CALLOC(config->args_count, sizeof(*option));
    clags_flag_t       *flags      = CLAGS_CALLOC(config->args_count, sizeof(*flags));
    clags_assert(positional && option && flags, "Out of memory!");

    clags_args_t args = {.positional=positional, .option=option, .flags=flags};
    clags__sort_args(&args, config);

    const char *ignore_prefix = config->options.ignore_prefix;
    size_t ignore_prefix_len = ignore_prefix?strlen(ignore_prefix):0;
    const char *list_term = config->options.list_terminator;

    // parse arguments
    bool arguments_ignored = false;
    bool in_list = false;
    bool parsing_optionals = false;
    bool accept_options = true;
    size_t positional_count = 0;
    size_t required_count = 0;
    for (size_t index=1; index<(size_t) argc; ++index){
        char *arg = argv[index];

        // toggle option and flag parsing based on '--'
        if (strcmp(arg, "--") == 0){
            if (accept_options || config->options.allow_option_parsing_toggle){
                accept_options = !accept_options;
                continue;
            }
        }

        // ignore arguments prefixed with `ignore_prefix`
        if (ignore_prefix && strncmp(arg, ignore_prefix, ignore_prefix_len) == 0){
            arguments_ignored = true;
            if (config->options.ignored_args){
                (void) clags__append_to_list(config, Clags_String, NULL, arg+ignore_prefix_len, config->options.ignored_args, NULL);
            }
            continue;
        }

        // detect list terminator
        if (list_term && strcmp(arg, list_term) == 0){
            if (in_list){
                in_list = false;
                positional_count += 1;
                if (!parsing_optionals) required_count += 1;
            }
            continue;
        }
        if (accept_options && strncmp(arg, "--", 2) == 0){
            // parse long flag or option
            arg += 2;
            if (*arg == '\0'){
                clags_log(config, Clags_Error, "Missing flag or option name: '--%s'!", arg);
                config->error = Clags_Error_InvalidOption;
                clags_return_defer(config);
            }

            // parse long option
            for (size_t i=0; i<args.option_count; ++i){
                clags_option_t opt = args.option[i];
                if (opt.long_flag == NULL) continue;
                size_t long_flag_len = strlen(opt.long_flag);
                if (strncmp(arg, opt.long_flag, long_flag_len) == 0){
                    char *value = arg + long_flag_len;
                    if (*value == '\0'){
                        // get value from the next not-ignored argument
                        while (true){
                            if (argc-index <= 1){
                                clags_log(config, Clags_Error, "Option flag %s requires argument!", arg);
                                config->error = Clags_Error_InvalidOption;
                                clags_return_defer(config);
                            }
                            value = argv[++index];
                            if (!ignore_prefix || strncmp(value, ignore_prefix, ignore_prefix_len) != 0) break;
                            arguments_ignored = true;
                        }
                    } else if (*value++ == '='){
                        if (*value == '\0'){
                            clags_log(config, Clags_Error, "Designated option assignment may not have an empty value: '%s'!", arg);
                            config->error = Clags_Error_InvalidOption;
                            clags_return_defer(config);
                        }
                    } else {
                        continue;
                    }
                    if (!clags__set_arg(config, opt.value_type, arg, value, opt.variable, opt._data, opt.is_list)) clags_return_defer(config);
                    goto next;
                }
            }
            // parse long flags
            for (size_t i=0; i<args.flag_count; ++i){
                clags_flag_t flag = args.flags[i];
                if (flag.long_flag && strcmp(arg, flag.long_flag) == 0){
                    clags__set_flag(config, &flag);
                    if (flag.exit) clags_return_defer(NULL);
                    goto next;
                }
            }
            clags_log(config, Clags_Error, "Unknown long flag or option: '--%s'!", arg);
            config->error = Clags_Error_InvalidOption;
            clags_return_defer(config);
        } else if (accept_options && *arg == '-' && !isdigit((unsigned char)arg[1])){
            // parse short flag or option
            arg += 1;
            size_t flag_len = strlen(arg);
            if (flag_len == 0){
                clags_log(config, Clags_Error, "Missing flag or option name: '-'!");
                config->error = Clags_Error_InvalidOption;
                clags_return_defer(config);
            }
            for (char* c=arg; c<arg+flag_len; ++c){
                // check for short option
                for (size_t i=0; i<args.option_count; ++i){
                    clags_option_t opt = args.option[i];
                    if (*c == opt.short_flag){
                        char *value = c+1;
                        if (*value == '\0'){
                            while (true){
                                if (argc-index <= 1){
                                    clags_log(config, Clags_Error, "Option flag %s requires argument!", arg);
                                    config->error = Clags_Error_InvalidOption;
                                    clags_return_defer(config);
                                }
                                value = argv[++index];
                                if (!ignore_prefix || strncmp(value, ignore_prefix, ignore_prefix_len) != 0) break;
                                arguments_ignored = true;
                            }
                        }
                        if (!clags__set_arg(config, opt.value_type, arg, value, opt.variable, opt._data, opt.is_list)) clags_return_defer(config);
                        goto next;
                    }
                }
                bool matched = false;
                for (size_t i=0; i<args.flag_count; ++i){
                    clags_flag_t flag = args.flags[i];
                    if (*c == flag.short_flag){
                        clags__set_flag(config, &flag);
                        if (flag.exit) clags_return_defer(NULL);
                        matched = true;
                    }
                }
                if (!matched){
                    if (flag_len > 1){
                        clags_log(config, Clags_Error, "Unknown short flag '-%c' in combination '-%s'!", *c, arg);
                    } else{
                        clags_log(config, Clags_Error, "Unknown short flag '-%c'!", *c);
                    }
                    config->error = Clags_Error_InvalidOption;
                    clags_return_defer(config);
                }
            }
        } else {
            // parse positional argument
            if (positional_count >= args.positional_count){
                clags_log(config, Clags_Error, "Unknown additional argument (%zu/%zu): '%s'!", positional_count+1, args.positional_count, arg);
                config->error = Clags_Error_TooManyArguments;
                clags_return_defer(config);
            }

            // verify and write argument
            clags_positional_t pos = args.positional[positional_count];

            // parse subcommands
            if (pos.value_type == Clags_Subcmd){
                clags_subcmd_t **subcmd = pos.variable;
                if (!clags__verify_funcs[pos.value_type](config, pos.arg_name, arg, subcmd, pos.subcmds)) clags_return_defer(config);
                if (subcmd == NULL) clags_return_defer(NULL);
                clags_return_defer(clags_parse((int) argc-index, argv+index, (*subcmd)->config));
            }
            if (pos.is_list){
                in_list = true;
            } else{
                positional_count += 1;
                if (!pos.optional) required_count += 1;
            }
            parsing_optionals = pos.optional;
            if (!clags__set_arg(config, pos.value_type, pos.arg_name, arg, pos.variable, pos._data, pos.is_list)) clags_return_defer(config);
        }
    next: continue;
    }
    if (in_list){
        positional_count += 1;
        if (!parsing_optionals) required_count += 1;
    }
    if (arguments_ignored) clags_log(config, Clags_Warning, "Arguments were ignored because they were prefixed with '%s'", ignore_prefix);

    // report missing positional arguments
    if (required_count < args.required_count){
        clags_sb_t sb = {0};
        clags_sb_appendf(&sb, "Missing required arguments (%zu/%zu):", required_count, args.required_count);
        for (size_t i=positional_count; i<args.required_count; ++i){
            clags_sb_appendf(&sb, " <%s>", args.positional[i].arg_name);
        }
        clags_sb_appendf(&sb, "!");
        clags_log_sb(config, Clags_Error, &sb);
        clags_sb_free(&sb);

        config->error = Clags_Error_TooFewArguments;
        clags_return_defer(config);
    }

    clags_return_defer(NULL);

defer:
    // cleanup memory of sorted args
    CLAGS_FREE(positional);
    CLAGS_FREE(option);
    CLAGS_FREE(flags);
    return result;
}

static void clags__format_lhs(char *buffer, size_t buf_size, char short_flag, const char *long_flag, const char *arg_name, bool *lines_cut_off)
{
    if (!buffer || buf_size == 0) return;
    buffer[0] = '\0';

    char temp[512] = {0};
    size_t needed = 0;

    if (short_flag && long_flag) {
        if (arg_name) snprintf(temp, sizeof(temp), "-%c, --%s(=)%s", short_flag, long_flag, arg_name);
        else          snprintf(temp, sizeof(temp), "-%c, --%s", short_flag, long_flag);
    } else if (short_flag) {
        if (arg_name) snprintf(temp, sizeof(temp), "-%c %s", short_flag, arg_name);
        else          snprintf(temp, sizeof(temp), "-%c", short_flag);
    } else if (long_flag) {
        if (arg_name) snprintf(temp, sizeof(temp), "--%s(=)%s", long_flag, arg_name);
        else          snprintf(temp, sizeof(temp), "--%s", long_flag);
    }

    needed = strlen(temp);

    if (needed < buf_size) {
        strncpy(buffer, temp, buf_size);
        buffer[buf_size-1] = '\0';
        return;
    }

    if (!long_flag) {
        strncpy(buffer, temp, buf_size-1);
        buffer[buf_size-1] = '\0';
        if (lines_cut_off) *lines_cut_off = true;
        return;
    }

    const char *suffix = arg_name ? arg_name : "";
    size_t suffix_len = strlen(suffix) + 3;
    size_t remaining = buf_size > 1 ? buf_size - 1 : 0;

    char prefix[32] = {0};
    if (short_flag && long_flag) strcpy(prefix, "-o, --");
    else strcpy(prefix, "--");

    size_t prefix_len = strlen(prefix);
    size_t max_long = 0;
    if (remaining > prefix_len + suffix_len) max_long = remaining - prefix_len - suffix_len;
    else max_long = 0;

    if (max_long > 0) {
        char trimmed_long[128] = {0};
        strncpy(trimmed_long, long_flag, max_long);
        if (max_long >= 2) {
            trimmed_long[max_long-2] = '.';
            trimmed_long[max_long-1] = '.';
        } else if (max_long == 1) {
            trimmed_long[0] = '.';
        }

        snprintf(buffer, buf_size, "%s%s%s%s", prefix, trimmed_long, arg_name ? "(=)" : "", suffix);
    } else {
        snprintf(buffer, buf_size, "%s%s", prefix, arg_name ? arg_name : "");
    }

    if (lines_cut_off) *lines_cut_off = true;
}

void clags_usage(const char *program_name, clags_config_t *config)
{
    if (!config || !config->args || config->invalid) return;

    clags_positional_t *positional = CLAGS_CALLOC(config->args_count, sizeof(*positional));
    clags_option_t     *option     = CLAGS_CALLOC(config->args_count, sizeof(*option));
    clags_flag_t       *flags      = CLAGS_CALLOC(config->args_count, sizeof(*flags));
    clags_assert(positional && option && flags, "Out of memory!");

    clags_args_t args = {.positional=positional, .option=option, .flags=flags};
    clags__sort_args(&args, config);

    char *temp_buffer = CLAGS_CALLOC(CLAGS__USAGE_TEMP_BUFFER_SIZE, sizeof(*temp_buffer));
    clags_assert(temp_buffer, "Out of memory!");

    bool lines_cut_off = false;

    clags__subcommand_path_usage(program_name, config);

    if (args.option_count) printf(" [OPTIONS]");
    if (args.flag_count) printf(" [FLAGS]");

    bool last_was_list = false;
    for (size_t i = 0; i < args.positional_count; ++i) {
        if (last_was_list) {
            printf(" %s", config->options.list_terminator);
            last_was_list = false;
        }
        clags_positional_t pos = args.positional[i];
        printf(" ");
        printf("%c", pos.optional? '[':'<');
        if (pos.is_list) {
            printf("%s..", pos.arg_name);
            last_was_list = true;
        } else {
            printf("%s", pos.arg_name);
        }
        printf("%c", pos.optional? ']':'>');
    }
    printf("\n");

    if (config->options.description) {
        const char *line = config->options.description;
        while (line && *line) {
            char *line_end = clags__strchrnull(line, '\n');
            int len = (int)(line_end - line);
            printf("%.*s\n", len, line);
            if (*line_end == '\0') break;
            line = line_end + 1;
        }
        printf("\n");
    }

    if (args.positional_count) {
        printf("  Arguments:\n");
        for (size_t i = 0; i < args.positional_count; ++i) {
            clags_positional_t pos = args.positional[i];
            char optional_hint[32] = {0};
            if (pos.optional) snprintf(optional_hint, sizeof(optional_hint), "(optional)");
            snprintf(temp_buffer, CLAGS__USAGE_TEMP_BUFFER_SIZE, "%s %s", pos.arg_name, optional_hint);
            printf("    %*s : %s", CLAGS__USAGE_PRINTF_ALIGNMENT, temp_buffer, pos.description);
            clags__type_usage(pos.value_type, pos._data, pos.is_list);
        }
    }

    if (args.option_count) {
        printf("  Options:\n");
        for (size_t i = 0; i < args.option_count; ++i) {
            clags_option_t opt = args.option[i];
            clags__format_lhs(temp_buffer, CLAGS__USAGE_TEMP_BUFFER_SIZE,
                              opt.short_flag, opt.long_flag, opt.arg_name,  &lines_cut_off);
            printf("    %*s : %s", CLAGS__USAGE_PRINTF_ALIGNMENT, temp_buffer, opt.description);
            clags__type_usage(opt.value_type, opt._data, opt.is_list);
        }
    }

    if (args.flag_count) {
        printf("  Flags:\n");
        for (size_t i = 0; i < args.flag_count; ++i) {
            clags_flag_t flag = args.flags[i];
            clags__format_lhs(temp_buffer, CLAGS__USAGE_TEMP_BUFFER_SIZE,
                              flag.short_flag, flag.long_flag, NULL, &lines_cut_off);
            printf("    %*s : %s%s\n", CLAGS__USAGE_PRINTF_ALIGNMENT, temp_buffer, flag.description, flag.exit?" and exit":"");
        }
    }

    if (!config->options.print_no_notes &&
        (config->options.list_terminator || config->options.ignore_prefix || config->options.allow_option_parsing_toggle)) {
        printf("\n  Notes:\n");
        if (config->options.allow_option_parsing_toggle){
            printf("    '--' toggles option and flag parsing and can re-enable parsing when provided again.\n");
        }
        if (config->options.list_terminator){
            printf("    '%s' terminates a list argument.\n", config->options.list_terminator);
        }
        if (config->options.ignore_prefix){
            printf("    Arguments prefixed with '%s' are ignored.\n", config->options.ignore_prefix);
        }
    }

    if (lines_cut_off){
        clags_log(config, Clags_ConfigWarning, "Some flag names were too long and were cut off! Increase `CLAGS_USAGE_ALIGNMENT` to give them more space.");
    }

    CLAGS_FREE(positional);
    CLAGS_FREE(option);
    CLAGS_FREE(flags);
    CLAGS_FREE(temp_buffer);
}

int clags_subcmd_index(clags_subcmds_t *subcmds, clags_subcmd_t *subcmd)
{
    if (!subcmds || !subcmd) return -1;
    for (size_t i=0; i<subcmds->count; ++i){
        if (&subcmds->items[i] == subcmd) return (int) i;
    }
    return -1;
}

int clags_choice_index(clags_choices_t *choices, clags_choice_t *choice)
{
    if (!choices || !choice) return -1;
    for (size_t i=0; i<choices->count; ++i){
        if (&choices->items[i] == choice) return (int) i;
    }
    return -1;
}

void clags_list_free(clags_list_t *list)
{
    if (list == NULL) return;
    CLAGS_FREE(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

void clags_config_free_allocs(clags_config_t *config)
{
    if (config == NULL) return;
    clags_list_t *allocs = &config->allocs;
    for (size_t i=0; i<allocs->count; ++i){
        CLAGS_FREE(((char**) allocs->items)[i]);
    }
    CLAGS_FREE(allocs->items);
    allocs->items = NULL;
    allocs->count = allocs->capacity = 0;
}

void clags_config_free(clags_config_t *config)
{
    if (config == NULL) return;
    for (size_t i=0; i<config->args_count; ++i){
        clags_arg_t arg = config->args[i];
        if (arg.type == Clags_Positional && arg.pos.is_list){
            clags_list_free(arg.pos.variable);
        } else if (arg.type == Clags_Option && arg.opt.is_list){
            clags_list_free(arg.opt.variable);
        }
    }
    clags_config_free_allocs(config);
}

const char* clags_error_description(clags_error_t error)
{
    switch(error){
#define X(type, desc) case type: return desc;
        clags__errors
#undef X
        default: return "unknown error";
    }
}

#endif // CLAGS_IMPLEMENTATION

