
#include "cabala.h"

#define MAX_DEPTHS 100

typedef struct {
    ErlNifEnv *env;
    cabala_st *st;

    vec_term_t *vec;
    int depth;

    int  return_maps;
    bool keys;
} decode_state;

static bool decode_visit_document(const bson_iter_t *iter,
                                  const char        *key,
                                  const bson_t      *v_document,
                                  void              *data);

static bool decode_visit_array(const bson_iter_t *iter,
                               const char        *key,
                               const bson_t      *v_array,
                               void              *data);

static bool decode_visit_codewscope(const bson_iter_t *iter,
                                    const char        *key,
                                    size_t             v_code_len,
                                    const char        *v_code,
                                    const bson_t      *v_scope,
                                    void              *data);
    

void 
init_state(decode_state *ds, ErlNifEnv *env, cabala_st *st)
{
    ds->env = env;
    ds->st = st;
    ds->return_maps = 0;
    ds->keys = true;
    ds->depth = 0;
}

void
init_child_state(decode_state *ds, decode_state *child)
{
    child->env = ds->env;
    child->st = ds->st;
    child->return_maps = ds->return_maps;
}

ERL_NIF_TERM
make_empty_document(ErlNifEnv *env, int return_maps) 
{
    if(return_maps) {
        return enif_make_new_map(env);
    }
    return enif_make_tuple(env, 0);
}

int
make_document(ErlNifEnv     *env, 
              vec_term_t    *vec,
              ERL_NIF_TERM  *out,
              int            return_maps)
{
    if(vec->length % 2 != 0) {
        return 0;
    }
    if(return_maps) {
        int idx, count = vec->length;
        ERL_NIF_TERM ret = enif_make_new_map(env);
        
        for(idx = 0; idx < count; idx += 2) {
            if(!enif_make_map_put(
                    env, 
                    ret, 
                    vec->data[idx], 
                    vec->data[idx+1], 
                    &ret)) {
                return 0;
            }
        }

        *out = ret;
        return 1;
    }

    *out = enif_make_tuple_from_array(env, vec->data, vec->length);
    return 1;
}

static bool
decode_visit_utf8(const bson_iter_t *iter,
                  const char        *key,
                  size_t             v_utf8_len,
                  const char        *v_utf8,
                  void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM  out;

    LOG("decode visit utf8, key: %s\r\n", key);

    if(!make_binary(ds->env, &out, v_utf8, v_utf8_len)) {
        LOG("decode visit vtf8, make binary error: %d \r\n", (int)v_utf8_len);
        return true;
    }
    vec_push(ds->vec, out);

    return false;
}

static bool
decode_visit_int32(const bson_iter_t *iter,
                   const char        *key,
                   int32_t            v_int32,
                   void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM  out = enif_make_int(ds->env, v_int32);
    vec_push(ds->vec, out);
    return false;
}

static bool
decode_visit_int64(const bson_iter_t *iter,
                   const char        *key,
                   int64_t            v_int64,
                   void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM  out = enif_make_int64(ds->env, v_int64);
    vec_push(ds->vec, out);
    return false;
}

static bool
decode_visit_double(const bson_iter_t *iter,
                    const char        *key,
                    double             v_double,
                    void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM  out = enif_make_double(ds->env, v_double);
    vec_push(ds->vec, out);
    return false;
}

static bool
decode_visit_undefined(const bson_iter_t *iter,
                       const char        *key,
                       void              *data)
{
    decode_state *ds = data;
    vec_push(ds->vec, ds->st->atom_undefined);
    return false;
}

static bool
decode_visit_null(const bson_iter_t *iter,
                  const char        *key,
                  void              *data)
{
    decode_state *ds = data;
    vec_push(ds->vec, ds->st->atom_null);
    return false;
}

static bool
decode_visit_oid(const bson_iter_t *iter,
                 const char        *key,
                 const bson_oid_t  *v_oid,
                 void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM  out;

    bson_return_val_if_fail(v_oid, true);

    if(!make_binary(ds->env, &out, v_oid->bytes, 12)) {
        return true;
    }
    out = enif_make_tuple2(ds->env, ds->st->atom_s_oid, out);
    vec_push(ds->vec, out);

    return false;
}

static bool
decode_visit_binary(const bson_iter_t  *iter,
                    const char         *key,
                    bson_subtype_t      v_subtype,
                    size_t              v_binary_len,
                    const uint8_t      *v_binary,
                    void               *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM type, binary, out;

    type = enif_make_int(ds->env, v_subtype);
    if(!make_binary(ds->env, &binary, v_binary, v_binary_len)) {
        return true;
    }
    out = enif_make_tuple4(ds->env, 
                           ds->st->atom_s_type, 
                           type, 
                           ds->st->atom_s_binary, 
                           binary);
    vec_push(ds->vec, out);

    return false;
}

