[![progress-banner](https://backend.codecrafters.io/progress/git/415c00cc-88bb-403a-a94e-1d61a82c27f0)](https://app.codecrafters.io/users/codecrafters-bot?r=2qF)
[![patreon](https://img.shields.io/badge/patreon-FF5441?style=for-the-badge&logo=Patreon)](https://www.patreon.com/hughdavenport)
[![youtube](https://img.shields.io/badge/youtube-FF0000?style=for-the-badge&logo=youtube)](https://www.youtube.com/playlist?list=PL5r5Q39GjMDe-3SsbXjmTimYajDjcQrF2)

This is a repository for my solutions to the
["Build Your Own Git" Challenge](https://codecrafters.io/challenges/git) in C. You can see my progress above.
You can also watch a [YouTube series](https://www.youtube.com/playlist?list=PL5r5Q39GjMDe-3SsbXjmTimYajDjcQrF2) where I discuss and code the solutions.

In this challenge, you'll build a small Git implementation that's capable of
initializing a repository, creating commits and cloning a public repository.
Along the way we'll learn about the `.git` directory, Git objects (blobs,
commits, trees etc.), Git's transfer protocols and more.

**Note**: If you're viewing this repo on GitHub, head over to
[codecrafters.io](https://codecrafters.io) to try the challenge.

# Running the program

The entry point for your git implementation is in `app/main.c`, but you can compile and run it with `your_program.sh`. This uses [CMake](https://cmake.org/) by default (Codecrafters provided a boilerplate), but you could compile it with `cc app/main.c`.

# Dependencies

This repo uses some stb-style header only libraries I wrote:

- [zlib.h]() TBD Link
- [deflate.h]() TBD Link
