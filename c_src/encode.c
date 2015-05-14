
#include "cabala.h"

typedef struct {
	ErlNifEnv 	 *env;
	cabala_st 	 *st;

	bson_t 		  bson;
} encode_state;

typedef struct {
	void   *data;
	size_t  size;
	bool	need_free;
} termstr;

typedef enum {
	DOC_TYPE_MAP 	= 0x00,
	DOC_TYPE_TUPLE  = 0x01,
	DOC_TYPE_LIST   = 0x02,
} doc_type;

typedef struct {
	doc_type type;
	union {
		ERL_NIF_TERM map;
		ERL_NIF_TERM list;
		struct {
			ERL_NIF_TERM *array;
			int arity;	
		} v_tuple;
	} value;
} enc_doc_t;

#define TERMSTR_INIT {NULL, 0, false}

int encode_doc_impl(enc_doc_t *ed, encode_state *es);
int encode_doc(ERL_NIF_TERM term, encode_state *es);

static inline int
termstr_cpy_make(ErlNifEnv *env, termstr *str, ERL_NIF_TERM term, bool bin_cpy)
{
	if(enif_is_binary(env, term)) {
		ErlNifBinary bin;

		if(!enif_inspect_binary(env, term, &bin)) {
			return 0;
		}

		if(bin_cpy) {
			char *data;

			data = bson_malloc(bin.size+1);
			if(!data) {
				return 0;
			}
			memcpy(data, bin.data, bin.size);
			data[bin.size] = '\0';

			str->data = data;
			str->size = bin.size+1;
			str->need_free = true;
		} else {
			str->data = bin.data;
			str->size = bin.size;
			str->need_free = false;
		}

		return 1; 
	} else if(enif_is_atom(env, term)) {
		char *data;
		unsigned int len;

		if(!enif_get_atom_length(env, term, &len, ERL_NIF_LATIN1)) {
			return 0;
		}
		data = bson_malloc(len+1);
		if(!data) {
			return 0;
		}
		if(!enif_get_atom(env, term, data, len+1, ERL_NIF_LATIN1)) {
			bson_free(data);
			return 0;
		}
		str->data = data;
		str->size = len;
		str->need_free = true;

		return 1;
	}
	return 0;
}

static inline int
termstr_make(ErlNifEnv *env, termstr *str, ERL_NIF_TERM term)
{
	return termstr_cpy_make(env, str, term, false);
}

static inline void
termstr_destroy(termstr *str)
{
	if(str->need_free) {
		bson_free(str->data);
	}
}

static encode_state *
es_new(ErlNifEnv *env, cabala_st *st) 
{
	encode_state *es = enif_alloc(sizeof(encode_state));
	if(!es) {
		return NULL;
	}

	es->env = env;
	es->st  = st;
	bson_init(&es->bson);

	return es;
}

static void
es_destroy(encode_state *es) 
{
	if(!es) return;
	bson_destroy(&es->bson);
	enif_free(es);
}

static inline int
append_keyval(ErlNifEnv *env, bson_t *bson, ERL_NIF_TERM key, bson_value_t *val)
{
	bool ret;

	termstr keystr = TERMSTR_INIT;
	if(!termstr_make(env, &keystr, key)) {
		return 0;
	}
	ret = bson_append_value(bson, 
							keystr.data, 
							keystr.size, 
							val);
	termstr_destroy(&keystr);

	return ret ? 1 : 0;
}

static inline int
append_utf8(ERL_NIF_TERM key, ERL_NIF_TERM term, encode_state *es)
{
	bson_value_t val;
	int ret;
	termstr valstr = TERMSTR_INIT;

	if(!termstr_make(es->env, &valstr, term)) {
		return 0;
	}

	val.value_type = BSON_TYPE_UTF8;
	val.value.v_utf8.str = valstr.data;
	val.value.v_utf8.len = valstr.size;
	ret = append_keyval(es->env, &es->bson, key, &val);

	termstr_destroy(&valstr);
	return ret;
}

