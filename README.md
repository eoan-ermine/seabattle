# seabattle

Sea battle game written with Boost.Asio

## Build

```shell
mkdir build && cd build
conan install .. -of .
cmake --preset conan-release .
cmake --build .
```
