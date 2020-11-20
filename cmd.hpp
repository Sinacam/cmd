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

#include "ztd/ztd.hpp"

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

    template <typename>
    struct must_specialize : std::false_type
    {
    };

    // from_string is the customization point for converting a token to the argument type.
    // from_string is already specialized for std::string_view, std::string, integral types and
    // floating types.
    template <typename>
    struct from_string;

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

    template <>
    struct from_string<std::string>
    {
        std::optional<std::string> operator()(std::string tok) { return tok; }
    };

    // to_string is the customization point for converting the return type to std::string.
    // to_string is already specialized for void, std::string, integral types and floating types.
    template <typename T>
    struct to_string;

    template <>
    struct to_string<void>
    {
    };

    template <typename T>
    requires requires(T& x, char* buf)
    {
        std::to_chars(buf, buf, x);
    }
    struct to_string<T>
    {
        std::string operator()(T x)
        {
            char buf[32]; // TODO: check correct length
            auto res = std::to_chars(buf, buf + sizeof(buf), x);
            return {buf, res.ptr};
        }
    };

    template <>
    struct to_string<std::string>
    {
        std::string operator()(std::string&& x) { return std::move(x); }
    };

    template <typename T>
    concept from_stringable = requires(std::string_view s, from_string<std::remove_cvref_t<T>> fs)
    {
        bool(fs(s));
        {
            *fs(s)
        }
        ->std::convertible_to<std::remove_cvref_t<T>>;
    };

    template <typename T>
    concept to_stringable = std::is_void_v<T> || requires(T x)
    {
        to_string<std::remove_cvref_t<T>>{}(x);
    };

    template <typename R, typename... Args>
    concept stringable = (to_stringable<R> && ... && from_stringable<Args>);

    // erased_func is a type-erased function which can be called with a span of strings,
    // where each string is converted to their respective argument by from_string.
    // erased_func can be constructed from a function pointer.
    class erased_func
    {
        using untyped_func = void();

        template <typename Callable, typename R, typename... Args>
        static std::optional<std::string> dispatch_func(untyped_func* uf,
                                                        std::span<std::string> toks)
        {
            if(toks.size() != sizeof...(Args))
                return {};

            return detail::index_upto<sizeof...(Args)>(
                [&](auto... is) -> std::optional<std::string> {
                    auto optargs = std::tuple{
                        from_string<std::remove_cvref_t<Args>>{}(std::move(toks[is]))...};
                    if((!get<is>(optargs) || ...))
                        return {};

                    auto fn = [uf] {
                        if constexpr(std::is_function_v<Callable>)
                            return (R(*)(Args...))uf;
                        else
                            return Callable{};
                    }();

                    using rR = std::remove_cvref_t<R>;
                    if constexpr(!std::is_void_v<rR>)
                    {
                        auto ret = fn(std::forward<Args>(*get<is>(optargs))...);
                        return to_string<rR>{}(std::move(ret));
                    }
                    else
                    {
                        fn(std::forward<Args>(*get<is>(optargs))...);
                        return "";
                    }
                });
        }

        template <typename Callable, typename R, typename... Args>
        erased_func(std::type_identity<Callable>, R (*)(Args...))
            : dispatch{dispatch_func<Callable, R, Args...>}, fn{nullptr}
        {
        }

      public:
        erased_func() = default;
        template <typename R, typename... Args>
        requires stringable<R, Args...> erased_func(R (*fn)(Args...))
            : dispatch{dispatch_func<R(Args...), R, Args...>}, fn{(untyped_func*)fn}
        {
        }

        template <typename T>
        requires std::is_empty_v<T> && requires {
            &T::operator();
        }
        erased_func(T)
            : erased_func{std::type_identity<T>{},
                          (typename ztd::mfp_traits<decltype(&T::operator())>::base_type*)nullptr}
        {
        }

        std::optional<std::string> call(std::span<std::string> toks) { return dispatch(fn, toks); }

      private:
        std::optional<std::string> (*dispatch)(untyped_func*, std::span<std::string>) = nullptr;
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
        bool sq = false, dq = false; // within single and double quotes
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
    //      int foo(int);
    //      registry r;
    //      r.register_func("foo", &foo);
    //      auto opt = r.call("foo 42");
    // calls foo(42) and returns  the result as as an std::optional<std::string>.
    // The arguments are parsed like bash, supporting quoting.
    // If parsing fails, opt is empty and the function isn't called.
    // The string is tokenized and converted to their respective arguments by calling
    //      from_string<T>{}(token);
    // The return value of the function is converted to std::string by
    //      to_string<T>{}(return_value);
    class registry
    {
      public:
        std::optional<std::string> call(std::string_view line)
        {
            auto [toks, quote] = tokenize(line);
            if(quote || toks.empty())
                return {};

            return call(toks[0], std::span{toks}.subspan(1));
        }

        std::optional<std::string> call(const std::string& name, std::span<std::string> toks)
        {
            auto it = table.find(name);
            if(it == table.end())
                return {};

            auto ef = it->second;
            return ef.call(toks);
        }

        template <typename R, typename... Args>
        requires stringable<R, Args...> void register_func(const std::string& name,
                                                           R (*fn)(Args...))
        {
            table[name] = fn;
        }

        template <typename Callable>
        requires std::is_empty_v<Callable> && requires { &Callable::operator(); }
        void register_func(const std::string& name, Callable callable)
        {
            table[name] = callable;
        }

      private:
        std::unordered_map<std::string, erased_func> table;
    };

    template<typename T, T>
    struct function;

    template<typename R, typename... Args, R (*fn)(Args...)>
    struct function<R (*)(Args...), fn>
    {
        R operator()(Args... args)
        {
            return fn(std::forward<Args>(args)...);
        }
    };

    template<auto x>
    inline constexpr function<decltype(x), x> func;

} // namespace cmd

#endif
