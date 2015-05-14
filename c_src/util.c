
#include "cabala.h"

ERL_NIF_TERM
make_atom(ErlNifEnv *env, const char *name)
{
    ERL_NIF_TERM ret;
    if(enif_make_existing_atom(env, name, &ret, ERL_NIF_LATIN1)) {
        return ret;
    }
    return enif_make_atom(env, name);
}

ERL_NIF_TERM
make_ok(cabala_st *st, ErlNifEnv *env, ERL_NIF_TERM value)
{
    return enif_make_tuple2(env, st->atom_ok, value);
}

ERL_NIF_TERM
make_error(cabala_st *st, ErlNifEnv *env, const char *error)
{
    return enif_make_tuple2(env, st->atom_error, make_atom(env, error));
}

ERL_NIF_TERM
make_obj_error(cabala_st    *st, 
               ErlNifEnv    *env,
               const char   *error, 
               ERL_NIF_TERM obj)
{
    ERL_NIF_TERM reason = enif_make_tuple2(env, make_atom(env, error), obj);
    return enif_make_tuple2(env, st->atom_error, reason);
}

int
make_binary(ErlNifEnv *env, ERL_NIF_TERM *out, const void* str, size_t len)
{
    ErlNifBinary bin;
    if(!enif_alloc_binary(len, &bin)) {
        return 0;
    }
    memcpy(bin.data, str, len);
    *out = enif_make_binary(env, &bin);
    return 1;
}