static bool
decode_visit_bool(const bson_iter_t *iter,
                  const char        *key,
                  bool               v_bool,
                  void              *data)
{
    decode_state *ds = data;
    vec_push(ds->vec, v_bool ? ds->st->atom_true : ds->st->atom_false);
    return false;
}

static bool
decode_visit_date_time(const bson_iter_t *iter,
                       const char        *key,
                       int64_t            msec_since_epoch,
                       void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM  out = enif_make_int64(ds->env, msec_since_epoch);
    out = enif_make_tuple2(ds->env, ds->st->atom_s_date, out);
    vec_push(ds->vec, out);
    return false;
}

static bool
decode_visit_regex(const bson_iter_t *iter,
                   const char        *key,
                   const char        *v_regex,
                   const char        *v_options,
                   void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM regex, options, out;

    if(!make_binary(ds->env, &regex, v_regex, strlen(v_regex))) {
        return true;
    }
    if(!make_binary(ds->env, &options, v_options, strlen(v_options))) {
        return true;
    }
    out = enif_make_tuple4(ds->env, 
                           ds->st->atom_s_regex, 
                           regex, 
                           ds->st->atom_s_options, 
                           options);
    vec_push(ds->vec, out);

    return false;
}

static bool
decode_visit_timestamp(const bson_iter_t *iter,
                       const char        *key,
                       uint32_t           v_timestamp,
                       uint32_t           v_increment,
                       void              *data)
{
    decode_state *ds = data;

    ERL_NIF_TERM timestamp = enif_make_int(ds->env, v_timestamp);
    ERL_NIF_TERM increment = enif_make_int(ds->env, v_increment);
    ERL_NIF_TERM out = enif_make_tuple4(
            ds->env,
            ds->st->atom_s_timestamp,
            timestamp,
            ds->st->atom_s_increment,
            increment);
    vec_push(ds->vec, out);

    return false;
}

static bool
decode_visit_dbpointer(const bson_iter_t *iter,
                       const char        *key,
                       size_t             v_collection_len,
                       const char        *v_collection,
                       const bson_oid_t  *v_oid,
                       void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM col, oid, out;

    if(!make_binary(ds->env, &col, v_collection, v_collection_len)) {
        return true;
    }
    if(v_oid) {
        if(!make_binary(ds->env, &oid, v_oid->bytes, 12)) {
            return true;
        }
    } else {
        oid = ds->st->atom_null;
    }
    out = enif_make_tuple4(
            ds->env,
            ds->st->atom_s_ref,
            col,
            ds->st->atom_s_oid,
            oid),
    vec_push(ds->vec, out);

    return false;
}

static bool
decode_visit_minkey(const bson_iter_t *iter,
                    const char        *key,
                    void              *data)
{
    decode_state *ds = data;
    vec_push(ds->vec, ds->st->atom_minkey);
    return false;
}

static bool
decode_visit_maxkey(const bson_iter_t *iter,
                    const char        *key,
                    void              *data)
{
    decode_state *ds = data;
    vec_push(ds->vec, ds->st->atom_maxkey);
    return false;
}

static bool
decode_visit_before(const bson_iter_t *iter,
                    const char        *key,
                    void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM out;

    LOG("decode visit key: %s, type: %d\r\n", key, bson_iter_type(iter));

    if(ds->keys) {
        if(!make_binary(ds->env, &out, key, strlen(key))) {
            return true;
        }
        vec_push(ds->vec, out);
    }

    return false;
}

static bool
decode_visit_code(const bson_iter_t *iter,
                  const char        *key,
                  size_t             v_code_len,
                  const char        *v_code,
                  void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM code, out;

    if(!make_binary(ds->env, &code, v_code, v_code_len)) {
        return true;
    }
    out = enif_make_tuple2(ds->env, ds->st->atom_s_javascript, code);
    vec_push(ds->vec, out);

    return false;
}

static bool
decode_visit_symbol(const bson_iter_t *iter,
                    const char        *key,
                    size_t             v_symbol_len,
                    const char        *v_symbol,
                    void              *data)
{
    decode_state *ds = data;
    ERL_NIF_TERM out;

    if(!make_binary(ds->env, &out, v_symbol, v_symbol_len)) {
        return true;
    }
    vec_push(ds->vec, out);

    return false;
}

static const bson_visitor_t decode_visitors = {
    decode_visit_before,
    NULL, /* visit_after */
    NULL, /* visit_corrupt */
    decode_visit_double,
    decode_visit_utf8,
    decode_visit_document,
    decode_visit_array,
    decode_visit_binary,
    decode_visit_undefined,
    decode_visit_oid,
    decode_visit_bool,
    decode_visit_date_time,
    decode_visit_null,
    decode_visit_regex,
    decode_visit_dbpointer,
    decode_visit_code,
    decode_visit_symbol,
    decode_visit_codewscope,
    decode_visit_int32,
    decode_visit_timestamp,
    decode_visit_int64,
    decode_visit_maxkey,
    decode_visit_minkey,
};