static inline int
append_oid(ERL_NIF_TERM key, ERL_NIF_TERM oid, encode_state *es)
{
	
	int ret;
	termstr oidstr = TERMSTR_INIT;

	if(!termstr_make(es->env, &oidstr, oid)) {
		return 0;
	}

	if(oidstr.size == 12) {
		bson_value_t val;
		bson_oid_t oid;

		memcpy(oid.bytes, oidstr.data, 12);
		val.value_type = BSON_TYPE_OID;
		val.value.v_oid = oid;
		ret = append_keyval(es->env, &es->bson, key, &val);
	} else {
		ret = 0;
	}

	termstr_destroy(&oidstr);
	return ret;
}

static inline int
append_binary(ERL_NIF_TERM  key, 
			  ERL_NIF_TERM  subtype, 
			  ERL_NIF_TERM  bin, 
			  encode_state *es)
{
	int v_subtype_i, ret;
	bson_subtype_t v_subtype;
	termstr datastr = TERMSTR_INIT;
	bson_value_t val;

	if(!enif_get_int(es->env, subtype, &v_subtype_i)) {
		return 0;
	}
	switch(v_subtype_i) {
	case 0x00:
		v_subtype = BSON_SUBTYPE_BINARY;
		break;
	case 0x01:
		v_subtype = BSON_SUBTYPE_FUNCTION;
		break;
	case 0x02:
		v_subtype = BSON_SUBTYPE_BINARY_DEPRECATED;
		break;
	case 0x03:
		v_subtype = BSON_SUBTYPE_UUID_DEPRECATED;
		break;
	case 0x04:
		v_subtype = BSON_SUBTYPE_UUID;
		break;
	case 0x05:
		v_subtype = BSON_SUBTYPE_MD5;
		break;
	case 0x80:
		v_subtype = BSON_SUBTYPE_USER;
		break;
	default:
		return 0;
	}

	if(!termstr_make(es->env, &datastr, bin)) {
		return 0;
	}

	val.value_type = BSON_TYPE_BINARY;
	val.value.v_binary.data = datastr.data;
	val.value.v_binary.data_len = datastr.size;
	val.value.v_binary.subtype = v_subtype;
	ret = append_keyval(es->env, &es->bson, key, &val);

	termstr_destroy(&datastr);
	return ret;
}

static inline int
append_regex(ERL_NIF_TERM key, 
			 ERL_NIF_TERM regex, 
			 ERL_NIF_TERM options, 
			 encode_state *es) 
{
	int ret;
	termstr regexstr  = TERMSTR_INIT;
	termstr optionstr = TERMSTR_INIT;
	bson_value_t val;

	if(!termstr_cpy_make(es->env, &regexstr, regex, true)) {
		ret = 0;
		goto done;
	}
	if(!termstr_cpy_make(es->env, &optionstr, options, true)) {
		ret = 0;
		goto done;
	}

	val.value_type = BSON_TYPE_REGEX;
	val.value.v_regex.regex = regexstr.data;
	val.value.v_regex.options = optionstr.data;
	ret = append_keyval(es->env, &es->bson, key, &val);

done:
	termstr_destroy(&regexstr);
	termstr_destroy(&optionstr);
	return ret;
}

static inline int
append_code(ERL_NIF_TERM key, ERL_NIF_TERM code, encode_state *es)
{
	int ret;
	termstr codestr = TERMSTR_INIT;
	bson_value_t val;

	if(!termstr_cpy_make(es->env, &codestr, code, true)) {
		return 0;
	}
	val.value_type = BSON_TYPE_CODE;
	val.value.v_code.code = codestr.data;
	val.value.v_code.code_len = codestr.size;

	ret = append_keyval(es->env, &es->bson, key, &val); 

	termstr_destroy(&codestr);
	return ret;
}

