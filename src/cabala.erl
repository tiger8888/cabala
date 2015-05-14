-module(cabala).

-export([encode/1, 
         decode/1,
		 decode/2]).

-on_load(init/0).

encode(Data) ->
    encode(Data, []).

encode(Data, Opts) when is_tuple(Data); is_map(Data) ->
	nif_encode(Data, Opts).

decode(Data) ->
    decode(Data, []).

decode(Data, Opts) when is_binary(Data) ->
	nif_decode(Data, Opts).

%%% -------------------------------------------------
%%% Nif Functions
%%% -------------------------------------------------
-define(NOT_LOADED, not_loaded(?LINE)).

init() ->
	Path = filename:join(nif_dir(), "cabala"),
    erlang:load_nif(Path, 0).

nif_dir() ->
	case code:priv_dir(?MODULE) of
        {error, _} ->
            EbinDir = filename:dirname(code:which(?MODULE)),
            AppPath = filename:dirname(EbinDir),
            filename:join(AppPath, "priv");
        Path ->
            Path
    end.

not_loaded(Line) ->
    erlang:nif_error({not_loaded, [{module, ?MODULE}, {line, Line}]}).

nif_decode(_Data, _Opts) ->
	?NOT_LOADED.

nif_encode(_Data, _Opts) ->
	?NOT_LOADED.