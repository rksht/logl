#include <learnogl/kitchen_sink.h>

#include <array>
#include <iostream>

void my_function()
{
    ONCE_BLOCK({
        std::cout << "Hello world";
        std::array<int, 100> a{};
    })

    std::cout << "called my_function\n";
}

int main()
{
    my_function();
    my_function();
    my_function();
    my_function();
    my_function();
}