static inline int
append_codescope(ERL_NIF_TERM key, 
				 ERL_NIF_TERM code, 
				 ERL_NIF_TERM scope, 
				 encode_state *es)
{
	int ret;
	termstr codestr = TERMSTR_INIT;
	bson_value_t val;
	encode_state *scope_es;

	if(!termstr_cpy_make(es->env, &codestr, code, true)) {
		return 0;
	}

	val.value_type = BSON_TYPE_CODEWSCOPE;
	val.value.v_codewscope.code = codestr.data;
	val.value.v_codewscope.code_len = codestr.size;


	scope_es = es_new(es->env, es->st);
	if(!scope_es) {
		ret = 0;
		goto done;
	}
	if(!encode_doc(scope, scope_es)) {
		ret = 0;
		goto done;
	}

	val.value.v_codewscope.scope_data = (uint8_t *)bson_get_data(&scope_es->bson);
	val.value.v_codewscope.scope_len = scope_es->bson.len;
	val.value.v_codewscope.code = codestr.data;
	val.value.v_codewscope.code_len = codestr.size;

	LOG("append codewscope, len: %d \r\n", (int)scope_es->bson.len);

	ret = append_keyval(es->env, &es->bson, key, &val);

done:
	es_destroy(scope_es);
	termstr_destroy(&codestr);
	return ret;
}

static inline int
append_datetime(ERL_NIF_TERM key, 
				ERL_NIF_TERM msec_since_epoch, 
				encode_state *es)
{
	ErlNifSInt64 v_dt;
	bson_value_t val;

	if(!enif_get_int64(es->env, msec_since_epoch, &v_dt)) {
		return 0;
	}

	val.value_type = BSON_TYPE_DATE_TIME;
	val.value.v_datetime = v_dt;

	return append_keyval(es->env, &es->bson, key, &val);
}

static inline int
append_timestamp(ERL_NIF_TERM key,
				 ERL_NIF_TERM timestamp,
				 ERL_NIF_TERM increment,
				 encode_state *es)
{
	int v_timestamp, v_increment;
	bson_value_t val;

	if(!enif_get_int(es->env, timestamp, &v_timestamp)) {
		return 0;
	}
	if(!enif_get_int(es->env, increment, &v_increment)) {
		return 0;
	}

	val.value_type = BSON_TYPE_TIMESTAMP;
	val.value.v_timestamp.timestamp = v_timestamp;
	val.value.v_timestamp.increment = v_increment;

	return append_keyval(es->env, &es->bson, key, &val);
}

static inline int
append_null(ERL_NIF_TERM key, encode_state *es)
{
	bson_value_t val;
	val.value_type = BSON_TYPE_NULL;
	return append_keyval(es->env, &es->bson, key, &val);
}

static inline int
append_bool(ERL_NIF_TERM key, encode_state *es, bool v_bool)
{
	bson_value_t val;
	val.value_type = BSON_TYPE_BOOL;
	val.value.v_bool = v_bool;
	return append_keyval(es->env, &es->bson, key, &val);
}

static inline int
append_minkey(ERL_NIF_TERM key, encode_state *es)
{
	bson_value_t val;
	val.value_type = BSON_TYPE_MINKEY;
	return append_keyval(es->env, &es->bson, key, &val);
}

static inline int
append_maxkey(ERL_NIF_TERM key, encode_state *es)
{
	bson_value_t val;
	val.value_type = BSON_TYPE_MAXKEY;
	return append_keyval(es->env, &es->bson, key, &val);
}

static inline int
append_doc(ERL_NIF_TERM key, enc_doc_t *ed, encode_state *es)
{
	bson_value_t val;
	int ret;
	encode_state *cs = es_new(es->env, es->st);
	if(!cs) {
		ret = 0;
		goto done;
	}

	ret = encode_doc_impl(ed, cs);
	if(!ret) {
		goto done;
	}
	switch(ed->type) {
	case DOC_TYPE_MAP:
		val.value_type = BSON_TYPE_DOCUMENT;
		break;
	case DOC_TYPE_TUPLE:
		val.value_type = BSON_TYPE_DOCUMENT;
		break;
	case DOC_TYPE_LIST:
		val.value_type = BSON_TYPE_ARRAY;
		break;
	default:
		ret = 0;
		goto done;
	}
	// No ideas except force (const uint8_t *) -> (uint8_t *)
	val.value.v_doc.data = (uint8_t *)bson_get_data(&cs->bson);
	val.value.v_doc.data_len = cs->bson.len;
	ret = append_keyval(es->env, &es->bson, key, &val);

done:
	es_destroy(cs);
	return ret;
}

