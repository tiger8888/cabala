#ifndef CABALA_H
#define CABALA_H

#include <libbson-1.0/bson.h>

#include "erl_nif.h"
#include "vec.h"

// #define CABALA_DEBUG 1

#ifdef CABALA_DEBUG
    #define LOG(fmt, ...) fprintf(stdout, fmt, __VA_ARGS__)
#else
    #define LOG(fmt, ...)
#endif

typedef struct {
	ERL_NIF_TERM 	atom_ok;			// 'ok'
    ERL_NIF_TERM    atom_error;			// 'error'
    ERL_NIF_TERM    atom_null;			// 'null'
    ERL_NIF_TERM	atom_undefined;		// 'undefined'
    ERL_NIF_TERM    atom_true;			// 'true'
    ERL_NIF_TERM    atom_false;			// 'false'
    ERL_NIF_TERM    atom_minkey;        // 'MIN_KEY'
    ERL_NIF_TERM    atom_maxkey;        // 'MAX_KEY'

    ERL_NIF_TERM	atom_s_oid;			// '$oid$'
    ERL_NIF_TERM	atom_s_type;		// '$type$'
    ERL_NIF_TERM    atom_s_binary;      // '$binary$'
    ERL_NIF_TERM	atom_s_javascript;	// '$javascript$'
    ERL_NIF_TERM	atom_s_scope;		// '$scope$'
    ERL_NIF_TERM	atom_s_ref;			// '$ref$'
    ERL_NIF_TERM    atom_s_date;        // '$date$'
    ERL_NIF_TERM    atom_s_regex;       // '$regex$'
    ERL_NIF_TERM    atom_s_options;     // '$options$'
    ERL_NIF_TERM    atom_s_timestamp;   // '$timestamp$'
    ERL_NIF_TERM    atom_s_increment;   // '$increment$'

    ERL_NIF_TERM    atom_return_maps;	// 'return_maps'
} cabala_st;

typedef vec_t(ERL_NIF_TERM) vec_term_t;

/* nif functions */
ERL_NIF_TERM decode(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);
ERL_NIF_TERM encode(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[]);

/* util functions */
ERL_NIF_TERM make_atom(ErlNifEnv *env, const char *name);
ERL_NIF_TERM make_ok(cabala_st *st, ErlNifEnv *env, ERL_NIF_TERM value);
ERL_NIF_TERM make_error(cabala_st *st, ErlNifEnv *env, const char *error);
ERL_NIF_TERM make_obj_error(cabala_st *st, ErlNifEnv *env, 
		const char *error, ERL_NIF_TERM obj);

int make_binary(ErlNifEnv *env, ERL_NIF_TERM *out, const void* str, size_t len);

#endif