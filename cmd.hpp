#ifndef CMD_HPP_INCLUDED
#define CMD_HPP_INCLUDED

#include <array>
#include <charconv>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cmd
{
    using std::size_t;

    namespace detail
    {
        // indexing helper, see index_upto
        template <typename F, size_t... Is>
        inline constexpr decltype(auto) index_over(F&& f, std::index_sequence<Is...>)
        {
            return std::forward<F>(f)(std::integral_constant<size_t, Is>{}...);
        }

        // indexing helper, use as
        //      index_upto<N>([&](auto... is){});
        // where is are integral constants in [0, N).
        template <size_t N, typename F>
        inline constexpr decltype(auto) index_upto(F&& f)
        {
            return index_over(std::forward<F>(f), std::make_index_sequence<N>{});
        }
    } // namespace detail

    template<typename>
    struct must_specialize : std::false_type {};

    // from_string is the customization point for converting a token to the argument type.
    // from_string is already specialized for std::string_view, std::string, integral types and floating types.
    template <typename T>
    struct from_string
    {
        static_assert(must_specialize<T>::value, "from_string must be specialized to parse that type");
    };

    // Integral types and floating types depends on std::from_chars,
    // which most compilers haven't implemented yet, sadly.
    template <typename T>
    requires requires(std::string_view tok, T x)
    {
        std::from_chars(tok.data(), tok.data() + tok.size(), x);
    }
    struct from_string<T>
    {
        std::optional<T> operator()(std::string_view tok)
        {
            T x = 0;
            auto res = std::from_chars(tok.data(), tok.data() + tok.size(), x);
            if(res.ec != std::errc{})
                return {};
            return x;
        }
    };

    template <>
    struct from_string<std::string_view>
    {
        std::optional<std::string_view> operator()(std::string_view tok) { return tok; }
    };

    template<>
    struct from_string<std::string>
    {
        std::optional<std::string> operator()(std::string tok) { return tok; }
    };

    // erased_func is a type-erased function which can be called with a span of strings,
    // where each string is converted to their respective argument by from_string.
    // erased_func can be constructed from a function pointer.
    class erased_func
    {
        using untyped_func = void();

        template <typename R, typename... Args>
        static bool dispatch_func(untyped_func* uf, std::span<std::string> toks)
        {
            if(toks.size() != sizeof...(Args))
                return false;

            return detail::index_upto<sizeof...(Args)>([&](auto... is) {
                auto optargs = std::tuple{from_string<Args>{}(std::move(toks[is]))...};
                if((!get<is>(optargs) || ...))
                    return false;
                auto fn = (R(*)(Args...))uf;
                fn(std::move(*get<is>(optargs))...);
                return true;
            });
        }

      public:
        erased_func() = default;
        template <typename R, typename... Args>
        erased_func(R (*fn)(Args...))
            : dispatch{dispatch_func<R, Args...>}, fn{(untyped_func*)fn}
        {
        }

        bool call(std::span<std::string> toks) { return dispatch(fn, toks); }

      private:
        bool (*dispatch)(untyped_func*, std::span<std::string>) = nullptr;
        untyped_func* fn = nullptr;
    };

    // tokenize has bash semantics, e.g.
    //      a b'c d'e f'"g"'
    // will be tokenized as
    //      a
    //      bc de
    //      f"g"
    // returns the tokens and whether there is an unclosed quote.
    std::pair<std::vector<std::string>, char> tokenize(std::string_view line)
    {
        bool sq = false, dq = false;    // within single and double quotes
        std::vector<std::string> toks;
        std::string cur;
        while(line.size() > 0)
        {
            if(sq)
            {
                auto i = line.find('\'');
                if(i == line.npos)
                {
                    cur += line;
                    toks.push_back(std::move(cur));
                    return std::pair{toks, '\''};
                }
                cur += line.substr(0, i);
                line = line.substr(i + 1);
                sq = false;
            }
            else if(dq)
            {
                auto i = line.find('"');
                if(i == line.npos)
                {
                    cur += line;
                    toks.push_back(std::move(cur));
                    return std::pair{toks, '"'};
                }
                cur += line.substr(0, i);
                line = line.substr(i + 1);
                dq = false;
            }
            else
            {
                auto i = line.find_first_of(" \"'");
                if(i == line.npos)
                {
                    cur += line;
                    toks.push_back(std::move(cur));
                    return std::pair{toks, 0};
                }

                cur += line.substr(0, i);
                switch(line[i])
                {
                case ' ':
                    if(cur.size() > 0)
                    {
                        toks.push_back(std::move(cur));
                        cur = {};
                    }
                    break;
                case '\'': sq = true; break;
                case '"': dq = true; break;
                }
                line = line.substr(i + 1);
            }
        }

        if(cur.size() > 0)
            toks.push_back(std::move(cur));
        return std::pair{toks, 0};
    }

    // registry holds registered functions that can later be called command line style with
    // full type-safety, e.g.
    //      void foo(int);
    //      registry r;
    //      r.register_func("foo", &foo);
    //      auto success = r.call("foo 42");
    // The return value of the function is ignored.
    // The arguments are parsed like bash, supporting quoting.
    // If parsing fails, success is false and the function isn't called.
    // The string is converted to their respective arguments by calling
    //      from_string<T>{}(token);
    class registry
    {
      public:
        bool call(std::string_view line)
        {
            auto [toks, quote] = tokenize(line);
            if(quote || toks.empty())
                return false;

            auto it = table.find(toks[0]);
            if(it == table.end())
                return false;

            auto ef = it->second;
            return ef.call(std::span{toks}.subspan(1));
        }

        template <typename R, typename... Args>
        void register_func(const std::string& name, R (*fn)(Args...))
        {
            table[name] = fn;
        }

      private:
        std::unordered_map<std::string, erased_func> table;
    };
} // namespace cmd

#endif