static inline int
encode_digit(ERL_NIF_TERM key, ERL_NIF_TERM term, encode_state *es)
{
	bson_value_t val;
	ErlNifSInt64 tmpi64;
	int tmpi;
	double tmpd;

	if(enif_get_int(es->env, term, &tmpi)) {
		val.value_type = BSON_TYPE_INT32;
		val.value.v_int32 = tmpi;
		return append_keyval(es->env, &es->bson, key, &val);
	}

	if(enif_get_int64(es->env, term, &tmpi64)) {
		val.value_type = BSON_TYPE_INT64;
		val.value.v_int64 = tmpi64;
		return append_keyval(es->env, &es->bson, key, &val);
	}

	if(enif_get_double(es->env, term, &tmpd)) {
		val.value_type = BSON_TYPE_DOUBLE;
		val.value.v_double = tmpd;
		return append_keyval(es->env, &es->bson, key, &val);
	}

	return 0;
}

static inline int
append_tuple(ERL_NIF_TERM key, ERL_NIF_TERM term, encode_state *es)
{
	enc_doc_t ed;
	ERL_NIF_TERM *array;
	int arity;

	if(!enif_get_tuple(es->env, 
					   term, 
					   &arity, 
					   (const ERL_NIF_TERM **)&array)) {
		return 0;
	}

	switch(arity) {
	case 2:
		if(enif_is_identical(array[0], es->st->atom_s_oid)) {
			return append_oid(key, array[1], es);
		}
		if(enif_is_identical(array[0], es->st->atom_s_date)) {
			return append_datetime(key, array[1], es);
		}
		if(enif_is_identical(array[0], es->st->atom_s_javascript)) {
		    LOG("append_js, key: %d, val: %d \r\n", (int32_t)key, (int32_t)term);
			return append_code(key, array[1], es);
		}
		break;
	case 4:
		if(enif_is_identical(array[0], es->st->atom_s_type) && 
				enif_is_identical(array[2], es->st->atom_s_binary)) {
			return append_binary(key, array[1], array[3], es);
		}
		if(enif_is_identical(array[0], es->st->atom_s_regex) && 
				enif_is_identical(array[2], es->st->atom_s_options)) {
			return append_regex(key, array[1], array[3], es);
		}
		if(enif_is_identical(array[0], es->st->atom_s_javascript) && 
				enif_is_identical(array[2], es->st->atom_s_scope)) {
			return append_codescope(key, array[1], array[3], es);
		}
		if(enif_is_identical(array[0], es->st->atom_s_timestamp) &&
				enif_is_identical(array[2], es->st->atom_s_increment)) {
			return append_timestamp(key, array[1], array[3], es);
		}
		break;
	default:
		break;
	}

	ed.type = DOC_TYPE_TUPLE;
	ed.value.v_tuple.array = array;
	ed.value.v_tuple.arity = arity;
	return append_doc(key, &ed, es);
}

int
encode_elem(ERL_NIF_TERM key, ERL_NIF_TERM term, encode_state *es) 
{
    LOG("encode_elem, key: %d, val: %d \r\n", (int32_t)key, (int32_t)term);

	if(enif_is_binary(es->env, term)) {
		return append_utf8(key, term, es);
	}

	if(enif_is_map(es->env, term)) {
		enc_doc_t ed;
		ed.type = DOC_TYPE_MAP;
		ed.value.map = term;
		return append_doc(key, &ed, es);
	}

	if(enif_is_tuple(es->env, term)) {
		return append_tuple(key, term, es);
	}

	if(enif_is_atom(es->env, term)) {
		if(enif_is_identical(term, es->st->atom_null) || 
				enif_is_identical(term, es->st->atom_undefined)) {
			return append_null(key, es);
		} else if(enif_is_identical(term, es->st->atom_true)) {
			return append_bool(key, es, true);
		} else if(enif_is_identical(term, es->st->atom_false)) {
			return append_bool(key, es, false);
		} else if(enif_is_identical(term, es->st->atom_minkey)) {
			return append_minkey(key, es);
		} else if(enif_is_identical(term, es->st->atom_maxkey)) {
			return append_maxkey(key, es);
		}
		return append_utf8(key, term, es);
	}

	if(enif_is_list(es->env, term)) {
		enc_doc_t ed;
		ed.type = DOC_TYPE_LIST;
		ed.value.list = term;
		return append_doc(key, &ed, es);
	}

	return encode_digit(key, term, es);
}

