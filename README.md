# Problem 020 - Buddy Algorithm

**ACMOJ Problem ID**: 1848

## Table of Contents

- [Problem 020 - Buddy Algorithm](#problem-020---buddy-algorithm)
  - [Table of Contents](#table-of-contents)
  - [Introduction](#introduction)
  - [Implementation Requirements](#implementation-requirements)
  - [Per-Testcase Resource Limits](#per-testcase-resource-limits)
  - [Submission Requirements](#submission-requirements)
    - [Submission Guidelines](#submission-guidelines)
    - [Evaluation Notes](#evaluation-notes)
    - [Academic Integrity](#academic-integrity)

## Introduction

The Buddy algorithm is a common memory allocation algorithm. The advantage of this algorithm is that it can effectively utilize memory space and avoid memory fragmentation. However, it also has some disadvantages, such as the memory pool size should be a power of 2, otherwise it will waste some memory space. In addition, the Buddy algorithm is prone to internal fragmentation.

In this assignment, you need to implement a simple Buddy algorithm and pass the tests.

## Implementation Requirements

In this assignment, the minimum allocation unit is agreed to be a page of size $4K$ (page), with $rank=1$ (page size is $4K \times 2^{rank-1}$).

You need to complete the content in `buddy.c`, and the functions to be implemented and their descriptions are as follows:

> Valid $rank = 1,2, ..., 16$
>
> For other definitions such as `OK`, `EINVAL`, `ENOSPC`, see `buddy.h`

1. `int init_page(void *p, int pgcount)`: Initialize pages

   - You need to manage `pgcount` $4K$ pages, these $4K$ pages are allocated consecutively and the starting address is `p`

2. `void *alloc_pages(int rank)`: Allocate a page with the specified $rank$
   - The return value is the starting address of the allocated page
   - If there is not enough space, return `-ENOSPC`
   - If the passed `rank` is illegal, return `-EINVAL`

3. `int return_pages(void *p)`: Release a page to the buddy system
   - Valid `p` is the page address returned by `alloc_pages` that has not been released yet
   - If the release is successful, return `OK`
   - If the passed page address is illegal, return `-EINVAL`

4. `int query_ranks(void *p)`: Query the rank of a page
   - Unallocated pages are queried according to their maximum rank
   - If the passed page address is illegal, return `-EINVAL`

5. `int query_page_counts(int rank)`: Query how many unallocated pages remain for the specified rank, if the passed rank is illegal, return `-EINVAL`

You need to ensure that your allocation algorithm can pass the tests and output. The final test is scored by comparing the output.

## Per-Testcase Resource Limits

- **Time Limit (per test case)**: 10000 ms
- **Memory Limit (per test case)**: 244 MiB
- **Disk Usage**: Disk access is not permitted

## Submission Requirements

### OJ Git Repository Compilation Process

For Git compilation, we will first clone the repository using a command similar to:
```bash
git clone <repo_url> . --depth 1 --recurse-submodules --shallow-submodules --no-local
```

Then we check if there is a `CMakeLists.txt` file. If it exists, we run (if not, a warning message will be displayed):
```bash
cmake .
```

Finally, we check if there is any of `GNUmakefile`/`makefile`/`Makefile` (if cmake was run previously, this will be the generated Makefile). If it exists, we run (if not, a warning message will be displayed):
```bash
make
```

After this process is complete, we will use the `code` file in the project root directory as the compilation result.

A `Makefile` file is provided in the project. You can use or modify it as needed. The local environment has gcc-13 and g++-13 available.

### Git Configuration Requirements

**IMPORTANT**: You must create a `.gitignore` file in your project root directory to avoid OJ evaluation conflicts.

The `.gitignore` file should include at least the following entries:

```gitignore
CMakeFiles/
CMakeCache.txt
```

### Submission Guidelines

- The submitted code must be able to compile successfully through the above compilation process
- The compiled executable file name must be `code`
- The program needs to be able to read data from standard input and write results to standard output
- Please ensure the code runs correctly within the given time and space limits
- **You must use C language** to implement this assignment

### Project Files

The provided files include:
- `buddy.c` - Main implementation file (to be completed)
- `buddy.h` - Header file with definitions
- `main.c` - Test driver
- `Makefile` - Build configuration
- `utils.h` - Utility definitions

### Evaluation Notes

- The evaluation system will test your program using the provided test data
- The program output must exactly match the expected output (including format)
- Exceeding time or memory limits will be judged as the corresponding error type
- Please pay attention to the overall time performance of your code and the time complexity of each part of your algorithm

### Academic Integrity

If any violations are found during evaluation or code review (including but not limited to using unconventional methods to pass test cases), your final score may be significantly reduced or become **0 points**.

