# cmd
cmd is a library for calling functions via command line style strings that is fully type-safe

````c++
char foo(int);
cmd::registry r;
r.register_func("foo", &foo);
auto opt = r.call("foo 42");
````
Calls `foo(42)` and returns  the result as as an `std::optional<std::string>`.  
The arguments are separated by spaces, unless they are inside single or double quotes.  
If parsing fails, `opt` is empty and the function isn't called.


# Installation
cmd is header only, just `#include"cmd.hpp"`. Requires C++20.

# Documentation

### `registry`
`registry` is a regular type.

#### `void registry::register_func(const std::string& name, R (*fn)(Args...))`
Registers the function as the given name.

#### `std::optional<std::string> registry::call(std::string_view line)`
Calls a registered function command line style.  
Returns the returned value converted to a string on success.  
Returns an empty optional if the function name is unrecognized or parsing fails.

### `from_string`
Strings are converted to their respective arguments by `from_string<T>{}(std::move(token))`.  
It is specialized for `std::string`, `std::string_view`, integral types and floating types.  
You may specialize `from_string` to support other types.

#### `std::optional<T> from_string<T>::operator()(/* constructible from rvalue of std::string */ token)`
If parsing fails, the optional should be empty.

### `to_string`
Return values are converted to strings by `to_string<T>{}(return_value)`.  
It is specialized for `void`, `std::string`, integral types and floating types.  
You may specialize `to_string` to support other types.

#### `std::string to_string<T>::operator()(/* constructible from rvalue of T */ return_value)`
Only called on successfully calling the function.

