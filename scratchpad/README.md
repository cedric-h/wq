when doing systems programming, one's archnemesis is frequently ... the system.

contained within this folder are isolated playgrounds for individual aspects of the system, as follows:

- net : basic udp sockets example that seems to work on windows and linux
- dl : basic dylib (.dll/.so) example that seems to work on windows and linux
- d3d11_pixels.c : shamelessly stolen from [the beloved mmozeiko](gists.github.com/mmozeiko), gets pixels on the screen on win32.

having isolated usage of these APIs is not only helpful for me to have on hand as I develop my project -- they're easy starting off points for unit tests and other exploratory blocks of code -- but they're also useful from an educational standpoint. 

### educational?
when interacting with the host operating system, there is a certain allure to the cross-platform API wrappers as found in the standard libraries of languages like Odin, Zig and Rust. however, I have found that even for games, which are often quite isolated from their host operating system, cross-platform wrappers around specific concepts like dylibs or networking sockets seem to still be ... leaky, almost like you'd have a clearer picture of what was really going on if you interfaced with the C directly.

so it is hoped that these standalone "scratchpad" examples demonstrate how to use the underlying C APIs in cross-platform ways. the aforementioned standard libraries are also fine examples of this, but building pure C code that links against these APIs comes with its own slew of headaches that hopefully these examples help address.

there is also a certain advantage to having the cross-platform "synchronization point" at the application level rather than the API level. e.g. I don't need to call `window_open` and get back a struct representing a cross-platform window handle; I just say "here are some pixels" and the cross-platform layer figures out the rest.

# building
the dl examples build beautifully with `zig cc` and `tcc`. the net and d3d11 examples are compiled using portable msvc as described [here](https://gist.github.com/mmozeiko/7f3162ec2988e81e56d5c4e22cde9977).

e.g.
```bat
REM download msvc
cd ~
python3 portable-msvc.py
cd msvc
setup.bat

REM go to code, compile, run
cd ../scratchpad
cl d3d11_pixels.c
d3d11pixels.exe
```
