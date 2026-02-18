Real Time Operating System
---

Goal
---
To design and implement a real time operating system that can run on ARM Cortex A/R and possibly M series.
- strictly test driven development
- Micro Kernel architecture
- Completely arhictected, designed with full documentation
- Written in C++ and Assembly where required
- Support C++ applications (which means I can write full C++ 17 code, build it for my os and my os should be able to load it and run it)
- Supports IPC (Ideas can be taken from /home/litu/sandbox/cpp/ directories ms-ringbuffer, ms-runloop and ms-ipc)
- Supports Multithreading, synchronization and pinning applications to a core
- printf and std outs should be directed to serial port which should be configurable
- should have everyhting that a RTOS usually requires like interrupts (applications should be able to register for interrupts that they want to get from OS for example any peripheral interrupt etc)
- should have heap
- should have device tree (simple version may be yaml or yml)
- should have bootloader (which means we will have to design the bootloader)
- I have three RTOS source code located at (/home/litu/sandbox/embedded/rtos) for reference. and our development enviroment will be ms-os/ directory
- May be we use nix as our development environment config tool ? what do you suggest anything better and easier ?

Step 1:
---
Create a step by step development plan

step2:
---
Propose where to start. I have a STM32F407VGt6 development board , i can add some test leds and i also have a JTAG debugger. we can start with this may be ?


CPP Coding standards: 
---
# AI Agent System Prompt -- C++ Coding Standards

You are generating C++17 code for a professional embedded /
systems-level project.

You MUST follow the rules below strictly.

------------------------------------------------------------------------

## 1. Platform Separation Rules

1.  Do NOT use `#ifdef`, `#ifndef`, or `#if defined(...)` inside `.cpp`
    source files.
2.  Platform-specific code must be separated by directory structure.
3.  Use the following layout:

```{=html}
<!-- -->
```
    include/
        core/
        platform/
            linux/
            macos/
            windows/

    src/
        core/
        platform/
            linux/
            macos/
            windows/

4.  The build system (CMake) selects platform-specific source files.
5.  Never mix multiple platform implementations in the same file.

------------------------------------------------------------------------

## 2. CMake Rules

1.  CMake must select platform files using `if(APPLE)`, `if(WIN32)`,
    `if(UNIX)` logic.
2.  Do NOT rely on preprocessor macros for platform branching inside
    source code.
3.  Keep CMake clean and minimal.
4.  Automatically include the correct platform directory.

Example:

``` cmake
if(APPLE)
    set(PLATFORM_SOURCES
        src/platform/macos/PlatformTimer.cpp
    )
elseif(WIN32)
    set(PLATFORM_SOURCES
        src/platform/windows/PlatformTimer.cpp
    )
elseif(UNIX)
    set(PLATFORM_SOURCES
        src/platform/linux/PlatformTimer.cpp
    )
endif()

add_library(MyLib
    src/core/EventDispatcher.cpp
    ${PLATFORM_SOURCES}
)

target_include_directories(MyLib PUBLIC
    include
)
```

------------------------------------------------------------------------

## 3. Naming Conventions

1.  Filenames: CamelCase
    -   `RingBuffer.h`
    -   `EventDispatcher.cpp`
    -   `PlatformTimer.h`
2.  Classes: CamelCase
    -   `EventDispatcher`
    -   `LatencyDeltaManager`
3.  Member variables:
    -   MUST be prefixed with `m_`
    -   Example: `m_buffer`, `m_isRunning`, `m_thread`
4.  Private helper functions:
    -   lowerCamelCase
5.  Constants:
    -   `kConstantName`

------------------------------------------------------------------------

## 4. Brace Style

Braces must be on the next line for: - classes - functions - if / for /
while / switch blocks

Correct style:

``` cpp
class EventDispatcher
{
public:
    void start();

private:
    bool m_isRunning;
};
```

``` cpp
void EventDispatcher::start()
{
    if (m_isRunning)
    {
        return;
    }

    m_isRunning = true;
}
```

Never use one-line braces.

------------------------------------------------------------------------

## 5. Error Handling Style

1.  Prefer early returns to reduce nesting.
2.  Early return in `void` functions is allowed and encouraged.
3.  Avoid deeply nested `if` blocks.

Preferred:

``` cpp
void process()
{
    if (!isValid())
    {
        return;
    }

    doWork();
}
```

------------------------------------------------------------------------

## 6. Modern C++ Guidelines

1.  Use C++17.
2.  Avoid raw `new` / `delete`.
3.  Prefer:
    -   `std::unique_ptr`
    -   `std::vector`
    -   `std::array`
    -   `std::optional`
4.  Use RAII.
5.  Avoid global state unless explicitly required.
6.  Design for testability (dependency injection where appropriate).

------------------------------------------------------------------------

## 7. Code Quality Rules

1.  No emojis or unicode characters in:
    -   Code
    -   Comments
    -   Markdown
2.  Keep code readable and explicit.
3.  Avoid clever tricks that reduce clarity.
4.  Keep `main()` minimal --- move logic into classes.
5.  Design components to be unit-test friendly.

------------------------------------------------------------------------

## 8. Threading / Concurrency (When Required)

1.  Clearly separate synchronization concerns.
2.  Avoid hidden locking.
3.  Prefer explicit ownership.
4.  Document thread safety expectations in class comments.

------------------------------------------------------------------------

## 9. Embedded Variant (Optional Mode)

If the project is embedded:

1.  No dynamic allocation unless explicitly allowed.
2.  Prefer static allocation.
3.  No exceptions.
4.  No RTTI.
5.  Deterministic behavior only.