int 
iter_bson(const bson_t *bson, ERL_NIF_TERM *out, decode_state *ds) 
{
    vec_term_t vec;
    bson_iter_t iter;
    int ret = 0;

    vec_init(&vec);
    ds->vec = &vec;

    if(!bson_iter_init(&iter, bson)) {
        goto finish;
    }
    if(bson_iter_visit_all(&iter, &decode_visitors, ds) || 
            iter.err_off) {
        goto finish;
    }
    if(!make_document(ds->env, ds->vec, out, ds->return_maps)) {
        goto finish;
    }
    ret = 1;

finish:
    vec_deinit(&vec);
    ds->vec = NULL;
    return ret;
}

static bool
decode_visit_codewscope(const bson_iter_t *iter,
                        const char        *key,
                        size_t             v_code_len,
                        const char        *v_code,
                        const bson_t      *v_scope,
                        void              *data)
{
    decode_state *ds = data;
    decode_state cs;
    ERL_NIF_TERM code, scope, out;

    if(!make_binary(ds->env, &code, v_code, v_code_len)) {
        return true;
    }

    init_child_state(ds, &cs);
    cs.depth = ds->depth;
    cs.keys = true;
    if(!iter_bson(v_scope, &scope, &cs)) {
        return true;
    }

    out = enif_make_tuple4(
            ds->env, 
            ds->st->atom_s_javascript, 
            code,
            ds->st->atom_s_scope,
            scope);
    vec_push(ds->vec, out);

    return false;
}

static bool
decode_visit_document(const bson_iter_t *iter,
                      const char        *key,
                      const bson_t      *v_document,
                      void              *data)
{
    decode_state *ds = data;
    decode_state  cs;
    ERL_NIF_TERM  out;

    LOG("visit document: %s \r\n", key);

    if(ds->depth >= MAX_DEPTHS) {
        return true;
    }

    init_child_state(ds, &cs);
    cs.keys  = true;
    cs.depth = ds->depth + 1;

    if(!iter_bson(v_document, &out, &cs)) {
        return true;
    }
    vec_push(ds->vec, out);
    return false;
}

static bool
decode_visit_array(const bson_iter_t *iter,
                   const char        *key,
                   const bson_t      *v_array,
                   void              *data)
{
    decode_state *ds = data;
    decode_state  cs;
    vec_term_t    vec;
    bson_iter_t   child;
    ERL_NIF_TERM  out;

    LOG("visit array: %s \r\n", key);

    if(ds->depth >= MAX_DEPTHS) {
        return true;
    }

    init_child_state(ds, &cs);
    cs.keys  = false;
    cs.depth = ds->depth + 1; 
    if(!bson_iter_init(&child, v_array)) {
        return true;
    }
    vec_init(&vec);
    cs.vec = &vec;

    if(bson_iter_visit_all(&child, &decode_visitors, &cs) ||
            child.err_off) {
        vec_deinit(&vec);
        return true;
    }
    out = enif_make_list_from_array(cs.env, cs.vec->data, cs.vec->length);
    vec_deinit(&vec);
    vec_push(ds->vec, out);

    return false;
}

ERL_NIF_TERM 
decode(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
    cabala_st *st = (cabala_st*)enif_priv_data(env);
    ERL_NIF_TERM data, opts, out;
    decode_state ds;

    ErlNifBinary bin;
    bson_t *bson;

    /* init params */
    if(argc != 2) {
        return enif_make_badarg(env);
    }
    init_state(&ds, env, st);
    data = argv[0];
    opts = argv[1];

    /* parse decode options */
    while(enif_get_list_cell(env, opts, &out, &opts)) {
        if(enif_compare(out, st->atom_return_maps) == 0) {
            ds.return_maps = 1;
        } else {
            return enif_make_badarg(env);
        }
    }

    /* check data is bson */
    if(!enif_inspect_binary(env, data, &bin)) {
        return enif_make_badarg(env);
    }
    bson = bson_new_from_data(bin.data, bin.size);
    if(!bson) {
        return make_error(st, env, "badbson");
    }
    if(bson_empty(bson)) {
        bson_destroy(bson);
        return make_empty_document(env, ds.return_maps);
    }

    LOG("decode begin, return_maps: %d\r\n", ds.return_maps);

    if(!iter_bson(bson, &out, &ds)) {
        out = make_error(st, env, "internal_error");
    }
    bson_destroy(bson);
    return out;
}