
#include "cabala.h"

static void* 
cabala_calloc(size_t n, size_t size) 
{
	size_t len = n*size;
	void* ptr = enif_alloc(len);
	if(ptr) {
		memset(ptr, 0, len);
	}
	return ptr;
}

/*
 * Use erlang vm memory manage functions avoid system call
 */
static const bson_mem_vtable_t erl_bson_vtable = {
	enif_alloc,
	cabala_calloc,
	enif_realloc,
	enif_free
};

static int
load(ErlNifEnv *env, void **priv, ERL_NIF_TERM info)
{
	cabala_st *st = enif_alloc(sizeof(cabala_st));
	if(st == NULL) {
		return 1;
	}

	st->atom_ok = make_atom(env, "ok");
	st->atom_error = make_atom(env, "error");
	st->atom_null = make_atom(env, "null");
	st->atom_undefined = make_atom(env, "undefined");
	st->atom_true = make_atom(env, "true");
	st->atom_false = make_atom(env, "false");
	st->atom_minkey = make_atom(env, "MIN_KEY");
	st->atom_maxkey = make_atom(env, "MAX_KEY");

	st->atom_s_oid = make_atom(env, "$oid$");
	st->atom_s_type = make_atom(env, "$type$");
	st->atom_s_binary = make_atom(env, "$binary$");
	st->atom_s_javascript = make_atom(env, "$javascript$");
	st->atom_s_scope = make_atom(env, "$scope$");
	st->atom_s_ref = make_atom(env, "$ref$");
	st->atom_s_date = make_atom(env, "$date$");
	st->atom_s_regex = make_atom(env, "$regex$");
	st->atom_s_options = make_atom(env, "$options$");
	st->atom_s_timestamp = make_atom(env, "$timestamp$");
	st->atom_s_increment = make_atom(env, "$increment$");
	
	st->atom_return_maps = make_atom(env, "return_maps");

	*priv = (void*)st;

	/* init bson memory control */
	bson_mem_set_vtable(&erl_bson_vtable);

	return 0;
}

static int
reload(ErlNifEnv *env, void **priv, ERL_NIF_TERM info) 
{
	return 0;
}

static int
upgrade(ErlNifEnv *env, void **priv, void **old_priv, ERL_NIF_TERM info) 
{
	return load(env, priv, info);
}

static void 
unload(ErlNifEnv *env, void *priv)
{
	enif_free(priv);
	return;
}

static ErlNifFunc funcs[] = 
{
	{"nif_decode", 2, decode},
	{"nif_encode", 2, encode}
};

ERL_NIF_INIT(cabala, funcs, &load, &reload, &upgrade, &unload);