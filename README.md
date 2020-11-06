# cmd
cmd is a library for calling functions via command line style strings that is fully type-safe

````c++
void foo(int);
cmd::registry r;
r.register_func("foo", &foo);
auto success = r.call("foo 42");
````

The return value of the function is ignored.  
The arguments are parsed like bash, supporting quoting.  
If parsing fails, success is false and the function isn't called.  


# Installation
cmd is header only, just `#include"cmd.hpp"`. Requires C++20.

# Documentation

### `registry`
`registry` is a regular type.

#### `void registry::register_func(const std::string& name, R (*fn)(Args...))`
Registers the function as the given name.

#### `bool registry::call(std::string_view line)`
Calls a registered function command line style. Returns false if the function name is unrecognized or parsing fails.

### `from_string`
Strings are converted to their respective argument types by `from_string<T>{}(std::move(token))`.  
It is specialized for `std::string`, `std::string_view`, integral types and floating types.  
You may specialize `from_string` to support other types.

#### `std::optional<T> from_string<T>::operator()(/* constructible from std::string&& */ token)`
If parsing fails, the optional should be empty.

