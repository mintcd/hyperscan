These steps are to build Hyperscan after cloning.

1. Modify the first line with

```cmake
cmake_minimum_required(VERSION 3.10)

# Silence developer warnings about removed Find modules when possible
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
if(POLICY CMP0148)
    cmake_policy(SET CMP0148 NEW)
endif()
```

2. Replace `find_package(PythonInterp)` by `find_package(Python COMPONENTS Interpreter REQUIRED)` and `PythonInterp_FOUND` by `Python_Interpreter_FOUND`

3. Download `sqlite3.c` and `sqlite3.h` and place in `hyperscan/sqlite3`
3. Replace

```cmake
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}  /O2 ${MSVC_WARNS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /O2 ${MSVC_WARNS} /wd4800 -DBOOST_DETAIL_NO_CONTAINER_FWD")
```

by

```cmake
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /arch:AVX2 /O2 ${MSVC_WARNS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2 /O2 ${MSVC_WARNS} /wd4800 -DBOOST_DETAIL_NO_CONTAINER_FWD")
```

3. Build and run in the `hyperscan` folder with MSVC

``` bash
cmake -S . -B build_msvc -G "Visual Studio 17 2022" -A x64 -DCMAKE_FIND_DEBUG_MODE=ON -DDUMP_SUPPORT=ON > build_config.log 2>&1
cmake --build build_msvc --config RelWithDebInfo --target dump_rose
$env:HS_DUMP_PATH = "hs_dumps"
build_msvc\bin\dump_nfa.exe "a"
```