int
encode_doc_impl(enc_doc_t *ed, encode_state *es)
{
	int ret;

	switch(ed->type) {
	case DOC_TYPE_MAP: {
		ErlNifMapIterator iter;
		ERL_NIF_TERM key, val;
		size_t size;

		if(!enif_get_map_size(es->env, ed->value.map, &size)) {
        	ret = 0;
        	goto done;
    	}
    	if(size == 0) {
    		ret = 1;
    		goto done;
    	}

		if(!enif_map_iterator_create(es->env, ed->value.map, &iter, 
				ERL_NIF_MAP_ITERATOR_HEAD)) {
			ret = 0;
			goto done;
		}
		do {
			if(!enif_map_iterator_get_pair(es->env, &iter, &key, &val)) {
           		ret = 0;
           		goto done;
        	}
        	if(!encode_elem(key, val, es)) {
        		ret = 0;
        		goto done;
        	}
		} while(enif_map_iterator_next(es->env, &iter));
		ret = 1;
		break;
	}
	case DOC_TYPE_TUPLE: {
		int idx, arity = ed->value.v_tuple.arity;
		ERL_NIF_TERM *list = ed->value.v_tuple.array;

		for(idx = 0; idx < arity; idx += 2) {
			if(!encode_elem(list[idx], list[idx+1], es)) {
				ret = 0;
				goto done;
			}
		}
		ret = 1;
		break;
	}
	case DOC_TYPE_LIST: {
		ERL_NIF_TERM list = ed->value.list;
		ERL_NIF_TERM item;

		char buf[16];
		const char *idxstr;
		uint32_t idx = 0;

		while(enif_get_list_cell(es->env, list, &item, &list)) {
			ERL_NIF_TERM key;
   			
   			bson_uint32_to_string(idx, &idxstr, buf, sizeof buf);

   			if(!make_binary(es->env, &key, idxstr, strlen(idxstr))) {
   				ret = 0;
   				goto done;
   			}
   			if(!encode_elem(key, item, es)) {
   				ret = 0;
   				goto done;
   			}

			idx++;
		}
		ret = 1;
		break;
	}
	default:
		ret = 0;
	}

done:
	return ret;
}

int
encode_doc(ERL_NIF_TERM term, encode_state *es)
{
	enc_doc_t ed;

	if(enif_is_map(es->env, term)) {
		ed.type = DOC_TYPE_MAP;
		ed.value.map = term;
		return encode_doc_impl(&ed, es);
	}

	if(enif_is_tuple(es->env, term)) {
		ERL_NIF_TERM *array;
		int arity;

		if(!enif_get_tuple(es->env, term, &arity, (const ERL_NIF_TERM **)&array)) {
			return 0;
		}
		if(arity < 0 || arity%2 != 0) {
			return 0;
		}
		ed.type = DOC_TYPE_TUPLE;
		ed.value.v_tuple.array = array;
		ed.value.v_tuple.arity = arity;

		return encode_doc_impl(&ed, es);
	}

	return 0;
}

int
encode_result(ERL_NIF_TERM *out, encode_state *es)
{
	const uint8_t *data = bson_get_data(&es->bson);
	return make_binary(es->env, out, data, es->bson.len);
}

ERL_NIF_TERM
encode(ErlNifEnv *env, int argc, const ERL_NIF_TERM argv[])
{
	cabala_st 	 *st = (cabala_st*)enif_priv_data(env);
	encode_state *es;
	ERL_NIF_TERM  out;

	if(argc != 2) {
		return enif_make_badarg(env);
	}
	if(!enif_is_tuple(env, argv[0]) && !enif_is_map(env, argv[0])) {
		return enif_make_badarg(env);
	}

	es = es_new(env, st);
	if(!es) {
		goto failure;
	}
	if(!encode_doc(argv[0], es)) {
		goto failure;
	}
	if(!encode_result(&out, es)) {
		goto failure;
	}

	es_destroy(es);
	return out;

failure:
	es_destroy(es);
	return make_error(st, env, "internal_error");
}