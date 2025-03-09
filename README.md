# zero
SubSpace/Continuum network bot. 

## Security
Connecting to SSC zones requires cryptinuum security solver service unless you have a VIE bypass. This is private to keep the integrity of the game.

Without the security solver, you can connect to asss zones that are using `enc_null` module and `Security::SecurityKickoff` to set 0 or by using `CAP_BYPASS_SECURITY`.

Set `Encryption = Subspace` in the config file to use VIE encryption.

## Config
1. Copy `zero.cfg.dist` to `zero.cfg`
2. Open `zero.cfg` and modify the login details.

Config file order:
1. Command line first argument `./zero.exe mybot.cfg`
2. `zero.cfg` if nothing is specified on command line.
3. `zero.cfg.dist` if `zero.cfg` was not found.

## Building
### Getting source
1. `git clone https://github.com/plushmonkey/zero`
2. `cd zero`
3. `git submodule init && git submodule update`

### Windows
1. Open `zero.sln`
2. Switch to Release x64 build
3. Build

### Linux
1. Optionally install GLFW3 development libraries (`sudo apt-get install libglfw3-dev` on Ubuntu). This is for the debug render window.
2. Install cmake
3. Open terminal in zero directory
4. `cmake -DCMAKE_BUILD_TYPE=Release -B build -S .`
5. `cd build`
6. `make -j 12`
7. `cp ../zero.cfg.dist zero.cfg`

### Debug renderer
1. Copy Continuum's graphics folder to the folder where you're running zero.
2. Change config file to enable `RenderWindow`.
