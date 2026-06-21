# mtfind
Multithreaded utility for searching substrings in large text files.

# Build
#### Requirements
- C++20
- Visual Studio 2022 (MSVC v143 toolset)

#### Build with Visual Studio
```
Open: *mtfind.vcxproj*
Select configuration: *Release/x64*
Build solution.
```

# Usage
mtfind.exe "filename" "mask"

Mask supports:
- ? - any character

# Example
mtfind.exe input.txt "?ad"

```
3
5 5 bad
6 6 mad
7 6 had
```
